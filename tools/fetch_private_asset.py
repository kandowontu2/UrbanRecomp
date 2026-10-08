#!/usr/bin/env python3
"""Download one checksummed asset from an authenticated temporary draft."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('repo');p.add_argument('release');p.add_argument('name');p.add_argument('output',type=Path);a=p.parse_args()
pages=json.loads(subprocess.check_output(['gh','api',f'repos/{a.repo}/releases','--paginate','--slurp']))
r=next(r for page in pages for r in page if r['tag_name']==a.release);assert r['draft']
asset=next(asset for asset in r['assets'] if asset['name']==a.name)
a.output.parent.mkdir(parents=True,exist_ok=True)
with a.output.open('wb') as out:subprocess.run(['gh','api',f'repos/{a.repo}/releases/assets/{asset["id"]}','-H','Accept: application/octet-stream'],stdout=out,check=True)
assert asset['digest']=='sha256:'+hashlib.sha256(a.output.read_bytes()).hexdigest()
print('Verified private artifact: '+a.name)
