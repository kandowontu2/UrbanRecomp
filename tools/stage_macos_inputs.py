#!/usr/bin/env python3
"""Validate and stage temporary private native-code/music build inputs.

The archive is never checked into Git or included in the distributable.
No ROMs, saves, snapshots or unrelated local files are accepted.
"""
import argparse
import hashlib
from pathlib import Path, PurePosixPath
import tarfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('archive',type=Path)
p.add_argument('--sha256',required=True)
p.add_argument('--destination',type=Path,default=Path('.'))
args=p.parse_args()
if hashlib.sha256(args.archive.read_bytes()).hexdigest()!=args.sha256:
    raise SystemExit('Private build input checksum mismatch')
with tarfile.open(args.archive,'r:gz') as archive:
    members=archive.getmembers()
    names=set()
    for member in members:
        path=PurePosixPath(member.name)
        native=path.parent==PurePosixPath('src/program_gen') and path.suffix in ('.c','.h')
        music=path.parent==PurePosixPath('music/restored') and path.name in {
            f'scity-msu1-{n}.pcm' for n in range(1,20)}
        if not member.isfile() or '..' in path.parts or not (native or music) or member.name in names:
            raise SystemExit('Unexpected private build archive member')
        names.add(member.name)
    if sum(m.size for m in members)>512*1024*1024:
        raise SystemExit('Private build archive exceeds expected size')
    required={'src/program_gen/sc_program_cold.h'} | {
        f'music/restored/scity-msu1-{n}.pcm' for n in range(1,20)}
    if not required.issubset(names):raise SystemExit('Incomplete native code or restored music')
    for member in members:
        target=args.destination/member.name
        target.parent.mkdir(parents=True,exist_ok=True)
        with archive.extractfile(member) as source,target.open('wb') as output:
            import shutil
            shutil.copyfileobj(source,output)
print(f'Staged {len(members)} private build inputs; no ROM or personal saves')
