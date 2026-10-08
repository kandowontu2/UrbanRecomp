# macOS app build

The Mac package is a self-contained universal `UrbanRecomp.app` for Apple
Silicon and Intel, targeting macOS 11 or newer. It contains the native game
executable, statically linked SDL3, launcher assets, restored soundtrack,
credits and license notices. Supply your own clean US SimCity SNES ROM.

Bundled apps store settings and saves in
`~/Library/Application Support/UrbanRecomp/UrbanRecomp/`. Shipped assets are
read from `Contents/Resources`; the app can remain in Applications or on a
read-only volume. Command-line developer builds keep their existing paths.
Finder launches write `urbanrecomp-startup.log` beside settings and saves.
Unreadable or unsupported ROMs show a startup error instead of silently
closing. Both headerless cartridges and images with a 512-byte copier header
use the same verified native profile; the ROM file remains unchanged.

Mac rendering uses Metal presentation and the CPU terrain/field fallback.
The Vulkan compute shaders used on Windows do not have a Metal implementation.
The self-contained Mac build uses bundled outline emoji fonts instead of
optional system FreeType/HarfBuzz libraries. `SC_MACOS_SELF_CONTAINED=OFF`
allows those optional host libraries for a developer build; packaged apps
must still contain both architectures and have no external dependencies.
This package is ad-hoc signed. Developer ID signing and notarization require
the maintainer's Apple signing credentials; an ad-hoc signature does not
establish notarization or Gatekeeper approval.

## Build on a Mac

Install Xcode command-line tools, CMake, Ninja and Python 3. Initialize the
pinned submodules and generate the private native sources from your own ROM
as described in [native execution](NATIVE_EXECUTION.md). Stage your locally
imported restored PCM tracks; do not commit these inputs.

Build SDL3 statically for both architectures, then use the same deployment
target and architecture list for the game:

```sh
git clone --depth 1 --branch release-3.4.16 https://github.com/libsdl-org/SDL.git mac-deps/SDL
cmake -S mac-deps/SDL -B mac-deps/sdl-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DSDL_SHARED=OFF -DSDL_STATIC=ON \
  -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX="$PWD/mac-deps/sdl-install"
cmake --build mac-deps/sdl-build --parallel 3
cmake --install mac-deps/sdl-build

cmake -S . -B mac-deps/game-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_PREFIX_PATH="$PWD/mac-deps/sdl-install" \
  -DSNESRECOMP_SDL_BACKEND=SDL3 -DSC_AOT=OFF -DSC_PROGRAM=ON \
  -DSC_INTERPRETER_REFERENCE=OFF -DSC_LTO=OFF
cmake --build mac-deps/game-build --parallel 2 --target UrbanRecomp
python3 tools/package_macos.py enhanced-v1.0.2 \
  --build-dir mac-deps/game-build --restored-music-dir music/restored
```

The packager verifies both Mach-O slices, rejects non-system dynamic
dependencies, includes the 19 restored tracks, optional milestone themes and
required notices, checks for
private files, signs the app and verifies its signature before making a ZIP.

## Hosted build

`.github/workflows/macos.yml` is manually dispatched. Generated native C and
restored music are supplied through a temporary authenticated draft release,
kept outside Git. The runner checks the archive checksum and allowlist; no
ROM or personal save is accepted. Build products are returned through the
same private draft. Retrieve and verify the finished ZIP, then delete the
draft release and its assets. The workflow does not publish a public release.

Host renderer, viewport, scrolling and deferred-terrain tests run on the Mac
runner. Packaged startup, both executable architectures, bundle metadata and
code signing are checked. Game replay tests require a ROM and remain local;
these no-ROM hosted checks do not establish Mac gameplay performance.
