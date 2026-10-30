// SPDX-FileCopyrightText: 2025-2026 JIN (GNBD)
// SPDX-License-Identifier: MIT
// ATS Mini standalone recovery firmware (beta) - Boot Manager
// Network update + WiFi setting beta. Runs from its own partition.
//
// Boot sequence:
//   1. Power ON -> recovery always boots first
//   2. Wait 1 second for encoder press
//   3. No encoder -> auto boot to app0
//   4. Encoder held -> recovery menu (STA WiFi, WiFi section, update, etc.)
//   5. Web server runs in a background task (STA, or AP when not connected)
//   6. Recovery always sets itself as next boot target before jumping to app
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <esp_littlefs.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_partition.h>
#include <esp_flash.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <nvs_flash.h>
#include <MD5Builder.h>
#include <qrcode.h>
#include "Display.h"
#include "encoder.h"
#include <freertos/task.h>
#include <time.h>

#define PIN_LCD_BL          38
#define ENCODER_PIN_A        2
#define ENCODER_PIN_B        1
#define ENCODER_PUSH_BUTTON 21
// v4.x DES: the recovery keeps its own NVS so wificfg/uicfg survive an
// environment switch. v4.x.md rule 4 allows exactly this one line.
#define STORAGE_PARTITION    "rec_settings"

#define WIFI_PAGE_SIZE       5
#define FILE_PAGE_SIZE       4
#define MAX_SCAN_NETWORKS    20
#define MAX_URLS             8
#define WIFI_CONNECT_TIMEOUT 15000
#define BOOT_CONNECT_TIMEOUT 5000
#define KB_MAX_KEYS          48
#define HOLD_NOTICE_MS       500

static constexpr const lgfx::IFont* FONT_LARGE = &lgfx::fonts::Font4;
static constexpr const lgfx::IFont* FONT_SMALL = &lgfx::fonts::Font2;
static constexpr const lgfx::IFont* FONT_TINY  = &lgfx::fonts::Font0;

#define SCR_W 320
#define SCR_H 170

// UiKit palette (shared with the "mini game" firmware). Black shell, the rest
// is coloured.
#define RGB(r, g, b) ((uint16_t)((((r) & 31) << 11) | (((g) & 63) << 5) | ((b) & 31)))

// miniradio default palette (UI theme 0): near-black shell, teal/green accents.
#define COL_BG      0x0862            // RGB(1,3,2)
#define COL_PANEL   0x18C4            // RGB(3,6,4)
#define COL_PANEL2  0x1148            // RGB(2,10,8)
#define COL_HL      0x1148            // RGB(2,10,8)
#define COL_CELL    0x10A3            // RGB(2,5,3)
#define COL_LINE    0x2987            // RGB(5,12,7)
#define COL_TRACK   0x2146            // RGB(4,10,6)
#define COL_TEXT    0xF79E            // RGB(30,60,30)
#define COL_MUTED   0x8C73            // RGB(17,35,19)
#define COL_DIM     0x4A8B            // RGB(9,20,11)
#define COL_ACC     0x0C3F            // RGB(1,33,31) teal
#define COL_ACC2    0x1F12            // RGB(3,54,50)
#define COL_GOLD    0xFCE2            // RGB(31,39,2)
#define COL_OK      0x368B            // RGB(6,52,11)
#define COL_WARN    0xFA27            // RGB(31,17,7)
#define COL_VIOLET  0xFCE2            // gold
#define COL_ROSE    0x1F12            // teal
#define COL_AP      0xFCE2            // gold
#define COL_KEY     0x10A3            // RGB(2,5,3)
#define COL_KEYSEL  0x1A0D            // RGB(3,16,13)

// Test builds stay on "testbuild" until a release version is assigned.
#define RECOVERY_VERSION "testbuild"

// Default: fetch this .txt (one URL per line). Local /update_url.txt and
// DEFAULT_UPDATE_URLS are fallbacks when the remote list is unavailable.
static const char *REMOTE_UPDATE_URL_TXT =
  "https://gnbdatsmini.netlify.app/update_url.txt";

static const char *DEFAULT_UPDATE_URLS[MAX_URLS] = {};

LGFX tft;

// All drawing goes to this off-screen buffer; screens are pushed to the LCD
// once they are complete, so a redraw no longer flickers. It lives in PSRAM.
static LGFX_Sprite gSprite(&tft);
WebServer server(80);
Preferences prefs;

static String apIP;
static volatile bool apModeActive = false;
static bool serverRunning = false;
static volatile bool gWebEnabled = true;
static volatile bool gWifiConnecting = false;
static volatile bool gBattHidden = false;
static volatile bool gUiQuiet = false;
static volatile bool gUiRefresh = false;
static TaskHandle_t webTaskHandle = nullptr;

static const char *gBootSlot = "App0";
static bool gBootIsApp1 = false;

// Set when the littlefs volume could not be repaired. Nothing may mount it
// then and the app must not start either: the stale superblock trips a
// lfs_fs_grow_ assert and the board reboots forever.
static bool gLittleFsBlocked = false;

// Set by the one-shot flash task while it erases/writes a slot. Up here
// because batteryTick() guards its redraw on it, and the sketch preprocessor
// emits forward declarations above everything past this point.
static volatile bool gFlashBusy = false;

// Sketch preprocessor forward declares drawEraseList()/runEraseList() above
// their definition, so the type they take has to exist that early too.
struct EraseList;

static const char *menu[] = {
  "app0",
  "app1",
  "OTA",
  "Partition",
  "Settings"
};
#define MENU_COUNT (sizeof(menu) / sizeof(menu[0]))

// Tiles that fit one page of the main menu (a 3x2 grid).
#define MENU_PAGE_TILES 6

// Settings > ...  (Network / Brightness / LittleFS / Backup / Guide / About /
// Callsign)
static const char *settingsMenu[] = {
  "Network",
  "Partition",
  "Erase",
  "Brightness",
  "LittleFS",
  "Backup",
  "Guide",
  "Callsign",
  "Boot menu",
  "About"
};
#define SETTINGS_COUNT (sizeof(settingsMenu) / sizeof(settingsMenu[0]))

static const char *wifiMenu[] = {
  "WiFi",
  "Network About"
};
#define WIFI_MENU_COUNT (sizeof(wifiMenu) / sizeof(wifiMenu[0]))

static const char *bootModeMenu[] = {
  "Default boot",
  "Hold mode",
  "Info"
};
#define BOOT_MODE_COUNT (sizeof(bootModeMenu) / sizeof(bootModeMenu[0]))


// Split sections, in the original source order.
#include "prototypes.h"

#include "01_input.h"
#include "02_ui.h"
#include "03_device.h"
#include "04_boot.h"
#include "05_menus.h"
#include "06_update.h"
#include "07_storage.h"
#include "08_keyboard.h"
#include "09_web.h"
#include "10_about.h"
#include "11_wifi.h"
#include "12_textinput.h"
#include "13_files.h"
#include "14_partition.h"
#include "15_backup.h"
#include "16_main.h"
