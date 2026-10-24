/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Shared application state and cross-module interfaces.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/i2c_master.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "si4732.h"
#include "ui.h"
#include "storage.h"
#include "rds.h"
#include "battery.h"
#include "version.h"
#include "eibi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TICK_MS(ms)        pdMS_TO_TICKS(ms)
#define VOL_TIMEOUT        3000
#define MENU_IDLE_MS       5000
#define LIST_IDLE_MS       300000
#define OPT_TIMEOUT        20000
#define TOAST_TIME         2000
#define SAVE_DELAY         2000
#define SEEK_TIMEOUT       10000
#define RSQ_PERIOD         250
#define RDS_PERIOD         500
#define BATT_PERIOD        2000
#define TUNE_COALESCE_MS   15
#define BFO_MAX_HZ         5000

enum {
    RM_BACK = 0,
    /* 수신 설정 */
    RM_BAND, RM_MODE, RM_STEP, RM_BW, RM_BFO,
    /* 탐색 */
    RM_SEEK, RM_SCAN, RM_ATS,
    /* 프리셋 / RDS */
    RM_FAV, RM_RDS, RM_STEREO, RM_DEEMPH,
    /* 오디오 */
    RM_VOLUME, RM_AGC, RM_AVC, RM_SOFTMUTE, RM_SQUELCH,
    RM_SETTINGS, RM_COUNT
};

enum {
    ST_BACK = 0,
    /* 표시/전원 */
    ST_BRIGHT, ST_SCREEN, ST_SLEEP,
    /* 표시 형식 */
    ST_UNIT, ST_THEME,
    /* 시계 */
    ST_TIME, ST_UTC,
    /* 시스템 */
    ST_BATT, ST_BATT_CAL, ST_CPU, ST_WIFI,
    /* 데이터 */
    ST_EIBI, ST_QUICKSET,
    ST_ABOUT, ST_COUNT
};

/* 화면 레이아웃 모드 */
enum {
    UI_MODE_EASY = 0,
    UI_MODE_STANDARD,
    UI_MODE_COUNT
};

#define FM_STEP_COUNT   2
#define AM_STEP_COUNT   4
#define SSB_STEP_COUNT  5

extern const uint32_t FM_STEPS[FM_STEP_COUNT];
extern const uint32_t AM_STEPS[AM_STEP_COUNT];
extern const uint32_t SSB_STEPS[SSB_STEP_COUNT];

typedef enum {
    HMI_TUNE, HMI_VOL, HMI_MENU, HMI_SET, HMI_OPT, HMI_SEEK,
    HMI_WIFIAPP, HMI_SCANNING, HMI_WIFISCAN,
    HMI_KEYPAD, HMI_TIMESET, HMI_FAV, HMI_ATS_LIST, HMI_EIBI,
    HMI_EIBI_DL, HMI_EIBI_CONFIRM, HMI_EIBI_VIEW, HMI_SPLASH, HMI_ABOUT,
    HMI_HOME, HMI_CLOCK, HMI_WEATHER, HMI_SCAN, HMI_QUICK, HMI_QUICKCFG
} hmi_t;

/* 퀵 메뉴 동작 */
enum {
    QA_VOLUME = 0, QA_ATS_LIST, QA_FAVORITES, QA_SCREEN_OFF, QA_SEEK,
    QA_SCAN, QA_ATS_RUN, QA_RDS, QA_STEREO, QA_BAND, QA_STEP, QA_BW, QA_SETTINGS,
    QA_COUNT
};
#define QUICK_MAX 6

typedef enum { ATS_IDLE, ATS_SEEK, ATS_DONE } ats_state_t;

typedef enum {
    WEB_CMD_NONE = 0,
    WEB_CMD_TUNE_STEP,
    WEB_CMD_TUNE_FREQ,
    WEB_CMD_VOLUME,
    WEB_CMD_BAND,
    WEB_CMD_MODE,
    WEB_CMD_PRESET_TUNE,
    WEB_CMD_PRESET_SAVE,
    WEB_CMD_PRESET_DEL,
    WEB_CMD_WIFI_OFF,
    WEB_CMD_WIFI_AP,
    WEB_CMD_WIFI_STA,
    WEB_CMD_WIFI_SCAN,
    WEB_CMD_RADIO,
    WEB_CMD_UTC,
    WEB_CMD_TIME,
    WEB_CMD_FOLLOW,
    WEB_CMD_SETUP_FINISH,
} web_cmd_type_t;

typedef struct {
    uint8_t type;
    int32_t a;
    int32_t b;
    char    s1[33];
    char    s2[65];
} web_cmd_t;

extern si4732_t           s_radio;
extern i2c_master_bus_handle_t s_i2c_bus;
extern bool               s_radio_ok;
extern bool               s_powered;
extern bool               s_ssb_prepared;
extern ui_state_t         s_ui;
extern storage_settings_t s_cfg;
extern preset_bank_t      s_presets;
extern rds_info_t         s_rds;
extern battery_state_t    s_batt;

extern hmi_t s_hmi;

extern int     s_step_idx[3];
extern uint8_t s_bw[3];
extern int32_t s_bfo;
extern uint8_t s_brightness;
extern bool    s_rds_on;
extern bool    s_stereo_force;
extern bool    s_deemph_75;
extern uint16_t s_sleep_min;
extern uint32_t s_sleep_deadline;
extern bool     s_sleep_armed;

extern storage_ext_t s_ext;
extern bool     s_agc_on;
extern uint8_t  s_softmute;
extern uint8_t  s_avc;
extern bool     s_squelch_on;
extern uint8_t  s_squelch_type[4];
extern uint8_t  s_squelch_thr[4];
extern bool     s_squelch_muted;

extern int      s_cpu_mhz;
extern uint8_t  s_batt_view;
extern int16_t  s_batt_cal_mv;
extern uint16_t s_screen_sec;
extern uint8_t  s_unit_mode;
extern bool     s_screen_off;
extern uint32_t s_last_input_ms;
extern uint32_t s_list_redraw_ms;
extern int      s_ts_year;
extern int      s_ts_mon;
extern int      s_ts_day;
extern int      s_ts_hour;
extern int      s_ts_min;
extern int      s_ts_field;

extern uint8_t  s_wifi_mode;
extern char     s_wifi_ssid[33];
extern char     s_wifi_pass[65];
extern bool     s_wifi_ready;
extern bool     s_wifi_started;
extern volatile bool s_scan_done;
extern volatile bool s_wifi_got_ip;
extern bool     s_scan_running;
extern uint16_t s_scan_n;
extern wifi_ap_record_t s_scan_recs[16];
extern httpd_handle_t s_http;
extern volatile bool s_sta_connected;
extern char     s_sta_ip[20];
extern volatile int s_wifi_rssi;
extern char     s_fav_lbl[STORAGE_PRESET_SLOTS + 1][8];
extern char     s_fav_val[STORAGE_PRESET_SLOTS + 1][24];
extern const char *s_fav_lblp[STORAGE_PRESET_SLOTS + 1];
extern const char *s_fav_valp[STORAGE_PRESET_SLOTS + 1];
extern ui_list_t s_fav_ui;
extern int      s_fav_cursor;
extern int      s_fav_top;
extern char     s_ats_lbl[STORAGE_ATS_SLOTS + 1][8];
extern char     s_ats_val[STORAGE_ATS_SLOTS + 1][24];
extern const char *s_ats_lblp[STORAGE_ATS_SLOTS + 1];
extern const char *s_ats_valp[STORAGE_ATS_SLOTS + 1];
extern ui_list_t s_ats_ui;
extern uint32_t s_ats_freq;
extern uint32_t s_ats_wait;
extern uint32_t s_ats_step;
extern bool     s_ats_hold;
extern int      s_ats_cur;
extern int      s_ats_top;
extern ats_bank_t s_ats_list;
extern const char *s_scan_ptr[16];
extern char     s_scan_buf[16][33];
extern bool     s_scan_secure[16];
extern int      s_scan_rssi[16];
extern int      s_wifi_cursor;
extern int      s_scan_cursor;
extern int      s_scan_top;

extern QueueHandle_t s_web_q;

extern ui_keypad_t s_kp;
extern char     s_kp_ssid[33];

extern int64_t  s_clock_base;
extern uint32_t s_clock_ms0;
extern bool     s_clock_set;
extern int16_t  s_utc_min;
extern bool     s_follow_browser;
extern bool     s_sntp_started;
extern uint8_t  s_ui_mode;
extern bool     s_setup_done;
extern bool     s_digit_mode;
extern int      s_digit_place;
extern volatile uint32_t s_radio_draw_seq;
extern char s_wifi_err[24];
extern char s_ap_pass[9];
extern uint8_t s_theme;
extern uint32_t s_save_ms;
extern uint32_t s_periodic_ms;
extern int      s_scan_anim_ms;

extern volatile int s_eibi_state;
extern bool     s_fs_mounted;
extern char     s_station[120];
extern uint32_t s_eibi_ms;
extern uint32_t s_nav_ms;
extern int      s_eibi_entries;
extern int      s_eibi_bytes;
extern uint32_t s_eibi_dl_ms;
extern int      s_eibi_cursor;
extern int      s_eibi_view_cur;
extern int      s_eibi_view_top;
extern bool     s_eibi_force;
extern bool     s_eibi_yes;

extern storage_splash_t s_splash;
extern int      s_splash_cursor;
extern uint16_t s_quick_mask;
extern int      s_qcfg_cur;
extern int      s_qcfg_top;
extern const char *const QA_LABELS[QA_COUNT];

extern uint32_t s_change_ms;
extern bool     s_dirty;
extern uint32_t s_vol_ms;
extern bool     s_vol_from_menu;
extern int      s_home_sel;
extern char     s_weather_location[48];
extern uint8_t  s_weather_mode;
extern char     s_toast[40];
extern uint32_t s_toast_ms;
extern bool     s_seeking;
extern uint32_t s_sw_orig;
extern uint32_t s_sw_step;
extern int      s_sw_dir;
extern int      s_sw_steps;
extern int      s_sw_max;
extern int      s_sw_rssi;
extern int      s_sw_snr;
extern bool     s_sw_waiting;
extern uint32_t s_sw_wait_until;
extern int32_t  s_pending_steps;
extern uint32_t s_last_tune_ms;
extern bool     s_freq_dirty;

extern ats_state_t s_ats;
extern uint32_t    s_ats_ms;
extern bool        s_ats_autolist;
extern uint32_t    s_ats_show_until;
extern uint32_t    s_ats_last_freq;
extern uint8_t     s_ats_found;
extern int         s_ats_band;

#define SPEC_SCAN_MAX 5000
extern uint8_t  s_spec_rssi[SPEC_SCAN_MAX];
extern uint8_t  s_spec_snr[SPEC_SCAN_MAX];
extern int      s_spec_count;
extern int      s_spec_idx;
extern int      s_spec_cursor;
extern int      s_spec_band;
extern uint32_t s_spec_min;
extern uint32_t s_spec_max;
extern uint32_t s_spec_step;
extern uint32_t s_spec_freq;
extern uint32_t s_spec_orig;
extern uint32_t s_spec_wait;
extern bool     s_spec_active;
extern bool     s_spec_done;

extern uint32_t s_band_freq[16];
extern bool     s_band_mem_valid[16];

extern const char *const RM_LABELS[RM_COUNT];
extern const char *const ST_LABELS[ST_COUNT];
extern ui_list_t s_rmenu;
extern ui_list_t s_settings;
extern char s_rbuf[RM_COUNT][20];
extern char s_sbuf[ST_COUNT][24];
extern const char *s_rv[RM_COUNT];
extern const char *s_sv[ST_COUNT];

extern ui_options_t s_opt;
extern int s_opt_page;
extern int s_opt_item;
extern char s_opt_buf[UI_OPT_MAX][24];
extern const char *s_opt_ptr[UI_OPT_MAX];


/* Cross-module functions (mechanical split, to be narrowed later). */
esp_err_t board_power_init(void);
void board_amp(bool on);
uint32_t now_ms(void);
int wrap(int v, int n);
int16_t accelerate_encoder(int8_t dir);
int accel_counts(int detents);
int mode_slot(radio_mode_t m);
const char *mode_str(radio_mode_t m);
int step_count(int slot);
uint32_t step_at(int slot, int idx);
uint32_t eff_step_hz(void);
void sync_step(void);
void mark_change(void);
void toast(const char *text);
bool toast_active(void);
void toast_redraw(void);
esp_err_t radio_set_volume(void);
int mode_dsp_slot(radio_mode_t m);
void dsp_apply(void);
esp_err_t cpu_apply(int mhz);
void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data);
void sntp_start(void);
esp_err_t wifi_init_once(void);
void json_escape(char *dst, size_t n, const char *src);
void html_escape(char *dst, size_t n, const char *src);
int web_nav(char *dst, size_t n, int active);
esp_err_t http_root_handler(httpd_req_t *req);
void url_decode(char *s);
void fmt_freq_web(uint32_t hz, radio_mode_t mode, char *buf, size_t n);
const char *wifi_state_str(void);
const char *wifi_ip_str(void);
esp_err_t http_status_handler(httpd_req_t *req);
esp_err_t http_wifi_api_handler(httpd_req_t *req);
bool web_cmd_push(const web_cmd_t *c);
bool web_back_target(const char *back, char *out, size_t n);
esp_err_t http_cmd_handler(httpd_req_t *req);
esp_err_t http_info_handler(httpd_req_t *req);
esp_err_t http_presets_handler(httpd_req_t *req);
esp_err_t http_wifi_handler(httpd_req_t *req);
esp_err_t http_settings_handler(httpd_req_t *req);
void http_start(void);
void http_stop(void);
void wifi_off(void);
esp_err_t wifi_boot(wifi_mode_t mode);
esp_err_t wifi_start_now(void);
esp_err_t wifi_start_ap(void);
esp_err_t wifi_sta_connect(void);
esp_err_t wifi_start_apsta(void);
esp_err_t wifi_setup_connect(const char *ssid, const char *pass);
void wifi_scan_start(void);
void open_splash_app(void);
void open_splash_keypad(int welcome);
void open_quick_cfg(void);
void open_about(void);
void about_page_move(int dir);
esp_err_t radio_ensure_mode(void);
esp_err_t radio_tune(void);
esp_err_t radio_apply(void);
void apply_step_snap(void);
void set_band(int idx);
bool seek_start(bool up);
esp_err_t seek_tune(uint32_t freq);
void seek_finish(bool found);
void seek_cancel(void);
void seek_end(void);
void seek_stop(void);
void seek_reverse(bool up);
void seek_poll(uint32_t now);
void ats_cancel(void);
void ats_finish(const char *msg);
void ats_start(void);
void ats_poll(uint32_t now);
void spectrum_scan_start(void);
void spectrum_scan_poll(uint32_t now);
void spectrum_scan_cancel(void);
void spectrum_scan_apply(void);
const char *bw_label(void);
int bw_max(void);
int bw_index(int slot, int code);
int16_t radio_bfo_value(void);
esp_err_t bw_apply(void);
int preset_index_for_freq(uint32_t freq, int band);
int ats_index_for_freq(uint32_t freq, int band);
void fmt_freq_short(uint32_t hz, int mode, char *buf, size_t n);
void rmenu_build(void);
void settings_build(void);
void opt_label(int i, const char *text);
int opt_sel(int page, int item);
void options_open(int page, int item);
void opt_apply(int page, int item, int idx);
void page_radio(void);
void page_menu(void);
void page_set(void);
void page_redraw(void);
void side_move(ui_list_t *l, ui_tab_t tab, int d);
int64_t days_from_civil(int y, int m, int d);
void civil_from_days(int64_t z, int *y, int *m, int *d);
int days_in_month(int y, int m);
void local_datetime(int *y, int *mo, int *d, int *h, int *mi);
void timeset_draw(void);
void open_time_set(void);
void timeset_move(int d);
void timeset_save(void);
void wifi_app_draw(void);
void open_wifi_app(void);
void start_scan_page(void);
void build_scan_arrays(void);
void scan_list_draw(void);
void open_scan_list(void);
void scan_list_move(int d);
void tune_entry(const preset_entry_t *p);
void fav_build(void);
void fav_side_draw(void);
void fav_draw(void);
void open_favorites(void);
void preview_entry(const preset_entry_t *p);
void fav_move(int d);
void fav_apply(void);
void fav_long(void);
void ats_build(void);
void ats_side_draw(void);
void open_ats_list(void);
void ats_list_move(int d);
void ats_list_apply(void);
void ats_list_long(void);
void wifi_app_action(void);
void open_keypad(const char *ssid);
void keypad_move(int d);
void keypad_act(void);
bool menu_item_is_action(int item);
void menu_action(int item);
void options_close(bool apply);
void options_move(int d);
void tune_commit(uint32_t now);
void on_encoder(int d);
void on_button(bool long_press);
void button_poll(uint32_t now);
void rsq_poll(uint32_t now);
void rds_poll(uint32_t now);
void battery_poll(uint32_t now);
void snapshot_fill(void);
void do_save(void);
void save_flush(uint32_t now);
void ui_refresh_top(void);
void clock_update(void);
void sleep_check(uint32_t now);
void restore_settings(const storage_settings_t *cfg);
void web_cmd_execute(const web_cmd_t *c);
void web_poll(void);

#ifdef __cplusplus
}
#endif
