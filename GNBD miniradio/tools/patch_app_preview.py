# -*- coding: utf-8 -*-
"""앱/스크립트 탭에서 레이아웃 미리보기 표시 + 캐시 무효화(v=2)."""
import io

# 1) index.html: 미리보기 캔버스 추가, 쿼리 v=2
p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()

old = '''<div class="panel" id="viewApp">
  <section class="pcard">
    <h3>앱 정보</h3>'''
new = '''<div class="panel" id="viewApp">
  <section class="pcard">
    <h3>레이아웃 미리보기</h3>
    <canvas id="previewCanvas" width="640" height="340"
      style="width:100%;max-width:320px;border:1px solid #e4e7ec;border-radius:8px;background:#080c10;image-rendering:pixelated"></canvas>
    <div class="phint">레이아웃 탭에서 배치한 결과입니다. 이 화면을 기준으로 스크립트/블록을 작성하세요.</div>
  </section>

  <section class="pcard">
    <h3>앱 정보</h3>'''
assert old in t
t = t.replace(old, new, 1)

t = t.replace('?v=1"', '?v=2"').replace('?v=1\'', '?v=2\'')
io.open(p, 'w', encoding='utf-8', newline='').write(t)

# 2) extended.js: 탭 전환 시 미리보기 렌더
p = 'uiedt/extended/js/extended.js'
t = io.open(p, encoding='utf-8').read()

old2 = '''  function tab(which) {
    const layout = which === "layout";
    $("tabLayout").classList.toggle("on", layout);
    $("tabApp").classList.toggle("on", !layout);
    $("viewLayout").style.display = layout ? "" : "none";
    $("viewApp").classList.toggle("on", !layout);
    if (layout && window.ED) ED.draw();
  }'''
new2 = '''  function drawPreview() {
    const cv = $("previewCanvas");
    if (!cv || !window.ED || !window.Render) return;
    const ctx = cv.getContext("2d");
    ctx.imageSmoothingEnabled = false;
    const mask = [1, 2, 4][ED.pmode || 0];
    Render.draw(ctx, { bg: ED.project.bg, items: ED.sortedItems() },
                ED.sim, 2, mask, true);
  }

  function tab(which) {
    const layout = which === "layout";
    $("tabLayout").classList.toggle("on", layout);
    $("tabApp").classList.toggle("on", !layout);
    $("viewLayout").style.display = layout ? "" : "none";
    $("viewApp").classList.toggle("on", !layout);
    if (layout && window.ED) {
      ED.draw();
    } else {
      drawPreview();
    }
  }'''
assert old2 in t
t = t.replace(old2, new2, 1)

old3 = '''    $("btnExluRefresh").onclick = refresh;
    $("btnExluApply").onclick = apply;'''
new3 = '''    $("btnExluRefresh").onclick = refresh;
    $("btnExluApply").onclick = apply;
    /* 레이아웃 변경 시 미리보기 갱신 */
    if (window.ED) {
      const origDraw = ED.draw.bind(ED);
      ED.draw = function () {
        origDraw();
        if ($("viewApp").classList.contains("on")) drawPreview();
      };
    }'''
assert old3 in t
t = t.replace(old3, new3, 1)

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
