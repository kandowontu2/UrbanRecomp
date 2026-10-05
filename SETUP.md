# Quick setup for testers (Windows / Linux)

This gets you from a fresh machine to a running build. You need your own
legally-dumped copy of the US release of the game (see [README.md](README.md))
-- **the ROM is never
included in this repo** and must not be shared; this project only ever
distributes source code.

## 0. Prerequisites

| | Windows | Linux |
|---|---|---|
| Git | [git-scm.com](https://git-scm.com/) (includes Git Bash, used below) | `sudo apt install git` (or your distro's equivalent) |
| CMake | [cmake.org](https://cmake.org/download/) (3.16+) | `sudo apt install cmake` |
| C compiler | Visual Studio 2022 (Desktop C++ workload) | `sudo apt install build-essential` |
| SDL | via [vcpkg](https://github.com/microsoft/vcpkg): `vcpkg install sdl3:x64-windows` | `sudo apt install libsdl2-dev libgl-dev` (SDL2; the launcher draws with OpenGL) |
| Ninja (optional, faster builds) | `winget install Ninja-build.Ninja` | `sudo apt install ninja-build` |

## 1. Clone

```bash
git clone --recurse-submodules https://github.com/blackerking/UrbanRecomp.git
cd UrbanRecomp
bash tools/bootstrap.sh
```

`bootstrap.sh` is safe to rerun any time; it makes sure the pinned
`snesrecomp` submodule is checked out at the right revision.

## 2. Provide and verify your ROM

Dump your own cartridge, or otherwise legally obtain a US release copy.
Place the file in the repository root -- any file name ending in `.sfc` or
`.smc` works; the game and the tools recognise it by its contents. Then
verify it's the expected release (this only checks *your* file against a
public hash -- it never uploads or shares the ROM itself):

```bash
# Windows (PowerShell):
Get-FileHash <your-rom>.sfc -Algorithm SHA256

# Linux / Git Bash:
sha256sum <your-rom>.sfc
```

Expected hash: `e9c0bc05511e05a0d7c3e7cc42e761e1e8e532d46f59b9854b6902e1a2e9dd0a`

If it doesn't match, the ROM is a different revision/region and the game
will very likely not boot correctly.

## 3. Generate and build

```bash
bash tools/regen.sh --no-tests
python tools/compile_native_program.py --rom /path/to/your/us.sfc
```

**Windows** (from a "Developer Command Prompt for VS 2022", or any shell
with `cl.exe` on PATH):
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```
(No Ninja/vcpkg handy? `cmake -S . -B build` with no extra flags falls back
to the Visual Studio generator, as long as SDL3 dev files are discoverable.)

**Linux** (Ubuntu's `libsdl2-dev` is SDL2, so say so):
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSNESRECOMP_SDL_BACKEND=SDL2 -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build --target UrbanRecomp
cmake --install build      # optional: menu entry with icon, see below
```

`cmake --install` puts the game under `~/.local/lib/.../urbanrecomp`, a
starter `urbanrecomp` in `~/.local/bin`, and a menu entry with the Urban Recomp
icon (all sizes, hicolor theme). Started from the menu, the game keeps its
ROM, settings and saves in `~/.local/share/urbanrecomp` -- put the ROM there,
or pick it in the launcher. Without installing, `build/UrbanRecomp` runs in
place as on Windows. The prefix is chosen when configuring because the menu
entry names the starter by its full path.

## 4. Run

```bash
build/UrbanRecomp        # Linux
build\Release\UrbanRecomp.exe   # Windows
```

The launcher opens first: pick the ROM there (any file name), and set
widescreen, language and the Sylt scenario. On a machine without OpenGL 3
the game starts directly with the saved settings. See the "Controls" section in
[README.md](README.md) for the keyboard mapping (arrow keys / U-H-J-K for
D-pad, X/Y/Z/A/S/Q/E for the face and shoulder buttons, Enter for Start).

## Troubleshooting

- **"no ROM"**: no file with the US ROM's contents is in the directory
  you run the executable from. Pick it in the launcher, pass its path
  (`UrbanRecomp.exe path/to/rom.sfc`), or copy it there.
- **CMake can't find SDL3**: double-check the vcpkg toolchain file path
  (Windows) or that `libsdl2-dev` is actually installed (Linux).
- **Build succeeds but the window is black/frozen on launch**: confirm the
  ROM hash from step 2 -- a mismatched or corrupt ROM is the most common
  cause.

## SDL backend

SDL3 is the default. SDL2 remains available as a fallback:

```sh
cmake -S . -B build                                  # SDL3
cmake -S . -B build-sdl2 -DSNESRECOMP_SDL_BACKEND=SDL2
```

Both backends are verified to produce byte-identical WRAM and identical
`--qualify` counters, so the choice is packaging, not behaviour.
