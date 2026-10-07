#!/usr/bin/env python3
import argparse, hashlib, plistlib, shutil, subprocess, zipfile
from pathlib import Path
from platform_assets import stage_assets
p=argparse.ArgumentParser();p.add_argument('version');p.add_argument('--app',type=Path,required=True);p.add_argument('--sdl-dir',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];dist=root/'dist';dist.mkdir(exist_ok=True)
stage=dist/('ios-'+a.version);app=stage/'Payload/UrbanRecomp.app'
if stage.exists():raise SystemExit('Preserve existing IPA staging: '+str(stage))
shutil.copytree(a.app,app)
stage_assets(root,app,a.sdl_dir)
for size in (120,152,167,180):
    subprocess.run(['sips','-z',str(size),str(size),str(root/'assets/icon.png'),'--out',str(app/f'Icon-{size}.png')],check=True,stdout=subprocess.DEVNULL)
info=plistlib.loads((app/'Info.plist').read_bytes())
info['CFBundleIcons']={'CFBundlePrimaryIcon':{'CFBundleIconFiles':['Icon-120','Icon-180'],'UIPrerenderedIcon':True}}
info['CFBundleIcons~ipad']={'CFBundlePrimaryIcon':{'CFBundleIconFiles':['Icon-152','Icon-167'],'UIPrerenderedIcon':True}}
(app/'Info.plist').write_bytes(plistlib.dumps(info))
subprocess.run(['plutil','-lint',str(app/'Info.plist')],check=True)
subprocess.run(['lipo',str(app/'UrbanRecomp'),'-verify_arch','arm64'],check=True)
out=dist/f'UrbanRecomp-{a.version}-ios-arm64-unsigned.ipa'
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
    for path in sorted(app.rglob('*')):
        if path.is_file():z.write(path,path.relative_to(stage).as_posix())
Path(str(out)+'.sha256').write_text(hashlib.sha256(out.read_bytes()).hexdigest()+'  '+out.name+'\n')
print(out)
