/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * 페이지형 UI (320x170).
 *
 *   RADIO  : 큰 주파수 + S/N 미터 + RDS + 하단 상태
 *   MENU   : 라디오 옵션 리스트 (BAND/MODE/BW/STEP/SEEK/PRESET/STORE/ATS/RDS/...)
 *   SET    : 하드웨어/잡설정 리스트 (BRIGHT/SLEEP/BATT)
 *
 * 값을 바꿀 때는 세로 옵션 목록 오버레이로 전체 선택지를 한눈에 보여준다.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "band.h"
#include "si4732.h"
#include "battery.h"
#include "rds.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_W 320
#define UI_H 170

/* iOS/macOS 다크 팔레트 */
#define UI_RGB(r, g, b) \
    ((uint16_t)(((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31))))

enum {
    UI_P_BG = 0, UI_P_PANEL, UI_P_HL, UI_P_HSEL, UI_P_CELL,
    UI_P_LINE, UI_P_TRACK, UI_P_FG, UI_P_MUTED, UI_P_ACC,
    UI_P_ACC_HI, UI_P_GOLD, UI_P_OK, UI_P_WARN, UI_P_RED,
    UI_P_BLUE, UI_P_COUNT
};

extern uint16_t ui_pal[UI_P_COUNT];

#define UI_BG       (ui_pal[UI_P_BG])
#define UI_PANEL    (ui_pal[UI_P_PANEL])
#define UI_HL       (ui_pal[UI_P_HL])
#define UI_HSEL     (ui_pal[UI_P_HSEL])
#define UI_CELL     (ui_pal[UI_P_CELL])
#define UI_LINE     (ui_pal[UI_P_LINE])
#define UI_TRACK    (ui_pal[UI_P_TRACK])
#define UI_FG       (ui_pal[UI_P_FG])
#define UI_MUTED    (ui_pal[UI_P_MUTED])
#define UI_ACC      (ui_pal[UI_P_ACC])
#define UI_ACC_HI   (ui_pal[UI_P_ACC_HI])
#define UI_GOLD     (ui_pal[UI_P_GOLD])
#define UI_OK       (ui_pal[UI_P_OK])
#define UI_WARN     (ui_pal[UI_P_WARN])
#define UI_RED      (ui_pal[UI_P_RED])
#define UI_BLUE     (ui_pal[UI_P_BLUE])
#define UI_BLACK   0x0000

#define UI_SIDE_ROWS 5
#define UI_OPT_ROWS  6
#define UI_OPT_MAX   128

typedef enum {
    UI_TAB_RADIO = 0,
    UI_TAB_MENU,
    UI_TAB_SET,
    UI_TAB_COUNT
} ui_tab_t;

typedef struct {
    const char *id;
    const char *name;
    const char *glyph;
    uint8_t     glyph_font;
    bool        needs_radio;
    void      (*launch)(void);
} ui_app_t;

typedef struct {
    int band_idx;
    uint32_t      freq_hz;
    radio_mode_t  mode;
    uint8_t       volume;        /* 0–63 */
    uint8_t       bw_code;
    int           step_hz;
    int16_t       bfo_hz;
    int           preset_slot;   /* -1 = 없음 */
    int           ats_slot;      /* ATS 매칭 슬롯 -1=없음 */
    bool          radio_ok;
    bool          patch_missing;
    bool          seeking;
    bool          seek_mode;
    int           sw_steps;
    int           sw_max;
    bool          stereo;
    bool          rds_on;
    bool          have_rsq;
    si4732_rsq_t  rsq;
    const rds_info_t *rds;
    bool          list_mode;     /* FAV/ATS 목록 표시 중 → hold=delete */
    battery_state_t battery;
    bool          have_battery;
    uint8_t       batt_view;     /* 0=icon,1=volt,2=%,3=%+icon,4=volt+icon */
    uint8_t       unit_mode;     /* 0=MHz, 1=kHz */
    uint8_t       ui_mode;       /* 0=easy, 1=standard */
    char          clock[6];      /* "HH:MM" */
    char          date[12];      /* "YYYY-MM-DD" */
    bool          save_active;
    uint8_t       wifi_state;    /* 0=off, 1=AP, 2=STA, 3=scan */
    int8_t        wifi_rssi;     /* STA 연결 신호 (dBm), 0=미연결 */
    const char   *station;       /* EiBi 스케줄 방송국 이름 (없으면 NULL) */
} ui_state_t;

/* 메뉴 표시 스타일 */
/* 세로 리스트 (MENU / SET 페이지) */
typedef struct {
    const char *const *labels;
    const char *const *values;
    int count;
    int cursor;
    int top;
} ui_list_t;

/* 세로 옵션 목록 / 값 슬라이더 오버레이 */
typedef struct {
    const char *title;
    const char *const *labels;
    int count;
    int cursor;
    int top;
    int sel;               /* 현재 적용된 값 (체크 표시), -1 없음 */
    bool slider;           /* true = 연속 값 슬라이더 */
    int min;
    int max;
    int step;
    int value;
    const char *unit;      /* 값 접미사 (예: "%", " dB", "m") */
    const char *zero_label;/* value==0 일 때 대신 표시 (예: "OFF") */
    bool live;             /* true = 회전 시 즉시 적용 */
} ui_options_t;

/* 온스크린 키패드 */
#define UI_KEY_COUNT 45
#define UI_KEY_DEL   40
#define UI_KEY_SHIFT 41
#define UI_KEY_SYM   42
#define UI_KEY_SPACE 43
#define UI_KEY_OK    44
#define UI_KEY_TEXT_MAX 65

typedef struct {
    const char *title;
    const char *subtitle;
    char text[UI_KEY_TEXT_MAX];
    int  len;
    int  cursor;
    bool shift;   /* 대소문자 전환 */
    bool sym;     /* 기호 레이어 */
} ui_keypad_t;

char ui_keypad_char(int idx, bool upper, bool sym);

void ui_init(void);
void ui_theme_set(int theme);
void ui_draw_all(const ui_state_t *s);        /* RADIO 페이지 전체 */
void ui_draw_radio_freq(const ui_state_t *s); /* 모드별 주파수 갱신 */
void ui_draw_dx_signal(const ui_state_t *s);
void ui_draw_dx_signal2(const ui_state_t *s);
void ui_draw_dx_signal3(const ui_state_t *s);
void ui_draw_adv_signal4(const ui_state_t *s);
void ui_draw_adv_signal4b(const ui_state_t *s);
void ui_draw_header(const ui_state_t *s);     /* RADIO 탭 헤더만 */
void ui_draw_freq(const ui_state_t *s);
void ui_draw_band_meta(const ui_state_t *s);
void ui_draw_freq_saved(void);
void ui_draw_smeter(const ui_state_t *s);
void ui_draw_rds(const ui_state_t *s);
void ui_draw_status(const ui_state_t *s);
void ui_draw_volume(const ui_state_t *s, bool show);
void ui_draw_loading(const char *msg);
void ui_draw_home(const ui_state_t *s, int cursor, const ui_app_t *apps,
                  int count);
void ui_draw_clock(const ui_state_t *s);
void ui_draw_setup_welcome(const char *name, const char *version,
                           int step, int total, int selected);
void ui_draw_weather(const ui_state_t *s, int state, int page,
                     const char *city, const char *resolved, const char *temp,
                     const char *cond, const char *humidity, const char *wind);
void ui_draw_spectrum(const ui_state_t *s, const char *band, uint32_t min_hz,
                      uint32_t max_hz, const uint8_t *rssi,
                      const uint8_t *snr, int count, int index, int cursor,
                      bool done);
void ui_draw_decode(const ui_state_t *s);
void ui_decode_refresh(void);
void ui_draw_hint(const ui_state_t *s, const char *toast);

void ui_draw_radio_panel(const ui_state_t *s);
void ui_draw_side_list(ui_tab_t tab, const ui_list_t *l, const ui_state_t *s);
void ui_draw_side_list_title(const char *title, const ui_list_t *l,
                             const ui_state_t *s);
void ui_draw_settings_page(const char *title, const ui_state_t *s,
                           const ui_list_t *l);
void ui_draw_options(const ui_options_t *o);
void ui_draw_keypad(const ui_keypad_t *k);
void ui_draw_time_set(int year, int month, int day, int hour, int minute,
                      int field, int utc_min);
void ui_draw_wifi_app(int cursor, uint8_t mode, const char *ssid,
                      const char *ip, int utc_min, const char *err);
void ui_draw_wifi_app2(int cursor, uint8_t mode, const char *ssid,
                       const char *ip, int utc_min, const char *err,
                       bool sel, int focus);
void ui_draw_wifi_app3(int cursor, uint8_t mode, const char *ssid,
                       const char *ip, int utc_min, const char *err,
                       bool sel, int focus);
void ui_draw_wifi_scanning(int dots);
void ui_draw_wifi_list(const char *title, const char *hint,
                       const char *const *names, const bool *secure,
                       const int *rssi, int count, int cursor, int top);
void ui_draw_eibi_app(int cursor, bool ready, int entries);
void ui_draw_splash_app(int cursor, bool on, int mode, const char *callsign,
                        const char *welcome);
void ui_draw_eibi_view(int cursor, int top, int total);
void ui_draw_eibi_dl(int entries, int bytes, int dots);
void ui_draw_confirm(const char *title, const char *msg, bool yes);
void ui_draw_quick(const ui_state_t *s, int cursor);
void ui_draw_quick_cfg(int cursor, int top, uint16_t mask);
void ui_draw_about(const char *version, const char *idf, const char *build,
                   int page);

#ifdef __cplusplus
}
#endif
