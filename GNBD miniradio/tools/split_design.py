# -*- coding: utf-8 -*-
"""함수→대상파일 매핑을 주면 교차 파일 static 심볼(=extern 처리 필요)을 보고한다."""
import json
import re
import sys
from collections import defaultdict

def main():
    jf, mapping_file = sys.argv[1], sys.argv[2]
    d = json.load(open(jf, encoding='utf-8'))
    mapping = json.load(open(mapping_file, encoding='utf-8'))  # {func: file}
    funcs = {f['name']: f for f in d['funcs']}
    usage = d['usage']
    syms = {s['name']: s for s in d['file_symbols'] if s['kind'] in ('staticvar', 'staticvar?', 'define')}

    # 심볼 → 사용 함수들 → 대상 파일 집합
    sym_files = defaultdict(set)
    sym_funcs = defaultdict(list)
    for fn, used in usage.items():
        tgt = mapping.get(fn, 'UNMAPPED')
        if tgt == 'SKIP':
            continue
        for u in used:
            if u in syms or u in funcs:  # 파일 수준 심볼 또는 함수
                sym_files[u].add(tgt)
                sym_funcs[u].append(fn)

    print('--- UNMAPPED functions ---')
    for fn in sorted(funcs):
        if fn not in mapping:
            print('  ', fn, funcs[fn]['start'])

    print('--- cross-file (needs extern or move) ---')
    for s, files in sorted(sym_files.items()):
        if len(files) > 1:
            kind = 'func' if s in funcs else 'sym'
            print(f'  {kind:4} {s:28} -> {sorted(files)}')

    print('--- single-file (can stay static) ---')
    for s, files in sorted(sym_files.items()):
        if len(files) == 1:
            kind = 'func' if s in funcs else 'sym'
            print(f'  {kind:4} {s:28} -> {sorted(files)}')

if __name__ == '__main__':
    main()
