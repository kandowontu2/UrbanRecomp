#!/usr/bin/env python3
"""Pack the Windows release ZIP.

    cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 \\
        -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake \\
        -DSNESRECOMP_SDL_BACKEND=SDL2 -DSC_AOT=OFF
    cmake --build build-release --config Release --target UrbanRecomp
    python tools/package_release.py v1.0.0

For a MinGW/SDL3 build, pass --build-dir <CMake-build-folder> and
--runtime-dir <MinGW-bin-folder>. Both SDL backends are detected from the
build folder. A SHA-256 file is written alongside the ZIP.

SC_AOT=OFF keeps the older CpuState AOT tier out of this enhanced host.
The compatible program_gen native tier is the production execution path.
This script refuses the legacy AOT tier and interpreter reference builds.

The package holds the executable, SDL, the compiler runtime DLLs it
needs, the launcher assets, the Sylt data, the translation builder, the
licences and a short readme.
No ROM images, generated source files, cartridge graphics, translation files
or personal saved states are bundled.
"""
import argparse
import glob
import hashlib
import os
import struct
import sys
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BUILD = os.path.join(ROOT, "build-release", "Release")
CRT_GLOB = r"C:\Program Files\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT"
CRT_FILES = ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll")


def crt_dir():
    dirs = sorted(glob.glob(CRT_GLOB))
    if not dirs:
        sys.exit("no Visual C++ redistributable folder found (%s)" % CRT_GLOB)
    return dirs[-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version")
    parser.add_argument("--build-dir", default=BUILD)
    parser.add_argument("--runtime-dir", help="MinGW runtime DLL directory; otherwise use MSVC CRT")
    parser.add_argument("--restored-music-dir", help="Include the credited local 19-track restored PCM set")
    args = parser.parse_args()
    version = args.version
    build = os.path.abspath(args.build_dir)
    exe = os.path.join(build, "UrbanRecomp.exe")
    if not os.path.isfile(exe):
        sys.exit("build first: %s is missing" % exe)
    with open(exe, "rb") as f:
        if b"bank_00_" in f.read():
            sys.exit("%s links the AOT tier; rebuild with -DSC_AOT=OFF" % exe)
    cache_path = os.path.join(build, "CMakeCache.txt")
    if not os.path.isfile(cache_path):
        cache_path = os.path.join(os.path.dirname(build), "CMakeCache.txt")
    with open(cache_path, encoding="utf-8") as f:
        cache = f.read()
        if "SC_AOT:BOOL=OFF" not in cache:
            sys.exit("public packages require a CMake build with SC_AOT=OFF")
        if "SC_INTERPRETER_REFERENCE:BOOL=OFF" not in cache:
            sys.exit("public packages require SC_INTERPRETER_REFERENCE=OFF")
    sdl = next((n for n in ("SDL3.dll", "SDL2.dll") if os.path.isfile(os.path.join(build, n))), None)
    if not sdl:
        sys.exit("missing SDL runtime in " + build)

    name = "UrbanRecomp-%s-windows-x64" % version
    out = os.path.join(ROOT, "dist", name + ".zip")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    files = [
        (exe, "UrbanRecomp.exe"),
        (os.path.join(build, sdl), sdl),
        (os.path.join(ROOT, "tools", "release_readme.txt"), "README.txt"),
        (os.path.join(ROOT, "LICENSE"), "LICENSE.txt"),
        (os.path.join(ROOT, "THIRD_PARTY_NOTICES.md"), "THIRD_PARTY_NOTICES.md"),
        (os.path.join(ROOT, "CREDITS.md"), "CREDITS.md"),
        (os.path.join(ROOT, "CHANGELOG.md"), "CHANGELOG.md"),
        (os.path.join(ROOT, "docs", "PC_ENHANCEMENTS.md"), "PC_ENHANCEMENTS.md"),
        (os.path.join(ROOT, "docs", "GPU_PERFORMANCE.md"), "GPU_PERFORMANCE.md"),
        (os.path.join(ROOT, "docs", "NATIVE_EXECUTION.md"), "NATIVE_EXECUTION.md"),
        (os.path.join(ROOT, "tools", "start_release.cmd"), "Start-UrbanRecomp.cmd"),
        (os.path.join(ROOT, "snesrecomp", "LICENSE"), "licenses/snesrecomp-LICENSE.txt"),
        (os.path.join(ROOT, "snesrecomp", "THIRD_PARTY_ATTRIBUTION.md"), "licenses/snesrecomp-THIRD_PARTY_ATTRIBUTION.md"),
        (os.path.join(ROOT, "recomp-ui", "LICENSE"), "licenses/recomp-ui-LICENSE.txt"),
        (os.path.join(ROOT, "recomp-ui", "src", "third_party", "imgui", "LICENSE.txt"),
         "licenses/imgui-LICENSE.txt"),
        (os.path.join(ROOT, "recomp-ui", "assets", "common", "fonts", "NOTICE.md"),
         "licenses/fonts-NOTICE.md"),
        (os.path.join(ROOT, "recomp-ui", "assets", "common", "img", "NOTICE.md"),
         "licenses/images-NOTICE.md"),
        (os.path.join(ROOT, "sylt_graphics", "sylt_map.bin"), "sylt_graphics/sylt_map.bin"),
        (os.path.join(ROOT, "sylt_graphics", "sylt_card.bin"), "sylt_graphics/sylt_card.bin"),
        (os.path.join(ROOT, "sylt_graphics", "PROVENANCE.md"), "sylt_graphics/PROVENANCE.md"),
    ]
    # tools/make_translations.py and what it runs, so a player can build the
    # German or French files from their own cartridges.
    for n in ("make_translations.py", "text_tool.py", "extract_graphics.py", "find_rom.py", "import_restored_music.py"):
        files.append((os.path.join(ROOT, "tools", n), "tools/" + n))
    if args.runtime_dir:
        runtime_files = ("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")
        files += [(os.path.join(args.runtime_dir, n), n) for n in runtime_files]
    else:
        files += [(os.path.join(crt_dir(), n), n) for n in CRT_FILES]
    for path in sorted(glob.glob(os.path.join(ROOT, "licenses", "*.txt"))):
        files.append((path, "licenses/" + os.path.basename(path)))
    for path in sorted(glob.glob(os.path.join(ROOT, "snesrecomp", "third_party", "psxrecomp_color_lut", "*"))):
        if os.path.isfile(path):
            files.append((path, "licenses/psxrecomp_color_lut/" + os.path.basename(path)))
    for path in sorted(glob.glob(os.path.join(build, "assets", "**", "*"), recursive=True)):
        if os.path.isfile(path):
            files.append((path, os.path.relpath(path, build).replace(os.sep, "/")))
    if args.restored_music_dir:
        for number in range(1, 20):
            name = "scity-msu1-%d.pcm" % number
            path = os.path.join(args.restored_music_dir, name)
            with open(path, "rb") as track:
                header = track.read(8)
            size = os.path.getsize(path)
            if len(header) != 8 or header[:4] != b"MSU1" or size < 12 or (size-8) % 4 or struct.unpack_from("<I", header, 4)[0] >= (size-8)//4:
                sys.exit("invalid restored track: " + path)
            files.append((path, "music/restored/" + name))

    missing = [src for src, _ in files if not os.path.isfile(src)]
    if missing:
        sys.exit("missing: " + ", ".join(missing))

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src, arc in files:
            z.write(src, name + "/" + arc)
    print("%s  (%d files, %.1f MB)" % (out, len(files), os.path.getsize(out) / 1e6))
    with open(out, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    with open(out + ".sha256", "w", encoding="utf-8", newline="\n") as f:
        f.write(digest + "  " + os.path.basename(out) + "\n")


if __name__ == "__main__":
    main()
