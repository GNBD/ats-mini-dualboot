/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * SI4732 명령어 코드 (AN332 Si47XX Programming Guide 기준).
 */
#pragma once

/* ---- 시스템 명령 ---- */
#define SI4732_CMD_POWER_UP         0x01
#define SI4732_CMD_GET_REV          0x10
#define SI4732_CMD_POWER_DOWN       0x11
#define SI4732_CMD_SET_PROPERTY     0x12
#define SI4732_CMD_GET_PROPERTY     0x13
#define SI4732_CMD_GET_INT_STATUS   0x14
#define SI4732_CMD_PATCH_ARGS       0x15  /* 펌웨어 패치 다운로드 인자 (예약) */
#define SI4732_CMD_PATCH_DATA       0x16  /* 펌웨어 패치 데이터 (예약) */

/* ---- FM 명령 ---- */
#define SI4732_CMD_FM_TUNE_FREQ     0x20
#define SI4732_CMD_FM_SEEK_START    0x21
#define SI4732_CMD_FM_TUNE_STATUS   0x22
#define SI4732_CMD_FM_RSQ_STATUS    0x23
#define SI4732_CMD_FM_RDS_STATUS    0x24

/* ---- AM/SSB 명령 (AM 계열 공용) ---- */
#define SI4732_CMD_AM_TUNE_FREQ     0x40
#define SI4732_CMD_AM_SEEK_START    0x41
#define SI4732_CMD_AM_TUNE_STATUS   0x42
#define SI4732_CMD_AM_RSQ_STATUS    0x43
#define SI4732_CMD_AM_AGC_STATUS    0x47
#define SI4732_CMD_AM_AGC_OVERRIDE  0x48

/* ---- GPIO ---- */
#define SI4732_CMD_GPIO_CTL         0x80
#define SI4732_CMD_GPIO_SET         0x81

/* ---- STATUS 바이트 (모든 응답의 첫 바이트) ---- */
#define SI4732_STATUS_CTS           0x80  /* 1 = 다음 명령 전송 가능 */
#define SI4732_STATUS_ERR           0x40  /* 1 = 명령 오류 */
#define SI4732_STATUS_RSQINT        0x08
#define SI4732_STATUS_RDSINT        0x04
#define SI4732_STATUS_STCINT        0x01  /* 튜닝/탐색 완료 */

/* ---- POWER_UP ARG ---- */
#define SI4732_POWERUP_XOSCEN       0x10  /* 32.768 kHz 크리스탈 사용 */
#define SI4732_POWERUP_PATCH        0x20  /* 패치 복사 후 부팅 */
#define SI4732_POWERUP_FUNC_AM      0x01
#define SI4732_POWERUP_FUNC_QUERY   0x0F  /* FUNC=15: Library ID 조회 */
#define SI4732_OPMODE_ANALOG_AUDIO  0x05  /* LOUT/ROUT 아나로그 출력 */

/* ---- TUNE ARG1 ---- */
#define SI4732_TUNE_FAST            0x01
#define SI4732_TUNE_FREEZE          0x02  /* FM 전용 */

/* ---- SEEK ARG1 (bit2=WRAP, bit3=SEEKUP) ---- */
#define SI4732_SEEK_WRAP            0x04
#define SI4732_SEEK_UP              0x08
