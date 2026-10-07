#!/usr/bin/env python3
"""Convert local milestone recordings to optional host city themes.

Requires ffmpeg. The recording repeats its 64-beat phrase at 123 BPM before
fading out. Keep that phrase, with a 5 ms equal-phase overlap and mixing
headroom, in optional host track 20, balanced with the restored city tracks.
LOOP16B keeps its full arrangement and original ending fade, trimming only
trailing silence, as host track 21. Both themes match the restored city mix.
Cartridge commands 20/21 stay unchanged.
"""
import argparse
import array
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('audio', type=Path)
p.add_argument('--theme', choices=('vanilla-lake-beta', 'loop16b'), default='vanilla-lake-beta')
p.add_argument('--output-dir', type=Path, required=True)
args = p.parse_args()
period = round(64 * 60 / 123 * 44100)
overlap = 220
with tempfile.TemporaryDirectory(prefix='sc-milestone-') as temporary:
    raw = Path(temporary) / 'audio.raw'
    command=['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', str(args.audio)]
    if args.theme=='vanilla-lake-beta':command+=['-t', '32']
    subprocess.run(command+['-ar', '44100', '-ac', '2', '-f', 's16le', str(raw)], check=True)
    samples = array.array('h', raw.read_bytes())
if sys.byteorder != 'little':
    samples.byteswap()
if args.theme=='vanilla-lake-beta':
    if len(samples) < (period + overlap) * 2:
        raise ValueError('The recording must contain the complete 31.22-second phrase')
    samples = samples[:(period + overlap) * 2]
    for frame in range(overlap):
        weight = (frame + 1) / overlap
        for channel in range(2):
            at = (period + frame) * 2 + channel
            samples[at] = round(samples[at] * (1 - weight) + samples[frame * 2 + channel] * weight)
    track,loop=20,overlap
else:
    end=len(samples)
    while end>2 and abs(samples[end-2])<=2 and abs(samples[end-1])<=2:end-=2
    samples=samples[:end];track,loop=21,0
    if len(samples)<44100*2:raise ValueError('The recording is empty or too short')
# Both recordings are mastered louder than the restored city music. These
# gains bring their measured RMS into the same range (about -24 dBFS).
gain = .32 if args.theme == 'vanilla-lake-beta' else .4
for i in range(len(samples)):
    samples[i] = round(samples[i] * gain)
peak = max(abs(x) for x in samples)
if sys.byteorder != 'little':
    samples.byteswap()
args.output_dir.mkdir(parents=True, exist_ok=True)
output = args.output_dir / f'scity-msu1-{track}.pcm'
output.write_bytes(b'MSU1' + struct.pack('<I', loop) + samples.tobytes())
print(f'{output}: {(len(samples)//2-loop)/44100:.6f}s loop, loop frame {loop}, peak {peak}')
