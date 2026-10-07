#!/usr/bin/env python3
import argparse, shutil
from pathlib import Path
from platform_assets import stage_assets
p=argparse.ArgumentParser();p.add_argument('destination',type=Path);p.add_argument('--sdl-dir',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1];stage_assets(root,a.destination,a.sdl_dir)
icon=root/'platform/android/app/src/main/res/drawable/icon.png'
icon.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/'assets/icon.png',icon)
