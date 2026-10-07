# UrbanRecomp Enhanced mobile controls

Import your own clean US SimCity SNES ROM through the launcher's Files picker.
No ROM is included. A 512-byte copier header is accepted and removed on import.
The restored soundtrack, milestone music, Sylt map, credits and licenses are bundled.

Tap or drag the city and menus to use the in-game mouse. `PAN` switches city
touches to one-finger drag panning; switch it off to build again. Two-finger
drag panning is also available. Pinch to zoom the city. The `-` and `+` buttons also change zoom.
`PAD` hides or shows the touch buttons. `F12` opens the enhancement options;
`SAVE` opens the in-game save dialog or backs out of a menu.

The virtual SNES controls include the D-pad, A/B/X/Y, L/R, Start and Select.
B confirms selections; A backs out in the standard game menus. A compatible
Bluetooth or USB controller is also supported. Its south/east buttons map to
SNES B/A; west/north map to Y/X. A hardware mouse and keyboard can be used too.

These first mobile packages use the same native game and expanded maps as the
desktop version. Very large cities require substantial memory and CPU time;
choose a smaller map for devices with limited memory. No artificial population
or growth changes are applied on mobile.

Android requires Android 8 or later and a 64-bit ARM or x86 device. Saves and
imported ROMs are in app storage; uninstalling the app removes them.
The APK is signed with the owner's existing release certificate.

iOS saves and the imported ROM appear in Files under On My iPhone/iPad →
UrbanRecomp Enhanced. iOS requires iOS/iPadOS 14 or later. The GitHub IPA must be signed using a
sideloading tool and your own Apple account before installation. It is not an
App Store or TestFlight distribution. Files are in the app's sandbox; saves
must be made in-game before closing the app. The iOS build uses Metal
presentation with CPU terrain/field rendering; Android uses Vulkan when
available and falls back to the SDL renderer when it is unavailable.
