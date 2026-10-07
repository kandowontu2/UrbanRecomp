#!/usr/bin/env python3
import argparse, platform, subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('target',choices=['device','simulator']);a=p.parse_args()
root=Path(__file__).resolve().parents[1];deps=root/'platform-deps'
sdk='iphoneos' if a.target=='device' else 'iphonesimulator';arch='arm64' if a.target=='device' or platform.machine()=='arm64' else 'x86_64'
common=['-DCMAKE_SYSTEM_NAME=iOS',f'-DCMAKE_OSX_SYSROOT={sdk}',f'-DCMAKE_OSX_ARCHITECTURES={arch}',
    '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0','-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO','-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO']
prefix=deps/(a.target+'-sdl-install');sdlbuild=deps/(a.target+'-sdl-build');gamebuild=deps/(a.target+'-game')
def run(*args):subprocess.run(list(map(str,args)),cwd=root,check=True)
run('cmake','-S',deps/'SDL','-B',sdlbuild,'-G','Ninja',*common,'-DSDL_STATIC=ON','-DSDL_SHARED=OFF','-DSDL_TEST_LIBRARY=OFF','-DSDL_TESTS=OFF',f'-DCMAKE_INSTALL_PREFIX={prefix}')
run('cmake','--build',sdlbuild,'--config','Release','--parallel','4')
run('cmake','--install',sdlbuild,'--config','Release')
run('cmake','-S',root,'-B',gamebuild,'-G','Xcode',*common,f'-DCMAKE_PREFIX_PATH={prefix}',f'-DSDL3_DIR={prefix}/lib/cmake/SDL3',
    '-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH',
    '-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG','-DSNESRECOMP_SDL_BACKEND=SDL3','-DSC_AOT=OFF',
    '-DSC_PROGRAM=ON','-DSC_INTERPRETER_REFERENCE=OFF','-DSC_LTO=OFF','-DBUILD_TESTING=OFF')
run('cmake','--build',gamebuild,'--config','Release','--parallel','4','--target','UrbanRecomp')
