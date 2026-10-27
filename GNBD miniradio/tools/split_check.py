# -*- coding: utf-8 -*-
"""교차 심볼 중 static(=외부 공유 필요)과 app.h 선언 여부를 구분."""
import json, re, sys

d = json.load(open(sys.argv[1], encoding='utf-8'))
mapping = json.load(open(sys.argv[2], encoding='utf-8'))
app_h = open('main/app.h', encoding='utf-8', errors='replace').read()

funcs = {f['name']: f for f in d['funcs']}
syms = {s['name']: s for s in d['file_symbols'] if s['kind'] == 'staticvar'}

cross = set()
for s in d['file_symbols']:
    pass
# cross-file 목록은 split_design 출력 재계산
from collections import defaultdict
usage = d['usage']
sym_files = defaultdict(set)
for fn, used in usage.items():
    tgt = mapping.get(fn, 'SKIP')
    if tgt == 'SKIP':
        continue
    for u in used:
        if u in funcs or u in syms or u in {x['name'] for x in d['file_symbols'] if x['kind'] == 'define'}:
            sym_files[u].add(tgt)

def declared(name, src):
    # 이름 뒤에 '(' 오는 선언 매칭
    return bool(re.search(r'\b' + re.escape(name) + r'\s*\(', src))

print('=== static FUNC cross-file (needs un-static + prototype) ===')
for s, files in sorted(sym_files.items()):
    if len(files) > 1 and s in funcs and funcs[s]['static']:
        in_app = declared(s, app_h)
        print(f'  {s:24} files={len(files)} app.h={"YES" if in_app else "NO"}')

print('=== non-static FUNC cross-file (already global, app.h? ) ===')
for s, files in sorted(sym_files.items()):
    if len(files) > 1 and s in funcs and not funcs[s]['static']:
        in_app = declared(s, app_h)
        if not in_app:
            print(f'  {s:24} files={len(files)} app.h=NO  <-- needs prototype')

print('=== static VAR cross-file (needs extern) ===')
for s, files in sorted(sym_files.items()):
    if len(files) > 1 and s in syms:
        in_app = bool(re.search(r'\bextern\b[^;]*\b' + re.escape(s) + r'\b', app_h))
        print(f'  {s:24} files={sorted(files)} app.h_extern={"YES" if in_app else "NO"}')

print('=== defines cross-file ===')
defs = {x['name'] for x in d['file_symbols'] if x['kind'] == 'define'}
for s, files in sorted(sym_files.items()):
    if len(files) > 1 and s in defs:
        in_app = bool(re.search(r'#\s*define\s+' + re.escape(s) + r'\b', app_h))
        print(f'  {s:24} files={len(files)} app.h_define={"YES" if in_app else "NO"}')
