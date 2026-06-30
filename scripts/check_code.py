#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""clang-format + clang-tidy 检查 main/ 源码。"""

import argparse
import difflib
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

from clang_common import (
    GREEN,
    NC,
    RED,
    YELLOW,
    collect_sources,
    enable_ansi,
    find_compile_db,
    find_llvm_tool,
    print_scope,
    project_root,
    scope_label,
)

TIDY_SKIP = ('/esp-idf/', '/managed_components/', 'expanded from macro')
TIDY_LINE_RE = re.compile(
    r'^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+):\s+'
    r'(?P<kind>warning|error):\s+(?P<msg>.+?)\s+\[(?P<check>[^\]]+)\]\s*$'
)


def rel_posix(root: Path, path: str) -> str:
    try:
        return Path(path).resolve().relative_to(root).as_posix()
    except ValueError:
        return path.replace('\\', '/')


def check_format(path: Path, root: Path, tool: str) -> bool:
    try:
        proc = subprocess.run(
            [tool, str(path)],
            cwd=root,
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='ignore',
        )
        if proc.returncode != 0:
            print(f'{RED}  format: {path.relative_to(root)}{NC}')
            return True

        original = path.read_text(encoding='utf-8', errors='ignore')
        if original == proc.stdout:
            return False

        print(f'{RED}  format: {path.relative_to(root)}{NC}')
        diff = difflib.unified_diff(
            original.splitlines(keepends=True),
            proc.stdout.splitlines(keepends=True),
            fromfile=str(path.relative_to(root)),
            tofile='formatted',
            n=2,
        )
        for line in list(diff)[:12]:
            print(f'    {line}', end='')
        return True
    except OSError as e:
        print(f'{RED}  format: {path.relative_to(root)} ({e}){NC}')
        return True


def collect_tidy_issues(path: Path, root: Path, tool: str, compile_db: Path):
    cmd = [
        tool,
        str(path),
        '--config-file',
        str(root / '.clang-tidy'),
        '-p',
        str(compile_db.parent),
        '--header-filter',
        f'^{str(root).replace(chr(92), "/")}/main/.*',
        '--system-headers=0',
        '--quiet',
    ]
    try:
        proc = subprocess.run(
            cmd,
            cwd=root,
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='ignore',
        )
    except OSError:
        return []

    issues = []
    for line in (proc.stdout or '').splitlines() + (proc.stderr or '').splitlines():
        if 'main/' not in line and 'main\\' not in line:
            continue
        if any(s in line for s in TIDY_SKIP):
            continue
        m = TIDY_LINE_RE.match(line.strip())
        if not m:
            continue
        loc = f"{rel_posix(root, m.group('file'))}:{m.group('line')}:{m.group('col')}"
        issues.append({
            'loc': loc,
            'msg': m.group('msg'),
            'check': m.group('check'),
            'kind': m.group('kind'),
        })
    return issues


def print_tidy_summary(all_issues, file_hits):
    if not all_issues:
        print('  无告警')
        return 0

    grouped = defaultdict(set)
    for item in all_issues:
        grouped[(item['check'], item['msg'])].add(item['loc'])

    print(f'  告警 {len(grouped)} 类，涉及 {file_hits} 个源文件')
    for (check, msg), locs in sorted(grouped.items(), key=lambda x: (-len(x[1]), x[0][0])):
        print(f'  [{check}] {msg}')
        shown = sorted(locs)
        for loc in shown[:3]:
            print(f'    {loc}')
        if len(shown) > 3:
            print(f'    ... 另有 {len(shown) - 3} 处')
    return len(grouped)


def run_tidy(files, root, tool, compile_db):
    all_issues = []
    file_hits = 0
    total = len(files)

    for idx, path in enumerate(files, 1):
        issues = collect_tidy_issues(path, root, tool, compile_db)
        if issues:
            file_hits += 1
            all_issues.extend(issues)
        if idx == total or idx % 20 == 0:
            print(f'  进度 {idx}/{total}', end='\r')

    print(' ' * 20, end='\r')
    return print_tidy_summary(all_issues, file_hits)


def main():
    parser = argparse.ArgumentParser(description='clang-format / clang-tidy 检查')
    parser.add_argument('paths', nargs='*', help='相对工程根的路径，默认 main/')
    args = parser.parse_args()

    enable_ansi()
    root = project_root()

    fmt = find_llvm_tool('clang-format')
    tidy = find_llvm_tool('clang-tidy')
    if not fmt or not tidy:
        missing = [n for n, t in (('clang-format', fmt), ('clang-tidy', tidy)) if not t]
        print(f'{YELLOW}未找到: {", ".join(missing)}{NC}', file=sys.stderr)
        sys.exit(1)

    files = collect_sources(root, args.paths or None)
    if not files:
        print(f'{YELLOW}未找到源文件{NC}', file=sys.stderr)
        sys.exit(1)

    print_scope(root, args.paths or None, files)
    print('')

    print('[1/2] clang-format')
    format_bad = [f for f in files if check_format(f, root, fmt)]
    if not format_bad:
        print(f'  {GREEN}通过{NC}')

    compile_db = find_compile_db(root)
    tidy_kinds = 0
    print('[2/2] clang-tidy')
    if compile_db is None:
        print(f'  {YELLOW}无 compile_commands.json，已跳过（先 idf.py build）{NC}')
    else:
        print(f'  compile_db: {compile_db.relative_to(root)}')
        tidy_kinds = run_tidy(files, root, tidy, compile_db)

    print('')
    if not format_bad and tidy_kinds == 0:
        print(f'{GREEN}通过 {len(files)} 文件  范围: {scope_label(args.paths or None)}{NC}')
        sys.exit(0)

    print(f'{YELLOW}问题: format={len(format_bad)}, tidy={tidy_kinds} 类  范围: {scope_label(args.paths or None)}{NC}')
    sys.exit(1)


if __name__ == '__main__':
    main()
