/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * 부팅 스플래시 화면.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 디스플레이 초기화 직후 호출. 스플래시를 그린다. */
void splash_show(void);

/** @brief 스플래시 중 인코더 클릭을 감지해 복구 메뉴를 띄운다.
 *  계속/공장초기화(NVS 삭제 후 재부팅) 선택 가능. */
void splash_service_input(void);

#ifdef __cplusplus
}
#endif
