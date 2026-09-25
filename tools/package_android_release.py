"""Package a pre-verified, signed Android APK without private build material."""
from pathlib import Path
import argparse
import hashlib
import re
import shutil
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--version', required=True)
parser.add_argument('--source-tag', '--source-ref', dest='source_tag', required=True)
parser.add_argument('--apk', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
if not re.fullmatch(r'[0-9]+\.[0-9]+(?:\.[0-9]+)?', args.version):
    parser.error('Expected a numeric version')
if not re.fullmatch(r'(?:v[0-9]+\.[0-9]+(?:\.[0-9]+)?|[0-9a-f]{40})', args.source_tag):
    parser.error('Expected a version tag or full commit SHA')
repo = Path(__file__).resolve().parent.parent
def git(*options):
    return subprocess.check_output(['git', *options], cwd=repo, text=True).strip()
if git('status', '--porcelain'):
    raise RuntimeError('Commit release sources before packaging')
with zipfile.ZipFile(args.apk) as apk:
    names = apk.namelist()
    if apk.testzip() is not None:
        raise RuntimeError('APK integrity check failed')
    required = {'lib/arm64-v8a/libDora64.so', 'lib/arm64-v8a/libSDL2.so'} | {
        f'assets/dora64/assets/localization/{name}_index.json' for name in ('dialogue', 'game_text')}
    if not required.issubset(names):
        raise RuntimeError('Missing native libraries or localization indices')
    for name in names:
        if name.endswith(('_source.json', '.z64', '.n64', '.v64', '.jks', '.keystore')):
            raise RuntimeError(f'Private or source data in APK: {name}')

name = f'Dora64-{args.version}-Android-arm64'
stage = args.output.resolve() / 'packages' / name
stage.mkdir(parents=True, exist_ok=False)
shutil.copy2(args.apk, stage / f'Dora64-{args.version}-Android-arm64.apk')
for language in ('ru', 'en'):
    shutil.copy2(repo / f'docs/walkthrough_{language}.txt', stage)
for relative in git('ls-files', '--recurse-submodules', '-z').split('\0'):
    path = repo / relative
    if relative.startswith('lib/') and re.match(r'^(LICENSE|COPYING|NOTICE)', path.name, re.I) and path.is_file():
        target = stage / 'licenses' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
(stage / 'README.txt').write_text(f'''Dora64 Android {args.version}

Requires Android 7.0+ (ARM64) and a Vulkan-capable GPU/driver.
Extract this ZIP, install the APK and allow installation from this source if Android asks.
On first launch, select your original Japanese .z64 ROM in the document picker.
ROMs are not included. Localization assets are already inside the APK.

Touch controls appear when you touch the screen and hide when a gamepad is used.
Open the Port menu for touch layout editing, camera, graphics, language and cheats.
The Modern profile defaults to 2x rendering. Higher resolutions and many translucent
effects can reduce performance. Full playthrough tested on Lenovo Legion Y700 (2025),
Snapdragon 8 Gen 3; other devices have not been playtested.

Data folder: Android/data/com.n64recomp.dora64/files/
Progress: saves/doraemon.n64.jp.bin (and .bak).
Settings: doraemon_pc_settings.json, doraemon_input_settings.json,
doraemon_touch_settings.json. Access may require a USB connection/ADB on newer Android.

Установка: распакуйте ZIP, установите APK и при первом запуске выберите свой
оригинальный японский ROM .z64. ROM в архив не входит, локализация встроена в APK.
Нужен Android 7.0+, 64-битный ARM и Vulkan. Управление — тач или геймпад;
положение кнопок, язык и графика настраиваются в меню Port.

walkthrough_ru.txt / walkthrough_en.txt: short collectible walkthroughs.
Dependency license texts: licenses/
Source: https://github.com/citrongames/Dora64/tree/{args.source_tag}
''', encoding='utf-8')
(stage / 'VERSION.txt').write_text(
    f'Dora64 Android {args.version}\nPlatform: Android-arm64\nSource ref: {args.source_tag}\n'
    f'Commit: {git("rev-parse", "HEAD")}\nConfiguration: Release (native RelWithDebInfo)\n'
    + git('submodule', 'status', '--recursive') + '\n', encoding='utf-8')
dist = args.output.resolve() / 'dist'
dist.mkdir(parents=True, exist_ok=True)
archive = dist / (name + '.zip')
if archive.exists():
    raise RuntimeError('Archive already exists')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            output.write(path, path.relative_to(stage.parent).as_posix())
with zipfile.ZipFile(archive) as output:
    if output.testzip() is not None:
        raise RuntimeError('ZIP integrity check failed')
archives = sorted(p for p in dist.glob('Dora64-*') if p.name.endswith(('.zip', '.tar.gz')))
(dist / 'SHA256SUMS.txt').write_text(''.join(
    f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in archives), encoding='ascii')
print(f'{archive.name}: {archive.stat().st_size:,} bytes; archive checked')
