# -*- coding: utf-8 -*-
"""EXTENDED 에디터: 디코드 예제 템플릿 제거 (블록 기능 후 재작성 예정)."""
import io

p = 'uiedt/extended/js/extended.js'
t = io.open(p, encoding='utf-8').read()

old = '''  if (typeof TEMPLATES !== "undefined") {
    TEMPLATES.length = 0;
    TEMPLATES.push(
      { id: "tpl-decode", name: "Decode UI (예제)", sub: "주파수 + 디코드 패널 (파라미터 없음)",
        ids: ["headerfull", "bandfull", "freq", "decode"] },
      { id: "tpl-decode-min", name: "Decode 최소", sub: "주파수 + 디코드 패널",
        ids: ["headerfull", "freq", "decode"] },
      { id: "tpl-blank", name: "빈 레이아웃", sub: "백지에서 시작", ids: [] }
    );
  }'''
new = '''  if (typeof TEMPLATES !== "undefined") {
    TEMPLATES.length = 0;
    /* 예제는 블록 기능과 함께 다시 제공한다 */
    TEMPLATES.push(
      { id: "tpl-blank", name: "빈 레이아웃", sub: "백지에서 시작", ids: [] }
    );
  }'''
assert old in t
t = t.replace(old, new, 1)

old2 = '''  const SCRIPT_EXAMPLE = [
    "-- Decode UI 예제 (확장 앱 스크립트)",
    "-- on_tick 은 100ms 마다 호출됩니다.",
    "function on_tick(ms)",
    "  -- 여기서 디코드 텍스트/레벨을 갱신할 수 있습니다",
    "end",
    "",
    "function on_click()",
    "  -- 디코드 모드 전환",
    "end",
  ].join("\\n");'''
new2 = '''  const SCRIPT_EXAMPLE = [
    "-- 확장 앱 스크립트 (고급 사용자용)",
    "-- 위젯 이벤트 핸들러 예: w12_click(uid, arg)",
    "-- 전역 훅: on_tick(ms), on_click()",
    "",
  ].join("\\n");'''
assert old2 in t
t = t.replace(old2, new2, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
