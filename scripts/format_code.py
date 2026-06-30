#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""clang-format 格式化 main/ 源码。"""

import argparse
import subprocess
import sys

from clang_common import (
    GREEN,
    NC,
    YELLOW,
    collect_sources,
    enable_ansi,
    find_llvm_tool,
    print_scope,
    project_root,
    scope_label,
)


def main():
    parser = argparse.ArgumentParser(description='clang-format 格式化')
    parser.add_argument('paths', nargs='*', help='相对工程根的路径，默认 main/')
    parser.add_argument('-y', '--yes', action='store_true', help='跳过确认')
    parser.add_argument('-v', '--verbose', action='store_true', help='打印每个文件')
    args = parser.parse_args()

    enable_ansi()
    root = project_root()
    tool = find_llvm_tool('clang-format')
    if not tool:
        print(f'{YELLOW}clang-format 未找到（PATH 或 CLANG_FORMAT_PATH）{NC}', file=sys.stderr)
        sys.exit(1)

    files = collect_sources(root, args.paths or None)
    if not files:
        print(f'{YELLOW}未找到源文件{NC}', file=sys.stderr)
        sys.exit(1)

    print_scope(root, args.paths or None, files)
    print(f'工具: {tool}')
    print('')

    if not args.yes:
        print(f'继续格式化? (y/n) ', end='')
        if input().strip().lower() != 'y':
            sys.exit(0)

    ok = 0
    for path in files:
        try:
            subprocess.run([tool, '-i', str(path)], cwd=root, check=True)
            ok += 1
            if args.verbose:
                print(path.relative_to(root))
        except subprocess.CalledProcessError:
            print(f'{YELLOW}失败: {path}{NC}', file=sys.stderr)

    print('')
    print(f'{GREEN}完成 {ok}/{len(files)}  范围: {scope_label(args.paths or None)}{NC}')


if __name__ == '__main__':
    main()
