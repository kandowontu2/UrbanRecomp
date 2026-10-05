#!/usr/bin/env python3
"""Package a native universal macOS app with assets/music and no ROM.

Run on macOS after a universal SDL3 static build. The app is ad-hoc signed;
Developer ID signing/notarization requires the owner's Apple credentials.
"""
import argparse
import hashlib
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('version')
p.add_argument('--build-dir',type=Path,required=True)
p.add_argument('--restored-music-dir',type=Path,required=True)
p.add_argument('--output-dir',type=Path,default=root/'dist')
args=p.parse_args()
if sys.platform!='darwin':raise SystemExit('Mac packaging requires macOS lipo, iconutil, codesign and ditto')
cache=(args.build_dir/'CMakeCache.txt').read_text()
if 'SC_AOT:BOOL=OFF' not in cache or 'SC_INTERPRETER_REFERENCE:BOOL=OFF' not in cache:
    raise SystemExit('Mac packages require the native production execution path')
exe=args.build_dir/'UrbanRecomp'
architectures=subprocess.check_output(['lipo','-archs',str(exe)],text=True).split()
if set(architectures)!={'arm64','x86_64'}:raise SystemExit('Expected Apple Silicon and Intel executable')
linked=subprocess.check_output(['otool','-L',str(exe)],text=True)
for line in linked.splitlines():
    if ' (compatibility version' in line and not line.strip().startswith(('/System/Library/','/usr/lib/')):
        raise SystemExit('App must be self-contained; external dependency: '+line.strip())
args.output_dir.mkdir(parents=True,exist_ok=True)
stage=args.output_dir/('macos-'+args.version)
app=stage/'UrbanRecomp.app'
if stage.exists():raise SystemExit('Preserve existing package: '+str(stage))
macos=app/'Contents/MacOS';resources=app/'Contents/Resources'
macos.mkdir(parents=True);resources.mkdir()
shutil.copy2(exe,macos/'UrbanRecomp')
(macos/'UrbanRecomp').chmod(0o755)
shutil.copytree(args.build_dir/'assets',resources/'assets')
(resources/'sylt_graphics').mkdir()
for name in ('sylt_map.bin','sylt_card.bin','PROVENANCE.md'):
    shutil.copy2(root/'sylt_graphics'/name,resources/'sylt_graphics'/name)
music=resources/'music/restored';music.mkdir(parents=True)
for n in range(1,20):
    track=args.restored_music_dir/f'scity-msu1-{n}.pcm'
    if track.read_bytes()[:4]!=b'MSU1':raise SystemExit('Invalid restored music: '+str(track))
    shutil.copy2(track,music/track.name)
for name in ('LICENSE','CREDITS.md','CHANGELOG.md','THIRD_PARTY_NOTICES.md'):
    shutil.copy2(root/name,resources/name)
for name in ('PC_ENHANCEMENTS.md','GPU_PERFORMANCE.md','NATIVE_EXECUTION.md'):
    shutil.copy2(root/'docs'/name,resources/name)
licenses=resources/'licenses';licenses.mkdir()
for path in (root/'licenses').glob('*.txt'):shutil.copy2(path,licenses/path.name)
for source,destination in (
    ('snesrecomp/LICENSE','snesrecomp-LICENSE.txt'),
    ('snesrecomp/THIRD_PARTY_ATTRIBUTION.md','snesrecomp-THIRD_PARTY_ATTRIBUTION.md'),
    ('recomp-ui/LICENSE','recomp-ui-LICENSE.txt'),
    ('recomp-ui/src/third_party/imgui/LICENSE.txt','imgui-LICENSE.txt'),
    ('recomp-ui/assets/common/fonts/NOTICE.md','fonts-NOTICE.md'),
    ('recomp-ui/assets/common/img/NOTICE.md','images-NOTICE.md')):
    shutil.copy2(root/source,licenses/destination)
shutil.copy2(args.build_dir.parent/'SDL/LICENSE.txt',licenses/'SDL3-LICENSE.txt')
iconset=stage/'UrbanRecomp.iconset';iconset.mkdir()
for size in (16,32,128,256,512):
    for scale in (1,2):
        name=f'icon_{size}x{size}'+('@2x' if scale==2 else '')+'.png'
        subprocess.run(['sips','-z',str(size*scale),str(size*scale),str(root/'assets/icon.png'),
                        '--out',str(iconset/name)],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['iconutil','-c','icns',str(iconset),'-o',str(resources/'UrbanRecomp.icns')],check=True)
info=dict(CFBundleExecutable='UrbanRecomp',CFBundleIdentifier='io.github.kandowontu2.UrbanRecomp',
    CFBundleName='UrbanRecomp',CFBundleDisplayName='Urban Recomp',CFBundlePackageType='APPL',
    CFBundleShortVersionString='1.2.0',CFBundleVersion=args.version.lstrip('v').replace('-enhanced.','.'),
    CFBundleIconFile='UrbanRecomp.icns',LSMinimumSystemVersion='11.0',
    LSApplicationCategoryType='public.app-category.simulation-games',NSHighResolutionCapable=True,
    SDL_FILESYSTEM_BASE_DIR_TYPE='resource')
with (app/'Contents/Info.plist').open('wb') as f:plistlib.dump(info,f)
(resources/'README.txt').write_text(
    'Urban Recomp Enhanced '+args.version+' for macOS 11 or newer\n\n'
    'Universal app: Apple Silicon and Intel. Copy UrbanRecomp.app to Applications\n'
    'and select your own clean US SimCity SNES ROM in the launcher.\n'
    'All assets, runtime code and 19 restored tracks are included; no ROM or saves.\n'
    'Settings and saves use ~/Library/Application Support/UrbanRecomp/UrbanRecomp/.\n'
    'This test build is ad-hoc signed, not Developer ID signed or notarized.\n'
    'If macOS blocks opening, use System Settings > Privacy & Security > Open Anyway.\n'
    'Full credits and licenses are in Contents/Resources.\n'
    'Rendering uses Metal presentation with the CPU terrain/field fallback;\n'
    'the Vulkan compute acceleration in the Windows build is unavailable here.\n')
for path in app.rglob('*'):
    if path.suffix.lower() in ('.sfc','.smc','.srm','.sav','.c','.h') or '.srm.' in path.name:
        raise SystemExit('Unexpected private content in Mac app: '+str(path))
subprocess.run(['codesign','--force','--sign','-','--timestamp=none',str(app)],check=True)
subprocess.run(['codesign','--verify','--deep','--strict','--verbose=2',str(app)],check=True)
out=args.output_dir/f'UrbanRecomp-{args.version}-macos-universal.zip'
if out.exists():raise SystemExit('Preserve existing archive: '+str(out))
subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(app),str(out)],check=True)
digest=hashlib.sha256(out.read_bytes()).hexdigest()
Path(str(out)+'.sha256').write_text(digest+'  '+out.name+'\n')
print(f'{out}: {out.stat().st_size/1e6:.1f} MB; universal native app, 19 restored tracks')
