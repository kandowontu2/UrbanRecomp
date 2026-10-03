#!/usr/bin/env python3
"""Import the user's local 19-track SimCity MSU-1 PCM pack without a ROM patch."""
import argparse
import hashlib
import json
import struct
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive', type=Path)
parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'music/restored')
args = parser.parse_args()
tracks = []
with zipfile.ZipFile(args.archive) as archive:
    # Read and validate the entire set before writing anything. Names are
    # selected explicitly, never interpreted as extraction paths.
    for number in range(1, 20):
        name = f'scity-msu1-{number}.pcm'
        matches = [entry for entry in archive.infolist() if Path(entry.filename).name == name]
        if len(matches) != 1:
            raise ValueError(f'Expected exactly one {name}')
        data = archive.read(matches[0])
        if len(data) < 12 or data[:4] != b'MSU1' or (len(data)-8) % 4:
            raise ValueError(f'Invalid PCM header or stereo data: {name}')
        loop, = struct.unpack_from('<I', data, 4)
        frames = (len(data)-8)//4
        if loop >= frames:
            raise ValueError(f'Invalid loop point: {name}')
        tracks.append((name, data, frames, loop))
args.output.mkdir(parents=True, exist_ok=True)
for name, data, frames, loop in tracks:
    target = args.output/name
    if target.exists() and target.read_bytes() != data:
        raise FileExistsError(f'Existing track differs; preserve it: {target}')
for name, data, frames, loop in tracks:
    target = args.output/name
    if not target.exists():
        target.write_bytes(data)
manifest = {'source': str(args.archive.resolve()), 'rate': 44100, 'channels': 2,
    'tracks': [{'file': name, 'frames': frames, 'loop': loop, 'sha256': hashlib.sha256(data).hexdigest()}
               for name, data, frames, loop in tracks]}
(args.output/'import.json').write_text(json.dumps(manifest, indent=2))
print(f'Validated and imported all 19 PCM tracks to {args.output.resolve()}')
