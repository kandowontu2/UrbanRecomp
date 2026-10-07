#!/usr/bin/env python3
"""Embed a complete Windows release ZIP in one GUI EXE; ROMs remain external.

Run on Windows with MinGW available. Windows' compression API supplies the
codec for both packaging and unpacking, so no third-party unpacker is shipped.
The bootstrap preserves command-line arguments and game exit codes.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
from pathlib import Path
import struct
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive', type=Path)
parser.add_argument('--output', type=Path, help='Portable EXE path; never replaces an existing file')
parser.add_argument('--compiler-dir', type=Path, default=Path('C:/Strawberry/c/bin'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
output = args.output or args.archive.with_suffix('.exe')
if output.exists():
    raise FileExistsError(f'Preserve existing build: {output}')
files = []
with zipfile.ZipFile(args.archive) as archive:
    for item in archive.infolist():
        name = item.filename.split('/', 1)[1]
        if item.is_dir():
            continue
        if any(part in ('', '.', '..') for part in name.split('/')):
            raise ValueError(name)
        if Path(name).suffix.lower() in ('.sfc', '.smc', '.srm', '.sav') or '.srm.' in name:
            raise ValueError(f'Personal/game data cannot be bundled: {name}')
        files.append((name.encode('ascii'), archive.read(item)))
tracks = {n for n, _ in files if n.endswith(b'.pcm')}
required_tracks = {f'music/restored/scity-msu1-{n}.pcm'.encode() for n in range(1, 20)}
assert required_tracks.issubset(tracks)
assert tracks <= required_tracks | {b'music/restored/scity-msu1-20.pcm',b'music/restored/scity-msu1-21.pcm'}
assert {b'UrbanRecomp.exe', b'SDL3.dll', b'CREDITS.md', b'CHANGELOG.md', b'THIRD_PARTY_NOTICES.md'}.issubset({n for n, _ in files})
raw = bytearray(b'SCFILES1' + struct.pack('<I', len(files)))
for name, data in files:
    raw += struct.pack('<IQ', len(name), len(data)) + hashlib.sha256(data).digest() + name + data
cabinet = c.WinDLL('cabinet', use_last_error=True)
cabinet.CreateCompressor.argtypes = [w.DWORD, c.c_void_p, c.POINTER(c.c_void_p)]
cabinet.CreateCompressor.restype = w.BOOL
cabinet.Compress.argtypes = [c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
cabinet.Compress.restype = w.BOOL
cabinet.CloseCompressor.argtypes = [c.c_void_p]
cabinet.CloseCompressor.restype = w.BOOL
handle = c.c_void_p()
assert cabinet.CreateCompressor(4, None, c.byref(handle)), c.get_last_error()
try:
    source = (c.c_ubyte * len(raw)).from_buffer(raw)
    needed = c.c_size_t()
    assert not cabinet.Compress(handle, source, len(raw), None, 0, c.byref(needed))
    assert c.get_last_error() == 122 and needed.value
    compressed = c.create_string_buffer(needed.value)
    assert cabinet.Compress(handle, source, len(raw), compressed, len(compressed), c.byref(needed)), c.get_last_error()
finally:
    cabinet.CloseCompressor(handle)
build = root / '.local/bundle-build'
build.mkdir(parents=True, exist_ok=True)
resource = build / 'portable.rc'
resource.write_text('1 ICON "' + (root/'assets/urbanrecomp.ico').as_posix() + '"\n')
# Relative tool inputs avoid windres' shell preprocessing of a spaced path.
subprocess.run([str(args.compiler_dir/'windres.exe'), '-I', '.', '-i', '.local/bundle-build/portable.rc',
                '-o', '.local/bundle-build/portable-icon.o'], cwd=root, check=True)
subprocess.run([str(args.compiler_dir/'gcc.exe'), '-O2', '-Wall', '-Wextra', '-municode', '-mwindows',
                '-static', 'tools/sc_portable.c', '.local/bundle-build/portable-icon.o',
                '-lbcrypt', '-lshell32', '-o', '.local/bundle-build/bootstrap.exe'], cwd=root, check=True)
with output.open('wb') as target:
    target.write((build/'bootstrap.exe').read_bytes())
    target.write(compressed.raw[:needed.value])
    target.write(b'SCBNDL01'+struct.pack('<QQ', needed.value, len(raw))+hashlib.sha256(raw).digest())
digest = hashlib.sha256(output.read_bytes()).hexdigest()
Path(str(output)+'.sha256').write_text(digest+'  '+output.name+'\n')
print(f'{output}: {output.stat().st_size/1e6:.1f} MB, {len(files)} embedded files, {len(tracks)} music tracks')
