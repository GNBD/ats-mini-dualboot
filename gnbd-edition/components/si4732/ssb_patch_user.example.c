/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) miniradio Contributors
 *
 * ─────────────────────────────────────────────────────────────────
 *  SSB 패치 데이터 사용자 플레이스홀더 (예제)
 *
 * 사용 방법:
 *   1. 이 파일을 복사해 같은 폴더에서 "ssb_patch_user.c" 로 이름 변경
 *   2. si4732_ssb_patch[] / si4732_ssb_patch_size 를 실제 패치로 교체
 *   3. idf.py reconfigure && idf.py build
 *      (CMake가 파일 존재 여부를 확인하므로 최초 1회 재구성 필요)
 *
 *  ⚠️ 라이선스: SSB 패치 데이터는 Silicon Labs 출처이며 저장소 배포 시
 *     라이선스 검증이 필요하다 (계획서 3.2/5장). 본인 기기 사용 전제.
 *
 *  데이터 형식: AN332 Rev 0.8 Amendment "patch content" 원본 배열
 *  (각 청크는 0x15 또는 0x16 명령 바이트로 시작, 8바이트 정렬 권장).
 * ─────────────────────────────────────────────────────────────────
 */
#include <stddef.h>
#include <stdint.h>

const uint8_t si4732_ssb_patch[] = {
    /* TODO: 실제 패치 데이터 삽입 (예: 0x15, ... , 0x16, ...) */
};

const size_t si4732_ssb_patch_size = sizeof(si4732_ssb_patch);
