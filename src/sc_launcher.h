/* The pre-boot launcher (recomp-ui) and the settings it edits.
 *
 * ScSettings is what a player chooses before the game starts: window, audio,
 * widescreen, language and the Sylt scenario. It lives in sc-settings.ini next
 * to the executable and is applied as the environment variables the host has
 * always read (SC_WIDESCREEN, SC_NINTH, SC_TRANSLATION, SC_PACKET_PATCH), so a
 * variable set by hand still wins. The launcher itself is optional: builds
 * without recomp-ui, and hosts without OpenGL 3, start the game directly with
 * the saved settings. */
#ifndef SC_LAUNCHER_H_INCLUDED
#define SC_LAUNCHER_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include "sc_video.h"

enum { SC_LANG_ENGLISH, SC_LANG_GERMAN, SC_LANG_FRENCH, SC_LANG_COUNT };

typedef struct ScSettings {
  char rom[1024];
  int skip_launcher;
  int window_scale;
  int fullscreen;          /* 0 window, 1 borderless, 2 exclusive */
  int linear_filter;
  int enable_audio;
  int widescreen;
  int language;            /* SC_LANG_* */
  int sylt;                /* Sylt as the ninth scenario */
  int large_maps;          /* new free-play cities: 0 Normal, 1 Big, 2 Huge, 3 960x800 */
} ScSettings;

extern const char *const kScSettingsPath;

void ScSettingsDefaults(ScSettings *s);
/* Missing file: defaults, and true. A malformed line is skipped. */
bool ScSettingsLoad(ScSettings *s, const char *path);
bool ScSettingsSave(const ScSettings *s, const char *path);
/* Sets the host's environment variables from `s`, leaving any that are
 * already set alone. */
void ScSettingsApply(const ScSettings *s);
const char *ScLanguageLabel(int language);

/* FNV-1a 32 of the five known 512 KB images (docs/REGIONS.md). */
#define SC_ROM_FNV_US 0xec01686aul
#define SC_ROM_FNV_EU 0xb76b1a0dul
#define SC_ROM_FNV_FR 0xe1f99069ul
#define SC_ROM_FNV_DE 0xaeca7623ul
#define SC_ROM_FNV_JP 0xccb8c347ul

/* The .sfc/.smc file in the working directory whose contents have this
 * fingerprint, whatever it is called. False when there is none. */
bool ScFindRom(unsigned long fnv, char *out, size_t out_n);

/* Returns 1 to launch (s->rom holds the ROM), 0 to quit, -1 when no launcher
 * could be shown -- then the caller starts as if it had been skipped. */
int ScLauncherRun(ScSettings *s, const char *settings_path,
                  ScVideoSettings *video, const char *video_path);

/* The SNES pad through the launcher's keyboard bindings (keybinds.ini).
 * ScKeybindsInit before the first read, once SDL video is up: a first run
 * writes this host's own layout, not recomp-ui's defaults. Returns false when
 * the build has no launcher; the caller keeps its fixed bindings then. */
bool ScKeybindsInit(void);
/* Serial-order pad bits (B=$0001 ... R=$0800), or 0 without bindings. */
unsigned ScKeybindsRead(const unsigned char *keys);
/* Whether a scancode is bound to any pad button. */
bool ScKeybindsUses(int scancode);

#endif /* SC_LAUNCHER_H_INCLUDED */
