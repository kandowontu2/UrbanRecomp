#!/usr/bin/env python3
"""Retrieve the owner's temporary draft inputs with the runner's repository token."""
import argparse,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('repo');p.add_argument('release');p.add_argument('sha256');p.add_argument('output',type=Path);a=p.parse_args()
pages=json.loads(subprocess.check_output(['gh','api',f'repos/{a.repo}/releases','--paginate','--slurp']))
r=next(r for page in pages for r in page if r['tag_name']==a.release)
if not r['draft']:raise SystemExit('Build inputs must remain private')
asset=next(asset for asset in r['assets'] if asset['name']=='macos-native-inputs.tar.gz')
with a.output.open('wb') as out:subprocess.run(['gh','api',f'repos/{a.repo}/releases/assets/{asset["id"]}','-H','Accept: application/octet-stream'],stdout=out,check=True)
subprocess.run(['python3','tools/stage_macos_inputs.py',str(a.output),'--sha256',a.sha256],check=True)
