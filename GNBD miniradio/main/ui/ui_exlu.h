/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) miniradio Contributors
 * Copyright (c) 2026 GNBD
 *
 * EXLU container: layout + optional Lua script + manifest in one file.
 *
 * Text format (UTF-8, LF):
 *   EXLU1
 *   #MANIFEST
 *   {"format":"exlu/1","kind":"theme","type":"layout","name":"...","author":"...","api":1,"perms":[]}
 *   #LAYOUT
 *   <layout text (UIR1 ...)>
 *   #SCRIPT
 *   <lua source, may be empty>
 *   #END
 *
 * The parser never hides failures: every rejection returns a code that
 * maps to a human readable reason.
 *
 * This source file is licensed under the MIT License.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXLU_MAX_FILE_SIZE   (128 * 1024)
#define EXLU_MAX_SCRIPT_SIZE (64 * 1024)
#define EXLU_NAME_MAX        32
#define EXLU_AUTHOR_MAX      32
#define EXLU_LABEL_MAX       32

typedef enum {
    EXLU_OK = 0,
    EXLU_ERR_ARG,          /* 잘못된 인자 */
    EXLU_ERR_OPEN,         /* 파일 열기/읽기 실패 */
    EXLU_ERR_SIZE,         /* 파일이 너무 큼 */
    EXLU_ERR_MAGIC,        /* EXLU1 헤더 아님 */
    EXLU_ERR_NO_MANIFEST,  /* 매니페스트 섹션 없음 */
    EXLU_ERR_MANIFEST,     /* 매니페스트 필드 누락/형식 오류 */
    EXLU_ERR_FORMAT,       /* format 값 불일치 */
    EXLU_ERR_API,          /* 지원하지 않는 API 버전 */
    EXLU_ERR_KIND,         /* kind 값 오류 (theme/app) */
    EXLU_ERR_TYPE,         /* type 값 오류 (layout/script) */
    EXLU_ERR_COMBO,        /* theme + script 등 금지 조합 */
    EXLU_ERR_NO_LAYOUT,    /* layout 타입인데 레이아웃 없음 */
    EXLU_ERR_SCRIPT,       /* script 크기 초과/형식 오류 */
    EXLU_ERR_LAYOUT,       /* 레이아웃 저장 실패 */
    EXLU_ERR_EXISTS,       /* 덮어쓰기 거부 */
    EXLU_ERR_NOT_FOUND,    /* 파일 없음 */
    EXLU_ERR_MEM,          /* 메모리 부족 */
} exlu_err_t;

typedef enum {
    EXLU_KIND_THEME = 0,   /* 라디오 화면 테마 (script 금지) */
    EXLU_KIND_APP,         /* 독립 앱 (layout 또는 script) */
} exlu_kind_t;

typedef enum {
    EXLU_TYPE_LAYOUT = 0,  /* 레이아웃 (+선택적 스크립트) */
    EXLU_TYPE_SCRIPT,      /* Lua 단독 액션 */
} exlu_type_t;

typedef struct {
    exlu_kind_t  kind;
    exlu_type_t  type;
    char         name[EXLU_LABEL_MAX];    /* 메뉴 표시 이름 */
    char         author[EXLU_AUTHOR_MAX];
    char         id[EXLU_NAME_MAX];       /* 파일/슬롯 식별자 */
    int          api;
    uint8_t      modes;                   /* bit0 FM, bit1 AM, bit2 SSB */
    bool         has_script;
} exlu_manifest_t;

typedef struct {
    exlu_manifest_t manifest;
    char  *layout;      /* heap, NUL 종료 (없으면 NULL) */
    size_t layout_len;
    char  *script;      /* heap, NUL 종료 (없으면 NULL) */
    size_t script_len;
} exlu_t;

const char *exlu_strerror(exlu_err_t err);

/* 메모리/파일 수명 */
void exlu_free(exlu_t *e);
exlu_err_t exlu_parse(const char *text, size_t len, exlu_t *out);

/* littlefs: /littlefs/ui/<id>.exlu */
exlu_err_t exlu_load(const char *id, exlu_t *out);
exlu_err_t exlu_save(const char *id, const exlu_t *e, bool overwrite);
/* 검증만 수행하고 원문을 그대로 저장/읽기 (알 수 없는 섹션 보존) */
exlu_err_t exlu_store_raw(const char *id, const char *text,
                          size_t len, bool overwrite);
exlu_err_t exlu_read_raw(const char *id, char **out, size_t *len);
exlu_err_t exlu_remove(const char *id);
int        exlu_list_ids(char ids[][EXLU_NAME_MAX], int max);
int        exlu_list(exlu_manifest_t *out, int max);

/* 테마/앱 적용 (레이아웃을 ui_runtime에 위임) */
exlu_err_t exlu_apply(const exlu_t *e);
void       exlu_boot_apply_theme(void);
