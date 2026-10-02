/* The pre-boot launcher and its settings -- see sc_launcher.h. */
#include "sc_launcher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <dirent.h>
#endif

#include "sc_sdl_compat.h"

#ifdef RECOMP_LAUNCHER
#include "recomp_launcher.h"
#include "sc_mods.h"
#include "launcher_profile.h"
#include "common/keybinds.h"   /* recomp-ui's, not the runner's */
#endif

/* The launcher window shows this file as its icon (GameInfo.window_icon_path);
 * the game window uses the pixels compiled in by src/sc_icon.c. */
static const char kIconAsset[] = "assets/img/icon.png";

const char *const kScSettingsPath = "sc-settings.ini";

static const char *const kLanguageLabels[SC_LANG_COUNT] = {
  "English", "Deutsch", "Fran\xc3\xa7" "ais",
};
static const char *const kLanguageCodes[SC_LANG_COUNT] = { "en", "de", "fr" };

const char *ScLanguageLabel(int language) {
  return language >= 0 && language < SC_LANG_COUNT ? kLanguageLabels[language] : "?";
}

static bool file_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f) fclose(f);
  return f != NULL;
}

/* A translation is two files, the message blob and the packet patch that
 * text_tool.py writes next to it. */
static void translation_files(int language, char *blob, size_t blob_n,
                              char *packets, size_t packets_n) {
  snprintf(blob, blob_n, "translation_%s.bin", kLanguageCodes[language]);
  snprintf(packets, packets_n, "translation_%s_selector.scpk", kLanguageCodes[language]);
}

void ScSettingsDefaults(ScSettings *s) {
  memset(s, 0, sizeof(*s));
  s->rom[0] = 0;   /* chosen in the launcher, or found by contents */
  s->window_scale = 3;
  s->enable_audio = 1;
  s->widescreen = 1;
  s->sylt = 1;
  /* The translation this project exists for, when it is there. */
  char blob[64], packets[64];
  translation_files(SC_LANG_GERMAN, blob, sizeof blob, packets, sizeof packets);
  s->language = file_exists(blob) && file_exists(packets) ? SC_LANG_GERMAN
                                                          : SC_LANG_ENGLISH;
}

static int ascii_casecmp(const char *a, const char *b) {
  for (;; a++, b++) {
    int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
    int cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
    if (ca != cb || !ca) return ca - cb;
  }
}

static unsigned long fnv_of_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  unsigned long h = 2166136261ul;
  long size = 0;
  int c;
  while ((c = fgetc(f)) != EOF) {
    h = ((h ^ (unsigned long)c) * 16777619ul) & 0xfffffffful;
    size++;
  }
  fclose(f);
  return size == 0x80000 ? h : 0;
}

static bool rom_name(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot && (!ascii_casecmp(dot, ".sfc") || !ascii_casecmp(dot, ".smc"));
}

#ifdef _WIN32
bool ScFindRom(unsigned long fnv, char *out, size_t out_n) {
  struct _finddata_t fd;
  intptr_t h = _findfirst("*.*", &fd);
  if (h == -1) return false;
  bool found = false;
  do {
    if ((fd.attrib & _A_SUBDIR) || !rom_name(fd.name) || fd.size != 0x80000) continue;
    if (fnv_of_file(fd.name) == fnv) {
      snprintf(out, out_n, "%s", fd.name);
      found = true;
    }
  } while (!found && _findnext(h, &fd) == 0);
  _findclose(h);
  return found;
}
#else
bool ScFindRom(unsigned long fnv, char *out, size_t out_n) {
  DIR *d = opendir(".");
  if (!d) return false;
  bool found = false;
  struct dirent *e;
  while (!found && (e = readdir(d)) != NULL) {
    if (rom_name(e->d_name) && fnv_of_file(e->d_name) == fnv) {
      snprintf(out, out_n, "%s", e->d_name);
      found = true;
    }
  }
  closedir(d);
  return found;
}
#endif

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

bool ScSettingsLoad(ScSettings *s, const char *path) {
  ScSettingsDefaults(s);
  FILE *f = fopen(path, "r");
  if (!f) return true;
  char line[1200];
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0] || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char *key = line, *val = eq + 1;
    if (!strcmp(key, "rom")) snprintf(s->rom, sizeof s->rom, "%s", val);
    else if (!strcmp(key, "skip_launcher")) s->skip_launcher = atoi(val) != 0;
    else if (!strcmp(key, "window_scale")) s->window_scale = clamp(atoi(val), 1, 8);
    else if (!strcmp(key, "fullscreen")) s->fullscreen = clamp(atoi(val), 0, 2);
    else if (!strcmp(key, "linear_filter")) s->linear_filter = atoi(val) != 0;
    else if (!strcmp(key, "enable_audio")) s->enable_audio = atoi(val) != 0;
    else if (!strcmp(key, "widescreen")) s->widescreen = atoi(val) != 0;
    else if (!strcmp(key, "sylt")) s->sylt = atoi(val) != 0;
    else if (!strcmp(key, "large_maps")) s->large_maps = clamp(atoi(val),0,3);
    else if (!strcmp(key, "language")) {
      for (int i = 0; i < SC_LANG_COUNT; i++)
        if (!strcmp(val, kLanguageCodes[i])) s->language = i;
    }
  }
  fclose(f);
  return true;
}

bool ScSettingsSave(const ScSettings *s, const char *path) {
  char tmp[512];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "w");
  if (!f) return false;
  fprintf(f, "; Urban Recomp settings, written by the launcher.\n"
             "; Environment variables set by hand take precedence.\n"
             "[Launcher]\nrom=%s\nskip_launcher=%d\n"
             "[Display]\nwindow_scale=%d\nfullscreen=%d\nlinear_filter=%d\nwidescreen=%d\n"
             "[Audio]\nenable_audio=%d\n"
             "[Game]\nlanguage=%s\nsylt=%d\nlarge_maps=%d\n",
          s->rom, s->skip_launcher, s->window_scale, s->fullscreen,
          s->linear_filter, s->widescreen, s->enable_audio,
          kLanguageCodes[clamp(s->language, 0, SC_LANG_COUNT - 1)], s->sylt, s->large_maps);
  bool ok = fclose(f) == 0;
  if (ok) {
    remove(path);
    ok = rename(tmp, path) == 0;
  }
  return ok;
}

static void set_default_env(const char *name, const char *value) {
  const char *have = getenv(name);
  if (have && *have) return;
#ifdef _WIN32
  _putenv_s(name, value);
#else
  setenv(name, value, 0);
#endif
}

void ScSettingsApply(const ScSettings *s) {
  set_default_env("SC_WIDESCREEN", s->widescreen ? "96" : "0");
  set_default_env("SC_NINTH", s->sylt ? "1" : "0");
  set_default_env("SC_LARGE_MAPS", s->large_maps==3 ? "3" : s->large_maps==2 ? "2" : s->large_maps ? "1" : "0");
  if (s->language != SC_LANG_ENGLISH) {
    char blob[64], packets[64];
    translation_files(s->language, blob, sizeof blob, packets, sizeof packets);
    if (file_exists(blob) && file_exists(packets)) {
      set_default_env("SC_TRANSLATION", blob);
      set_default_env("SC_PACKET_PATCH", packets);
    } else {
      fprintf(stderr, "settings: %s needs %s and %s next to the game; "
                      "starting in English\n", kLanguageLabels[s->language],
              blob, packets);
    }
  }
  fprintf(stderr, "settings: widescreen=%d language=%s sylt=%d scale=%d "
                  "fullscreen=%d audio=%d\n", s->widescreen,
          kLanguageCodes[s->language], s->sylt, s->window_scale,
          s->fullscreen, s->enable_audio);
}

#ifndef RECOMP_LAUNCHER

int ScLauncherRun(ScSettings *s, const char *settings_path,
                  ScVideoSettings *video, const char *video_path) {
  (void)s; (void)settings_path; (void)video; (void)video_path;
  return -1;
}
bool ScKeybindsInit(void) { return false; }
unsigned ScKeybindsRead(const unsigned char *keys) { (void)keys; return 0; }
bool ScKeybindsUses(int scancode) { (void)scancode; return false; }
#else /* RECOMP_LAUNCHER */

/* ── keyboard bindings ─────────────────────────────────────────────────── */

/* recomp-ui's button order: a b x y l r start select up down left right,
 * then l2 r2 l3 r3. This host's pad bits, in the same order. */
static const unsigned kPadBit[12] = {
  0x0100, 0x0001, 0x0200, 0x0002, 0x0400, 0x0800,
  0x0008, 0x0004, 0x0010, 0x0020, 0x0040, 0x0080,
};
static bool s_keybinds;

bool ScKeybindsInit(void) {
  if (s_keybinds) return true;
  const bool first_run = !file_exists("keybinds.ini");
  recompui_keybinds_init(NULL);
  if (first_run) {
    /* This host's layout, by key LABEL (so Y is Y on a QWERTZ keyboard):
     * S=A, X=B, A=X, Y=Y, Q=L, W=R, Return=Start, B=Select, arrows. */
    const SDL_Scancode layout[16] = {
      sc_scancode_from_key(SDLK_s), sc_scancode_from_key(SDLK_x),
      sc_scancode_from_key(SDLK_a), sc_scancode_from_key(SDLK_y),
      sc_scancode_from_key(SDLK_q), sc_scancode_from_key(SDLK_w),
      SDL_SCANCODE_RETURN, sc_scancode_from_key(SDLK_b),
      SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
      SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
      SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    };
    const int n = recompui_keybinds_button_count();
    for (int player = 1; player <= 2; player++)
      for (int b = 0; b < n && b < 16; b++)
        recompui_keybinds_set_button(player, b, layout[b]);
    recompui_keybinds_save();
  }
  s_keybinds = true;
  return true;
}

unsigned ScKeybindsRead(const unsigned char *keys) {
  if (!s_keybinds || !keys) return 0;
  unsigned pad = 0;
  for (int b = 0; b < 12; b++) {
    const SDL_Scancode sc = recompui_keybinds_get_button(1, b);
    if (sc != SDL_SCANCODE_UNKNOWN && keys[sc]) pad |= kPadBit[b];
  }
  return pad;
}

bool ScKeybindsUses(int scancode) {
  if (!s_keybinds) return false;
  for (int b = 0; b < 12; b++)
    if ((int)recompui_keybinds_get_button(1, b) == scancode) return true;
  return false;
}

/* ── OpenGL 3 probe ────────────────────────────────────────────────────── */

/* recomp-ui draws with OpenGL 3.3 and does not check that it got it: on a
 * host with only Windows' GDI OpenGL 1.1 (a Hyper-V VM without a GPU) it
 * printed "Failed to initialize OpenGL loader!" and crashed. So ask SDL for
 * a 3.3 core context first and skip the launcher when there is none. */
static bool gl3_available(void) {
  SDL_GL_ResetAttributes();
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_Window *w = snesrecomp_sdl_create_window(
      "Urban Recomp", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  bool ok = false;
  if (w) {
    SDL_GLContext ctx = SDL_GL_CreateContext(w);
    if (ctx) {
      /* SDL_GL_GetAttribute only echoes the request, and Windows can hand
       * out a context that is no 3.3 at all -- the first version of this
       * probe passed on exactly the host it was written for. Ask the
       * context itself. */
      typedef const unsigned char *(*GetStringFn)(unsigned int);
      GetStringFn get_string = (GetStringFn)SDL_GL_GetProcAddress("glGetString");
      const char *version = NULL, *renderer = NULL;
#if SNESRECOMP_SDL3
      const bool current = SDL_GL_MakeCurrent(w, ctx);
#else
      const bool current = SDL_GL_MakeCurrent(w, ctx) == 0;
#endif
      if (get_string && current) {
        version = (const char *)get_string(0x1F02);    /* GL_VERSION */
        renderer = (const char *)get_string(0x1F01);   /* GL_RENDERER */
      }
      int major=0, minor=0;
      ok = version && sscanf(version, "%d.%d", &major, &minor)==2 &&
           (major>3 || (major==3 && minor>=3));
      fprintf(stderr, "launcher: OpenGL %s (%s)\n", version ? version : "?",
              renderer ? renderer : "?");
      SDL_GL_MakeCurrent(w, NULL);
#if SNESRECOMP_SDL3
      SDL_GL_DestroyContext(ctx);
#else
      SDL_GL_DeleteContext(ctx);
#endif
    }
    SDL_DestroyWindow(w);
  }
  SDL_GL_ResetAttributes();
  return ok;
}

/* ── the Sylt scenario, as the launcher's one mod ──────────────────────── */

static ScSettings *s_mod_settings;
static const RecompLauncherCModProvider *s_adaptive;
#define COPY(field, text) snprintf(field, sizeof(field), "%s", text)

static int is_sylt(const char *p, const char *f) {
  return p && f && !strcmp(p, "sc-sylt") && !strcmp(f, "sylt");
}
static int one(void *ctx) { (void)ctx; return 2; }
static int package_get(void *ctx, int i, RecompLauncherCModPackage *out) {
  if (i == 0) return s_adaptive->package_get(ctx, 0, out);
  --i;
  (void)ctx;
  if (i || !out) return 0;
  memset(out, 0, sizeof *out);
  COPY(out->id, "sc-sylt");
  COPY(out->name, "Sylt");
  COPY(out->version, "1");
  COPY(out->author, "Urban Recomp contributors");
  COPY(out->description, "The island of Sylt as a ninth scenario after Las Vegas.");
  out->enabled = s_mod_settings->sylt;
  return 1;
}
static int feature_get(void *ctx, int i, RecompLauncherCModFeature *out) {
  if (i == 0) return s_adaptive->feature_get(ctx, 0, out);
  --i;
  (void)ctx;
  if (i || !out) return 0;
  memset(out, 0, sizeof *out);
  COPY(out->id, "sylt");
  COPY(out->package_id, "sc-sylt");
  COPY(out->package_name, "Sylt");
  COPY(out->package_version, "1");
  COPY(out->name, "Sylt scenario");
  COPY(out->group, "Scenarios");
  COPY(out->author, "Urban Recomp contributors");
  COPY(out->description, "Adds Sylt, 2047-2057, as the ninth card on the scenario "
                         "selector. Goal: a city score of 500 and 10,000 people.");
  out->enabled = s_mod_settings->sylt;
  out->option_count = 0;
  COPY(out->status, s_mod_settings->sylt ? "Enabled" : "Disabled");
  return 1;
}
static int no_option(void *ctx, const char *p, const char *f, int i,
                     RecompLauncherCModOption *out) {
  return s_adaptive->feature_option_get(ctx, p, f, i, out);
}
static int no_choice(void *ctx, const char *p, const char *f, const char *o, int i,
                     RecompLauncherCModChoice *out) {
  return s_adaptive->feature_choice_get(ctx, p, f, o, i, out);
}
static int enable(void *ctx, const char *p, const char *f, int on) {
  (void)ctx;
  if (!is_sylt(p, f)) return s_adaptive->feature_enable(ctx, p, f, on);
  s_mod_settings->sylt = on != 0;
  return 1;
}
static int set_option(void *ctx, const char *p, const char *f, const char *o,
                      const char *v) {
  return s_adaptive->feature_set_option(ctx, p, f, o, v);
}
static int commit(void *ctx, const char *image) {
  (void)ctx; (void)image;
  return s_adaptive->commit(ctx, image);
}
static const char *last_error(void *ctx) { return s_adaptive->last_error(ctx); }

static const RecompLauncherCModProvider *sylt_provider(ScSettings *s) {
  static RecompLauncherCModProvider p;
  s_mod_settings = s;
  memset(&p, 0, sizeof p);
  p.package_count = one;
  p.package_get = package_get;
  p.feature_count = one;
  p.feature_get = feature_get;
  p.feature_option_get = no_option;
  p.feature_choice_get = no_choice;
  p.feature_enable = enable;
  p.feature_set_option = set_option;
  p.commit = commit;
  p.last_error = last_error;
  p.archive_extension = ".snesmod";
  p.archive_description = "SNESRecomp mod package";
  return &p;
}

/* ── the launcher ─────────────────────────────────────────────────────── */

int ScLauncherRun(ScSettings *s, const char *settings_path,
                  ScVideoSettings *video, const char *video_path) {
#if SNESRECOMP_SDL3
  const bool video_ready = SDL_InitSubSystem(SDL_INIT_VIDEO);
#else
  const bool video_ready = SDL_InitSubSystem(SDL_INIT_VIDEO) == 0;
#endif
  if (!video_ready) {
    fprintf(stderr, "launcher: no video (%s); starting the game\n", SDL_GetError());
    return -1;
  }
  ScKeybindsInit();
  if (!gl3_available()) {
    fprintf(stderr, "launcher: this system has no OpenGL 3.3, which the "
                    "launcher draws with; starting the game with the saved "
                    "settings (%s)\n", settings_path);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return -1;
  }

  static const uint8_t kUsSha256[1][32] = {{
    0xe9, 0xc0, 0xbc, 0x05, 0x51, 0x1e, 0x05, 0xa0, 0xd7, 0xc3, 0xe7, 0xcc,
    0x42, 0xe7, 0x61, 0xe1, 0xe8, 0xe5, 0x32, 0xd4, 0x6f, 0x59, 0xb9, 0x85,
    0x4b, 0x69, 0x02, 0xe1, 0xa2, 0xe9, 0xdd, 0x0a }};
  RecompLauncherCGameInfo game;
  memset(&game, 0, sizeof game);
  launcher_profile_apply("snes", &game);
  game.name = "Urban Recomp";
  game.region = "USA";
  game.platform = "SUPER NINTENDO";
  game.num_players = 1;
  game.known_sha256 = kUsSha256;
  game.num_known_sha256 = 1;
  game.expected_crc = 0x8aedd3a1u;
  game.has_expected_crc = 1;
  game.widescreen_supported = 1;
  game.language_labels = kLanguageLabels;
  game.num_languages = SC_LANG_COUNT;
  game.lock_device = 0;
#if RECOMP_UI_ENABLE_MODS
  s_adaptive = ScModsProvider(video, video_path);
  game.mods = sylt_provider(s);
#endif

  RecompLauncherCSettings io;
  memset(&io, 0, sizeof io);
  io.window_scale = s->window_scale;
  io.fullscreen = s->fullscreen;
  io.linear_filter = s->linear_filter;
  io.widescreen = s->widescreen;
  io.enable_audio = s->enable_audio;
  io.audio_freq = 32040;
  io.volume = 100;
  io.language_index = s->language;
  io.skip_launcher = s->skip_launcher;
  io.player_src[0] = 1;   /* keyboard */

#if SNESRECOMP_SDL3
  const char *base = SDL_GetBasePath();
#else
  char *base = SDL_GetBasePath();
#endif
  char icon[1024];
  snprintf(icon, sizeof icon, "%s%s", base ? base : "", kIconAsset);
  game.boxart_path = "assets/img/boxart.jpg";
  game.window_icon_path = icon;
  char out_rom[sizeof s->rom];
  out_rom[0] = 0;
  const int rc = recomp_launcher_run_window("Urban Recomp", &io, &game,
                                            base ? base : ".", s->rom,
                                            out_rom, sizeof out_rom);
#if !SNESRECOMP_SDL3
  SDL_free(base);
#endif
  if (rc == RECOMP_LAUNCHER_RESULT_QUIT) return 0;
  if (rc != RECOMP_LAUNCHER_RESULT_LAUNCH || !out_rom[0]) {
    fprintf(stderr, "launcher: unavailable (%d); starting the game\n", rc);
    return -1;
  }
  snprintf(s->rom, sizeof s->rom, "%s", out_rom);
  s->window_scale = clamp(io.window_scale, 1, 8);
  s->fullscreen = clamp(io.fullscreen, 0, 2);
  s->linear_filter = io.linear_filter != 0;
  s->widescreen = io.widescreen != 0;
  s->enable_audio = io.enable_audio != 0;
  s->language = clamp(io.language_index, 0, SC_LANG_COUNT - 1);
  s->skip_launcher = io.skip_launcher != 0;
  if (!ScSettingsSave(s, settings_path))
    fprintf(stderr, "launcher: could not save %s\n", settings_path);
  return 1;
}

#endif /* RECOMP_LAUNCHER */
