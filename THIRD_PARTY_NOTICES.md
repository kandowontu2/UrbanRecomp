# Third-party notices

Urban Recomp itself is under the MIT licence (`LICENSE`). A program built from
this repository also contains, or ships next to, the following.

## snesrecomp -- PolyForm Noncommercial 1.0.0

The recompilation framework and the SNES device models (CPU interpreter, PPU,
APU, DMA, cartridge), linked into `UrbanRecomp.exe`.

Required Notice: Copyright (c) 2026 Matthew Stan

<https://polyformproject.org/licenses/noncommercial/1.0.0>. The full text is
`snesrecomp/LICENSE` in the source tree and `licenses/snesrecomp-LICENSE.txt`
in the Windows package. Because of it, the program may be used and passed on
for noncommercial purposes only.

## recomp-ui -- MIT

The launcher. Copyright (c) 2026 Matthew Stanley. Full text:
`recomp-ui/LICENSE`, or `licenses/recomp-ui-LICENSE.txt` in the package.

It bundles:

- **Dear ImGui** -- MIT, Copyright (c) 2014-2025 Omar Cornut
- **stb_image, stb_image_write, stb_truetype** -- MIT or public domain, Sean Barrett
- **tinyfiledialogs** -- zlib, Guillaume Vareille
- **gl_core_3_1** -- generated OpenGL loader

## Fonts and images in `assets/`

- **Lato** (`LatoLatin-*.ttf`) -- SIL Open Font License 1.1, Łukasz Dziedzic
- **Noto Sans Symbols 2** -- SIL Open Font License 1.1, The Noto Project Authors
- **OpenMoji** (`OpenMoji-black-glyf.ttf`) -- CC BY-SA 4.0, OpenMoji Project
- **Country flags** (`flags.png`) -- rendered from Noto Color Emoji, SIL Open
  Font License 1.1, The Noto Project Authors

recomp-ui's own notices for these are in `recomp-ui/assets/common/fonts/NOTICE.md`
and `recomp-ui/assets/common/img/NOTICE.md`.

## SDL2 / SDL3 -- zlib licence

`SDL2.dll` or `SDL3.dll`, depending on the build. Copyright (C) 1997-2026
Sam Lantinga. Full text: `licenses/SDL-LICENSE.txt`. The enhanced Windows
prerelease ships SDL 3.4.16; source: <https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16>.

## Microsoft Visual C++ runtime

`vcruntime140.dll`, `vcruntime140_1.dll` and `msvcp140.dll` in the Windows
package are redistributable files of Microsoft Visual Studio 2022.
These files are used by MSVC packages; the enhanced MinGW package instead
ships the runtimes listed below.

## MinGW GCC runtime

The enhanced Windows package is built with GCC 13.2.0 and ships
`libgcc_s_seh-1.dll` and `libstdc++-6.dll`. Their GPLv3 and GCC Runtime Library
Exception texts are included as `licenses/GCC-COPYING3.txt` and
`licenses/GCC-RUNTIME-EXCEPTION.txt`.
GCC 13.2.0 source: <https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz>.

`libwinpthread-1.dll` is the mingw-w64 winpthreads runtime. Its notices are in
`licenses/winpthreads-COPYING.txt`; source:
<https://github.com/mingw-w64/mingw-w64/tree/master/mingw-w64-libraries/winpthreads>.

## Sylt

`sylt_graphics/sylt_map.bin` and `sylt_card.bin`: the Sylt map is lytron's
work, used with written permission, and the card artwork is this project's
own. See `sylt_graphics/PROVENANCE.md`.

## Credits for ideas, no code taken

- **Truttle1** found the post-load power bug and the power bit.
- **Selicre**'s community mouse patch identified the cursor bytes that the
  mouse control drives (<https://github.com/Selicre/simcity-mouse>).
