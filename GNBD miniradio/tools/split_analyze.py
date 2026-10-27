# -*- coding: utf-8 -*-
"""main.c / ui.c 파일 분할용 의존성 분석.
파일 수준(스코프 0) 심볼 정의와 각 함수가 참조하는 심볼을 매트릭스로 출력한다.
"""
import json
import re
import sys
from collections import defaultdict

FUNC_START = re.compile(
    r'^(static\s+)?(inline\s+)?(const\s+)?[\w\s\*]+?[\s\*]([A-Za-z_]\w*)\s*\([^;{]*\)\s*(\{)?\s*$'
)
STATIC_VAR = re.compile(r'^static\s+.+?([A-Za-z_]\w*)\s*(\[[^\]]*\])*\s*(=[^;]+)?;\s*(/\*.*)?$')
EXTERN_DECL = re.compile(r'^extern\s+')
PROTO = re.compile(r'^[\w\s\*]+?\(.*\)\s*;\s*$')
DEFINE = re.compile(r'^#\s*define\s+([A-Za-z_]\w*)')


def strip_comments(text):
    """주석을 공백으로 대체(줄 수 유지). 문자열 리터럴은 대충 처리."""
    out = []
    i, n, in_str, in_chr = 0, len(text), False, False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == '\\':
                if i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
            elif c == '"':
                in_str = False
            i += 1
            continue
        if in_chr:
            out.append(c)
            if c == '\\':
                if i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
            elif c == "'":
                in_chr = False
            i += 1
            continue
        if c == '"':
            in_str = True
            out.append(c)
            i += 1
            continue
        if c == "'":
            in_chr = True
            out.append(c)
            i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            while i < n and text[i] != '\n':
                out.append(' ')
                i += 1
            continue
        if c == '/' and i + 1 < n and text[i + 1] == '*':
            out.append(' ')
            out.append(' ')
            i += 2
            while i < n and not (text[i] == '*' and i + 1 < n and text[i + 1] == '/'):
                out.append('\n' if text[i] == '\n' else ' ')
                i += 1
            i = min(i + 2, n)
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def analyze(path):
    raw = open(path, encoding='utf-8', errors='replace').read()
    text = strip_comments(raw)
    lines = text.split('\n')
    orig_lines = raw.split('\n')

    depth = 0
    funcs = []          # {name, start, end, static, proto_only}
    defs = []           # {name, start, end}  함수 정의
    file_symbols = []   # {name, kind: staticvar|define|externproto, line}
    pending = None      # 함수 헤더 대기(여러 줄 시그니처)
    paren_depth = 0
    i = 0
    n = len(lines)

    # 1차 스캔: depth 계산하며 함수 정의/파일 심볼 수집
    sig_start = None
    sig_buf = []
    for ln, line in enumerate(lines, 1):
        s = line.strip()
        if depth == 0:
            m = DEFINE.match(s)
            if m:
                file_symbols.append({'name': m.group(1), 'kind': 'define', 'line': ln})
            if EXTERN_DECL.match(s):
                file_symbols.append({'name': re.search(r'([A-Za-z_]\w*)\s*(\[[^\]]*\])*\s*;', s).group(1) if re.search(r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\])*\s*;', s) else '?', 'kind': 'extern', 'line': ln})
            if s.startswith('static') and '(' not in s and ';' in s and not s.startswith('static void') :
                m2 = STATIC_VAR.match(s)
                if m2:
                    file_symbols.append({'name': m2.group(1), 'kind': 'staticvar', 'line': ln})
            # 함수 시그니처 시작 감지(중괄호 없는 행에서 '(' 있고 ';' 없음)
            if sig_start is None and '(' in s and not s.startswith('#') and not s.endswith(';') and not s.startswith('typedef'):
                if re.match(r'^(static\s+)?(inline\s+)?[\w\s\*]+\(', s):
                    sig_start = ln
                    sig_buf = [s]
                    if '{' in s:
                        # 한 줄 함수 시그니처+본문 시작
                        pass
            elif sig_start is not None:
                sig_buf.append(s)
            if sig_start is not None:
                joined = ' '.join(sig_buf)
                if ';' in joined and '{' not in joined:
                    # 프로토타입(중간 공백 포함)
                    joined2 = re.sub(r'__attribute__\s*\(\([^)]*\)\)', '', joined)
                    pm = re.search(r'([A-Za-z_]\w*)\s*\(', joined2)
                    if pm:
                        file_symbols.append({'name': pm.group(1), 'kind': 'proto', 'line': sig_start})
                    sig_start, sig_buf = None, []
                elif '{' in joined:
                    joined = re.sub(r'__attribute__\s*\(\([^)]*\)\)', '', joined)
                    fm = re.search(r'([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{', joined)
                    if fm and '(' in joined.split('{')[0]:
                        name = fm.group(1)
                        is_static = joined.startswith('static')
                        defs.append({'name': name, 'start': sig_start, 'static': is_static, 'brace_line': ln})
                    else:
                        # 함수 아닐 수 있음(예: 전역 초기화)
                        sig_start, sig_buf = None, []
                    if defs and defs[-1].get('brace_line') == ln:
                        sig_start, sig_buf = None, []
        # 깊이 계산(중괄호 수)
        depth += line.count('{') - line.count('}')
        if defs and defs[-1].get('brace_line') and 'end' not in defs[-1] and depth == 0 and line.count('}'):
            defs[-1]['end'] = ln

    # 일부 블록이 닫히지 않았으면 파일 끝으로
    for d in defs:
        d.setdefault('end', n)

    return {'raw': raw, 'orig_lines': orig_lines, 'funcs': defs, 'file_symbols': file_symbols}


def symbol_usage(result):
    """각 함수 본문에서 참조하는 파일 수준 심볼/타 함수 이름 집합."""
    orig = result['orig_lines']
    names = set(f['name'] for f in result['funcs'])
    names |= set(s['name'] for s in result['file_symbols'] if s['kind'] in ('staticvar', 'define'))
    usage = {}
    for f in result['funcs']:
        body = '\n'.join(orig[f['start'] - 1:f['end']])
        used = set()
        for nm in names:
            if nm == f['name']:
                continue
            if re.search(r'\b' + re.escape(nm) + r'\b', body):
                used.add(nm)
        usage[f['name']] = sorted(used)
    return usage


if __name__ == '__main__':
    path = sys.argv[1]
    res = analyze(path)
    usage = symbol_usage(res)
    out = {'file': path, 'funcs': res['funcs'], 'file_symbols': res['file_symbols'], 'usage': usage}
    json.dump(out, open(sys.argv[2], 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
    print(f"funcs={len(res['funcs'])} symbols={len(res['file_symbols'])}")
    for f in res['funcs']:
        print(f"  {f['start']:5}-{f['end']:5} {'static ' if f['static'] else '        '}{f['name']}")
