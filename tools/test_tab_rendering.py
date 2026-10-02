#!/usr/bin/env python3
"""Compare undisplayed Tab frames against full native raster work.

Private ROM/state inputs stay outside the repository. Choose an end frame
three guest frames beyond the saved frame (or any multiple of three).
"""
import argparse
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--exe',type=Path,required=True)
p.add_argument('--rom',type=Path,required=True)
p.add_argument('--state',type=Path,required=True)
p.add_argument('--end-frame',type=int,required=True)
p.add_argument('--gpu',action='store_true')
p.add_argument('--mouse-drag',action='store_true',help='Replay a construction drag outside both window edges')
p.add_argument('--pairs',type=int,default=3)
args=p.parse_args()
if not 1<=args.pairs<=10: p.error('--pairs must be between 1 and 10')
env={k:v for k,v in os.environ.items() if not k.startswith(('SC_','SDL_','SNESRECOMP_'))}
env.update(SDL_AUDIODRIVER='dummy',SDL_RENDER_DRIVER='direct3d11',SC_SCRIPTED_INPUT='1',
    SC_DEVELOPMENT_SPEED='50',SC_FAST_FORWARD='1',SC_TAB_TEST_BATCH='3',SC_PERF='1',
    SC_GPU_TERRAIN=str(int(args.gpu)))
times={0:[],1:[]}
if args.mouse_drag:
    env['SC_MOUSE_INPUT']='0:180:170:0,2:180:170:1,6:220:190:1,8:-20:190:1,10:700:190:1,12:220:190:0'
with tempfile.TemporaryDirectory(prefix='simcity-tab-') as folder:
    out=Path(folder)
    for pair in range(args.pairs):
        for skip in ((0,1) if pair%2==0 else (1,0)):
            stem=out/f'{pair}-{skip}'
            run_env=env|dict(SC_TAB_SKIP_PIXELS=str(skip),SC_SRAM_PATH=str(out/'test.srm'),
                SC_RENDER_DUMP_AT=str(args.end_frame),SC_RENDER_DUMP_PATH=str(stem.with_suffix('.ppm')),
                SC_RENDER_STATE_PATH=str(stem.with_suffix('.bin')))
            result=subprocess.run([str(args.exe.resolve()),str(args.rom.resolve()),'--no-settings',
                '--load-state',str(args.state.resolve()),'--widescreen','--aspect','21:9',
                '--window-size','1344x784'],cwd=out,env=run_env,capture_output=True,timeout=90)
            log=(result.stdout+result.stderr).decode(errors='replace')
            assert result.returncode==0,log
            total=next(line for line in log.splitlines() if '[perf total]' in line)
            values=re.search(r'emu ([0-9.]+) draw ([0-9.]+) present ([0-9.]+)',total)
            times[skip].append(sum(map(float,values.groups())))
            print(pair,skip,total,flush=True)
        before=out/f'{pair}-0';after=out/f'{pair}-1'
        for suffix in ('.bin','.ppm'):
            assert before.with_suffix(suffix).read_bytes()==after.with_suffix(suffix).read_bytes(),suffix+' mismatch'
    a,b=statistics.median(times[0]),statistics.median(times[1])
    print(f'PASS: complete state and presented pixels match; median work {a:.3f} -> {b:.3f} ms ({100*(1-b/a):.1f}% less)')
