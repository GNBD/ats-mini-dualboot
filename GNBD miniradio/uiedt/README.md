# MINI-radio UI Editor

ATS Mini 320×170 화면 전용 **UI 에디터**입니다.
블록 로직, 메뉴/설정 편집, C 모듈, 펌웨어 빌드는 포함하지 않습니다.

## 기능

- FM / AM / SSB **독립 레이아웃**
- `PANEL (MENU/SETTINGS)` UI 모듈 + 패널별 **가로/세로** 배치
- 우측 **COLORS** 팔레트: 배경/패널/강조/경고/OK 등 16색 편집
- 모듈 드래그 배치, 크기 조절, 다중 선택, 정렬/분배
- 레이어(z-order), 상속, 표시/숨김
- 스냅, 미리보기, Undo
- 프로젝트 저장/불러오기 (`.mrpack`, UI만 유지)
- `ui_custom_layout.h` 생성

## 실행

정적 파일만으로 동작합니다.

```powershell
cd uiedt
python -m http.server 8766 --bind 127.0.0.1
# http://localhost:8766/
```

펌웨어 웹에 내장할 때는 `index.html`, `css/`, `js/`를 같은 경로에 두면 됩니다.
서버 API, 빌드 도구, 외부 라이브러리 의존이 없습니다.

## 파일 구조

```
index.html
css/app.css
js/
  core/
    schema.js     UI 스키마/위젯 타입/바인딩/한도
    project.js    정규화 + 구버전 .mrpack에서 UI만 추출
    validate.js   위젯/레이어/레이아웃 검증
    layout_c.js   ui_custom_layout.h 생성
  catalog.js      모듈/템플릿/목데이터
  render.js       320×170 렌더러
  editor.js       캔버스 조작 + 모드별 레이아웃
  properties.js   속성 패널
  palette.js      모듈 팔레트
  layers.js       레이어 뷰어
  export.js       저장/불러오기/C 내보내기
  app.js          부트스트랩
```

## 템플릿

- **Default (기본 UI)**: 펌웨어 기본 화면과 동일한 구성 (Wi-Fi/배터리/시계, VOL/BRT, PARAM, RDS, SIG)
- Radio (현재 구현), Radio + Band map, Minimal
- 에디터 시작 시 Default가 자동 로드됨

## 라디오 연동

- `Save to radio`: 이름을 정해 저장하고 **즉시 적용**
- `Load`: 라디오에 저장된 UI 목록에서 불러오기
- 이름은 `영문/숫자/_/-` 1~24자
- 같은 이름(대소문자 무시)은 덮어쓰기 확인 없이는 저장되지 않음
- 저장 위치: `/littlefs/ui/<name>.layout`, `<name>.mrpack`
- 라디오 **SETTINGS > THEME**에 저장된 파일명이 그대로 표시됨
- THEME에서 파일명을 선택하면 모든 오버레이가 닫히고 즉시 라디오 화면으로 전환
- 테마 적용/저장 시 **토스트 없음**
- 테마 모드에서는 MENU/SETTINGS 패널도 레이아웃의 `PANEL` 위치·크기·가로/세로를 따름
- 패널이 없으면 기존 기본 패널로 폴백
- 저장된 색상 팔레트(16색)가 테마 팔레트로 적용됨

## COLORS / SIG / PARAM

- **COLORS**: 우측 패널에서 16색을 직접 지정. 프로젝트와 `PAL` 라인에 저장됨.
- **SIG**: 펌웨어에서 RSSI/SNR 꺾은선 그래프로 렌더링 (250ms 샘플).
- **Param grid**: `line1/line2/line3` 바인딩이 실제 값으로 표시됨.
- 에디터 미리보기는 펌웨어와 같은 8/16/32px 폰트, 폭 초과 시 절단 규칙을 사용.

## 펌웨어 웹 내장

- 펌웨어가 `uiedt` 정적 파일을 내장해 `/ui/`로 제공
- 웹 **Settings** 페이지에 `Open UI Editor` 링크
- 에디터 수정 후 `tools/embed_ui_editor.py`로 자산 재생성:

```powershell
python tools\embed_ui_editor.py
idf.py build
```

## API

| Method | Path | 역할 |
|---|---|---|
| GET | `/api/ui/list` | 저장된 이름 + 활성 이름 |
| GET | `/api/ui/layout?name=` | 런타임 레이아웃 텍스트 (PAL 포함) |
| GET/POST | `/api/ui/project?name=` | `.mrpack` 프로젝트 |
| POST | `/api/ui/save?name=&overwrite=` | 레이아웃 저장 (중복 시 409) |
| POST | `/api/ui/activate?name=` | 적용 + 테마 전환 |
| POST | `/api/ui/delete?name=` | 삭제 |

## 모드별 레이아웃

- 저장: `ui.layouts = { FM, AM, SSB }`, `ui.activeMode`
- 처음 모드를 전환하면 현재 레이아웃을 복사해 시작
- 내보내기: 각 모드 위젯에 `UC_M_FM` / `UC_M_AM` / `UC_M_SSB` 강제
- 기존 단일 레이아웃은 위젯별 `modes` 마스크로 유지

## MENU/SETTINGS 패널

- 팔레트의 `PANEL (MENU/SETTINGS)` 모듈
- 속성에서 `List` = MENU / SETTINGS
- 속성에서 `Layout` = 세로 / 가로
- 실제 메뉴 내용은 펌웨어가 그리며, 이 모듈은 위치·크기·배치 가이드입니다.
- 레이아웃 C 내보내기에서는 제외됩니다.

## 저장 형식
```json
{
  "format": "gnbd-ui-project",
  "schemaVersion": 1,
  "manifest": { "name": "my-ui" },
  "ui": {
    "bg": "bg",
    "items": [],
    "layouts": { "FM": [], "AM": [], "SSB": [] },
    "activeMode": "FM"
  }
}
```

기존 `mrpack/1` 파일은 UI 부분만 가져옵니다.

## 펌웨어 웹 내장 시

- 현재는 다운로드 방식 (자동 저장 없음)
- 내장 후에는 `/api/ui/layout` 같은 엔드포인트로 프로젝트를 주고받을 수 있음
- 브라우저에서 직접 빌드/플래시하지 않음
- 펌웨어는 기존 `uc_widget_t` / `UC_M_*` 렌더러를 그대로 사용

## 라이선스

- miniradio firmware: MIT
- LovyanGFX: BSD-2-Clause / MIT
- littlefs (joltwallet): MIT
- ESP-IDF: Apache-2.0
