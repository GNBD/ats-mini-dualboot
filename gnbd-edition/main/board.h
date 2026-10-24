/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * ATS Mini 보드 핀 맵.
 * 출처: https://esp32-si4732.github.io/ats-mini/hardware.html (Pinout)
 */
#pragma once

#include "driver/gpio.h"

/* ---- 전원 제어 ---- */
#define BOARD_RADIO_EN_GPIO      15   /* 1 = SI4732용 LDO 인가 */
#define BOARD_AMP_EN_GPIO        10   /* 1 = 오디오 앰프 인가 */
#define BOARD_AUDIO_MUTE_GPIO     3   /* 1 = L/R 음소거 */
#define BOARD_VBAT_ADC_GPIO       4   /* 배터리 전압 ADC (IO4/ADC1_CH3) */

/* ---- SI4732 (I²C) ---- */
#define BOARD_I2C_PORT           I2C_NUM_0
#define BOARD_I2C_SCL_GPIO      17
#define BOARD_I2C_SDA_GPIO      18
#define BOARD_I2C_FREQ_HZ      100000
#define BOARD_SI4732_RESET_GPIO  16   /* SI4732 RESET (active low) */

/* ---- LCD ST7789 (8080 병렬 8비트) ----
   실 패널: 170x320 ST7789 (원본 setRotation(3) = MADCTL MV|MY → 가로 320x170).
   offset_x=35 창 보정은 드라이버 gap으로 적용 (원본 Display.h 기준). */
#define BOARD_LCD_RST_GPIO        5
#define BOARD_LCD_CS_GPIO         6
#define BOARD_LCD_DC_GPIO         7
#define BOARD_LCD_WR_GPIO         8   /* WR = 픽셀 클럭 */
#define BOARD_LCD_RD_GPIO         9   /* RD (구동 X, 항상 HIGH) */
#define BOARD_LCD_BL_GPIO         38   /* 백라이트 */
#define BOARD_LCD_D0_GPIO         39
#define BOARD_LCD_D1_GPIO         40
#define BOARD_LCD_D2_GPIO         41
#define BOARD_LCD_D3_GPIO         42
#define BOARD_LCD_D4_GPIO         45
#define BOARD_LCD_D5_GPIO         46
#define BOARD_LCD_D6_GPIO         47
#define BOARD_LCD_D7_GPIO         48
#define BOARD_LCD_WIDTH           320
#define BOARD_LCD_HEIGHT          170
#define BOARD_LCD_ROTATE_LANDSCAPE 1   /* MADCTL = MV|MY (0xA0) */
#define BOARD_LCD_GAP_X           0    /* CASET 보정 (원본 offset_x → 회전 후 colstart) */
#define BOARD_LCD_GAP_Y           35   /* RASET 보정 (원본 offset_x → 회전 후 rowstart) */

/* ---- EC11 엔코더 ---- */
#define BOARD_ENC_A_GPIO          2
#define BOARD_ENC_B_GPIO          1
#define BOARD_ENC_PUSH_GPIO      21
