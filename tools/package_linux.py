#!/usr/bin/env python3
import argparse, hashlib, shutil, subprocess, tarfile
from pathlib import Path
from platform_assets import stage_assets

p=argparse.ArgumentParser();p.add_argument('version');p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--sdl-dir',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1]
name=f'UrbanRecomp-{a.version}-linux-x64';stage=root/'dist'/name
if stage.exists():raise SystemExit('Preserve existing package: '+str(stage))
cache=(a.build_dir/'CMakeCache.txt').read_text()
assert 'SC_INTERPRETER_REFERENCE:BOOL=OFF' in cache and 'SC_AOT:BOOL=OFF' in cache
stage_assets(root,stage,a.sdl_dir)
shutil.copy2(a.build_dir/'UrbanRecomp',stage/'UrbanRecomp');(stage/'UrbanRecomp').chmod(0o755)
shutil.copytree(a.build_dir/'assets',stage/'assets')
linked=subprocess.check_output(['ldd',str(stage/'UrbanRecomp')],text=True)
if 'not found' in linked or 'libSDL' in linked:raise SystemExit('Runtime is missing dependencies or SDL was not linked statically: '+linked)
(stage/'urbanrecomp').write_text('''#!/bin/sh
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
data=${URBANRECOMP_HOME:-${XDG_DATA_HOME:-$HOME/.local/share}/urbanrecomp}
mkdir -p "$data"
cd "$data"
exec "$base/UrbanRecomp" "$@"
''');(stage/'urbanrecomp').chmod(0o755)
(stage/'README-LINUX.txt').write_text('UrbanRecomp Enhanced '+a.version+'\n\n'
    'Linux x86-64, glibc 2.35 or newer (Ubuntu 22.04+ and compatible distributions).\n'
    'Extract the entire folder and run ./urbanrecomp. Select your own US SimCity SNES ROM.\n'
    'SDL3, launcher assets, 21 restored music tracks and credits are bundled. No ROM is included.\n'
    'Saves/settings: ${XDG_DATA_HOME:-~/.local/share}/urbanrecomp/\n'
    'Override with URBANRECOMP_HOME. Vulkan terrain/field acceleration is used when available;\n'
    'otherwise SDL and CPU rendering are used. System graphics/audio drivers are required.\n')
out=stage.parent/(name+'.tar.gz')
with tarfile.open(out,'w:gz') as tar:tar.add(stage,arcname=name)
digest=hashlib.sha256(out.read_bytes()).hexdigest();Path(str(out)+'.sha256').write_text(digest+'  '+out.name+'\n')
print(out)
