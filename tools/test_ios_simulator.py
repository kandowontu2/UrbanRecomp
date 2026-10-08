#!/usr/bin/env python3
import json, plistlib, shutil, subprocess, time
from pathlib import Path
from platform_assets import stage_assets
root=Path(__file__).resolve().parents[1];deps=root/'platform-deps';app=deps/'simulator-game/UrbanRecomp.app'
stage_assets(root,app,deps/'SDL')
subprocess.run(['codesign','--force','--sign','-',str(app)],check=True)
devices=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','available','--json']))['devices']
device=next(d for ds in devices.values() for d in ds if 'iPhone' in d['name'] and d['isAvailable']);udid=device['udid']
subprocess.run(['xcrun','simctl','boot',udid],check=True)
subprocess.run(['xcrun','simctl','bootstatus',udid,'-b'],check=True)
subprocess.run(['xcrun','simctl','install',udid,str(app)],check=True)
out=deps/'ios-validation';out.mkdir(exist_ok=True)
launched=subprocess.check_output(['xcrun','simctl','launch','--stdout='+str(out/'stdout.log'),'--stderr='+str(out/'stderr.log'),udid,'io.github.kandowontu2.UrbanRecomp'],text=True)
(out/'launch.txt').write_text(launched)
time.sleep(15)
subprocess.run(['xcrun','simctl','io',udid,'screenshot',str(out/'rom-picker.png')],check=True)
assert '[mobile] saves/settings:' in (out/'stderr.log').read_text()
subprocess.run(['xcrun','simctl','terminate',udid,'io.github.kandowontu2.UrbanRecomp'],check=True)
subprocess.run(['xcrun','simctl','shutdown',udid],check=True)
print('iOS simulator launched successfully and presented the ROM picker; no ROM uploaded')
