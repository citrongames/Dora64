from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import tarfile
import zipfile

parser = argparse.ArgumentParser(description="Package a built Dora64 release without ROMs or local source catalogs.")
parser.add_argument("--version", required=True)
parser.add_argument("--platform", required=True, choices=("Windows-x64", "Linux-x86_64"))
parser.add_argument("--binary-dir", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--crt-dir", type=Path, help="Windows MSVC x64 CRT redistributable directory")
parser.add_argument("--dxc-license", required=True, type=Path, help="DirectXShaderCompiler LICENSE.TXT")
args = parser.parse_args()
if re.fullmatch(r"[0-9]+\.[0-9]+(?:\.[0-9]+)?", args.version) is None:
    parser.error("Version must be numeric: e.g. 1.0.1")
if args.platform == "Windows-x64" and args.crt_dir is None:
    parser.error("Windows packaging requires --crt-dir")
repo = Path(__file__).resolve().parent.parent
work = args.output.resolve()
dist = work / 'dist'
dist.mkdir(parents=True, exist_ok=True)
commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
if subprocess.check_output(['git', 'status', '--porcelain'], cwd=repo):
    raise RuntimeError('Commit release sources before packaging')
tracked = subprocess.check_output(['git', 'ls-files', '--recurse-submodules', '-z'], cwd=repo).decode().split('\0')
assets = [p for p in tracked if p.startswith('assets/icons/') or
          p in ('assets/localization/dialogue_font.bmp', 'assets/localization/dialogue_font.json',
                'assets/localization/languages.json', 'assets/localization/dialogue_index.json',
                'assets/localization/game_text_index.json') or
          p.startswith(('assets/localization/en/', 'assets/localization/ru/'))]
licenses = [p for p in tracked if p.startswith('lib/') and
            re.match(r'^(LICENSE|COPYING|NOTICE)', Path(p).name, re.I) and (repo / p).is_file()]

# Required runtime indices contain metadata only. Do not substitute local *_source.json.
for filename, key in (("dialogue_index.json", "messages"), ("game_text_index.json", "records")):
    relative = 'assets/localization/' + filename
    if relative not in assets:
        raise RuntimeError(f'Missing tracked runtime index: {relative}')
    document = json.loads((repo / relative).read_text(encoding='utf-8'))
    if document.get('format_version') != 1 or not document.get(key):
        raise RuntimeError(f'Invalid runtime index: {relative}')
    for record in document[key]:
        if set(record) != ({'id', 'byte_length', 'category'} if key == 'records' else {'id', 'byte_length'}):
            raise RuntimeError(f'Unexpected data in runtime index: {relative}')
        if re.fullmatch(r'[0-9A-F]{8}', record['id']) is None or not 2 <= record['byte_length'] <= 4096:
            raise RuntimeError(f'Invalid ROM range: {record}')

dxc_license = args.dxc_license
windows_build = linux_build = args.binary_dir
crt = args.crt_dir

def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)

common_readme = f'''Dora64 {args.version}

Place your own original Japanese .z64 ROM directly beside the executable.
Any filename is accepted; the game validates the ROM contents.
ROMs, saves and personal settings are not included.
Keep the assets folder beside the executable. Settings and saves are created here.

walkthrough_ru.txt — краткое прохождение на русском.
walkthrough_en.txt — short English walkthrough.

Положите оригинальный японский ROM .z64 рядом с исполняемым файлом.
Имя файла любое; игра проверяет содержимое. ROM в архив не входит.
Папка assets должна оставаться рядом с игрой. Сохранения и настройки создаются здесь.

Source and build instructions: https://github.com/citrongames/Dora64/tree/v{args.version}
Dependency license texts are in licenses/.
'''

for platform in (args.platform,):
    if platform not in ('Windows-x64', 'Linux-x86_64'):
        raise ValueError(platform)
    name = f'Dora64-{args.version}-{platform}'
    stage = work / 'packages' / name
    stage.mkdir(parents=True, exist_ok=False)
    for path in assets:
        copy(repo / path, stage / path)
    for path in licenses:
        copy(repo / path, stage / 'licenses' / path)
    copy(dxc_license, stage / 'licenses/DXC-LICENSE.txt')
    for lang in ('ru', 'en'):
        copy(repo / f'docs/walkthrough_{lang}.txt', stage / f'walkthrough_{lang}.txt')
    if platform.startswith('Windows'):
        for filename in ('Dora64.exe', 'SDL2.dll', 'dxcompiler.dll', 'dxil.dll'):
            copy(windows_build / filename, stage / filename)
        for filename in ('msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
            copy(crt / filename, stage / filename)
        extra = '\nWindows x64: extract the whole archive and run Dora64.exe.\nWindows 11 is the primary tested platform. Update your GPU driver.\n\nWindows: распакуйте архив целиком и запустите Dora64.exe.\nОсновная проверенная система — Windows 11.\n'
    else:
        copy(linux_build / 'Dora64', stage / 'Dora64')
        (stage / 'Dora64').chmod(0o755)
        for filename in ('dora64.desktop', 'install-desktop.py'):
            copy(repo / 'resources/linux' / filename, stage / filename)
        extra = '''
Linux x86_64: built on Ubuntu 24.04; requires glibc 2.39 or newer,
SDL2, GTK3, libstdc++ and a working Vulkan GPU driver.
Ubuntu 24.04 runtime packages:
  sudo apt install libsdl2-2.0-0 libgtk-3-0t64 libstdc++6 libvulkan1
Launch from this extracted directory:
  ./Dora64
Optional applications-menu shortcut (requires Python 3):
  python3 install-desktop.py

Linux x86_64: сборка для Ubuntu 24.04 и совместимых более новых систем.
Нужны glibc 2.39+, SDL2, GTK3, libstdc++ и драйвер Vulkan.
После распаковки запуск: ./Dora64
'''
    (stage / 'README.txt').write_text(common_readme + extra, encoding='utf-8')
    submodules = subprocess.check_output(['git', 'submodule', 'status', '--recursive'], cwd=repo, text=True)
    (stage / 'VERSION.txt').write_text(
        f'Dora64 {args.version}\nPlatform: {platform}\nConfiguration: Release\nCommit: {commit}\n'
        + submodules, encoding='utf-8')

    files = sorted(p for p in stage.rglob('*') if p.is_file())
    banned = {'.z64', '.n64', '.v64', '.pdb', '.log', '.aseprite', '.xcf'}
    for path in files:
        if path.suffix.lower() in banned or path.name.endswith('_source.json'):
            raise RuntimeError(f'Forbidden package file: {path}')
        if path.name.startswith('doraemon_') and path.name.endswith('_settings.json'):
            raise RuntimeError(f'Personal settings in package: {path}')
    required = {f'assets/localization/{n}_index.json' for n in ('dialogue', 'game_text')}
    if not required.issubset({p.relative_to(stage).as_posix() for p in files}):
        raise RuntimeError('Localization indices missing from package')
    if platform.startswith('Windows'):
        archive = dist / (name + '.zip')
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for path in files:
                z.write(path, path.relative_to(stage.parent).as_posix())
        with zipfile.ZipFile(archive) as z:
            if not {f'{name}/{p}' for p in required}.issubset(z.namelist()):
                raise RuntimeError('Localization indices missing from ZIP')
            if z.testzip() is not None:
                raise RuntimeError('ZIP integrity check failed')
    else:
        archive = dist / (name + '.tar.gz')
        with tarfile.open(archive, 'w:gz', compresslevel=9) as t:
            t.add(stage, arcname=name)
        with tarfile.open(archive, 'r:gz') as t:
            for path in files:
                member = path.relative_to(stage.parent).as_posix()
                if hashlib.sha256(t.extractfile(member).read()).digest() != hashlib.sha256(path.read_bytes()).digest():
                    raise RuntimeError(f'TAR integrity check failed: {member}')
    print(f'{archive.name}: {archive.stat().st_size:,} bytes; {len(files)} files; archive checked', flush=True)

archives = sorted(dist.glob(f'Dora64-{args.version}-*'))
(dist / 'SHA256SUMS.txt').write_text(''.join(
    f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in archives), encoding='ascii')
