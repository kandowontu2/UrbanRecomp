#!/usr/bin/env python3
"""Check the actual distributed mobile ZIP payload, not just the staging tree."""
import argparse, plistlib, struct, zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('package',type=Path);p.add_argument('--platform',choices=['android','ios'],required=True);a=p.parse_args()
with zipfile.ZipFile(a.package) as z:
    assert z.testzip() is None
    names=z.namelist();prefix='assets/' if a.platform=='android' else 'Payload/UrbanRecomp.app/'
    for name in ('CREDITS.md','LICENSE','CHANGELOG.md','MOBILE.md','THIRD_PARTY_NOTICES.md','licenses/SDL3-LICENSE.txt','sylt_graphics/sylt_map.bin','sylt_graphics/sylt_card.bin'):
        assert prefix+name in names,name
    for n in range(1,22):
        music=z.read(prefix+f'music/restored/scity-msu1-{n}.pcm');assert music[:4]==b'MSU1' and len(music)>12
        assert struct.unpack_from('<I',music,4)[0]<(len(music)-8)//4
    for name in names:
        assert not name.lower().endswith(('.sfc','.smc','.srm','.sav','.c','.h','.p12','.keystore')) and '.srm.' not in name,name
    if a.platform=='android':
        for abi in ('arm64-v8a','x86_64'):
            for lib in ('libmain.so','libSDL3.so'):
                data=z.read(f'lib/{abi}/{lib}');assert data[:4]==b'\x7fELF' and data[4]==2
                assert struct.unpack_from('<H',data,18)[0]==(183 if abi=='arm64-v8a' else 62)
    else:
        info=plistlib.loads(z.read(prefix+'Info.plist'));assert info['CFBundleIdentifier']=='io.github.kandowontu2.UrbanRecomp'
        assert info['CFBundleShortVersionString']=='1.0.1' and info['MinimumOSVersion']=='14.0'
        assert info.get('UIApplicationSupportsIndirectInputEvents') is True
        data=z.read(prefix+'UrbanRecomp');assert data[:4]==b'\xcf\xfa\xed\xfe' and struct.unpack_from('<I',data,4)[0]==0x100000c
print('Mobile archive verified: architectures, 21 tracks, credits, no ROM/state/signing key')
