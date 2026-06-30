#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""将 ESP-IDF 构建产物打包为发布目录（分镜像 + 可选全量合并 bin + OTA 包）。

用法:
  python scripts/package_firmware.py          # 交互式菜单（默认）
  python scripts/package_firmware.py --zip    # 非交互，直接按参数打包
  python scripts/package_firmware.py -i       # 强制交互式
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass
class PackageOptions:
    build_dir: Path
    out_dir: Path
    version: str | None
    copy_flash: bool
    copy_ota: bool
    merge: bool
    zip_archive: bool


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def read_project_ver_from_cmake(root: Path) -> str | None:
    cmake = root / 'CMakeLists.txt'
    if not cmake.is_file():
        return None
    text = cmake.read_text(encoding='utf-8')
    m = re.search(r'set\s*\(\s*PROJECT_VER\s+"([^"]+)"\s*\)', text)
    return m.group(1) if m else None


def load_json(path: Path) -> dict:
    with path.open(encoding='utf-8') as f:
        return json.load(f)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def sanitize_version(ver: str) -> str:
    s = ver.strip()
    s = re.sub(r'[<>:"/\\|?*]', '_', s)
    return s or 'unknown'


def find_esptool() -> list[str] | None:
    if shutil.which('esptool.py'):
        return ['esptool.py']
    if shutil.which('esptool'):
        return ['esptool']

    candidates: list[list[str]] = []

    idf_path = os.environ.get('IDF_PATH')
    if idf_path:
        esptool_py = Path(idf_path) / 'components' / 'esptool_py' / 'esptool' / 'esptool.py'
        if esptool_py.is_file():
            candidates.append([sys.executable, str(esptool_py)])

    for py in (
        Path(os.environ.get('IDF_PYTHON_ENV_PATH', '')) / 'Scripts' / 'python.exe',
        Path(os.environ.get('IDF_PYTHON_ENV_PATH', '')) / 'bin' / 'python',
    ):
        if py.is_file():
            candidates.append([str(py), '-m', 'esptool'])

    try:
        import esptool  # noqa: F401
    except ImportError:
        pass
    else:
        candidates.append([sys.executable, '-m', 'esptool'])

    for cmd in candidates:
        try:
            subprocess.run(
                cmd + ['version'],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            return cmd
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
    return None


def resolve_build_files(build_dir: Path, flash_files: dict[str, str]) -> list[tuple[int, Path]]:
    items: list[tuple[int, Path]] = []
    for offset_s, rel in flash_files.items():
        path = build_dir / rel.replace('/', '\\').replace('\\', '/')
        if not path.is_file():
            path = build_dir / Path(rel)
        if not path.is_file():
            raise FileNotFoundError(f'构建产物缺失: {path}')
        items.append((int(offset_s, 0), path))
    items.sort(key=lambda x: x[0])
    return items


def merge_flash_image(
    esptool_cmd: list[str],
    build_dir: Path,
    flasher: dict,
    out_path: Path,
) -> None:
    args = flasher.get('write_flash_args', [])
    flash_mode = 'dio'
    flash_freq = '80m'
    flash_size = '16MB'
    for i, arg in enumerate(args):
        if arg == '--flash-mode' and i + 1 < len(args):
            flash_mode = args[i + 1]
        elif arg == '--flash-size' and i + 1 < len(args):
            flash_size = args[i + 1]
        elif arg == '--flash-freq' and i + 1 < len(args):
            flash_freq = args[i + 1]

    segments = resolve_build_files(build_dir, flasher['flash_files'])
    cmd = esptool_cmd + [
        '--chip',
        flasher.get('extra_esptool_args', {}).get('chip', 'esp32s3'),
        'merge-bin',
        '-o',
        str(out_path),
        '--flash-mode',
        flash_mode,
        '--flash-freq',
        flash_freq,
        '--flash-size',
        flash_size,
    ]
    for offset, path in segments:
        cmd.append(f'0x{offset:x}')
        cmd.append(str(path))

    print(f'合并全量镜像: {out_path.name}')
    subprocess.run(cmd, check=True)


def git_short_hash(root: Path) -> str | None:
    try:
        out = subprocess.check_output(
            ['git', 'rev-parse', '--short', 'HEAD'],
            cwd=root,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        return out.strip() or None
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None


def write_flash_md(path: Path, manifest: dict, merge_name: str | None) -> None:
    lines = [
        f'# {manifest["project"]} 烧录说明',
        '',
        f'- 版本: `{manifest["version"]}`',
        f'- 芯片: `{manifest["target"]}`',
        f'- Flash: `{manifest["flash_size"]}`',
        f'- 打包时间: `{manifest["packed_at"]}`',
        '',
        '## 方式一：idf.py（开发）',
        '',
        '```bash',
        'idf.py build flash',
        '```',
        '',
        '## 方式二：esptool 分文件烧录',
        '',
        '在 `flash/` 目录下执行（请按实际串口修改 `COMx`）：',
        '',
        '```bash',
    ]
    for entry in manifest['flash_files']:
        if entry.get('merged'):
            continue
        lines.append(
            f'esptool.py --chip {manifest["target"]} -b 460800 write_flash '
            f'{entry["offset"]} {entry["name"]}'
        )
    lines += [
        '```',
        '',
    ]
    if merge_name:
        lines += [
            '## 方式三：全量合并镜像（工厂烧录）',
            '',
            '```bash',
            f'esptool.py --chip {manifest["target"]} -b 460800 write_flash 0x0 {merge_name}',
            '```',
            '',
        ]
    lines += [
        '## OTA',
        '',
        '仅升级应用程序分区时使用 `ota/Ebook.bin`（写入 `ota_0` / `ota_1` 对应偏移，由 OTA 服务处理）。',
        '',
    ]
    path.write_text('\n'.join(lines), encoding='utf-8')


def detect_version(root: Path, build_dir: Path) -> str:
    desc_path = build_dir / 'project_description.json'
    desc = load_json(desc_path) if desc_path.is_file() else {}
    return (
        read_project_ver_from_cmake(root)
        or desc.get('project_version')
        or 'unknown'
    )


def build_ready(build_dir: Path) -> tuple[bool, str]:
    flasher_path = build_dir / 'flasher_args.json'
    if not flasher_path.is_file():
        return False, f'未找到 {flasher_path}，请先执行 idf.py build'
    return True, '构建产物就绪'


def is_interactive_tty() -> bool:
    try:
        return sys.stdin.isatty()
    except Exception:
        return False


def parse_bool_answer(raw: str, default: bool) -> bool:
    s = raw.strip().lower()
    if not s:
        return default
    if s in ('y', 'yes', '1', '是', 'true', 'on'):
        return True
    if s in ('n', 'no', '0', '否', 'false', 'off'):
        return False
    return default


def prompt_yes_no(prompt: str, default: bool) -> bool:
    default_hint = '是' if default else '否'
    while True:
        raw = input(f'{prompt} [Y/N，默认{default_hint}]: ').strip()
        if not raw:
            return default
        if raw.lower() in ('y', 'yes', '1', '是'):
            return True
        if raw.lower() in ('n', 'no', '0', '否'):
            return False
        print('  请输入 Y/是 或 N/否，或直接回车使用默认值。')


def prompt_optional(prompt: str, default: str) -> str:
    raw = input(f'{prompt} [{default}]: ').strip()
    return raw or default


def prompt_optional_version(default: str) -> str | None:
    raw = input(f'覆盖版本号（回车使用 {default}）: ').strip()
    return raw if raw else None


def print_banner() -> None:
    print()
    print('=' * 52)
    print('  Ebook 固件打包')
    print('=' * 52)


def print_build_summary(root: Path, build_dir: Path, out_dir: Path) -> None:
    ok, msg = build_ready(build_dir)
    ver = detect_version(root, build_dir) if ok else '(未知)'
    esptool = find_esptool()
    print()
    print('【环境】')
    print(f'  工程目录 : {root}')
    print(f'  构建目录 : {build_dir}')
    print(f'  输出目录 : {out_dir}')
    print(f'  固件版本 : {ver}')
    print(f'  构建状态 : {msg}')
    print(f'  esptool  : {"已找到" if esptool else "未找到（无法合并全量 bin）"}')
    print()


def print_options_summary(opts: PackageOptions) -> None:
    print('【本次选项】')
    print(f'  分文件镜像 (flash/) : {"是" if opts.copy_flash else "否"}')
    print(f'  OTA 包 (ota/)       : {"是" if opts.copy_ota else "否"}')
    print(f'  全量合并 bin        : {"是" if opts.merge else "否"}')
    print(f'  ZIP 压缩包          : {"是" if opts.zip_archive else "否"}')
    if opts.version:
        print(f'  版本覆盖            : {opts.version}')
    print()


def interactive_menu(root: Path, build_dir: Path, out_dir: Path) -> PackageOptions | None:
    print_banner()
    print_build_summary(root, build_dir, out_dir)

    ok, _ = build_ready(build_dir)
    if not ok:
        if prompt_yes_no('构建目录未就绪，仍要继续吗', False):
            pass
        else:
            return None

    presets = {
        '1': ('标准发布包', True, True, True, False),
        '2': ('标准发布包 + ZIP', True, True, True, True),
        '3': ('仅 OTA 应用包', False, True, False, False),
        '4': ('完整包（不含全量合并）', True, True, False, False),
        '5': ('自定义选项', None, None, None, None),
    }

    print('【快捷方案】')
    for key, (label, *_) in presets.items():
        print(f'  {key}. {label}')
    print('  0. 退出')
    print()

    while True:
        choice = input('请选择: ').strip() or '1'
        if choice == '0':
            print('已取消。')
            return None
        if choice not in presets:
            print('无效选项，请重新输入。')
            continue
        break

    label, copy_flash, copy_ota, merge, zip_archive = presets[choice]

    if choice == '5':
        print()
        print('【自定义】逐项选择（回车=默认）')
        copy_flash = prompt_yes_no('  复制分文件镜像到 flash/', True)
        copy_ota = prompt_yes_no('  复制 OTA 应用包到 ota/', True)
        if copy_flash:
            merge_default = find_esptool() is not None
            merge = prompt_yes_no('  生成全量合并 flash bin', merge_default)
        else:
            merge = False
        zip_archive = prompt_yes_no('  额外生成 .zip 压缩包', False)
    else:
        print(f'\n已选: {label}')
        if merge and find_esptool() is None:
            print('  提示: 未找到 esptool，全量合并将自动跳过。')

    if not copy_flash and not copy_ota:
        print('错误: 至少需要 flash 分文件或 OTA 包之一。')
        return None

    print()
    if prompt_yes_no('修改构建/输出目录', False):
        build_dir = Path(prompt_optional('  构建目录', str(build_dir))).resolve()
        out_dir = Path(prompt_optional('  输出目录', str(out_dir))).resolve()
        out_dir.mkdir(parents=True, exist_ok=True)

    version: str | None = None
    if prompt_yes_no('覆盖 manifest 版本号', False):
        version = prompt_optional_version(detect_version(root, build_dir))

    opts = PackageOptions(
        build_dir=build_dir,
        out_dir=out_dir,
        version=version,
        copy_flash=copy_flash,
        copy_ota=copy_ota,
        merge=merge and copy_flash,
        zip_archive=zip_archive,
    )
    print_options_summary(opts)
    if not prompt_yes_no('确认开始打包', True):
        print('已取消。')
        return None
    return opts


def package_firmware(root: Path, opts: PackageOptions) -> Path:
    build_dir = opts.build_dir
    out_dir = opts.out_dir

    flasher_path = build_dir / 'flasher_args.json'
    desc_path = build_dir / 'project_description.json'
    if not flasher_path.is_file():
        raise FileNotFoundError(
            f'未找到 {flasher_path}，请先执行 idf.py build'
        )

    flasher = load_json(flasher_path)
    desc = load_json(desc_path) if desc_path.is_file() else {}

    ver = opts.version or detect_version(root, build_dir)
    ver_safe = sanitize_version(ver)
    target = flasher.get('extra_esptool_args', {}).get('chip', desc.get('target', 'esp32s3'))
    flash_size = flasher.get('flash_settings', {}).get('flash_size', '16MB')

    project_name = desc.get('project_name', 'Ebook')
    if ver_safe.lower().startswith(project_name.lower() + '-') or ver_safe.lower() == project_name.lower():
        bundle_name = ver_safe
    else:
        bundle_name = f'{project_name}-{ver_safe}'
    bundle_dir = out_dir / bundle_name
    if bundle_dir.exists():
        shutil.rmtree(bundle_dir)
    flash_dir = bundle_dir / 'flash'
    ota_dir = bundle_dir / 'ota'
    if opts.copy_flash:
        flash_dir.mkdir(parents=True)
    if opts.copy_ota:
        ota_dir.mkdir(parents=True)
    bundle_dir.mkdir(parents=True, exist_ok=True)

    flash_entries: list[dict] = []
    file_hashes: dict[str, str] = {}

    segments = resolve_build_files(build_dir, flasher['flash_files'])
    app_bin: Path | None = None
    for offset, src in segments:
        dst_name = src.name
        if dst_name.endswith('.bin') and offset == 0x20000:
            app_bin = src
        if opts.copy_flash:
            dst = flash_dir / dst_name
            shutil.copy2(src, dst)
        file_hashes[dst_name] = sha256_file(src)
        flash_entries.append({
            'offset': f'0x{offset:x}',
            'name': dst_name,
            'size': src.stat().st_size,
            'sha256': file_hashes[dst_name],
        })

    if app_bin is None:
        app_rel = flasher.get('app', {}).get('file', 'Ebook.bin')
        app_bin = build_dir / app_rel
        if not app_bin.is_file():
            raise FileNotFoundError(f'未找到应用程序镜像: {app_bin}')

    ota_dst: Path | None = None
    if opts.copy_ota:
        ota_dst = ota_dir / app_bin.name
        shutil.copy2(app_bin, ota_dst)
        file_hashes[f'ota/{ota_dst.name}'] = sha256_file(ota_dst)

    merge_name: str | None = None
    if opts.merge and opts.copy_flash:
        esptool_cmd = find_esptool()
        if esptool_cmd is None:
            print(
                '警告: 未找到 esptool，跳过全量合并镜像（分文件与 OTA 包仍会生成）。'
                '请先执行 ESP-IDF 的 export.bat / export.ps1，或: pip install esptool',
                file=sys.stderr,
            )
        else:
            merge_name = f'{bundle_name}_flash.bin'
            merge_path = flash_dir / merge_name
            merge_flash_image(esptool_cmd, build_dir, flasher, merge_path)
            file_hashes[merge_name] = sha256_file(merge_path)
            flash_entries.append({
                'offset': '0x0',
                'name': merge_name,
                'size': merge_path.stat().st_size,
                'sha256': file_hashes[merge_name],
                'merged': True,
            })

    manifest = {
        'project': desc.get('project_name', 'Ebook'),
        'version': ver,
        'target': target,
        'flash_size': flash_size,
        'packed_at': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
        'git': git_short_hash(root),
        'idf_version': desc.get('git_revision'),
        'flash_files': flash_entries,
        'sha256': file_hashes,
    }
    if ota_dst is not None:
        manifest['ota_app'] = {
            'name': ota_dst.name,
            'size': ota_dst.stat().st_size,
            'sha256': file_hashes[f'ota/{ota_dst.name}'],
            'partition': 'ota_0',
        }

    manifest_path = bundle_dir / 'manifest.json'
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + '\n',
        encoding='utf-8',
    )
    write_flash_md(bundle_dir / 'FLASH.md', manifest, merge_name)

    if opts.zip_archive:
        zip_base = str(bundle_dir)
        print(f'压缩: {zip_base}.zip')
        shutil.make_archive(zip_base, 'zip', root_dir=bundle_dir.parent, base_dir=bundle_name)

    print()
    print(f'打包完成: {bundle_dir}')
    print(f'  版本: {ver}')
    if ota_dst is not None:
        print(f'  OTA:  {ota_dst}')
    if merge_name:
        print(f'  全量: {flash_dir / merge_name}')
    if opts.zip_archive:
        print(f'  ZIP:  {bundle_dir}.zip')
    return bundle_dir


def has_cli_pack_flags(args: argparse.Namespace) -> bool:
    return any((
        args.zip,
        args.no_merge,
        args.ota_only,
        args.flash_only,
        args.version is not None,
        args.build_dir is not None,
        args.out is not None,
    ))


def options_from_args(root: Path, args: argparse.Namespace) -> PackageOptions:
    build_dir = (args.build_dir or (root / 'build')).resolve()
    out_dir = (args.out or (root / 'dist')).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.ota_only:
        return PackageOptions(
            build_dir=build_dir,
            out_dir=out_dir,
            version=args.version,
            copy_flash=False,
            copy_ota=True,
            merge=False,
            zip_archive=args.zip,
        )
    if args.flash_only:
        return PackageOptions(
            build_dir=build_dir,
            out_dir=out_dir,
            version=args.version,
            copy_flash=True,
            copy_ota=False,
            merge=not args.no_merge,
            zip_archive=args.zip,
        )
    return PackageOptions(
        build_dir=build_dir,
        out_dir=out_dir,
        version=args.version,
        copy_flash=True,
        copy_ota=True,
        merge=not args.no_merge,
        zip_archive=args.zip,
    )


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description='Ebook 固件打包（无参数时进入交互式菜单）',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            '示例:\n'
            '  python scripts/package_firmware.py\n'
            '  python scripts/package_firmware.py --zip\n'
            '  python scripts/package_firmware.py --ota-only\n'
            '  python scripts/package_firmware.py --no-merge --version 1.0.0\n'
        ),
    )
    parser.add_argument(
        '--build-dir',
        type=Path,
        default=None,
        help='构建目录（默认 <工程>/build）',
    )
    parser.add_argument(
        '--out',
        type=Path,
        default=None,
        help='输出根目录（默认 <工程>/dist）',
    )
    parser.add_argument(
        '--version',
        default=None,
        help='覆盖 manifest 中的版本号',
    )
    parser.add_argument(
        '--no-merge',
        action='store_true',
        help='不生成全量合并 flash bin',
    )
    parser.add_argument(
        '--ota-only',
        action='store_true',
        help='仅复制 OTA 应用包',
    )
    parser.add_argument(
        '--flash-only',
        action='store_true',
        help='仅复制分文件镜像（不含 OTA）',
    )
    parser.add_argument(
        '--zip',
        action='store_true',
        help='额外生成 .zip 压缩包',
    )
    parser.add_argument(
        '-i', '--interactive',
        action='store_true',
        help='强制进入交互式菜单',
    )
    parser.add_argument(
        '-y', '--yes',
        action='store_true',
        help='非交互模式下跳过确认（与 --interactive 互斥）',
    )
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    if args.ota_only and args.flash_only:
        print('错误: --ota-only 与 --flash-only 不能同时使用', file=sys.stderr)
        return 1

    root = project_root()
    default_build = (root / 'build').resolve()
    default_out = (root / 'dist').resolve()
    default_out.mkdir(parents=True, exist_ok=True)

    use_interactive = args.interactive or (
        not has_cli_pack_flags(args) and is_interactive_tty()
    )

    try:
        if use_interactive:
            build_dir = (args.build_dir or default_build).resolve()
            out_dir = (args.out or default_out).resolve()
            out_dir.mkdir(parents=True, exist_ok=True)
            opts = interactive_menu(root, build_dir, out_dir)
            if opts is None:
                return 0
            package_firmware(root, opts)
        else:
            opts = options_from_args(root, args)
            if not args.yes and is_interactive_tty():
                print_options_summary(opts)
                if not prompt_yes_no('确认开始打包', True):
                    print('已取消。')
                    return 0
            package_firmware(root, opts)
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        print(f'错误: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
