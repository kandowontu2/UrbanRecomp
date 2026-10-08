"""Shared allowlisted release payload; never collect a working directory wholesale."""
from pathlib import Path
import shutil
import struct

def stage_assets(root: Path, destination: Path, sdl: Path):
    destination.mkdir(parents=True,exist_ok=True)
    for name in ('LICENSE','CREDITS.md','CHANGELOG.md','THIRD_PARTY_NOTICES.md'):
        shutil.copy2(root/name,destination/name)
    for name in ('MOBILE.md','PC_ENHANCEMENTS.md','TEST_CITY_LAYOUT.md','PLACEMENT_EFFECTS.md','GPU_PERFORMANCE.md','NATIVE_EXECUTION.md'):
        shutil.copy2(root/'docs'/name,destination/name)
    sylt=destination/'sylt_graphics';sylt.mkdir()
    for name in ('sylt_map.bin','sylt_card.bin','PROVENANCE.md'):shutil.copy2(root/'sylt_graphics'/name,sylt/name)
    music=destination/'music/restored';music.mkdir(parents=True,exist_ok=True)
    for n in range(1,22):
        track=root/'music/restored'/f'scity-msu1-{n}.pcm'
        header=track.read_bytes()[:8];size=track.stat().st_size
        if header[:4]!=b'MSU1' or size<12 or (size-8)%4 or struct.unpack_from('<I',header,4)[0]>=(size-8)//4:
            raise SystemExit(f'Invalid restored track {n}')
        shutil.copy2(track,music/track.name)
    licenses=destination/'licenses';licenses.mkdir()
    for path in (root/'licenses').glob('*.txt'):shutil.copy2(path,licenses/path.name)
    for source,name in (
        ('snesrecomp/LICENSE','snesrecomp-LICENSE.txt'),
        ('snesrecomp/THIRD_PARTY_ATTRIBUTION.md','snesrecomp-THIRD_PARTY_ATTRIBUTION.md'),
        ('recomp-ui/LICENSE','recomp-ui-LICENSE.txt'),
        ('recomp-ui/src/third_party/imgui/LICENSE.txt','imgui-LICENSE.txt'),
        ('recomp-ui/assets/common/fonts/NOTICE.md','fonts-NOTICE.md'),
        ('recomp-ui/assets/common/img/NOTICE.md','images-NOTICE.md')):
        shutil.copy2(root/source,licenses/name)
    shutil.copy2(sdl/'LICENSE.txt',licenses/'SDL3-LICENSE.txt')
    shutil.copytree(root/'snesrecomp/third_party/psxrecomp_color_lut',licenses/'psxrecomp_color_lut')
    for path in destination.rglob('*'):
        if path.suffix.lower() in ('.sfc','.smc','.srm','.sav','.c','.h') or '.srm.' in path.name:
            raise SystemExit('Unexpected private file in package: '+str(path))
