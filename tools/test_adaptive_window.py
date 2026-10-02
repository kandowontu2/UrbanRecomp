#!/usr/bin/env python3
"""Resize this test's Windows child and capture each canvas; optional desktop screenshots."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import time
from PIL import Image, ImageGrab


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','rom','state','artifacts'): ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--screenshots',action='store_true',help='Bring the child forward for window screenshots')
    args=ap.parse_args()
    if os.name!='nt': ap.error('Windows is required')
    args.artifacts.mkdir(parents=True,exist_ok=True)
    root=Path(tempfile.mkdtemp(prefix='simcity-resize-',dir=args.artifacts.resolve()))
    print('Artifacts:',root,flush=True)
    env={k:v for k,v in os.environ.items() if not k.startswith(('SC_','LNG_','SNESRECOMP_'))}
    env['SC_LIVE_CAPTURE']=str(root/'canvas')
    env['SC_SCRIPTED_INPUT']='1'
    # A persisted Fit scale must survive startup and every drawable resize.
    scale=2.5; pixel_aspect=7/6
    config=root/'sc-video.ini'
    config.write_text(f'Enabled=1\nAspect=Fit\nCentered=0\nFitScale={scale}\nFitPixelAspect={pixel_aspect:.17g}\n')
    user=c.WinDLL('user32',use_last_error=True); user.SetProcessDPIAware()
    callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
    user.EnumWindows.argtypes=[callback,w.LPARAM]
    user.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
    user.IsWindowVisible.argtypes=[w.HWND]
    user.GetClientRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
    user.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
    user.SetWindowPos.argtypes=[w.HWND,w.HWND,c.c_int,c.c_int,c.c_int,c.c_int,w.UINT]
    user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
    user.SetForegroundWindow.argtypes=[w.HWND]
    user.GetForegroundWindow.restype=w.HWND
    samples=[]; hwnd=None
    with (root/'game.log').open('wb') as log:
        p=subprocess.Popen([str(args.exe.resolve(strict=True)),str(args.rom.resolve(strict=True)),
          '--no-settings','--video-config',str(config),
          '--load-state',str(args.state.resolve(strict=True)),
          '--window-size','800x600'],cwd=root,env=env,stdout=log,stderr=log)
        try:
            windows=[]
            @callback
            def visit(handle,_):
                owner=w.DWORD(); user.GetWindowThreadProcessId(handle,c.byref(owner))
                if owner.value==p.pid and user.IsWindowVisible(handle): windows.append(handle)
                return True
            deadline=time.monotonic()+15
            while not windows:
                if p.poll() is not None or time.monotonic()>deadline: raise RuntimeError('No game window')
                user.EnumWindows(visit,0); time.sleep(.1)
            hwnd=windows[0]; time.sleep(.5)
            for index,(width,height) in enumerate([
                (1280,720),(1260,540),(1440,405),
                (600,900),(900,900),(800,600)]):
                expected=(max(256,min(2048,math.floor(width/(scale*pixel_aspect)/2+1e-9)*2)),
                          max(224,min(2048,math.floor(height/scale/2+1e-9)*2)))
                owner=w.DWORD(); user.GetWindowThreadProcessId(hwnd,c.byref(owner))
                assert owner.value==p.pid and p.poll() is None
                client,outer=w.RECT(),w.RECT()
                user.GetClientRect(hwnd,c.byref(client)); user.GetWindowRect(hwnd,c.byref(outer))
                extra_w=outer.right-outer.left-client.right
                extra_h=outer.bottom-outer.top-client.bottom
                if not user.SetWindowPos(hwnd,None,40,60,width+extra_w,height+extra_h,0x0014):
                    raise c.WinError(c.get_last_error())
                capture=root/f'canvas-{expected[0]}x{expected[1]}.ppm'
                deadline=time.monotonic()+8
                while not capture.exists():
                    if p.poll() is not None or time.monotonic()>deadline: raise RuntimeError(f'No complete {expected} canvas')
                    time.sleep(.1)
                time.sleep(.4)
                user.GetClientRect(hwnd,c.byref(client))
                assert (client.right,client.bottom)==(width,height)
                image=Image.open(capture); assert image.size==expected
                image.save(root/f'{index}-canvas.png')
                # PrintWindow cannot reliably read this SDL3 GPU swapchain.
                # Put only our child in front and capture its desktop bounds.
                if args.screenshots:
                    user.SetForegroundWindow(hwnd)
                    time.sleep(.2)
                    if user.GetForegroundWindow()==hwnd:
                        user.GetWindowRect(hwnd,c.byref(outer))
                        shot=ImageGrab.grab(bbox=(outer.left,outer.top,outer.right,outer.bottom),all_screens=True)
                        if user.GetForegroundWindow()==hwnd: shot.save(root/f'{index}-window.png')
                samples.append({'window':[width,height],'canvas':expected})
                print('PASS',width,height,'->',expected,flush=True)
        finally:
            if p.poll() is None:
                if hwnd: user.PostMessageW(hwnd,0x10,0,0)
                try: p.wait(timeout=10)
                except subprocess.TimeoutExpired: p.terminate(); p.wait(timeout=10)
        if p.returncode: raise RuntimeError(f'Child exited {p.returncode}')
    (root/'report.json').write_text(json.dumps(samples,indent=2)+'\n')
    print('PASS: six actual window resizes; inspect captured canvases.',flush=True)


if __name__=='__main__': main()
