# -*- coding: utf-8 -*-
"""부팅 선택: 타임아웃 시 아무것도 적용하지 않음(None)."""
import io

p = 'main/ui/ui_exlu_app.c'
t = io.open(p, encoding='utf-8').read()

start = t.find('    /* 타임아웃: 마지막으로 선택했던 항목이 있으면 자동 실행 */')
assert start > 0, 'timeout block not found'
end_marker = '    return false;\n}'
end = t.find(end_marker, start)
assert end > 0, 'end not found'
t = t[:start] + '    /* 타임아웃: None 과 동일하게 아무것도 적용하지 않는다 */\n' + t[end:]

io.open(p, 'w', encoding='utf-8', newline='').write(t)
print('ok')
