/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * 컴포넌트 내부 저수준 I²C 유틸리티 (si4732.c 구현, 내부용).
 */
#pragma once

#include "si4732.h"

/** @brief STATUS 바이트에서 CTS 비트 폴링 (타임아웃: dev->timeout_ms) */
esp_err_t si4732_wait_cts(si4732_t *dev);

/** @brief 원시 I²C 쓰기 */
esp_err_t si4732_write(si4732_t *dev, const uint8_t *buf, size_t len);

/**
 * @brief 명령 전송: wait_cts → [CMD, ARGS...] → wait_cts
 * @note  응답이 있는 명령은 si4732_read_response()/si4732_command_read() 사용
 */
esp_err_t si4732_command(si4732_t *dev, uint8_t cmd, const uint8_t *args, size_t args_len);

/**
 * @brief 응답 읽기: [STATUS, payload...] 프레임에서 payload만 추출
 * @note  STATUS의 ERR 비트가 set되면 ESP_ERR_INVALID_RESPONSE
 */
esp_err_t si4732_read_response(si4732_t *dev, uint8_t *payload, size_t payload_len);

/** @brief command + read_response 원자 연산 */
esp_err_t si4732_command_read(si4732_t *dev, uint8_t cmd,
                              const uint8_t *args, size_t args_len,
                              uint8_t *payload, size_t payload_len);
