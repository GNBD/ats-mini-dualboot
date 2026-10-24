/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * Shared application state.
 */
#include "app.h"

const uint32_t FM_STEPS[FM_STEP_COUNT]   = {50000, 100000};
const uint32_t AM_STEPS[AM_STEP_COUNT]   = {1000, 5000, 9000, 10000};
const uint32_t SSB_STEPS[SSB_STEP_COUNT] = {10, 100, 1000, 5000, 10000};

si4732_t           s_radio;
i2c_master_bus_handle_t s_i2c_bus;
bool               s_radio_ok;
bool               s_powered;
bool               s_ssb_prepared;
ui_state_t         s_ui;
storage_settings_t s_cfg;
preset_bank_t      s_presets;
rds_info_t         s_rds;
battery_state_t    s_batt;

hmi_t s_hmi = HMI_TUNE;

int     s_step_idx[3] = {-1, -1, -1};
uint8_t s_bw[3] = {0, 3, 1};
int32_t s_bfo;
uint8_t s_brightness = 100;
bool    s_power_save;
bool    s_rds_on = true;
bool    s_stereo_force = false;
bool    s_deemph_75 = false;
uint16_t s_sleep_min = 0;
uint32_t s_sleep_deadline;
bool     s_sleep_armed;

storage_ext_t s_ext;
bool     s_agc_on = true;
uint8_t  s_softmute = 4;
uint8_t  s_avc = 60;
bool     s_squelch_on = false;
uint8_t  s_squelch_type[4] = {0, 0, 0, 0};
uint8_t  s_squelch_thr[4]  = {4, 4, 4, 4};
bool     s_squelch_muted = false;

int      s_cpu_mhz = 240;
uint8_t  s_batt_view = 3;
int16_t  s_batt_cal_mv;
uint16_t s_screen_sec = 0;
uint8_t  s_unit_mode = 0;
uint8_t  s_ui_mode = 1;
bool     s_setup_done;
bool     s_digit_mode;
int      s_digit_place;
volatile uint32_t s_radio_draw_seq;
char s_wifi_err[24];
char s_ap_pass[9] = "12345678";
uint8_t s_theme;
bool     s_screen_off = false;
uint32_t s_last_input_ms;
uint32_t s_list_redraw_ms;
int      s_ts_year;
int      s_ts_mon;
int      s_ts_day;
int      s_ts_hour;
int      s_ts_min;
int      s_ts_field;

uint8_t  s_wifi_mode = 0;
char     s_wifi_ssid[33];
char     s_wifi_pass[65];
bool     s_wifi_ready = false;
bool     s_wifi_started = false;
volatile bool s_scan_done = false;
volatile bool s_wifi_got_ip = false;
bool     s_scan_running = false;
uint16_t s_scan_n = 0;
wifi_ap_record_t s_scan_recs[16];
httpd_handle_t s_http = NULL;
volatile bool s_sta_connected = false;
char     s_sta_ip[20];
volatile int s_wifi_rssi;
char     s_fav_lbl[STORAGE_PRESET_SLOTS + 1][8];
char     s_fav_val[STORAGE_PRESET_SLOTS + 1][24];
const char *s_fav_lblp[STORAGE_PRESET_SLOTS + 1];
const char *s_fav_valp[STORAGE_PRESET_SLOTS + 1];
ui_list_t s_fav_ui;
int      s_fav_cursor;
int      s_fav_top;
char     s_ats_lbl[STORAGE_ATS_SLOTS + 1][8];
char     s_ats_val[STORAGE_ATS_SLOTS + 1][24];
const char *s_ats_lblp[STORAGE_ATS_SLOTS + 1];
const char *s_ats_valp[STORAGE_ATS_SLOTS + 1];
ui_list_t s_ats_ui;
uint32_t s_ats_freq;
uint32_t s_ats_wait;
uint32_t s_ats_step;
bool     s_ats_hold;
int      s_ats_cur;
int      s_ats_top;
ats_bank_t    s_ats_list;
const char *s_scan_ptr[16];
char     s_scan_buf[16][33];
bool     s_scan_secure[16];
int      s_scan_rssi[16];
int      s_wifi_cursor;
int      s_scan_cursor;
int      s_scan_top;

QueueHandle_t s_web_q;

ui_keypad_t s_kp;
char     s_kp_ssid[33];

int64_t  s_clock_base;
uint32_t s_clock_ms0;
bool     s_clock_set;
int16_t  s_utc_min;
bool     s_follow_browser;
bool     s_sntp_started;
uint32_t s_save_ms;
uint32_t s_periodic_ms;
int      s_scan_anim_ms;

volatile int s_eibi_state;
bool     s_fs_mounted;
char     s_station[120];
uint32_t s_eibi_ms;
uint32_t s_nav_ms;
int      s_eibi_entries;
int      s_eibi_bytes;
uint32_t s_eibi_dl_ms;
int      s_eibi_cursor;
int      s_eibi_view_cur;
int      s_eibi_view_top;
bool     s_eibi_force;
bool     s_eibi_yes;

storage_splash_t s_splash;

uint16_t s_quick_mask = 0x000F;
int      s_qcfg_cur;
int      s_qcfg_top;
const char *const QA_LABELS[QA_COUNT] = {
    "VOLUME", "ATS LIST", "FAVORITES", "SCREEN OFF", "SEEK", "SCAN",
    "ATS RUN", "RDS", "STEREO", "BAND", "STEP", "BW", "SETTINGS"
};
int      s_splash_cursor;

uint32_t s_change_ms;
bool s_dirty;
uint32_t s_vol_ms;
bool     s_vol_from_menu;
int      s_home_sel;
char     s_weather_location[48];
uint8_t  s_weather_mode;
char     s_toast[40];
uint32_t s_toast_ms;
bool     s_seeking;
uint32_t s_sw_orig;
uint32_t s_sw_step;
int      s_sw_dir;
int      s_sw_steps;
int      s_sw_max;
int      s_sw_rssi;
int      s_sw_snr;
bool     s_sw_waiting;
uint32_t s_sw_wait_until;

int32_t  s_pending_steps;
uint32_t s_last_tune_ms;
bool     s_freq_dirty;

ats_state_t s_ats;
uint32_t    s_ats_ms;
bool        s_ats_autolist;
uint32_t    s_ats_show_until;
uint32_t    s_ats_last_freq;
uint8_t     s_ats_found;
int         s_ats_band;

uint8_t  s_spec_rssi[SPEC_SCAN_MAX];
uint8_t  s_spec_snr[SPEC_SCAN_MAX];
int      s_spec_count;
int      s_spec_idx;
int      s_spec_cursor;
int      s_spec_band;
uint32_t s_spec_min;
uint32_t s_spec_max;
uint32_t s_spec_step;
uint32_t s_spec_freq;
uint32_t s_spec_orig;
uint32_t s_spec_wait;
bool     s_spec_active;
bool     s_spec_done;

uint32_t s_band_freq[16];
bool     s_band_mem_valid[16];

ui_list_t s_rmenu;
ui_list_t s_settings;
char s_rbuf[RM_COUNT][20];
char s_sbuf[ST_COUNT][24];
const char *s_rv[RM_COUNT];
const char *s_sv[ST_COUNT];

ui_options_t s_opt;
int s_opt_page;
int s_opt_item;
char s_opt_buf[UI_OPT_MAX][24];
const char *s_opt_ptr[UI_OPT_MAX];
