# -*- coding: utf-8 -*-
"""블록 편집 UI 추가 (index.html) + CSS + blocks.js 로드."""
import io

p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()

# 1) CSS
old_css = '''  .phint{font-size:11px;color:#667085;margin-top:6px}'''
new_css = '''  .phint{font-size:11px;color:#667085;margin-top:6px}
  .blkpalette{display:flex;flex-wrap:wrap;gap:6px;margin:10px 0}
  .blkadd{border:1px solid #cfd6e4;background:#fbfcfe;border-radius:8px;
          padding:6px 9px;font:inherit;font-size:11px;font-weight:700;cursor:pointer}
  .blkadd:hover{border-color:#0072de;color:#0072de}
  .blkstack{display:flex;flex-direction:column;gap:6px;margin:8px 0}
  .blk{display:flex;align-items:center;gap:6px;background:#eef4ff;
       border:1px solid #b9d4f8;border-left:6px solid #0072de;
       border-radius:8px;padding:6px 8px}
  .blk .blkname{font-size:11px;font-weight:700;color:#0b4a8f;min-width:96px}
  .blk input,.blk select{border:1px solid #cfd6e4;border-radius:6px;
       padding:3px 5px;font:inherit;font-size:11px;background:#fff;max-width:120px}'''
assert old_css in t
t = t.replace(old_css, new_css, 1)

# 2) 블록 섹션 (스크립트 카드 앞)
old = '''  <section class="pcard">
    <h3>Lua 스크립트</h3>'''
new = '''  <section class="pcard">
    <h3>블록 (에셋별 동작)</h3>
    <div class="prow">
      <select id="blkWidget" style="flex:2"></select>
      <select id="blkEvent" style="flex:1">
        <option value="click">클릭</option>
        <option value="rotate">회전</option>
        <option value="long">길게</option>
        <option value="tick">틱</option>
      </select>
    </div>
    <div id="blkPalette" class="blkpalette"></div>
    <div id="blkStack" class="blkstack"></div>
    <div class="prow">
      <button class="pbtn primary" id="blkApply">스크립트에 반영</button>
    </div>
    <div id="blkMsg" class="pmsg"></div>
    <h3 style="margin-top:12px">생성된 Lua</h3>
    <textarea id="blkCode" class="script" readonly style="min-height:140px"></textarea>
  </section>

  <section class="pcard">
    <h3>Lua 스크립트</h3>'''
assert old in t
t = t.replace(old, new, 1)

# 3) blocks.js 로드 (app.js 뒤)
old2 = '<script src="../js/app.js?v=5"></script>'
new2 = '<script src="../js/app.js?v=5"></script>\n<script src="js/blocks.js?v=5"></script>'
assert old2 in t
t = t.replace(old2, new2, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
