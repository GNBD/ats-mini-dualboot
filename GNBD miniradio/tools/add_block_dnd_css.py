# -*- coding: utf-8 -*-
"""블록 드래그앤드롭 CSS 추가."""
import io

p = 'uiedt/extended/index.html'
t = io.open(p, encoding='utf-8').read()

old = '''  .blk input,.blk select{border:1px solid #cfd6e4;border-radius:6px;
       padding:3px 5px;font:inherit;font-size:11px;background:#fff;max-width:120px}'''
new = '''  .blk input,.blk select{border:1px solid #cfd6e4;border-radius:6px;
       padding:3px 5px;font:inherit;font-size:11px;background:#fff;max-width:120px}
  .blk .grip{color:#7aa7d8;font-size:11px;cursor:grab;user-select:none}
  .blk.ctrl{background:#fff4e5;border-color:#f7c98b;border-left-color:#f79009}
  .blk.ctrl .blkname{color:#8a4b00}
  .blkadd.ctrl{background:#fff4e5;border-color:#f7c98b;color:#8a4b00}
  .blkstack.sub{border-left:2px dashed #cfd6e4;padding-left:6px;margin-top:2px}
  .blk.over,.blkstack.over{outline:2px dashed #0072de;outline-offset:2px}
  .blkstack.over{background:#f2f8ff;border-radius:8px}'''
assert old in t
t = t.replace(old, new, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
