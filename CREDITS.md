# Credits

Urban Recomp Enhanced builds on [blackerking/UrbanRecomp](https://github.com/blackerking/UrbanRecomp).
The upstream project's history, contributors, notices and licenses are retained.
The enhanced fork is maintained by [kandowontu2](https://github.com/kandowontu2/UrbanRecomp).

## Engine, launcher and bundled components

- **Matthew Stan / Matthew Stanley:** [snesrecomp](https://github.com/mstan/snesrecomp)
  and recomp-ui, including the SNES device models and launcher.
- **angelo_wf and contributors:** the MIT-licensed LakeSnes CPU core used by
  snesrecomp, also the semantic source for the compatible ROM-to-C tier and
  native interrupt/idle CPU control and register/save-state support.
  Copyright (c) 2021-2023 angelo_wf and
  contributors; the retained MIT notice is in the bundled attribution.
  **JRickey / gba-recomp and PSXRecomp contributors:** color models.
  snesrecomp's complete third-party attribution and color-model licenses are
  included in the bundled `licenses` directory.
- **SDL contributors, Sam Lantinga:** SDL, used for windowing, input, audio and
  shared Vulkan presentation/compute, with Metal presentation on macOS.
- **Omar Cornut and Dear ImGui contributors:** launcher UI.
- **Sean Barrett:** stb libraries. **Guillaume Vareille:** tinyfiledialogs.
- **Łukasz Dziedzic:** Lato. **The Noto Project Authors:** Noto fonts and flags.
  **OpenMoji Project:** OpenMoji.
- **GCC and mingw-w64 contributors:** the Windows compiler runtime.
- **lytron:** Sylt map, used by upstream with written permission.

The exact required notices and license texts are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the bundled `licenses` directory.
The single-file distribution includes these files internally; run
`UrbanRecomp.exe --portable-docs` to open them.
Mac app bundles include the same notices in `Contents/Resources`.

## Community research and behavior references

- **Truttle1:** upstream's post-load power bug and power-bit identification.
- **Peter's SimCity SNES Guide**, **Brian Sulpher**, **FatRatKnight**,
  **PrinceMercury** and **Cyan_of_Ages**: SNES zoning/gift strategy references.
  Their advice is checked against the US cartridge routines in
  [the placement-effects guide](docs/PLACEMENT_EFFECTS.md); no guide text,
  ROM patch or map data is imported into the generated city.
- **Selicre:** [community mouse patch](https://github.com/Selicre/simcity-mouse),
  identifying the original cursor bytes used by mouse control.
- **Vitor Vilela:** [SimCity SA-1 Beta 2](https://www.patreon.com/vitorvilela/posts/simcity-sa-1-2-168886217),
  a reference for requested mouse behavior. This fork's implementation is
  independent; it contains no SA-1 patch bytes. His
  [animated map-generation demonstration](https://x.com/HackerVilela/status/2106821857437737450)
  also provided the behavior reference for the staged map-select preview;
  geography and preview animation are independently implemented.

## Restored soundtrack

The bundled **MSU1 SimCity (Restored)** set was supplied for this project by the
owner. Credit to **Pinci**, associated with **Church of Kondo**, for the restored
music, and **Relikk** for the restored PCM set. The
[SimCity MSU-1 project page](https://www.zeldix.net/t1602-simcity) identifies Pinci's
restoration and Relikk's PCM pack. The original MSU-1 work by **pev / pepillopev**
provided a reference for track-command mapping; its ROM patch is not bundled.
The 19 tracks retain their authored stereo audio and loop points. The host's
native sound effects continue to play alongside them.

## Additional city themes

The **Megagopolos** city theme uses **Markify's** restoration,
[Super Mario Kart: Vanilla Lake Beta Theme (Restored)](https://www.youtube.com/watch?v=NjLiCEoSGWo),
the unused Vanilla Lake composition by **Soyo Oka**. This is a separate
restoration from the Church of Kondo SimCity set. The project prepares a
31.2195-second repeating phrase with a short seam blend, mixing headroom and
volume balanced with the restored city themes;
`tools/import_milestone_music.py` reproduces the PCM conversion from a local
copy of that recording.

The **Gigagopolois** city theme is **LOOP16B**, track 7 from
[LOOP816 / Soyo Oka (2023)](https://downloads.khinsider.com/game-soundtracks/album/loop816-soyo-oka-2023).
**Soyo Oka / 岡素世** is credited as the artist, composer and publisher.
The full arrangement and its original ending fade are retained; only trailing
silence is trimmed for repeat playback. It plays after the 100-million milestone.
Its playback volume is balanced with the restored city music.

## Original game

**SimCity (SNES, 1991): Nintendo, Maxis and Electronic Arts**, and the original
game's developers and musicians. Game code, artwork and fonts used during play
are loaded from the player's own US cartridge ROM. The ROM is not included.
This is an unofficial fan project and is not affiliated with those companies.

The enhanced fork's stadium dome overlays are drawn by host code over the
player-loaded stadium artwork, using its existing palette. No cartridge
graphics are embedded in the distribution.
