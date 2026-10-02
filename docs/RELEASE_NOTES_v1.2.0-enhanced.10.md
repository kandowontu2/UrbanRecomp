Expanded maps now have a native-font MAP SIZE screen before starting a city or Practice, with 120x100, 240x200, 480x400 and full 960x800 support. Map size has been removed from F12. Saved cities restore their full geometry correctly.

Fit to Screen adds visible land at the existing tile scale, with adjusted HUD, navigation and mouse regions. Hold Ctrl for 3x scrolling. Panning repairs stale interior terrain and roofs, and Tab skips intermediate image work while retaining simulation, interrupts and audio.

The renderer shares tile and warning lookups across pixel spans. Vacant spatial cells use equivalent C kernels; other spatial work uses batches bounded by beam and IRQ events. Cached power comparisons and flag updates process multiple cells per word. GPU terrain now starts enabled on supported Windows Direct3D 11 renderers, with automatic CPU fallback and an F12 toggle.

Paired tests on a private 960x800 save at 50x development, using a 730x532 Fit canvas in a 2560x1600 window, showed about **44% less frame work**. Heavy simulation phases still miss 60 FPS; this is a measured improvement, not a steady-60-FPS claim. Detailed timings and host-load limitations are included in GPU_PERFORMANCE.md.

Loading a city now rebuilds its actual power network before development resumes, across every map size. Plant capacity and disconnected zones still apply. The tested save's brownouts were genuine nuclear-capacity exhaustion, not lost power flags.

Validation: five CTest checks; actual Direct3D GPU pixel comparisons; 135 full spatial-cell and 1,536 vacant-terrain comparisons against native execution; full-world census/save migration; reload and speed-ratio power tests; and a real desktop launch plus 600-frame qualification. Reference and optimized Direct3D replays produced identical presented screenshots and city/CPU data.

Extract the Windows x64 ZIP into a separate folder and use your own clean US ROM. Copy urbanrecomp-us.srm, urbanrecomp-us.srm.world and urbanrecomp-us.srm.population together to retain existing cities. The package contains no ROM, user saves or personal settings. This remains an enhanced prerelease.
