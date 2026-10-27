/*
 * SPDX-License-Identifier: MIT
 * i18n.js - Korean / English strings for the UI editor.
 *
 * Usage:
 *   T("한국어")              -> English when lang === "en", Korean otherwise
 *   TF("'{0}' 삭제?", name)  -> {0} replaced after translation
 *   I18N.set("en")           -> switch + persist + re-render
 */
const I18N = {
  lang: "en",

  dict: {
    "타입 (읽기 전용)": "Type (read only)",
    "이름": "Name",
    "X / Y 위치": "X / Y position",
    "너비 / 높이": "Width / Height",
    "크기 %": "Scale %",
    "레이어 (z)": "Layer (z)",
    " 레이어 상속": "Layer inherit",
    "레이어 상속": "Layer inherit",
    "순서": "Order",
    "값 바인딩": "Binding",
    "표시 텍스트": "Text",
    "폰트 크기": "Font size",
    "칩 배경": "Chip background",
    "채우기 색": "Fill color",
    "테두리 색": "Stroke color",
    "모서리 반경": "Corner radius",
    "최대값": "Max value",
    "막대 수 (0~4, 0=자동)": "Bars (0-4, 0=auto)",
    "옵션 (예: noterm)": "Options (e.g. noterm)",
    "목록 종류": "List kind",
    "배치 방향": "Orientation",
    "캔버스: 드래그 이동 · 가장자리/모서리 핸들로 크기 조절": "Canvas: drag to move, edge/corner handles to resize",
    "이 모듈은 위치·크기·색상만 바꿀 수 있고, 표시 내용은 펌웨어가 그립니다.": "This module only changes position/size/color; the firmware draws the content.",
    "디코드 패널: 옵션 문자열로 설정합니다. 예) gpio=4;mode=0;lines=4 (mode 0=CW, 1=RTTY)": "Decode panel options, e.g. gpio=4;mode=0;lines=4 (mode 0=CW, 1=RTTY)",
    "사각 게이지: 바인딩 값(0~최대값)을 테두리+채움으로 표시합니다. 옵션에 noterm 입력 시 단자를 숨깁니다.": "Quad gauge: shows the bound value (0..max) as border + fill. Add noterm to hide the terminal.",
    "Wi-Fi 세기 아이콘: 막대 수를 고정하거나 0으로 두면 실제 상태(RSSI)에 따라 자동 표시합니다.": "Wi-Fi icon: fix the bar count, or 0 to follow the actual RSSI.",
    "Sleep 남은 시간": "Sleep remaining",
    "SLEEP 초 표시": "Show remaining sleep seconds",
    "디코드 패널: GPIO/모드/줄 수를 옵션 문자열로 지정합니다.": "Decode panel: set GPIO / mode / lines with the option string.",
    "GPIO/모드를 프로퍼티에서 설정": "Set GPIO / mode in properties",
    "맨 앞으로": "Bring to front",
    "맨 뒤로": "Send to back",
    "기기에 저장": "Save to device",
    "PC로 내려받기": "Download to PC",
    "레이아웃 미리보기": "Layout preview",
    "레이아웃 탭에서 배치한 결과입니다. 이 화면을 기준으로 스크립트/블록을 작성하세요.": "This is the layout result. Write scripts/blocks based on this screen.",
    "디코드 상태/레벨/텍스트": "decode status / level / text",
    "디코드 상태/레벨": "decode status / level",
    "SLEEP 초": "SLEEP seconds",
    /* ---------- static shell ---------- */
    "에디터를 불러오는 중...": "Loading editor...",
    "저장 안 됨 — Download로 저장하세요": "Unsaved — use Download to save",
    "스냅 간격": "Snap spacing",
    "스냅 사용": "Snap to grid",
    "UI 프로젝트 파일 다운로드": "Download the UI project file",
    "main/ui_custom_layout.h 다운로드": "Download main/ui_custom_layout.h",
    "라디오에 저장하고 즉시 적용": "Save to the radio and apply immediately",
    "저장된 UI 목록/불러오기/삭제": "Saved UI list · load · delete",
    "모듈 검색...": "Search modules...",
    "왼쪽 맞춤": "Align left",
    "가로 가운데": "Align horizontal center",
    "오른쪽 맞춤": "Align right",
    "위쪽 맞춤": "Align top",
    "세로 가운데": "Align vertical center",
    "아래쪽 맞춤": "Align bottom",
    "같은 너비": "Equal width",
    "같은 높이": "Equal height",
    "가로 균등 분배": "Distribute horizontally",
    "세로 균등 분배": "Distribute vertically",
    "FM 레이아웃": "FM layout",
    "AM 레이아웃": "AM layout",
    "SSB 레이아웃": "SSB layout",
    "FM/AM/SSB는 각각 별도 레이아웃 · 드래그로 배치 · Delete 삭제":
      "FM / AM / SSB use separate layouts · drag to place · Delete to remove",
    "PREVIEW — 실제 기기 렌더링과 동일한 좌표/색/폰트 크기":
      "PREVIEW — same coordinates, colors and font sizes as the device",
    "{0} selected": "{0} selected",
    "언어 변경": "Switch language",
    "미리보기": "Preview",
    "스냅": "Snap",
    "실행취소": "Undo",
    "다시 실행": "Redo",
    "문서": "Docs",
    "시그널 아이콘": "Signal icon",
    "(텍스트)": "(text)",
    "UI Editor v0.1.0 beta · 자세한 사용법은 Docs에서 확인하세요.": "UI Editor v0.1.0 beta · see Docs for the full guide.",
    "자세한 내용": "Details",
    "확대": "Zoom",
    "확대 비율": "Zoom level",
    "확대 기준점": "Zoom anchor",
    "중앙": "Center",
    "좌상단": "Top-left",
    "새로 만들기": "New",
    "가져오기": "Import",
    "다운로드": "Download .mrpack",
    "C 헤더 다운로드": "Download C",
    "라디오에 저장": "Save to radio",
    "정렬": "ALIGN",
    "레이아웃": "LAYOUT",
    "레이어": "LAYERS",
    "속성": "Properties",

    /* ---------- app.js ---------- */
    "저장 이름 (영문/숫자/_/- 1~24자)": "Save name (letters/digits/_/- 1-24)",
    "'{0}' 파일이 이미 있습니다. 덮어쓸까요?": "'{0}' already exists. Overwrite?",
    "라디오 런타임은 위젯 128개까지 지원합니다. 현재 {0}개":
      "The radio runtime supports up to 128 widgets. Now {0}.",
    "저장된 UI가 없습니다.": "No saved UI.",
    "저장된 UI": "Saved UI",
    "'{0}' UI를 삭제할까요?": "Delete the UI '{0}'?",
    "새 UI 프로젝트로 초기화할까요?": "Start a new UI project?",
    "저장하지 않은 변경사항이 있습니다.\nDownload로 저장하세요.":
      "You have unsaved changes.\nUse Download to save them.",
    "저장 중...": "saving...",
    "저장+적용됨: {0}": "saved+applied: {0}",
    "불러오는 중...": "loading...",
    "불러옴: {0}": "loaded: {0}",
    "삭제됨: {0}": "deleted: {0}",
    "라디오 연결 없음": "radio unavailable",
    "이름이 올바르지 않음": "invalid name",
    "프로젝트가 올바르지 않음": "invalid project",
    "위젯이 너무 많음": "too many widgets",
    "저장 실패: {0}": "save failed: {0}",
    "불러오기 실패: {0}": "load failed: {0}",
    "삭제 실패: {0}": "delete failed: {0}",
    "가져오기 실패": "Import failed",
    "올바르지 않은 UI 프로젝트": "Invalid UI project",
    "파싱 오류: {0}": "Parse error: {0}",
    "너무 많음": "Too many",
    "덮어쓰기": "Overwrite",
    "UI 삭제": "Delete UI",
    "새 프로젝트": "New project",
    "라디오에 저장": "Save to radio",
    "올바르지 않은 프로젝트": "Invalid project",
    "UI 목록": "UI List",

    /* ---------- modal ---------- */
    "취소": "Cancel",
    "확인": "OK",
    "불러오기": "Load",
    "삭제": "Delete",
    "알림": "Notice",

    /* ---------- colors ---------- */
    "색상을 기본값으로 되돌릴까요?": "Reset all colors to defaults?",
    "색상 초기화": "Reset colors",
    "색상": "COLORS",

    /* ---------- layers ---------- */
    " (상속)": " (inherited)",
    "표시": "Show",
    "숨김": "Hide",

    /* ---------- palette / catalog ---------- */
    "템플릿 · {0}": "template · {0}",
    "가이드 · 가로/세로": "guide · horizontal/vertical",
    "Default (기본 UI)": "Default (built-in UI)",
    "firmware 기본 화면": "firmware default screen",
    "Radio (현재 구현)": "Radio (current build)",
    "header+freq+rsq+rds+sig": "header+freq+rsq+rds+sig",
    "하단 전체 밴드맵": "full-width band map",
    "주파수 + RSSI만": "frequency + RSSI only",
    "TEMPLATES": "TEMPLATES",
    "SCREEN MODULES": "SCREEN MODULES",
    "PRIMITIVES": "PRIMITIVES",

    /* bindings */
    "주파수 (문자열)": "frequency (text)",
    "모드 FM/AM/LSB/USB": "mode FM/AM/LSB/USB",
    "밴드명": "band name",
    "시계 HH:MM": "clock HH:MM",
    "날짜": "date",
    "프리셋 번호": "preset number",
    "프리셋 이름": "preset name",
    "ATS 번호": "ATS number",
    "배터리 %": "battery %",
    "볼륨": "volume",
    "대역폭": "bandwidth",
    "스텝": "step",
    "소프트뮤트": "soft mute",
    "스퀠치": "squelch",
    "스테레오": "stereo",
    "EiBi 방송": "EiBi station",
    "토스트": "toast",
    "Wi-Fi 상태": "Wi-Fi status",
    "VOL/BRT 상태": "VOL/BRT status",
    "배터리 전압": "battery voltage",

    /* ---------- properties ---------- */
    "레이어 +1": "Layer +1",
    "레이어 -1": "Layer -1",
    "부모의 레이어를 그대로 사용 (동일 레이어)":
      "Inherit the parent's layer (same layer)",
    "세로": "Vertical",
    "가로": "Horizontal",
    "이 모듈은 위치/크기만 조정합니다 (내용은 펌웨어가 그림).":
      "Only position and size can be set (the firmware draws the content).",
    "복제": "Duplicate",
    "설계 가이드입니다. 실제 MENU/SETTINGS 내용은 펌웨어가 그리고,":
      "This is a design guide. The firmware draws the real MENU/SETTINGS,",
    "이 모듈은 위치/크기와 가로·세로 배치만 보여줍니다 (내보내기 제외).":
      "and this module only shows position, size and horizontal/vertical layout (not exported).",
    "모듈을 선택하세요.": "Select a module.",
    "캔버스: 드래그 이동 · 우하단 핸들 크기조절":
      "Canvas: drag to move · bottom-right handle to resize",
    "Shift+클릭 다중선택 · 방향키 미세조정":
      "Shift+click to multi-select · arrow keys to nudge",
    "Delete 삭제 · Ctrl+D 복제 · Ctrl+Z 실행취소":
      "Delete to remove · Ctrl+D to duplicate · Ctrl+Z to undo",
    "레이어: 오른쪽 LAYERS에서 드래그 (아래 = 앞)":
      "Layers: drag in LAYERS on the right (bottom = front)",
    "FM / AM / SSB는 각각 별도 레이아웃입니다.": "FM / AM / SSB each have their own layout.",
    "items: ": "items: ",

    /* ---------- validation ---------- */
    "위젯이 객체가 아닙니다.": "Widget is not an object.",
    "지원하지 않는 위젯 타입: ": "Unsupported widget type: ",
    "x/y는 정수여야 합니다.": "x/y must be integers.",
    "위젯이 화면 밖에 있습니다.": "Widget is outside the screen.",
    "w/h는 1 이상의 정수여야 합니다.": "w/h must be integers >= 1.",
    "위젯 크기가 화면(320x170)을 넘습니다.": "Widget is larger than the screen (320x170).",
    "Layer는 -1000..1000 정수여야 합니다.": "Layer must be an integer in -1000..1000.",
    "허용되지 않는 색상: ": "Unsupported color: ",
    "허용되지 않는 폰트: ": "Unsupported font: ",
    "허용되지 않는 폰트 크기: ": "Unsupported font size: ",
    "허용되지 않는 정렬: ": "Unsupported alignment: ",
    "허용되지 않는 바인딩: ": "Unsupported binding: ",
    "모드 마스크는 0..7이어야 합니다.": "Mode mask must be 0..7.",
    "max는 0..65535 정수여야 합니다.": "max must be an integer in 0..65535.",
    "text는 문자열이어야 합니다.": "text must be a string.",
    "text가 너무 깁니다.": "text is too long.",
    "MENU/SETTINGS만 허용됩니다.": "Only MENU/SETTINGS is allowed.",
    "가로/세로만 허용됩니다.": "Only horizontal/vertical is allowed.",
    "텍스트도 바인딩도 없습니다.": "No text and no binding.",
    "uid는 정수여야 합니다.": "uid must be an integer.",
    "Scale은 50..200 정수여야 합니다.": "Scale must be an integer in 50..200.",
    "중복 uid: ": "Duplicate uid: ",
    "상속 대상 uid가 없습니다: ": "Inherited uid not found: ",
    "레이어 상속이 순환합니다.": "Layer inheritance is circular.",
    "프로젝트가 객체가 아닙니다.": "Project is not an object.",
    "더 새로운 스키마입니다.": "This schema is newer.",
    "알 수 없는 형식: ": "Unknown format: ",
    "이전 형식(": "Older format (",
    "에서 UI만 가져왔습니다.": ") and only the UI was imported.",
    "프로젝트 이름이 너무 깁니다.": "Project name is too long.",
    "색상은 #rrggbb 형식이어야 합니다: ": "Color must be in #rrggbb format: ",
    "위젯이 너무 많습니다 (최대 ": "Too many widgets (max ",
    "전체 항목 수가 너무 많습니다.": "Too many items in total.",
  },

  init() {
    let saved = null;
    try { saved = localStorage.getItem("uiedt.lang"); } catch (e) { /* ignore */ }
    if (saved === "en" || saved === "ko") this.lang = saved;
  },

  t(s) {
    if (s == null) return "";
    if (this.lang !== "en") return s;
    const v = this.dict[s];
    return v == null ? s : v;
  },

  f(s, ...args) {
    const out = this.t(s);
    return out.replace(/\{(\d+)\}/g, (m, i) =>
      args[i] == null ? m : String(args[i]));
  },

  /* 메시지 전체 일치 → 없으면 가장 긴 접두사 키로 번역 */
  tm(s) {
    if (this.lang !== "en" || !s) return s;
    if (this.dict[s] != null) return this.dict[s];
    let best = null;
    for (const k of Object.keys(this.dict)) {
      if (k.length > 1 && s.startsWith(k) && (!best || k.length > best.length)) {
        best = k;
      }
    }
    return best ? this.dict[best] + s.slice(best.length) : s;
  },

  set(lang) {
    this.lang = lang === "en" ? "en" : "ko";
    try { localStorage.setItem("uiedt.lang", this.lang); } catch (e) { /* ignore */ }
    document.documentElement.lang = this.lang;
    this.apply();
    if (typeof refreshLocale === "function") refreshLocale();
    else if (typeof ED !== "undefined") ED.draw();
  },

  toggle() { this.set(this.lang === "en" ? "ko" : "en"); },

  apply(root) {
    const scope = root || document;
    scope.querySelectorAll("[data-i18n]").forEach((el) => {
      el.textContent = this.t(el.dataset.i18n);
    });
    scope.querySelectorAll("[data-i18n-title]").forEach((el) => {
      el.title = this.t(el.dataset.i18nTitle);
    });
    scope.querySelectorAll("[data-i18n-ph]").forEach((el) => {
      el.placeholder = this.t(el.dataset.i18nPh);
    });
    const btn = document.getElementById("langBtn");
    if (btn) {
      btn.textContent = this.lang === "en" ? "KO" : "EN";
      btn.title = this.t("언어 변경");
    }
  },
};

function T(s) { return I18N.t(s); }
function TF(s, ...a) { return I18N.f(s, ...a); }
function TP(ko, en) { return I18N.lang === "en" ? en : ko; }
I18N.init();

window.I18N = I18N;
window.T = T;
window.TF = TF;
window.TP = TP;
