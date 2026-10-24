/*
 * SPDX-FileCopyrightText: 2025 ESP32-SI4732 Radio
 * SPDX-FileCopyrightText: 2026 Max Arnold
 * SPDX-FileCopyrightText: miniradio Contributors
 * SPDX-License-Identifier: MIT
 *
 * ST7789 LCD 드라이버 — LovyanGFX 백엔드 (8080 병렬 8비트).
 * 패널/버스/시퀀스 클래스는 원본 ats-mini-recovery Display.h에서 포팅:
 *  - RDDID 자동 감지: ST7789 원본/하이감마, GC9307 미러패널 폴백
 *  - INIT_SEQUENCE_3 (TFT_eSPI JLX240) 시퀀스, MADCTL 회전 테이블
 *  - LCD_CAM OEN 핸드오프 픽스 (Bus_Parallel8_ATSMini)
 *
 * 깜빡임 방지:
 *  - PSRAM 스프라이트(320x170 RGB565)에 모든 그리기를 수행
 *  - 변경 영역(dirty rect)만 패널로 flush
 *  - PSRAM 없으면 직접 렌더링(부분 영역 클리어로 최소화)
 * 백라이트는 LEDC PWM (0–100%, 20kHz).
 */
#include <LovyanGFX.hpp>
#include <esp_rom_gpio.h>
#include <hal/gpio_ll.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"

#include "display.h"
#include "lgfx/utility/lgfx_qrcode.h"

static const char *TAG = "display";

#define LCD_FREQ_WRITE_HZ  20000000   /* 원본 freq_write=20MHz */
#define LCD_PANEL_WIDTH    170        /* 세로 패널 원본 폭 */
#define LCD_PANEL_OFFSET_X 35         /* 원본 offset_x=35 */

#define LCD_ID_ORIGINAL    0x8181B3u
#define LCD_ID_HIGH_GAMMA  0x858552u
#define LCD_ID_MIRRORED    0x009307u
#define LCD_CMD_RDDID      0x04

/* --------------------------------------------------------------------------
 * 원본 Display.h 클래스 포팅 (버스 OEN 픽스 / INIT_SEQUENCE_3 / GC9307)
 * ------------------------------------------------------------------------ */

class Bus_Parallel8_ATSMini : public lgfx::Bus_Parallel8
{
  bool initialized = false;

public:
  using lgfx::Bus_Parallel8::beginRead;

  bool init(void) override
  {
    if (!initialized) initialized = lgfx::Bus_Parallel8::init();
    return initialized;
  }

  void release(void) override
  {
    if (!initialized) return;
    lgfx::Bus_Parallel8::release();
    initialized = false;
  }

  void beginRead(void) override
  {
    wait();
    const auto& cfg = config();
    for (int8_t pin : cfg.pin_data)
    {
      gpio_ll_set_output_enable_ctrl(&GPIO, pin, false, false);
      gpio_ll_output_disable(&GPIO, pin);
    }

    gpio_ll_set_level(&GPIO, cfg.pin_rs, 1);
    esp_rom_gpio_connect_out_signal(cfg.pin_rs, SIG_GPIO_OUT_IDX, false, false);
    gpio_ll_set_level(&GPIO, cfg.pin_rd, 0);
  }
};

class Panel_ST7789_ATSMini : public lgfx::Panel_ST7789
{
public:
  bool highGammaDisplay = false;

  Panel_ST7789_ATSMini()
  {
    _nop_closing = false; /* 전용 CS 핀 보드 */
  }

protected:
  uint8_t getMadCtl(uint8_t rotation) const override
  {
    static constexpr uint8_t values[] =
    {
      0,
      MAD_MX | MAD_MV,
      MAD_MX | MAD_MY,
      MAD_MV | MAD_MY,
    };
    return values[rotation & 3];
  }

  const uint8_t* getInitCommands(uint8_t listno) const override
  {
    static constexpr uint8_t list0[] =
    {
      CMD_SLPOUT   , CMD_INIT_DELAY, 120,
      CMD_NORON    , 0,
      CMD_PORCTRL  , 5, 0x0B, 0x0B, 0x00, 0x33, 0x33,
      CMD_GCTRL    , 1, 0x75,
      CMD_VCOMS    , 1, 0x28,
      CMD_LCMCTRL  , 1, 0x2C,
      CMD_VDVVRHEN , 1, 0x01,
      CMD_VRHS     , 1, 0x1F,
      CMD_FRCTR2   , 1, 0x13,
      CMD_PWCTRL1  , 1, 0xA7,
      CMD_PWCTRL1  , 2, 0xA4, 0xA1,
      0xD6         , 1, 0xA1,
      CMD_PVGAMCTRL, 14, 0xF0, 0x05, 0x0A, 0x06, 0x06, 0x03, 0x2B,
                       0x32, 0x43, 0x36, 0x11, 0x10, 0x2B, 0x32,
      CMD_NVGAMCTRL, 14, 0xF0, 0x08, 0x0C, 0x0B, 0x09, 0x24, 0x2B,
                       0x22, 0x43, 0x38, 0x15, 0x16, 0x2F, 0x37,
      CMD_DISPON   , CMD_INIT_DELAY, 120,
      0xFF, 0xFF,
    };
    static constexpr uint8_t highGamma[] =
    {
      0x26, 1, 0x08, /* GAMSET: Gamma Curve 3 */
      0x55, 1, 0xB1, /* WRCACE: High enhancement, UI mode */
      0xFF, 0xFF,
    };
    if (listno == 0) return list0;
    if (listno == 1 && highGammaDisplay) return highGamma;
    return nullptr;
  }
};

class Panel_GC9307_ATSMini : public lgfx::Panel_GC9307
{
public:
  void setWindow(uint_fast16_t xs, uint_fast16_t ys,
                 uint_fast16_t xe, uint_fast16_t ye) override
  {
    /* Panel_GC9307의 하드코딩 34px 대신 설정된 offset 사용 */
    lgfx::Panel_GC9xxx::setWindow(xs, ys, xe, ye);
  }
};

class MiniLgfx : public lgfx::LGFX_Device
{
  uint32_t displayId = 0;
  Panel_ST7789_ATSMini displayPanel;
  Panel_GC9307_ATSMini gc9307Panel;
  Bus_Parallel8_ATSMini bus;

public:
  void configure(const display_config_t *cfg)
  {
    {
      auto c = bus.config();
      c.freq_write = LCD_FREQ_WRITE_HZ;
      c.pin_wr = cfg->pin_wr;
      c.pin_rd = cfg->pin_rd;
      c.pin_rs = cfg->pin_dc;
      c.pin_d0 = cfg->pin_d[0];
      c.pin_d1 = cfg->pin_d[1];
      c.pin_d2 = cfg->pin_d[2];
      c.pin_d3 = cfg->pin_d[3];
      c.pin_d4 = cfg->pin_d[4];
      c.pin_d5 = cfg->pin_d[5];
      c.pin_d6 = cfg->pin_d[6];
      c.pin_d7 = cfg->pin_d[7];
      bus.config(c);
      displayPanel.setBus(&bus);
    }
    {
      auto c = displayPanel.config();
      c.pin_cs = cfg->pin_cs;
      c.pin_rst = cfg->pin_rst;
      c.rgb_order = true;          /* LCMCTRL=0x2C(XBGR=1) → MADCTL RGB */
      c.panel_width = LCD_PANEL_WIDTH;
      c.offset_x = LCD_PANEL_OFFSET_X;
      c.dummy_read_pixel = 8;
      c.invert = true;
      c.bus_shared = false;
      displayPanel.config(c);

      auto g = gc9307Panel.config();
      g.pin_cs = c.pin_cs;
      g.pin_rst = c.pin_rst;
      g.panel_width = LCD_PANEL_WIDTH;
      g.offset_x = LCD_PANEL_OFFSET_X;
      g.bus_shared = false;
      gc9307Panel.config(g);
      gc9307Panel.setBus(&bus);
    }
    setPanel(&displayPanel);
  }

protected:
  bool init_impl(bool use_reset, bool use_clear) override
  {
    /* RDDID 읽기 전용으로 패널 초기화 테이블 없이 버스/리셋만 준비 */
    if (!displayPanel.lgfx::Panel_Device::init(use_reset)) return false;
    displayId = displayPanel.isReadable()
        ? __builtin_bswap32(displayPanel.readCommand(LCD_CMD_RDDID, 0, 3)) >> 8
        : 0;
    displayPanel.highGammaDisplay = displayId == LCD_ID_HIGH_GAMMA;
    if (displayId == LCD_ID_MIRRORED) {
      setPanel(&gc9307Panel);
    } else {
      setPanel(&displayPanel);
    }
    return lgfx::LGFX_Device::init_impl(false, use_clear);
  }

public:
  uint32_t getDisplayId() const { return displayId; }
};

/* --------------------------------------------------------------------------
 * 백라이트 (LEDC PWM, GPIO 폴백)
 * ------------------------------------------------------------------------ */

#define BL_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define BL_LEDC_TIMER    LEDC_TIMER_0
#define BL_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BL_LEDC_FREQ_HZ  20000   /* 가시 플리커 제거 */

static int       s_bl = -1;
static bool      s_bl_pwm;
static MiniLgfx  s_tft;
static bool      s_inited;

/* 스프라이트 (PSRAM) + dirty rect */
static lgfx::LGFX_Sprite *s_sprite;
static bool      s_use_sprite;
static int16_t   s_dx0, s_dy0, s_dx1, s_dy1;   /* [x0, x1) [y0, y1) */
static int16_t   s_w, s_h;

static esp_err_t backlight_level(uint8_t percent);

static esp_err_t backlight_init(int pin, uint8_t percent)
{
    s_bl = pin;
    if (pin < 0) {
        return ESP_OK;
    }
    ledc_timer_config_t tcfg = {};
    tcfg.speed_mode = BL_LEDC_MODE;
    tcfg.duty_resolution = LEDC_TIMER_8_BIT;
    tcfg.timer_num = BL_LEDC_TIMER;
    tcfg.freq_hz = BL_LEDC_FREQ_HZ;
    tcfg.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ledc timer failed (%s), GPIO fallback", esp_err_to_name(err));
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << pin;
        io.mode = GPIO_MODE_OUTPUT;
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "bl gpio");
        gpio_set_level((gpio_num_t)pin, percent ? 1 : 0);
        s_bl_pwm = false;
        return ESP_OK;
    }
    ledc_channel_config_t ccfg = {};
    ccfg.gpio_num = pin;
    ccfg.speed_mode = BL_LEDC_MODE;
    ccfg.channel = BL_LEDC_CHANNEL;
    ccfg.timer_sel = BL_LEDC_TIMER;
    ccfg.duty = 0;
    ccfg.hpoint = 0;
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ccfg), TAG, "bl channel");
    s_bl_pwm = true;
    return backlight_level(percent);
}

static esp_err_t backlight_level(uint8_t percent)
{
    ESP_RETURN_ON_FALSE(s_bl >= 0, ESP_ERR_NOT_SUPPORTED, TAG, "no bl pin");
    if (percent > 100) {
        percent = 100;
    }
    if (s_bl_pwm) {
        uint32_t duty = (uint32_t)percent * 255 / 100;
        ESP_RETURN_ON_ERROR(ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty),
                            TAG, "set duty");
        return ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
    }
    gpio_set_level((gpio_num_t)s_bl, percent ? 1 : 0);
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * dirty rect + 스프라이트
 * ------------------------------------------------------------------------ */

static void dirty_reset(void)
{
    s_dx0 = s_w;  s_dy0 = s_h;
    s_dx1 = 0;    s_dy1 = 0;
}

void display_mark_dirty(int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    if (w == 0 || h == 0) {
        return;
    }
    int32_t x0 = x, y0 = y;
    int32_t x1 = (int32_t)x + w, y1 = (int32_t)y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s_w) x1 = s_w;
    if (y1 > s_h) y1 = s_h;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    if (x0 < s_dx0) s_dx0 = (int16_t)x0;
    if (y0 < s_dy0) s_dy0 = (int16_t)y0;
    if (x1 > s_dx1) s_dx1 = (int16_t)x1;
    if (y1 > s_dy1) s_dy1 = (int16_t)y1;
}

void display_invalidate_all(void)
{
    display_mark_dirty(0, 0, (uint16_t)s_w, (uint16_t)s_h);
}

void display_clear_dirty(void)
{
    dirty_reset();
}

/* 그리기 대상: 스프라이트 또는 패널 */
static inline lgfx::LovyanGFX *canvas(void)
{
    return s_use_sprite ? (lgfx::LovyanGFX *)s_sprite : (lgfx::LovyanGFX *)&s_tft;
}

esp_err_t display_flush(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_dx1 <= s_dx0 || s_dy1 <= s_dy0) {
        return ESP_OK;
    }
    if (!s_use_sprite) {
        dirty_reset();
        return ESP_OK;
    }
    int32_t x = s_dx0, y = s_dy0;
    int32_t w = s_dx1 - s_dx0, h = s_dy1 - s_dy0;
    /* 스프라이트 버퍼에서 dirty 영역만 pushImage */
    const uint16_t *buf = (const uint16_t *)s_sprite->getBuffer();
    if (buf) {
        int32_t stride = s_sprite->width();
        s_tft.startWrite();
        s_tft.setAddrWindow(x, y, w, h);
        for (int32_t row = 0; row < h; ++row) {
            s_tft.writePixels(&buf[(y + row) * stride + x], w);
        }
        s_tft.endWrite();
    }
    dirty_reset();
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * C API
 * ------------------------------------------------------------------------ */

esp_err_t display_init(const display_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg && cfg->width && cfg->height, ESP_ERR_INVALID_ARG,
                        TAG, "bad cfg");

    s_w = (int16_t)cfg->width;
    s_h = (int16_t)cfg->height;
    dirty_reset();

    s_tft.configure(cfg);
    if (!s_tft.init()) {
        ESP_LOGE(TAG, "LovyanGFX init failed");
        return ESP_FAIL;
    }
    if (cfg->rotate_landscape) {
        s_tft.setRotation(3);
    }

    uint32_t id = s_tft.getDisplayId();
    const char *kind = "unknown";
    if (id == LCD_ID_ORIGINAL)        kind = "ST7789 original";
    else if (id == LCD_ID_HIGH_GAMMA) kind = "ST7789 high-gamma";
    else if (id == LCD_ID_MIRRORED)   kind = "GC9307 mirrored";
    ESP_LOGI(TAG, "LovyanGFX init ok (%ux%u, panel 0x%06lX %s, pclk %d MHz)",
             cfg->width, cfg->height, (unsigned long)id, kind,
             LCD_FREQ_WRITE_HZ / 1000000);

    /* 스프라이트 생성 (PSRAM 우선) */
    size_t need = (size_t)cfg->width * cfg->height * 2;
    s_sprite = new lgfx::LGFX_Sprite(&s_tft);
    if (s_sprite) {
        s_sprite->setColorDepth(16);
        s_sprite->setPsram(cfg->use_psram);
        if (s_sprite->createSprite(cfg->width, cfg->height)) {
            s_use_sprite = true;
            s_sprite->fillScreen(0);
            ESP_LOGI(TAG, "sprite %ux%u (%u KB, %s)", cfg->width, cfg->height,
                     (unsigned)(need / 1024), cfg->use_psram ? "PSRAM" : "internal");
        } else {
            ESP_LOGW(TAG, "sprite alloc failed — direct render");
            delete s_sprite;
            s_sprite = nullptr;
        }
    }

    ESP_RETURN_ON_ERROR(backlight_init(cfg->pin_bl, 0), TAG, "backlight");
    s_inited = true;
    return ESP_OK;
}

bool display_has_sprite(void)
{
    return s_use_sprite;
}

esp_err_t display_capture_png(void **data, size_t *len)
{
    if (!s_inited || !s_use_sprite || !data || !len) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t n = 0;
    void *png = s_sprite->createPng(&n, 0, 0, s_w, s_h);
    if (!png || n == 0) {
        free(png);
        return ESP_FAIL;
    }
    *data = png;
    *len = n;
    return ESP_OK;
}

void display_capture_free(void *data)
{
    free(data);
}

esp_err_t display_fill_screen(uint16_t color)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not init");
    canvas()->fillRect(0, 0, s_w, s_h, color);
    display_invalidate_all();
    return ESP_OK;
}

esp_err_t display_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            uint16_t color)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not init");
    canvas()->fillRect(x, y, w, h, color);
    display_mark_dirty(x, y, w, h);
    return ESP_OK;
}

esp_err_t display_draw_qr(const char *text, int16_t x, int16_t y,
                          uint8_t scale, uint16_t fg, uint16_t bg)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not init");
    ESP_RETURN_ON_FALSE(text && scale > 0, ESP_ERR_INVALID_ARG, TAG, "bad qr");

    static uint8_t modules[256];
    QRCode qr;
    if (lgfx_qrcode_initText(&qr, modules, 3, ECC_LOW, text) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t w = (int16_t)qr.size * scale;
    canvas()->fillRect(x, y, w, w, bg);
    for (uint8_t my = 0; my < qr.size; ++my) {
        for (uint8_t mx = 0; mx < qr.size; ++mx) {
            if (lgfx_qrcode_getModule(&qr, mx, my)) {
                canvas()->fillRect(x + mx * scale, y + my * scale,
                                   scale, scale, fg);
            }
        }
    }
    display_mark_dirty(x, y, (uint16_t)w, (uint16_t)w);
    return ESP_OK;
}

esp_err_t display_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                            uint16_t color)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not init");
    canvas()->drawLine(x0, y0, x1, y1, color);
    int16_t x = x0 < x1 ? x0 : x1;
    int16_t y = y0 < y1 ? y0 : y1;
    uint16_t w = (uint16_t)((x0 < x1 ? x1 - x0 : x0 - x1) + 1);
    uint16_t h = (uint16_t)((y0 < y1 ? y1 - y0 : y0 - y1) + 1);
    display_mark_dirty(x, y, w, h);
    return ESP_OK;
}

esp_err_t display_set_clip(int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not init");
    canvas()->setClipRect(x, y, w, h);
    return ESP_OK;
}

void display_clear_clip(void)
{
    if (s_inited) {
        canvas()->clearClipRect();
    }
}

esp_err_t display_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            uint16_t color)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not init");
    canvas()->drawRect(x, y, w, h, color);
    display_mark_dirty(x, y, w, h);
    return ESP_OK;
}

esp_err_t display_fill_round_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                                  uint16_t r, uint16_t color)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not init");
    canvas()->fillRoundRect(x, y, w, h, r, color);
    display_mark_dirty(x, y, w, h);
    return ESP_OK;
}

esp_err_t display_draw_round_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                                  uint16_t r, uint16_t color)
{
    ESP_RETURN_ON_FALSE(s_inited, ESP_ERR_INVALID_STATE, TAG, "not init");
    canvas()->drawRoundRect(x, y, w, h, r, color);
    display_mark_dirty(x, y, w, h);
    return ESP_OK;
}

static const lgfx::IFont *font_of(display_font_t font)
{
    switch (font) {
    case DISPLAY_FONT_TINY:  return &lgfx::fonts::Font0;
    case DISPLAY_FONT_SMALL: return &lgfx::fonts::Font2;
    case DISPLAY_FONT_LARGE: return &lgfx::fonts::Font4;
    case DISPLAY_FONT_DIGIT: return &lgfx::fonts::Font7;
    case DISPLAY_FONT_HERO:  return &lgfx::fonts::Font6;
    case DISPLAY_FONT_TEXT48: return &lgfx::fonts::AsciiFont24x48;
    }
    return &lgfx::fonts::Font0;
}

static lgfx::textdatum_t datum_of(display_datum_t datum)
{
    switch (datum) {
    case DISPLAY_DATUM_TC: return lgfx::TC_DATUM;
    case DISPLAY_DATUM_TR: return lgfx::TR_DATUM;
    case DISPLAY_DATUM_ML: return lgfx::ML_DATUM;
    case DISPLAY_DATUM_MC: return lgfx::MC_DATUM;
    case DISPLAY_DATUM_MR: return lgfx::MR_DATUM;
    case DISPLAY_DATUM_BL: return lgfx::BL_DATUM;
    case DISPLAY_DATUM_BC: return lgfx::BC_DATUM;
    case DISPLAY_DATUM_BR: return lgfx::BR_DATUM;
    case DISPLAY_DATUM_TL: break;
    }
    return lgfx::TL_DATUM;
}

uint16_t display_draw_text(int16_t x, int16_t y, const char *text,
                           display_font_t font, uint16_t fg, uint16_t bg,
                           display_datum_t datum)
{
    if (!text || !*text) {
        return 0;
    }
    if (!s_inited) {
        return 0;
    }
    lgfx::LovyanGFX *c = canvas();
    const lgfx::IFont *f = font_of(font);
    c->setFont(f);
    c->setTextColor(fg, bg);
    c->setTextDatum(datum_of(datum));
    c->drawString(text, x, y);
    uint16_t w = (uint16_t)c->textWidth(text);
    int16_t th = (int16_t)c->fontHeight();
    /* 대략적인 텍스트 영역 dirty 표시 */
    int16_t tx = x, ty = y;
    switch (datum) {
    case DISPLAY_DATUM_TC: tx = (int16_t)(x - w / 2); ty = y; break;
    case DISPLAY_DATUM_TR: tx = (int16_t)(x - w);     ty = y; break;
    case DISPLAY_DATUM_ML: tx = x;          ty = (int16_t)(y - th / 2); break;
    case DISPLAY_DATUM_MC: tx = (int16_t)(x - w / 2); ty = (int16_t)(y - th / 2); break;
    case DISPLAY_DATUM_MR: tx = (int16_t)(x - w);     ty = (int16_t)(y - th / 2); break;
    case DISPLAY_DATUM_BL: tx = x;          ty = (int16_t)(y - th); break;
    case DISPLAY_DATUM_BC: tx = (int16_t)(x - w / 2); ty = (int16_t)(y - th); break;
    case DISPLAY_DATUM_BR: tx = (int16_t)(x - w);     ty = (int16_t)(y - th); break;
    case DISPLAY_DATUM_TL: break;
    }
    display_mark_dirty(tx, ty, w, (uint16_t)th);
    return w;
}

uint16_t display_text_width(const char *text, display_font_t font)
{
    if (!text || !*text) {
        return 0;
    }
    if (!s_inited) {
        return 0;
    }
    lgfx::LovyanGFX *c = canvas();
    c->setFont(font_of(font));
    return (uint16_t)c->textWidth(text);
}

uint16_t display_draw_text_scaled(int16_t x, int16_t y, const char *text,
                                  display_font_t font, uint16_t fg, uint16_t bg,
                                  display_datum_t datum, uint8_t scale)
{
    if (!text || !*text || !s_inited) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    lgfx::LovyanGFX *c = canvas();
    const lgfx::IFont *f = font_of(font);
    c->setFont(f);
    c->setTextSize(scale);
    c->setTextColor(fg, bg);
    c->setTextDatum(datum_of(datum));
    c->drawString(text, x, y);
    uint16_t w = (uint16_t)c->textWidth(text);
    int16_t th = (int16_t)c->fontHeight();
    c->setTextSize(1);

    int16_t tx = x, ty = y;
    switch (datum) {
    case DISPLAY_DATUM_TC: tx = (int16_t)(x - w / 2); ty = y; break;
    case DISPLAY_DATUM_TR: tx = (int16_t)(x - w);     ty = y; break;
    case DISPLAY_DATUM_ML: tx = x;          ty = (int16_t)(y - th / 2); break;
    case DISPLAY_DATUM_MC: tx = (int16_t)(x - w / 2); ty = (int16_t)(y - th / 2); break;
    case DISPLAY_DATUM_MR: tx = (int16_t)(x - w);     ty = (int16_t)(y - th / 2); break;
    case DISPLAY_DATUM_BL: tx = x;          ty = (int16_t)(y - th); break;
    case DISPLAY_DATUM_BC: tx = (int16_t)(x - w / 2); ty = (int16_t)(y - th); break;
    case DISPLAY_DATUM_BR: tx = (int16_t)(x - w);     ty = (int16_t)(y - th); break;
    case DISPLAY_DATUM_TL: break;
    }
    display_mark_dirty(tx, ty, w, (uint16_t)th);
    return w;
}

uint16_t display_text_width_scaled(const char *text, display_font_t font,
                                   uint8_t scale)
{
    if (!text || !*text || !s_inited) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }
    lgfx::LovyanGFX *c = canvas();
    c->setFont(font_of(font));
    c->setTextSize(scale);
    uint16_t w = (uint16_t)c->textWidth(text);
    c->setTextSize(1);
    return w;
}

uint16_t display_font_height(display_font_t font)
{
    if (!s_inited) {
        return 0;
    }
    lgfx::LovyanGFX *c = canvas();
    c->setFont(font_of(font));
    return (uint16_t)c->fontHeight();
}

esp_err_t display_set_backlight_level(uint8_t percent)
{
    return backlight_level(percent);
}

esp_err_t display_set_backlight(bool on)
{
    return backlight_level(on ? 100 : 0);
}
