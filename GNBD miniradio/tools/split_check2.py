# -*- coding: utf-8 -*-
"""교차 심볼 판정 (정의 파일 포함). static 유지 불가 대상과 선언 누락을 보고."""
import json
import re
import sys
from collections import defaultdict

d = json.load(open(sys.argv[1], encoding='utf-8'))
mapping = json.load(open(sys.argv[2], encoding='utf-8'))
app_h = open('main/app.h', encoding='utf-8', errors='replace').read()

funcs = {f['name']: f for f in d['funcs']}
syms = {s['name']: s for s in d['file_symbols'] if s['kind'] == 'staticvar'}
defs = {x['name'] for x in d['file_symbols'] if x['kind'] == 'define'}

# 심볼 -> 사용 함수들의 대상 파일 집합
use_files = defaultdict(set)
use_funcs = defaultdict(list)
for fn, used in d['usage'].items():
    tgt = mapping.get(fn, 'SKIP')
    if tgt == 'SKIP':
        continue
    for u in used:
        if u in funcs or u in syms or u in defs:
            use_files[u].add(tgt)
            use_funcs[u].append(fn)


def home(name):
    """정의(또는 매핑) 파일."""
    if name in funcs:
        return mapping.get(name, '?')
    # 심볼: 라인 위치로 판별
    if name in syms:
        return None  # 별도 처리
    return None


def sym_home(name):
    if name in syms:
        ln = syms[name]['line']
    else:
        d2 = [x for x in d['file_symbols'] if x['name'] == name]
        if not d2:
            return None
        ln = d2[0]['line']
    return 'main.c'  # main.c 파일 수준 심볼


def declared(name, src):
    return bool(re.search(r'\b' + re.escape(name) + r'\s*\(', src))


print('=== static FUNC: 정의파일 외 호출자 -> un-static + hmi.h 프로토타입 ===')
for name, f in sorted(funcs.items()):
    if not f['static']:
        continue
    h = mapping.get(name)
    ext = use_files[name] - {h}
    if ext:
        print('  %-24s def=%s  callers_elsewhere=%s  app.h=%s' %
              (name, h, sorted(ext), 'YES' if declared(name, app_h) else 'NO'))

print('=== non-static FUNC: 정의파일 외 호출자 + app.h 선언 없음 -> hmi.h ===')
for name, f in sorted(funcs.items()):
    if f['static']:
        continue
    h = mapping.get(name)
    ext = use_files[name] - {h}
    if ext and not declared(name, app_h):
        print('  %-24s def=%s  callers_elsewhere=%s' % (name, h, sorted(ext)))

print('=== static VAR: 정의파일(main.c) 외 사용 -> un-static + hmi.h extern ===')
for name in sorted(syms):
    h = 'main.c'
    ext = use_files[name] - {h}
    if ext:
        print('  %-24s callers=%s' % (name, sorted(use_files[name])))

print('=== static VAR: main.c 외 단일 사용파일 -> 그 파일로 이동(유지) ===')
for name in sorted(syms):
    if use_files[name] and 'main.c' not in use_files[name]:
        print('  %-24s -> %s' % (name, sorted(use_files[name])))

print('=== define: 정의파일 외 사용 ===')
for name in sorted(defs):
    ext = use_files[name] - {'main.c'}
    if ext:
        in_app = bool(re.search(r'#\s*define\s+' + re.escape(name) + r'\b', app_h))
        print('  %-24s callers=%s app.h_define=%s' %
              (name, sorted(use_files[name]), in_app))
