/* main.c -- UrbanRecomp desktop host.
 *
 * Generated native C and high-level simulation helpers run over the original
 * device models and register/save ABI. The default target omits the 65816
 * interpreter; SC_INTERPRETER_REFERENCE explicitly links the independent
 * oracle for migration controls. Game hooks and beam/APU events remain
 * observable at original instruction boundaries. See NATIVE_EXECUTION.md.
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <limits.h>

/* Shared SDL2/SDL3 include boundary. SNESRECOMP_SDL3 is set by
 * snesrecomp_target_sdl() in CMakeLists.txt; the shim pulls in the right
 * SDL and turns on the transitional old-name aliases so constants and types
 * keep their SDL2 spellings. Calls whose SIGNATURES changed are handled
 * explicitly at their call sites -- see runner/src/desktop/mmx23_host_main.inc
 * for how upstream does each one. */
#include "sc_sdl_compat.h"
#include "sc_macos.h"
#include "sc_gpu_terrain.h"
#include "sc_gpu_fields.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
/* SDL3 switched the renderer rect APIs from SDL_Rect (int) to SDL_FRect
 * (float). SDL_ENABLE_OLD_NAMES preserves the NAMES but not the signatures,
 * so passing an SDL_Rect* to SDL_RenderFillRect under SDL3 reinterprets four
 * ints as two floats: the rect lands somewhere meaningless and the overlay
 * silently does not appear. That is what hid the F10 settings menu -- it was
 * toggling (the OPEN/CLOSED log proves it) and drawing off-screen. */
#if SNESRECOMP_SDL3
typedef SDL_FRect ScRect;
#define SC_RECT(x, y, w, h) ((ScRect){ (float)(x), (float)(y), (float)(w), (float)(h) })
#else
typedef SDL_Rect ScRect;
#define SC_RECT(x, y, w, h) ((ScRect){ (int)(x), (int)(y), (int)(w), (int)(h) })
#endif

/* SDL3 returns true on success where SDL2 returned 0. */
#if SNESRECOMP_SDL3
#define SC_SDL_OK
#else
#define SC_SDL_OK == 0
#endif

#include "snes/snes.h"
#include "snes/apu.h"
#include "snes/dsp.h"
#include "snes/dsp_shadow.h"
#include "snes/spc.h"
#include "snes/ppu.h"
#include "snes/dma.h"
#include "snes/cart.h"
#include "snes/dsp1.h"
#include "snes/interp816.h"
#include "sc_program.h"
#include "types.h"

/* â”€â”€ globals the shared runner device sources reference â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
/* SC_AOT_TIER: built as part of the AOT/CpuState migration (see
 * src/aot_probe.c and the UrbanRecompAOT target). In that build the
 * shared runtime is linked in, and it already defines several of the symbols
 * this file provides for the standalone interpreter build. They are the same
 * objects with the same meaning -- common_rtl.c's g_ram is a 0x20000 array
 * with identical $7E/$7F semantics, and this host passes g_ram straight to
 * snes_init(), so both tiers end up sharing one WRAM array rather than
 * needing any copying between them. Defining them here too would just be a
 * duplicate symbol, so the AOT build defers to the runtime's copies. */
#ifdef SC_AOT_TIER
#include "common_rtl.h"
#include "common_rtl.h"          /* extern uint8 g_ram[0x20000]; */
#include "sc_fiberdrive.h"
#else
uint8_t    g_ram[0x20000];
#endif
/* OUTSIDE the AOT guard, deliberately. Placed inside it, the plain build never
 * saw the prototype, implicitly declared ScMapView_Render as returning `int`,
 * and read all of EAX where the callee had only set AL -- so a function that
 * returned false was observed as true, and the US-only gate silently passed on
 * a German ROM. It compiled and linked without a word. */
#include "sc_mapview.h"
#include "sc_launcher.h"
#include "sc_sram.h"
#include "sc_icon.h"
#include "sc_vehicles.h"
#include "sc_fleet.h"
static ScFleet *s_fleet;
#include "sc_selector.h"
#include "sc_titlesign.h"
#include "sc_mapgen.h"
#include "sc_decomp.h"
#include "sc_development.h"
#include "sc_development_batches.h"
#include "sc_math.h"
#include "sc_tile_lookup.h"
#include "sc_wait.h"
#include "sc_sprite.h"
#include "sc_postpass.h"
#include "sc_sweep.h"
#include "sc_music.h"
#include "sc_beam.h"
#include "sc_ppu.h"
#include "sc_population.h"
#include "sc_mouse_ui.h"
#include "sc_construction.h"
#include "sc_clipboard.h"
#include "sc_power_refresh.h"
#include "sc_power_traversal.h"
#include "sc_world_guest.h"
#include "sc_journey.h"
#include "sc_test_city.h"
static ScWorld s_world;
static ScWorldGuest s_world_guest;
static bool s_perf_detail;
static double s_perf_clock_ms,s_perf_raster_ms,s_perf_power_ms;
static uint64_t s_perf_spatial_cells;
static uint64_t s_perf_native_development_calls;
static bool s_skip_custom_frame;
static bool s_wait_lane_enabled;
static bool s_measure_custom_frame;
static double s_custom_frame_ms;
static double s_perf_custom_ms,s_perf_native_ms;
static int s_large_maps; /* 0 Normal, 1 Big, 2 Huge, 3 960x800, 4 1920x1600, 5 3840x3200 */
static int s_terrain_style;
static bool s_journey_arming;
static unsigned s_journey_menu_selection;
static unsigned s_loading_slot;
static bool s_city_loading;
static ScMouseUiPointer s_ui_mouse_pointer;
static uint64_t s_preview_started;
static unsigned s_map_number_high;
static bool s_map_number_dirty;
static bool s_preview_expanded,s_preview_complete,s_preview_left_down,s_preview_click_owned,s_preview_input_blocked;
static double s_preview_cursor_x,s_preview_cursor_y;
static bool s_city_present_pending,s_city_fade_started,s_city_black_seen;
static bool s_test_revealed,s_test_load_pending,s_test_generate_pending,s_test_saving,s_test_swap;
static bool s_test_menu_pending;
static uint8_t s_test_native_backup[0x8000],s_test_names_backup[32];
static unsigned s_test_flags_backup;
static void test_city_restore_sram(void);
static bool s_save_dialog_pending,s_save_dialog_active;
static Interp816 s_save_dialog_return;
static unsigned s_save_dialog_phase;
static uint16_t s_save_dialog_page,s_save_dialog_x,s_save_dialog_y;
static unsigned s_escape_back_frames;
static uint16_t s_escape_back_input;
static bool s_size_selecting, s_speed_selecting, s_practice_size_pending;
static unsigned s_new_city_speed=1,s_speed_selection=1;
static const unsigned kCityDevelopmentSpeeds[]={1,3,5,10,20,50};
static unsigned s_size_game_choice;
static void save_large_map_setting(void);
static int s_scroll_multiplier=1;
static bool s_keyboard_pan_latched;
static unsigned scroll_key_multiplier(const uint8_t *keys) {
  if(!keys[SDL_SCANCODE_LCTRL] && !keys[SDL_SCANCODE_RCTRL])return 1;
  return keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]?10:3;
}
static struct {unsigned extra;uint16_t sp;bool repeating;} s_scroll_pass[2];
static char s_world_path[1100];
static void world_saved_city(bool save);
/* Declared, not #included: cpu_trace.h pulls in cpu_state.h, whose CpuState
 * collides with the interp816 core this target actually builds against.
 * Only present in a build configured with SNESRECOMP_TRACE=1. */
#if defined(SNESRECOMP_TRACE) && SNESRECOMP_TRACE
void cpu_trace_set_wram_watch(uint8_t bank, uint16_t addr, int width,
                              int match_value, uint8_t value, int enabled);
void cpu_trace_clear_wram_watches(void);
void cpu_trace_dump_wram(const char *tag, int scan_n);
#endif
#include "sc_renderer.h"
#ifdef RECOMP_LAUNCHER
#include "sc_mods.h"
#endif
static ScVideoSettings s_custom_video;
static bool s_fit_screen_requested;
static bool s_gpu_terrain_enabled;
static ScRenderer s_custom_renderer;
static const char *s_video_config = "sc-video.ini";
static int s_window_width = 1024, s_window_height = 768;
static ScVideoRect s_destination;
static ScBuildPlan s_build_plan;
static ScBuildWork *s_build_work;
static uint64_t s_build_started;
static bool s_build_pending, s_build_active, s_build_cancelled;
static int s_build_x0, s_build_y0, s_build_x1, s_build_y1;
static int s_build_scroll_x, s_build_scroll_y;
static unsigned s_build_tool;
static ScClipboard s_clipboard;
static unsigned s_clip_tool, s_clip_pending; /* 0 native, 1 Copy, 2 Paste */
static ScMousePan s_middle_pan;
static bool s_clip_drag, s_clip_hover;
static int s_clip_x0,s_clip_y0,s_clip_x1,s_clip_y1;
static int s_clip_scroll_x,s_clip_scroll_y;
static unsigned s_clip_native_tool;
static void commit_clipboard(void);
static bool s_map_mouse_accept_pending;
static bool s_map_mouse_refresh_pending;
static void commit_mouse_construction(void);
static void refresh_fast_power(bool bitmap_available);
static void ram_set_w(uint32_t a,uint16_t v);
static uint16_t ram_w(uint32_t a);
Snes      *g_snes;
Ppu       *g_ppu;
static Interp816 *g_cpu;
static uint64_t   g_master_cycles;
static uint64_t sc_audio_guest_cycle(void) {
  return (g_master_cycles/21477272u)*1025280u+
      (g_master_cycles%21477272u)*1025280u/21477272u;
}
static void sc_catchup_apu(Snes *snes) {
  if(ScMusicRunning()) {
    /* Preserve the guest clock's fraction while the audio worker owns SPC
     * synthesis. No mutex or sample production on the hot opcode path. */
    if(snes->apuCatchupCycles>=1.0) snes->apuCatchupCycles-=(uint64_t)snes->apuCatchupCycles;
  } else snes_catchupApu(snes);
}

/* â”€â”€ RTL glue the device sources call. This host bypasses common_rtl.c (the
 * AOT/CpuState runtime) entirely for Phase 1, so these are the same
 * minimal no-op/direct implementations snesrecomp's own reference driver
 * uses (cosim/ref_driver.c) rather than a game-specific reinterpretation. */
void RtlApuLock(void)   {ScMusicLock();}
void RtlApuUnlock(void) {ScMusicUnlock();}
void rtl_sync_apu_to_cpu_locked(void) {ScMusicSyncLocked(sc_audio_guest_cycle());}
void RtlApuWrite(uint16 adr, uint8 val) {
  if(ScMusicRunning()) ScMusicWrite(adr&3,val,sc_audio_guest_cycle());
  else g_snes->apu->inPorts[adr&3]=val;
}
void rtl_accumulate_apu_catchup(void) {}
void NORETURN Die(const char *e) { fprintf(stderr, "FATAL: %s\n", e ? e : "(null)"); exit(1); }
void debug_on_wram_write_byte(uint32_t a, uint8_t o, uint8_t n) { (void)a; (void)o; (void)n; }
void debug_on_wram_write_word(uint32_t a, uint16_t o, uint16_t n) { (void)a; (void)o; (void)n; }
bool g_fail = false;
uint8 g_snesrecomp_last_hdmaen;
/* Referenced by snes.c/interp_bridge.c; only meaningful once the AOT/
 * CpuState hybrid tier (interp_bridge.c) is wired in. 0 = not driving. */
#ifndef SC_AOT_TIER
/* All three are provided by the shared runtime in the AOT build:
 * g_interp_apu_driving and ppudma_record_dma by common_rtl.c /
 * ppu_dma_trace.c, interp816_opcode_hook by interp_bridge.c. */
int g_interp_apu_driving = 0;
void ppudma_record_dma(int ch, int fromB, uint8_t aBank, uint16_t aAdr,
                       uint8_t bAdr, uint16_t size) {
  (void)ch; (void)fromB; (void)aBank; (void)aAdr; (void)bAdr; (void)size;
}
int interp816_opcode_hook(uint32_t addr) { (void)addr; return 0; }
/* Upstream's snes.c now logs every direct WRAM write through
 * wlog_addr_note_direct(), which lives in cpu_state.c -- and this target
 * deliberately builds the interp816 core WITHOUT the CpuState runtime (see the
 * SC_DEVICE_SOURCES note in CMakeLists.txt).
 *
 * Stubbing it is safe here in a way that stubbing sc_advance_until_input_ready
 * would NOT have been: wlog_addr_note_via() returns immediately unless a WRAM
 * write-address log has been configured, so the real function is a diagnostic
 * and nothing else. This only means SNESRECOMP_WLOG_ADDR is unavailable in
 * this target; no emulation behaviour changes. */
void wlog_addr_note_direct(uint32_t wa, uint8_t v, const char *via) {
  (void)wa; (void)v; (void)via;
}
#else
/* Conversely, the runtime expects the GAME to supply these. The desktop
 * hosts define them in their host_main include; this host defines them here.
 * The APU locks are real work once the AOT tier drives audio -- for now this
 * host still owns the DSP drain loop single-threaded, so no-ops are correct
 * and must be revisited when that changes. */
/* Only g_spc_player is genuinely missing: common_rtl.h and debug_server.h
 * already carry inline bodies for the APU locks and the debug write hooks,
 * so defining those here is a redefinition, not a fill-in. (The aot_probe
 * target does need them, because it does not include those headers.) */
#include "spc_player.h"
SpcPlayer *g_spc_player = NULL;
/* debug_server.h declares but does not define this one (unlike the wram
 * write hooks); interp_bridge.c calls it per interpreted block. */
void debug_on_block_enter(uint32_t pc, uint32_t a, uint32_t x, uint32_t y)
  { (void)pc; (void)a; (void)x; (void)y; }
#endif
DspShadow *dsp_shadow_create(void) { return NULL; }
void dsp_shadow_free(DspShadow *sh) { (void)sh; }
void dsp_shadow_process(DspShadow *sh, Dsp *dsp, int cL, int cR, int *oL, int *oR) {
  (void)sh; (void)dsp; *oL = cL; *oR = cR;
}
void dsp_shadow_verify_brr(const uint8_t *aram, uint16_t bs, int a, int b, const int16_t *c) {
  (void)aram; (void)bs; (void)a; (void)b; (void)c;
}
void dsp_shadow_verify_echo(const int16_t *l, const int16_t *r, const int8_t *co,
                            int idx, int sL, int sR) {
  (void)l; (void)r; (void)co; (void)idx; (void)sL; (void)sR;
}

static uint64_t s_frames;
/* From sc-settings.ini (src/sc_launcher.c): 0 window, 1 borderless, 2 exclusive. */
static int  s_fullscreen;
static bool s_linear_filter;
static bool s_enable_audio = true;
static uint64_t s_nmi_requests;
static uint64_t s_nmi_serviced;

/* â”€â”€ debugging tools (env-gated, zero cost when unset) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 * SC_IO_TRACE=<end-frame>   log every joypad-register read/write ($4016/17,
 *                           $4200, $4218-421b) up to that frame.
 * SC_PC_TRACE=<frame>       on the first $4218 read at/after that frame,
 *                           dump the next 60 executed PCs + registers --
 *                           useful for finding what code just consumed a
 *                           controller read.
 * SC_DEBUG=<interval>       every <interval> frames (default 60), log
 *                           cpu.pc plus $011b (P1 held state), $01df
 *                           (screen-mode index), $0c0f (cursor-mover gate),
 *                           $020d (selected build tool) -- see
 *                           docs/INVESTIGATION_dpad.md.
 * SC_AOT_VARIANTS=<file>   with SC_FIBER, list the compiled variants the run
 *                           ENTERED, as `pc24:MmXn hits=N` -- the same key
 *                           the program manifest uses, so the two join
 *                           directly. An entry is NOT an extent: the body
 *                           then runs an unknown number of opcodes without
 *                           reporting them, so this must never be expanded
 *                           into an executed-PC bitmap.
 * SC_WRAM_DUMP_PC=<pc24>   with SC_WRAM_DUMP_DIR, fire each periodic WRAM
 *                           dump when the guest reaches that PC instead of at
 *                           the host frame boundary. Two hosts park the guest
 *                           in different places, so a host-frame-aligned dump
 *                           compares different guest moments -- see
 *                           docs/MIGRATION_step3.md 23.1.
 * SC_DUMP_AT=<frame> + SC_DUMP_PATH=<file.ppm>   write that frame's
 *                           rendered framebuffer as a P6 PPM.
 * SC_ADDR_TRACE=<bank:addr>[,<bank:addr>...][@<start-frame>]
 *                           log (rate-limited, 200 hits per address) every
 *                           time execution reaches any of the given
 *                           bank:addr PCs, with full registers -- the
 *                           general "is this code path ever reached, and
 *                           with what state" tool. Optional @<start-frame>
 *                           delays tracing until that frame.
 * SC_GFX_TRACE=1            log (rate-limited, 300 hits) every write to
 *                           BGMODE ($2105), the Mode 7 matrix/center regs
 *                           ($211b-$2114), HDMAEN ($420c), and every HDMA
 *                           channel's control/dest/addr regs ($43x0-$43xa)
 *                           -- for finding whether/how a screen sets up
 *                           Mode 7 + HDMA (e.g. the tilted "View" map).
 * SC_DECOMP_TRACE=1         log every call to the LC_LZ5 decompressor at
 *                           00:90dd -- source pointer, destination, and the
 *                           true output extent measured off the bus. This is
 *                           what settled the scenario-map format; see the
 *                           long comment above bus_read and
 *                           docs/REFERENCE_map_format.md.
 * SC_MAP_WRITE_TRACE=1      log writes into the live 24000-byte map buffer at
 *                           $7F0200, with a distinct-PC histogram at exit --
 *                           finds what fills the map without assuming which
 *                           routine does it.
 * SC_WRAM_MAP=<file>        map WRAM usage over a session: per byte, whether
 *                           it was read/written, the LAST PC to write it, and
 *                           a saturating write count. The data counterpart to
 *                           SC_PC_BITMAP_BANK -- see docs/ROM_MAP.md "WRAM
 *                           usage map".
 * SC_FRAME_BANK_TRACE=<start-frame>,<end-frame>
 *                           log the CPU's bank:PC at every frame boundary
 *                           in that range, unconditionally (not gated on
 *                           reaching any particular PC) -- for finding
 *                           what's actually executing during frames a
 *                           lower-priority per-frame task doesn't get a
 *                           turn on. This is what found bank $03 (the city
 *                           simulation tick) cooperatively pre-empting the
 *                           bank $01 cursor dispatcher for several frames
 *                           at a time -- see docs/INVESTIGATION_cursor_
 *                           cadence.md. PC-history alone can't answer this
 *                           kind of question: its ring buffer is far too
 *                           shallow (128 opcodes) to span multiple whole
 *                           frames of execution. */
static bool s_gfx_trace;
static uint32_t s_gfx_trace_hits;
static bool s_view_watch; /* cached SC_VIEW_WATCH check -- see bus_read/bus_write;
                            * getenv() is NOT cheap enough to call unconditionally
                            * on every single memory access (this is what caused a
                            * qualify hang/severe slowdown before being cached). */
static uint32_t s_dbg_live_hits;
static uint64_t s_io_trace_until;
static int s_pc_capture_after = -1;
static uint64_t s_pc_trace_at_frame;
#define SC_ADDR_TRACE_MAX 64
static uint32_t s_addr_trace_pcs[SC_ADDR_TRACE_MAX];   /* (bank<<16)|addr */
static uint32_t s_addr_trace_hits[SC_ADDR_TRACE_MAX];
static int s_addr_trace_count;
static uint64_t s_addr_trace_start_frame;
#define SC_PC_HISTORY_SIZE 128
static uint32_t s_pc_history[SC_PC_HISTORY_SIZE]; /* ring buffer of (bank<<16)|pc, one entry per executed opcode */
static int s_pc_history_head;
static int s_pc_history_filled;
/* SC_PC_BITMAP_BANK=<bank hex>|all + SC_PC_BITMAP_PATH=<file> [+ SC_PC_BITMAP_START=<frame>]:
 * records a 1-bit-per-address "was this PC ever executed" bitmap over the given
 * bank's 32KB ROM window ($8000-$ffff) -- or, with "all", over every bank
 * $00-$3F at once (16x4KB) -- dumped to file at exit (both --qualify and
 * windowed/interactive mode, so this also works live: launch, play, close the
 * window, the dump is written on exit same as qualify-exit). Meant to be
 * diffed between two runs (e.g. direction held vs not) to find exactly which
 * code paths differ, instead of guessing candidate addresses from static
 * disassembly. */
static int s_pc_bitmap_bank = -1; /* -1 = off, -2 = all banks, else single bank */
static uint64_t s_pc_bitmap_start_frame;
static uint8_t s_pc_bitmap[4096];
static uint8_t s_pc_bitmap_all[64][4096];

/* SC_MX_BITMAP=<path>: the same executed-PC bitmap, but split four ways by the
 * live (m,x) width flags. The point is the exit-M/X fixpoint that blocks the
 * remaining ~5% of AOT coverage (docs/OPEN_QUESTIONS.md A1): the analyzer
 * cannot prove what widths a callee returns in, but the machine knows -- the
 * widths observed at a call's *return address* are exactly the callee's exit
 * widths. Recording (pc, m, x) turns that from a proof obligation into a
 * measurement, which is the same move the execution bitmap already made for
 * "is this address code".
 *
 * Deliberately separate from s_pc_bitmap_all rather than replacing it: the
 * existing bitmaps are the basis of every coverage number in the README, and
 * silently changing their format would invalidate the ones already on disk. */
static uint8_t (*s_mx_bitmap)[64][4096];   /* [mx][bank][byte], mx = m<<1 | x */
static const char *s_mx_bitmap_path;
static uint64_t s_banks_seen; /* bit N set if bank N ever held cpu->k (diagnostic only) */

/* SC_WRAM_MAP=<file>: build a live map of WRAM usage over a play session --
 * which of the 128KB is read, which is written, and *which PC first wrote
 * each byte*.
 *
 * The PC-execution bitmap (SC_PC_BITMAP_BANK) answers "which routines run".
 * This is its counterpart for data: it answers "which variables exist, and
 * who owns them". docs/ROM_MAP.md's WRAM table was built one address at a
 * time from targeted investigations; this produces the whole picture in one
 * session, and the first-writer PC turns an anonymous address into a lead --
 * find the routine, and you have the variable's meaning.
 *
 * Records the LAST writer, not the first. Measured: the boot path writes all
 * 131072 bytes of WRAM (it clears the lot), so a first-writer map is entirely
 * owned by the clear loop and carries no signal whatsoever. The last writer
 * after a play session is the routine that actually maintains the byte.
 * A saturating write count comes along too, which separates hot per-frame
 * state from something touched once at init.
 *
 * Dumped at exit as a flat binary: 0x20000 flag bytes (bit0 read, bit1
 * written), then 0x20000 little-endian uint32 last-writer PCs (0xFFFFFFFF
 * where never written), then 0x20000 little-endian uint16 write counts.
 * Cheap enough to leave on for a whole session: a few array stores per bus
 * access. */
/* SC_WRAM_DUMP_PC=<pc24>: take the periodic WRAM dump at a GUEST-defined
 * moment rather than a host-defined one.
 *
 * Comparing two hosts at "the same frame" is confounded, because they park the
 * guest in different places. Measured at frame 600: the fiber host leaves the
 * guest at 00:9311 with S=$1FF5 -- its yield point, by construction -- while
 * the per-opcode host is at 00:8F1F with S=$1FEA. Eleven bytes of call depth
 * apart and in unrelated code, so stack residue and direct-page scratch differ
 * for reasons that have nothing to do with either host being wrong.
 *
 * Arming the dump on a PC makes both hosts sample the same guest moment. Fiber
 * mode ignores it and dumps as usual: it is already parked at 00:9311 when the
 * frame ends. */
static bool write_wram_dump(const char *path);
static uint32_t s_dump_pc24 = 0xffffffffu;
static bool     s_dump_pc_armed;
static uint64_t s_dump_pc_frame;
static char     s_dump_pc_dir[400];

static bool s_wram_map;
static uint8_t *s_wram_flags;      /* [0x20000] bit0 = read, bit1 = written */
static uint32_t *s_wram_last_pc;   /* [0x20000] last writer, (bank<<16)|pc */
static uint16_t *s_wram_wcount;    /* [0x20000] saturating write count */

/* WRAM offset for a 24-bit bus address, or -1 if it is not WRAM.
 * $7E/$7F are direct; banks $00-$3F and $80-$BF mirror $7E0000-$7E1FFF at
 * $0000-$1FFF, which is where nearly every variable this project has named
 * actually lives. */
static inline int wram_offset(uint32_t adr) {
  uint8_t bank = (uint8_t)(adr >> 16);
  uint16_t off = (uint16_t)adr;
  if (bank == 0x7e) return off;
  if (bank == 0x7f) return 0x10000 + off;
  if ((bank < 0x40 || (bank >= 0x80 && bank < 0xc0)) && off < 0x2000) return off;
  return -1;
}

static void wram_map_note(uint32_t adr, bool write) {
  int o = wram_offset(adr);
  if (o < 0) return;
  if (!write) { s_wram_flags[o] |= 0x01; return; }
  s_wram_flags[o] |= 0x02;
  s_wram_last_pc[o] = ((uint32_t)g_cpu->k << 16) | g_cpu->pc;
  if (s_wram_wcount[o] != 0xffff) s_wram_wcount[o]++;
}

static void write_wram_map(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "failed to open %s\n", path); return; }
  fwrite(s_wram_flags, 1, 0x20000, f);
  fwrite(s_wram_last_pc, 4, 0x20000, f);
  fwrite(s_wram_wcount, 2, 0x20000, f);
  fclose(f);
  uint32_t r = 0, w = 0, hot = 0;
  for (int i = 0; i < 0x20000; i++) {
    if (s_wram_flags[i] & 1) r++;
    if (s_wram_flags[i] & 2) w++;
    if (s_wram_wcount[i] > 16) hot++;
  }
  fprintf(stderr, "[wrammap] read=%u written=%u hot(>16 writes)=%u of 131072 -> %s\n",
          r, w, hot, path);
}

/* SC_DECOMP_TRACE=1: instrument the LC_LZ5 decompressor at 00:90dd, logging
 * one line per call with its source pointer, its output range, and how many
 * bytes it actually produced.
 *
 * This exists to settle the scenario-map compression question (see
 * docs/REFERENCE_map_format.md): the eight scenario map pointers in the
 * struct-of-arrays table at 03:ce70 are known, but the format they're stored
 * in is not, and two static guesses (LC_LZ5 at the pointer itself; a raw
 * 16-bit cell array) have already been tested and failed. Rather than guess a
 * third time, watch the load path do it. Correct output is known exactly --
 * 24000 bytes (120x100 cells x 2) of 10-bit indices -- so a call whose output
 * length is 24000 is the map load, whatever its source turns out to be.
 *
 * Calling convention, decoded by hand from 00:90dd (the disassembler
 * misaligns here: SEP #$20 makes A 8-bit, so `c9 ff` at 00:9102 is CMP #$ff,
 * the terminator test, not a 16-bit compare):
 *
 *   00:90dd  PHP / PHB / SEP #$20 / REP #$10
 *   00:90e3  LDA $000b ; PHA ; PLB     ; DB = source bank
 *   00:90eb  LDX $000e                 ; X   = output index
 *   00:90ef  LDY $0009                 ; Y   = source offset
 *   ...      STA $7e8000,X             ; output base is $7E8000, not $7E0000
 *   00:9106  PLB / PLP / RTS           ; reached on the $ff terminator
 *
 * so the source is $0b:$0009 and the destination is $7E8000 + $000e. Note
 * $7E8000 + a 16-bit X spans up to $7F7FFF, which does reach the live map
 * buffer at $7F0200 ($7E8000 + $8200).
 *
 * Entry is sampled at 00:90eb rather than 00:90dd so cpu->db is already the
 * source bank: `LDA $000b` at 00:90e3 runs under the *caller's* DB, so the
 * post-PLB register is the authoritative source bank, not our own read of
 * $000b. The bank-cross helper at 00:926d bumps the DB register (and resets Y
 * to $8000) without updating $000b, so the exit sample must come from cpu->db
 * too -- taken at 00:9106, before PLB restores the caller's bank. */
static bool s_decomp_trace;
static bool s_decomp_active;
static uint32_t s_decomp_src;       /* (bank<<16)|offset, sampled at entry */
static uint32_t s_decomp_out_base;  /* $7e8000 + $000e, sampled at entry */
static uint32_t s_decomp_wmin, s_decomp_wmax; /* observed output extent */
static uint32_t s_decomp_wcount;
static uint32_t s_decomp_calls;

/* SC_MAP_WRITE_TRACE=1: watch every write into the live map buffer
 * ($7F0200..$7F607F, 120x100x2 bytes) and report which PCs produce it.
 *
 * Complements the decompressor trace above rather than duplicating it: it
 * makes no assumption that 00:90dd is what fills the map. If the scenario
 * maps are unpacked by some other routine entirely, this finds it, and if
 * nothing writes the region at all then the map lives somewhere other than
 * where the Lua viewer reads it. Rate-limited to the first few writes with
 * full PCs, plus a distinct-PC histogram at exit, since a full map fill is
 * 24000 writes. */
#define kMapBufStart 0x7f0200u
#define kMapBufEnd   (0x7f0200u + 24000u)
static bool s_map_write_trace;
static uint32_t s_map_write_hits;
#define SC_MAP_WRITE_PCS 16
static struct { uint32_t pc; uint32_t count; } s_map_write_pcs[SC_MAP_WRITE_PCS];
static int s_map_write_pc_count;

static uint8_t bus_read(void *mem, uint32_t adr) {
  (void)mem;
  uint8_t world_value;
  if (ScWorldGuestRead(&s_world_guest, adr, &world_value)) return world_value;
  if (ScJourneyMenuRead(adr,g_ram[0x14],&world_value) ||
       (s_world.journey_announcing && ScJourneyMessageRead(s_world.journey_notice,adr,&world_value)))
    return world_value;
  uint8_t v = snes_read(g_snes, adr);
  if (s_wram_map) wram_map_note(adr, false);
  uint16_t reg = (uint16_t)adr;
  uint8_t bank = (uint8_t)(adr >> 16);
  bool hw = bank < 0x40 || (bank >= 0x80 && bank < 0xc0);
  if (s_io_trace_until && s_frames < s_io_trace_until && hw &&
      (reg == 0x4016 || reg == 0x4017 || reg == 0x4200 ||
       reg == 0x4218 || reg == 0x4219 || reg == 0x421a || reg == 0x421b))
    fprintf(stderr, "[io f=%llu] READ  %04x = %02x\n",
            (unsigned long long)s_frames, reg, v);
  if (hw && reg == 0x4218 && s_pc_capture_after == -2 && s_frames >= s_pc_trace_at_frame) {
    s_pc_capture_after = 60;
    fprintf(stderr, "[pctrace f=%llu] $4218 read = %02x -- capturing next 60 PCs\n",
            (unsigned long long)s_frames, v);
  }
  /* Gated behind its own SC_CADENCE_WATCH flag, not just s_addr_trace_count
   * -- this used to piggyback on any SC_ADDR_TRACE use at all, which meant
   * tracing something unrelated (e.g. a task-scheduler lead far from the
   * cadence investigation this was built for) silently also turned on
   * hundreds of lines/frame of unrelated $011b/$011c read spam, tanking
   * framerate badly enough to make the window unplayable long before
   * reaching whatever screen was actually being tested. */
  if (s_addr_trace_count && s_frames >= s_addr_trace_start_frame &&
      getenv("SC_CADENCE_WATCH") &&
      (reg == 0x011b || reg == 0x011c)) {
    static uint32_t s_read_watch_hits;
    if (s_read_watch_hits < 400) {
      fprintf(stderr, "[readwatch f=%llu] pc=%02x:%04x READ $%04x = %02x\n",
              (unsigned long long)s_frames, g_cpu->k, g_cpu->pc, reg, v);
      s_read_watch_hits++;
    }
  }
  /* SC_VIEW_WATCH=1: live read watch on $7e21b4/$7e21b5 (the View screen's
   * D-pad-adjusted position -- write side confirmed working, but nothing
   * visibly renders; see docs/INVESTIGATION_dpad.md "Open item"). Catches
   * *every* addressing mode (including dynamic/indirect), unlike a static
   * opcode-pattern scan, which only found the value's own read-modify-
   * write increment, not a genuine external consumer. */
  if (s_view_watch && bank == 0x7e && (reg == 0x21b4 || reg == 0x21b5)) {
    static uint32_t s_view_watch_hits;
    if (s_view_watch_hits < 400) {
      fprintf(stderr, "[viewwatch f=%llu] pc=%02x:%04x READ $7e%04x = %02x\n",
              (unsigned long long)s_frames, g_cpu->k, g_cpu->pc, reg, v);
      s_view_watch_hits++;
    }
  }
  return v;
}
static void bus_write(void *mem, uint32_t adr, uint8_t v) {
  (void)mem;
  if (s_wram_map) wram_map_note(adr, true);
  uint16_t reg = (uint16_t)adr;
  uint8_t bank = (uint8_t)(adr >> 16);
  bool hw = bank < 0x40 || (bank >= 0x80 && bank < 0xc0);
  if (s_io_trace_until && s_frames < s_io_trace_until && hw &&
      (reg == 0x4016 || reg == 0x4017 || reg == 0x4200 ||
       reg == 0x4218 || reg == 0x4219 || reg == 0x421a || reg == 0x421b))
    fprintf(stderr, "[io f=%llu] WRITE %04x = %02x\n",
            (unsigned long long)s_frames, reg, v);
  /* Output-extent tracking for SC_DECOMP_TRACE. Deliberately measured from
   * the bus rather than from X at the RTS: it captures what the routine
   * really wrote, including any bank-crossing past $7E:ffff, and needs no
   * assumption about which register holds the final output index. */
  if (s_decomp_active && (bank == 0x7e || bank == 0x7f)) {
    if (adr < s_decomp_wmin) s_decomp_wmin = adr;
    if (adr > s_decomp_wmax) s_decomp_wmax = adr;
    s_decomp_wcount++;
  }
  if (s_map_write_trace && adr >= kMapBufStart && adr < kMapBufEnd) {
    uint32_t pc = ((uint32_t)g_cpu->k << 16) | g_cpu->pc;
    if (s_map_write_hits < 12)
      fprintf(stderr, "[mapwrite f=%llu] pc=%02x:%04x $%06x = %02x\n",
              (unsigned long long)s_frames, g_cpu->k, g_cpu->pc, adr, v);
    s_map_write_hits++;
    int i = 0;
    for (; i < s_map_write_pc_count; i++)
      if (s_map_write_pcs[i].pc == pc) { s_map_write_pcs[i].count++; break; }
    if (i == s_map_write_pc_count && s_map_write_pc_count < SC_MAP_WRITE_PCS) {
      s_map_write_pcs[s_map_write_pc_count].pc = pc;
      s_map_write_pcs[s_map_write_pc_count].count = 1;
      s_map_write_pc_count++;
    }
  }
  if (s_view_watch && bank == 0x7e && (reg == 0x21b4 || reg == 0x21b5)) {
    static uint32_t s_view_write_hits;
    if (s_view_write_hits < 400) {
      fprintf(stderr, "[viewwatch f=%llu] pc=%02x:%04x WRITE $7e%04x = %02x\n",
              (unsigned long long)s_frames, g_cpu->k, g_cpu->pc, reg, v);
      s_view_write_hits++;
    }
  }
  if (s_addr_trace_count && s_frames >= s_addr_trace_start_frame &&
      getenv("SC_CADENCE_WATCH") &&
      (reg == 0x01eb || reg == 0x01ec || reg == 0x01ed || reg == 0x01ee || reg == 0x007c)) {
    static uint32_t s_wram_watch_hits;
    if (s_wram_watch_hits < 200) {
      fprintf(stderr, "[wramwrite f=%llu] pc=%02x:%04x $%04x = %02x\n",
              (unsigned long long)s_frames, g_cpu->k, g_cpu->pc, reg, v);
      s_wram_watch_hits++;
    }
  }
  /* Dedicated $011b/$011c write watch (separate counter from the mouse-
   * cursor watch above so the two don't compete for the same hit budget).
   * Added to find what, if anything, writes over the edge-detector's
   * (00:92c7) correct per-frame data in between its write and the
   * fast-travel modifier check (01:c01e) reading it as zero four frames
   * out of five -- see docs/INVESTIGATION_dpad.md "Fast travel". Paired
   * with the existing readwatch above (same two addresses), so a single
   * SC_CADENCE_WATCH run gives every read AND write to both bytes, in
   * order, across consecutive frames. */
  if (s_addr_trace_count && s_frames >= s_addr_trace_start_frame &&
      getenv("SC_CADENCE_WATCH") &&
      (reg == 0x011b || reg == 0x011c)) {
    static uint32_t s_011b_write_hits;
    if (s_011b_write_hits < 400) {
      fprintf(stderr, "[011bwrite f=%llu] pc=%02x:%04x $%04x = %02x\n",
              (unsigned long long)s_frames, g_cpu->k, g_cpu->pc, reg, v);
      s_011b_write_hits++;
    }
  }
  /* $d7 write watch: the dispatcher-select state machine (see ROM_MAP.md
   * `$00d7` entry) -- `01:8b3b` branches on it to pick the default
   * cursor-sprite dispatcher ($d7==0, only reaches the B/X reason-1 path)
   * vs. the second dispatcher at `01:8c55` ($d7==1, "the one that actually
   * handles Y+direction (fast travel)"). Every trace this session (bsnes
   * and this recomp) has shown $d7==0 throughout a held Y+direction --
   * this watch is to find whether/when anything ever writes it to 1. */
  if (s_addr_trace_count && s_frames >= s_addr_trace_start_frame &&
      getenv("SC_CADENCE_WATCH") && reg == 0x00d7) {
    static uint32_t s_d7_write_hits;
    if (s_d7_write_hits < 200) {
      fprintf(stderr, "[d7write f=%llu] pc=%02x:%04x $d7 = %02x\n",
              (unsigned long long)s_frames, g_cpu->k, g_cpu->pc, v);
      s_d7_write_hits++;
    }
  }
  if (s_gfx_trace && hw &&
      (reg == 0x2105 || (reg >= 0x211b && reg <= 0x2114) || reg == 0x420c ||
       (reg >= 0x4300 && reg <= 0x437f && (reg & 0x0f) <= 0x0a))) {
    if (s_gfx_trace_hits < 300) {
      fprintf(stderr, "[gfxtrace f=%llu] pc=%02x:%04x WRITE $%04x = %02x\n",
              (unsigned long long)s_frames, g_cpu->k, g_cpu->pc, reg, v);
      s_gfx_trace_hits++;
    }
  }
  if (!ScWorldGuestWrite(&s_world_guest, adr, v)) snes_write(g_snes, adr, v);
}

/* SPC cycles per master clock: 32 per DSP sample at 32040 Hz, over the NTSC
 * master clock of 21477272 Hz -- 533.12 samples per frame at 60.0988 fps.
 *
 * LakeSnes writes it as (32040*32)/(1364*262*60), with 60 fps, which is 534
 * samples a frame. While a DMA transfer's time never reached the APU nobody
 * noticed; once it did (sc_own_the_beam), the DSP made 534 a frame against
 * the 533.12 the output plays, and the backlog grew by 53 samples a second
 * until the ring was full: sound a quarter of a second behind the picture. */
static const double kApuCyclesPerMaster = (32040.0 * 32.0) / 21477272.0;
/* What that makes per frame (1364 x 262 master clocks): 533.125. The audio
 * drain takes exactly this many, so the DSP ring neither fills nor starves. */
static const double kDspSamplesPerFrame = 1364.0 * 262.0 * 32040.0 / 21477272.0;

/* SC_WIDESCREEN=<pixels per side>: widen the rendered picture.
 *
 * The runner's PPU already supports this -- PpuSetExtraSpace() sets a
 * symmetric border and the internal render width becomes 256 + 2*extra, up to
 * kPpuExtraLeftRight (96) per side. Nothing here reimplements a renderer; the
 * guest still draws every pixel, so all the existing verification stays valid
 * (see docs/PLAN_renderer.md, Stage 1).
 *
 * Motivation is the map-scroll complaint: showing more map at once is a
 * different answer to "panning is slow" than making the pan faster, and unlike
 * the pan work it costs no authenticity.
 *
 * Buffers are sized for the maximum so the allocation never depends on the
 * runtime value; only the active width does. */
enum { kVideoWidth = 256, kVideoHeight = 224, kVideoPitch = kVideoWidth * 4 };
enum { kVideoWidthMax = kVideoWidth + 96 * 2,
       kVideoPitchMax = kVideoWidthMax * 4 };
static int s_ws_extra;                 /* pixels per side; 0 = authentic 256 */
static int s_video_w = kVideoWidth;    /* active render width */
/* Which BG layers stay pinned to the authentic 256 columns in widescreen.
 * Bit per layer, BG1..BG4. 0x0F clamps all four, which is safe but leaves the
 * margins empty on screens whose background would happily tile. SC_WS_CLAMP
 * overrides it so a screen can be widened one layer at a time. */
static uint8_t s_ws_clamp = 0x0F;
static bool s_ws_clamp_auto = true;   /* derive it per frame; SC_WS_CLAMP pins it */
static bool s_ws_oam_strict = true;   /* SC_WS_OAM=0 for the permissive decode */
/* OFF by default. Clipping sprites out of the margins was added to stop the
 * title's light row leaking, but it cannot tell a leaked sprite from a wanted
 * one: on the scenario selector any card that sits in a margin loses its red
 * win mark AND the green selection cursor, because the ROM places those as
 * sprites at coordinates that fall outside the authentic 256. Reported from
 * play. Losing game state off the screen is worse than a cosmetic leak, so the
 * default now favours showing everything. SC_WS_OBJ_CLIP=1 restores it. */
static bool s_ws_obj_clip;
static bool s_ws_widen_menu = true;   /* SC_WS_MENU=0 to leave the main menu narrow */
static bool s_ws_widen_title = true;  /* SC_WS_TITLE=0 to clamp the title's sky */
static bool s_ws_widen_lights = true;   /* SC_WS_LIGHTS=0 to leave it alone */
/* One bit per OAM slot, published to the PPU each frame. A sprite this host
 * places at X >= 256 is a GENUINE right-margin sprite, so it must be marked
 * or the strict decode wraps it negative and hides it -- which is exactly
 * what happened to the extra lights on the right. */
static uint8_t s_oam_right_hints[16];
/* Same idea for the LEFT margin. A sprite the authentic 256-wide viewport
 * clips entirely is hardware-hidden there, so the lights this host places at
 * negative X never appeared -- 0 px in the left margin against 1293 in the
 * right, on every title frame. Marking the slots opts them back in. */
static uint8_t s_oam_left_hints[16];
static bool s_bg3_widened;            /* set by widen_wood_bg() for this frame only */
static uint8_t s_ws_clamp_now = 0x0f;  /* the mask actually pushed this frame */
static bool s_wood_widened;           /* BG3 carries real wood into the margins */
/* Wood-only stand-in map, for screens with no VRAM to relocate into. Armed by
 * widen_wood_bg(), swapped in around the margin pass only. */
static uint16_t s_wood_pass_map[0x400];
static int      s_wood_pass_layer = -1;
static unsigned s_wood_pass_src;
static bool     s_wood_pass_ready;    /* a usable map has been built at least once */
static bool s_ws_bg_margins;          /* margins come from a backdrop-only pass */
static int  s_ws_margin_layer;        /* which BG that pass draws */
static uint8_t s_ws_mirror;           /* SC_WS_MIRROR: layers padded by mirroring */
static bool s_ws_margin_fill = true;  /* SC_WS_FILL=0 to leave empty margins black */
static uint8_t *s_ws_scratch;
/* A SECOND scratch surface, for the backdrop/wood margin pass.
 *
 * It cannot share the sprite-clip one. Both render a line into scratch
 * before the picture is drawn and both copy margins out of it afterwards, so
 * with one buffer the second render overwrites the first's result and both
 * copies take the same pixels. Harmless while no screen ran both -- and then
 * View Mode did, and its margins came out as the city's black wrapping in
 * rather than the wood the pass had drawn. */
static uint8_t *s_ws_scratch_bg;
/* OBJ-only re-render, so the host-map margins can carry sprites. */
static uint8_t *s_ws_obj_layer;
static int s_margin_obj_on = 1;   /* SC_WS_MARGIN_OBJ */
/* Vehicles the game drops at the view's right edge, kept and drawn in the
 * margin (src/sc_vehicles.c). SC_WS_VEHICLES=0 turns it off. */
static bool s_ws_vehicles = true;
/* Render flags handed to PpuBeginDrawing. 0 selects ppu_draw_whole_line_legacy;
 * kPpuRenderFlags_NewRenderer (1) selects PpuDrawWholeLine.
 *
 * This matters for widescreen. The legacy renderer walks
 * -extraLeftCur .. 256+extraRightCur one pixel at a time and never calls
 * PpuWindows_Clear/_Calc, so it never consults PpuWidescreenLayerExtra() --
 * which means wsLayerClamp, wsLayerMirror, wsLayerRepeat and the clamp bands
 * are ALL dead on it. Grep ppu_legacy.c: zero references. That is why
 * SC_WS_CLAMP measurably did nothing. */
static uint32_t s_render_flags = 0;
static bool s_force_legacy;           /* SC_NEW_RENDERER=0 pins the legacy path */
static int s_video_pitch = kVideoPitch;
static uint8_t s_video_pixels[kVideoPitchMax * kVideoHeight];

/* `input1_currentState`'s bit layout is NOT the plain hardware joypad
 * register layout. The shared runner's auto-joy-read path (snes.c) does
 * `$4218 = SwapInputBits(input1_currentState) & 0xff; $4219 = ... >> 8`,
 * where SwapInputBits reverses all 16 bits. So bit i of input1_currentState
 * ends up at bit (15-i) of the value $4218/4219 are split from. Working
 * backwards from the real hardware layout the game actually reads --
 *
 *     $4218 (JOY1L)  bit7=A  bit6=X  bit5=L  bit4=R, bits3-0 = pad ID
 *     $4219 (JOY1H)  bit7=B  bit6=Y  bit5=Select bit4=Start
 *                    bit3=Up bit2=Down bit1=Left bit0=Right
 *
 * -- the constants below are what must be set in input1_currentState;
 * verified empirically against the game itself (holding each direction moves
 * exactly one cursor axis in the right direction: Left drives $01EB down,
 * Right up, Up drives $01ED down, Down up). Do not "simplify" these to the
 * naive hardware bit order -- that was the original bug.
 *
 * An earlier revision of this comment named the two registers the other way
 * round ("$4218 bit7=B..bit0=Right / $4219 bit7=A,6=X,5=L,4=R"), which
 * contradicted the enum three lines below it and is simply wrong: $4218 is
 * the LOW byte, so it carries A/X/L/R. The enum was always right. Same class
 * of defect as the stale keybinds.h comment corrected upstream in
 * mstan/snesrecomp#17, and worth the same care -- a wrong comment next to
 * right code is how the original transposition survived as long as it did. */
enum {
  /* Serial order, LSB first -- the order the pad shifts out of $4016:
   * B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R. The runner
   * reverses all 16 bits and splits the result, so this convention lands
   * $4218 = A,X,L,R,0,0,0,0 and $4219 = B,Y,Select,Start,Up,Down,Left,Right
   * -- the real hardware layout -- with the runner UNMODIFIED. */
  kPad_B = 0x0001, kPad_Y = 0x0002, kPad_Select = 0x0004, kPad_Start = 0x0008,
  kPad_Up = 0x0010, kPad_Down = 0x0020, kPad_Left = 0x0040, kPad_Right = 0x0080,
  kPad_A = 0x0100, kPad_X = 0x0200, kPad_L = 0x0400, kPad_R = 0x0800,
};

/* HDMA per-scanline execution. The shared engine's cycle-accurate DMA path
 * (snes/dma.c) tracks $420C-enabled channels via `hdmaActive` but never
 * actually walks their tables -- only plain DMA ($420B, dma_doDma) is wired
 * up. A per-line table-walk implementation exists in the shared runtime
 * (common_rtl.c's SimpleHdma_Init/DoLine), but it's written for the
 * AOT/decompiled recomp path (raw host pointers into its own g_ram, which
 * would collide with this file's g_ram if common_rtl.c were linked in) and
 * this interpreter-only project never calls it anyway, so every HDMA-driven
 * effect in this ROM (found so far: the View screen's per-scanline window
 * (`$2126-$2129`) + BG1/BG2 horizontal-scroll (`$210d`/`$210f`) tilt effect)
 * silently never applies -- confirmed via SC_GFX_TRACE, which showed the
 * channels correctly configured and enabled every frame but their target
 * PPU registers never actually written. Ported here instead, using the same
 * snes_read/snes_writeBBus bus primitives dma.c's own plain-DMA path already
 * uses (see dma_transferByte), so it works uniformly for WRAM- or
 * ROM-sourced tables without needing raw host pointers. This is a
 * game-specific addition, not a shared-runtime change, but it fixes HDMA
 * generally for this ROM, not just the one effect that surfaced the gap. */
typedef struct {
  bool active;
  uint8_t bank;       /* bank of the table pointer (and, in direct mode, the data) */
  uint16_t addr;       /* current table read pointer */
  uint8_t repCount;
  uint8_t mode;         /* dc->mode (bits 0-2) | 0x40 if indirect */
  uint8_t ppuAddr;      /* B-bus dest offset, $00-$3f */
  uint8_t indirBank;
  uint16_t indirAddr;    /* current indirect data pointer (mode & 0x40 only) */
} HdmaChanState;
static HdmaChanState s_hdma[8];
/* SC_HOST_HDMA=0 turns this host's own HDMA off.
 *
 * It exists because the runner's interpreter tier does not walk HDMA tables
 * at all (upstream issue #15), so this host walks them itself. Upstream PR #16
 * proposes doing it in the runner instead, and the only way to test that is to
 * stand ours down and see whether the picture survives -- with both running,
 * every channel would transfer twice. */
static bool s_host_hdma = true;

static void hdma_init_channel(HdmaChanState *c, const DmaChannel *dc) {
  if (!dc->hdmaActive) { c->active = false; return; }
  c->active = true;
  c->bank = dc->aBank;
  c->addr = dc->aAdr;
  c->repCount = 0;
  c->mode = (uint8_t)(dc->mode | (dc->indirect ? 0x40 : 0));
  c->ppuAddr = dc->bAdr;
  c->indirBank = dc->indBank;
}

static void hdma_do_line(HdmaChanState *c) {
  static const uint8_t kBAdrOffsets[8][4] = {
    {0, 0, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
    {0, 1, 2, 3}, {0, 1, 0, 1}, {0, 0, 0, 0}, {0, 0, 1, 1},
  };
  static const uint8_t kTransferLength[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };

  if (!c->active) return;
  bool do_transfer = false;
  if ((c->repCount & 0x7f) == 0) {
    c->repCount = snes_read(g_snes, ((uint32_t)c->bank << 16) | c->addr);
    c->addr++;
    if (c->repCount == 0) { c->active = false; return; }
    if (c->mode & 0x40) {
      uint8_t lo = snes_read(g_snes, ((uint32_t)c->bank << 16) | c->addr); c->addr++;
      uint8_t hi = snes_read(g_snes, ((uint32_t)c->bank << 16) | c->addr); c->addr++;
      c->indirAddr = (uint16_t)(lo | (hi << 8));
    }
    do_transfer = true;
  }
  if (do_transfer || (c->repCount & 0x80)) {
    int len = kTransferLength[c->mode & 7];
    for (int j = 0; j < len; j++) {
      uint8_t val;
      if (c->mode & 0x40) {
        val = snes_read(g_snes, ((uint32_t)c->indirBank << 16) | c->indirAddr);
        c->indirAddr++;
      } else {
        val = snes_read(g_snes, ((uint32_t)c->bank << 16) | c->addr);
        c->addr++;
      }
      uint8_t reg = (uint8_t)(c->ppuAddr + kBAdrOffsets[c->mode & 7][j]);
      snes_writeBBus(g_snes, reg, val);
    }
  }
  c->repCount--;
}

/* â”€â”€ accurate H/V position driver, ported from snesrecomp/cosim/ref_driver.c
 * (the framework's own game-neutral reference frame loop) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
/* Defined with the host-map block far below; used from the frame loop here. */
static void host_map_arm_captures(void);
static void host_map_compose(void);
static void ws_fix_scroll_seam(void);
static bool host_map_screen_live(void);
static void selector_extend_tilemap(void);
static void widen_wood_bg(void);
static void widen_title_lights(void);
static uint8_t sc_bus_rom_read(void *ctx, uint32_t addr);
static bool selector_on_screen(void);
static void title_sign_place(void);
static void title_sign_align(void);
static void selector_after_upload(void);
static void selector_hint_margin_sprites(void);
static void selector_sylt_sprites(void);
static void title_ws_update(void);
static bool title_ws_live(void);
static void ws_fill_margins(void);
static void ws_fill_flat_margins(void);
static void ws_hide_backdrop_furniture(void);
static void sylt_place_card(void);
static void sylt_write_brief_tilemap(void);
/* Briefing page geometry -- up here because the composer is called from the
 * opcode loop, which comes earlier in this file than the composer itself. */
#define SC_BRIEF_COLS 32
#define SC_BRIEF_ROWS 64
#define SC_BRIEF_BLANK 0x3FFu
static bool brief_compose_page(uint32_t src, uint8_t *dst);
static void place_translated_cards(void);
static void apply_surfaces(void);
/* OFF by default, still. The compositor it depends on works now, but the gate
 * that decides WHICH screen it may draw on does not separate cleanly: the
 * tax, evaluation, overview and history pages all sit at $14 == 0 alongside
 * the city view, and turning this on by default painted terrain across all
 * of them. SC_HOST_MAP=1 to use it. */
/* ON by default.
 *
 * It was off while the compositor took the guest's picture apart and tried to
 * reassemble it, which never worked. It now keeps the guest's 256 columns
 * verbatim -- measured 0 pixels different from what the game draws, on every
 * screen -- and host terrain is only ever seen past the guest's right edge.
 * SC_HOST_MAP=0 turns it off. */
static bool     s_host_map = true;
static uint8_t *s_hud_pixels;
static uint8_t *s_guest_pixels;  /* the guest's finished frame, kept verbatim */
/* Host map render target, deliberately larger than the frame: the sub-cell
 * shift below reads up to 8 px right of and below the visible window. */
static uint8_t *s_hostmap_px;
static int      s_hostmap_pitch;
/* The host loop's own frame index, which is what SC_DUMP_AT counts -- not
 * s_frames, which after --load-state resumes at the saved state's number. */
static unsigned long long s_loop_frame;
/* Layers taken into the HUD pass. SC_HUD_MASK overrides it: bit0 BG1,
 * bit1 BG2, bit2 BG3, bit3 BG4, bit4 OBJ. Adjustable because "every layer
 * except BG2" also drags in whatever BG1 paints behind the toolbar, which
 * then composites over the host map. */
static uint8_t s_hud_mask = (uint8_t)~0x02;

static bool s_rom_is_us = true;

static void handle_pos_stuff(void) {
  Snes *snes = g_snes;
  Interp816 *cpu = g_cpu;
  if (snes->autoJoyTimer)
    snes->autoJoyTimer = snes->autoJoyTimer <= 2 ? 0 : (uint16_t)(snes->autoJoyTimer - 2);

  if (snes->vIrqEnabled && snes->hIrqEnabled) {
    if (snes->vPos == (snes->vTimer + 1) && snes->hPos == (4 * snes->hTimer)) {
      snes->inIrq = true; cpu->irqWanted = true;
    }
  } else if (snes->vIrqEnabled && !snes->hIrqEnabled) {
    if (snes->vPos == (snes->vTimer + 1) && snes->hPos == 1024) {
      snes->inIrq = true; cpu->irqWanted = true;
    }
  } else if (!snes->vIrqEnabled && snes->hIrqEnabled) {
    if (snes->hPos == (4 * snes->hTimer)) { snes->inIrq = true; cpu->irqWanted = true; }
  }

  if (snes->hPos == 0) {
    bool startingVblank = false;
    if(s_rom_is_us && snes->vPos==1) {
      unsigned screen=ram_w(0x14);
      s_custom_renderer.map_preview_frame=ScRendererMapPreviewVisible(g_ppu,g_ram);
      /* COP 2 builds the next list before OAM DMA displays it. Match the
       * live list before publishing glyphs at the first visible scanline. */
      if(screen==2 || screen==3 || screen==18)
        ScJourneyMenuPresent(g_ppu->vram,g_ppu->oam,ram_w(0x44)!=0);
      if(screen==2 || screen==3 || screen==17 || screen==18)
        ScJourneyMenuFrame(g_ppu->vram,PPU_bgTilemapAdr(g_ppu,2));
      ScSavedCityMenuFont(g_ppu->vram,g_ppu->oam,g_ppu->highOam,
          ram_w(0x44),screen==17,s_test_revealed);
    }
    uint16_t mouse_oam[256];uint8_t mouse_high[32];
    bool free_ui_cursor=s_ui_mouse_pointer.active && ScMouseUiPointerScreen(g_ram) &&
        !(s_custom_video.enabled &&
          (ScMouseUiArrowScreen(g_ram) || ScSelector_OnScreen(g_ram[0x14])));
    if(free_ui_cursor) {
      memcpy(mouse_oam,g_ppu->oam,sizeof mouse_oam);memcpy(mouse_high,g_ppu->highOam,sizeof mouse_high);
      /* Presentation only: menu loops can keep jumping their own cursor.
       * The PPU/GPU see the same mouse endpoint; restore before guest work. */
      ScMouseUiCursorPlace(g_ram,g_ppu->oam,g_ppu->highOam,s_ui_mouse_pointer.x,s_ui_mouse_pointer.y);
    }
    if (snes->vPos <= kVideoHeight) {
      uint64_t raster_t0=s_perf_detail?SDL_GetPerformanceCounter():0;
      /* Host-map mode renders each visible line TWICE: once with the layer
       * mask limited to BG3|OBJ into a scratch buffer, once normally. That
       * gets the HUD and sprites in isolation without the overlay export,
       * which arms cleanly but exports nothing. Needs no cooperation from the
       * runner beyond retargeting PpuBeginDrawing between the two calls. */
      /* Same $14 == 0 gate as host_map_compose(): this pass exists only to
       * feed it, and rendering every line twice on screens the compose
       * will not touch changes their picture for nothing. */
      if (s_ws_bg_margins && s_ws_extra > 0 && s_ws_scratch_bg && snes->vPos > 0) {
        /* Where widen_wood_bg() could not relocate the map, swap a wood-only
         * copy in for the length of this pass, so the margins get the desk
         * without the furniture standing on it. Restored immediately after,
         * before the picture proper is drawn from the same map. */
        static uint16_t held[0x400];
        const bool swap = s_wood_pass_layer >= 0;
        if (swap) {
          memcpy(held, &g_ppu->vram[s_wood_pass_src], sizeof held);
          memcpy(&g_ppu->vram[s_wood_pass_src], s_wood_pass_map, sizeof held);
        }
        g_snes_ppu_dbg_layer_mask = (uint8_t)(1u << s_ws_margin_layer);
        PpuBeginDrawing(g_ppu, s_ws_scratch_bg, (size_t)s_video_pitch, s_render_flags);
        ppu_runLine(g_ppu, snes->vPos);
        g_snes_ppu_dbg_layer_mask = 0xff;
        PpuBeginDrawing(g_ppu, s_video_pixels, (size_t)s_video_pitch, s_render_flags);
        if (swap) memcpy(&g_ppu->vram[s_wood_pass_src], held, sizeof held);
      }
      /* Both diagnostics below are read once: this runs every scanline, and
       * getenv() is a locked scan of the whole environment on Windows. */
      static int ws_diag = -1, pass_diag;
      if (ws_diag < 0) {
        ws_diag = getenv("SC_WS_DIAG") != NULL;
        pass_diag = getenv("SC_PASS_DIAG") != NULL;
      }
      if (ws_diag && snes->vPos == 100) { static int n;
        if (n < 3) { n++;
          fprintf(stderr, "[wsdiag] clamp=%02x widenMask=%02x extraL=%u extraR=%u budget=%u\n",
                  g_ppu->wsLayerClamp, g_ppu->wsLayerWidenMask,
                  g_ppu->extraLeftCur, g_ppu->extraRightCur,
                  g_ppu->extraLeftRight); } }
      /* SC_LAYER_MASK=<bits>: restrict the MAIN render pass. bit0 BG1,
       * bit1 BG2, bit2 BG3, bit3 BG4, bit4 OBJ. Diagnostic only -- it answers
       * "which layer actually puts those pixels there", which the widescreen
       * clamp cannot, because that mask only covers BG1..BG4. */
      { static int mask = -1;
        if (mask == -1) { const char *e = getenv("SC_LAYER_MASK");
          mask = e && *e ? (int)strtol(e, NULL, 0) : 0xff; }
        g_snes_ppu_dbg_layer_mask = (uint8_t)mask; }
      /* Keep sprites out of the widescreen margins.
       *
       * PpuWidescreenLayerExtra() only consults wsLayerClamp for layer < 4, so
       * OBJ reaches the margins however the backgrounds are clamped -- measured
       * on the title, BG2 and BG3 clamp to zero margin pixels while OBJ still
       * puts 1623 there. What shows up is the off-screen half of sprites the
       * hardware clips at the screen edge: the title billboard and the row of
       * blinking lights along the bottom, reported from play as a blinking
       * rope. The strict OAM decode does not help -- that governs the right
       * band [256, 256+extraRight), and these are all on the left.
       *
       * So the line is rendered a second time with OBJ masked off, and the
       * margin columns are taken from that. The authentic 256 keep every
       * sprite. Same two-pass shape the host-map HUD capture uses. */
      if (s_ws_extra > 0 && s_ws_obj_clip && s_ws_scratch) {
        g_snes_ppu_dbg_layer_mask &= (uint8_t)~0x10;
        PpuBeginDrawing(g_ppu, s_ws_scratch, (size_t)s_video_pitch, s_render_flags);
        ppu_runLine(g_ppu, snes->vPos);
        g_snes_ppu_dbg_layer_mask |= 0x10;
        PpuBeginDrawing(g_ppu, s_video_pixels, (size_t)s_video_pitch, s_render_flags);
      }
      /* Margin sprites for the host-map view.
       *
       * host_map_compose() builds the picture as the guest's 256 columns
       * followed by host-rendered map, and the host map draws BG tiles only,
       * so any sprite past the guest's edge is discarded however it decodes.
       * Render the line again with OBJ alone and hand that to the compositor.
       *
       * SC_WS_MARGIN_OBJ=0 disables it. */
      if (s_ws_extra > 0 && s_ws_obj_layer && snes->vPos > 0 &&
          snes->vPos <= kVideoHeight && s_margin_obj_on && host_map_screen_live()) {
        g_snes_ppu_dbg_layer_mask = 0x10;          /* OBJ alone */
        PpuBeginDrawing(g_ppu, s_ws_obj_layer, (size_t)s_video_pitch, s_render_flags);
        ppu_runLine(g_ppu, snes->vPos);
        g_snes_ppu_dbg_layer_mask = 0xff;
        PpuBeginDrawing(g_ppu, s_video_pixels, (size_t)s_video_pitch, s_render_flags);
      }
      /* SC_PASS_DIAG: render the SAME line twice into two surfaces, with
       * nothing changed between them, and see whether the pixels match. The
       * seam repair's re-render approach assumes they do; its measured result
       * (cloning got WORSE, not better) says they may not. */
      if (pass_diag && s_ws_scratch) {
        /* Compare an EXTRA pass against the MAIN one -- the comparison that
         * matters. Diffing two extra passes against each other, as this first
         * did, cannot catch a difference between the first render of a line and
         * later ones, because neither of them is the first. */
        PpuBeginDrawing(g_ppu, s_ws_scratch, (size_t)s_video_pitch, s_render_flags);
        ppu_runLine(g_ppu, snes->vPos);
        PpuBeginDrawing(g_ppu, s_video_pixels, (size_t)s_video_pitch, s_render_flags);
      }
      /* Diagnostic companion to SC_LAYER_MASK: isolate the advisor's
       * background without its old panel-shaped subscreen occlusion.
       * Restore the register before any guest execution or custom rendering. */
      { static int sub_window_mask = -2;
        if (sub_window_mask == -2) {
          const char *e = getenv("SC_SUB_WINDOW_MASK");
          sub_window_mask = e ? (int)strtol(e,NULL,0) : -1;
        }
        const uint8_t saved = g_ppu->screenWindowed[1];
        if (sub_window_mask >= 0) g_ppu->screenWindowed[1]=(uint8_t)sub_window_mask;
        bool skipped=ScPpuPixelsSkipped();
        s_custom_renderer.native_line=false;
        bool native_gpu=s_custom_video.enabled && !s_skip_custom_frame &&
            sub_window_mask<0 && g_snes_ppu_dbg_layer_mask==0xff &&
            ScRendererDeferNativeLine(&s_custom_renderer,g_ppu,g_ram,snes->vPos-1);
        if(native_gpu) ScPpuSkipPixels(true);
        ScPpuDeferObjects(native_gpu);
        ppu_runLine(g_ppu, snes->vPos);
        ScPpuDeferObjects(false);
        ScPpuSkipPixels(skipped);
        g_ppu->screenWindowed[1]=saved;
      }
      if (s_custom_video.enabled && !s_skip_custom_frame && snes->vPos == 1) {
        s_custom_renderer.vehicle_count = s_ws_vehicles?ScVehicles_Shown(s_custom_renderer.vehicles,19):0;
        if(s_ws_vehicles && s_world.active && host_map_screen_live()) {
          if(!s_fleet)s_fleet=ScFleetCreate();
          ScFleetStep(s_fleet,&s_world,s_frames,g_ram[0x193]!=0);
          int nx=(int16_t)ram_w(0x1bd)*8+(g_ppu->hScroll[1]&7),ny=(int16_t)ram_w(0x1bf)*8+(g_ppu->vScroll[1]&7);
          double zoom=s_custom_renderer.map_zoom>0?s_custom_renderer.map_zoom:1;
          int left=nx+s_custom_renderer.camera_x-s_custom_renderer.gameplay_view.core_x/zoom-64;
          int top=ny+s_custom_renderer.camera_y-s_custom_renderer.gameplay_view.core_y/zoom-64;
          s_custom_renderer.vehicle_count+=ScFleetShown(s_fleet,&s_world,g_snes->cart->rom,nx,ny,left,top,
            left+s_custom_renderer.gameplay_view.width/zoom+128,top+s_custom_renderer.gameplay_view.height/zoom+128,
            s_custom_renderer.vehicles+s_custom_renderer.vehicle_count,SC_VEHICLE_SPRITES-s_custom_renderer.vehicle_count);
          if(getenv("SC_FLEET_DIAG") && !(s_frames%60))fprintf(stderr,"[fleet] frame %llu trains=%u planes=%u ships=%u helicopters=%u shown=%d\n",
            (unsigned long long)s_frames,ScFleetCount(s_fleet,0),ScFleetCount(s_fleet,1),ScFleetCount(s_fleet,2),ScFleetCount(s_fleet,3),s_custom_renderer.vehicle_count);
        }
      }
      if (s_custom_video.enabled && !s_skip_custom_frame && snes->vPos > 0 && snes->vPos <= 224) {
        uint64_t custom_t0=s_measure_custom_frame?SDL_GetPerformanceCounter():0;
        ScRendererLine(&s_custom_renderer, g_ppu, g_ram, snes->vPos - 1,
          (const uint32_t *)(s_video_pixels + (size_t)(snes->vPos - 1) * s_video_pitch));
        if(s_measure_custom_frame) s_custom_frame_ms+=(SDL_GetPerformanceCounter()-custom_t0)*s_perf_clock_ms;
      }
      /* Blank the margins when nothing is entitled to draw there.
       *
       * Clamping only governs the four backgrounds on the MAIN screen. The
       * subscreen is not covered, so a screen that shows the city through
       * colour math puts it in the margins whatever the clamp says -- which is
       * what the advice popup and the graphs page were doing: city map either
       * side, at full brightness, while the authentic 256 showed it dimmed
       * behind the panel.
       *
       * Clearing BEFORE the render does not help, because those pixels are
       * genuinely drawn, not left over. (An earlier reading called them stale
       * on the evidence that the margins changed by 0 pixels between frames --
       * which proves nothing on a screen that is standing still.) So it is
       * done after, and only when every background is clamped: if not one of
       * them may reach the margins, whatever arrived there came in past the
       * clamp and does not belong. Screens that legitimately fill their
       * margins -- the title, the selector, anything the wood pass or the host
       * map is handling -- always leave at least one layer unclamped and are
       * never touched. */
      if (s_ws_extra > 0 && snes->vPos >= 1 && snes->vPos <= kVideoHeight &&
          (s_ws_clamp_now & 0x0fu) == 0x0fu &&
          !(s_host_map && host_map_screen_live())) {
        /* Force blank paints BLACK, not the backdrop.
         *
         * brightnessMult covers a fade, and misses the other way a SNES shows
         * nothing. The game ends a fade by writing $8f -- force blank on,
         * brightness restored to 15 -- so this computed cgram[0] at FULL
         * intensity and painted the sky into the margins while the guest was
         * black. Leaving the loan screen that is twelve frames of bright sky
         * either side of a black picture, reported from play as the
         * transition back to the map not fading correctly.
         *
         * Found by probing one margin pixel through the line: after
         * ppu_runLine it is 000000, and after this block adbdce. The PPU had
         * already blanked the line correctly; this painted over it.
         *
         * The same fault the compositor handles with its own `blanked` test,
         * on the path that runs when the compositor does not. */
        const uint16_t bd = g_ppu->cgram[0];
        const uint32_t back = PPU_forcedBlank(g_ppu) ? 0xFF000000u : (0xFF000000u
            | ((uint32_t)g_ppu->brightnessMult[bd & 0x1f] << 16)
            | ((uint32_t)g_ppu->brightnessMult[(bd >> 5) & 0x1f] << 8)
            | (uint32_t)g_ppu->brightnessMult[(bd >> 10) & 0x1f]);
        uint32_t *row = (uint32_t *)(s_video_pixels +
                                     (size_t)(snes->vPos - 1) * (size_t)s_video_pitch);
        for (int x = 0; x < s_ws_extra; x++) row[x] = back;
        for (int x = s_video_w - s_ws_extra; x < s_video_w; x++) row[x] = back;
      }
      /* AFTER the real render, not before.
       *
       * Every extra ppu_runLine for the same scanline disturbs what the next
       * one produces -- measured on the city view, turning the host map on
       * changed the GUEST's own frame: the status bar's dark background at
       * authentic (60,8) went from $311000 to the map's own colour, with no
       * change to the guest's code path other than this pass existing. The
       * overlay is taken from the guest's finished frame, so that frame has to
       * be drawn from clean state; the capture can have whatever is left. */
      if (s_host_map && s_hud_pixels && snes->vPos > 0 && host_map_screen_live()) {
        /* Everything EXCEPT BG2, not just BG3|OBJ.
         *
         * BG2 is the map -- the only layer being replaced. Capturing every
         * other layer means anything the guest draws wins over the host map
         * automatically: the HUD, sprites, AND any menu, including the ones
         * that open *inside* the city view without changing $01df. Reported
         * from play: savestate_3 opens such a menu and $01df stays 3
         * throughout, so no screen-mode gate could ever have caught it.
         *
         * Self-correcting by construction, which is why it beats hunting for
         * a "menu is open" flag -- a search through the WRAM delta across the
         * B press turned up only transient direct-page scratch. */
        g_snes_ppu_dbg_layer_mask = s_hud_mask;   /* default: all but BG2 */
        PpuBeginDrawing(g_ppu, s_hud_pixels, (size_t)s_video_pitch, s_render_flags);
        ppu_runLine(g_ppu, snes->vPos);
        g_snes_ppu_dbg_layer_mask = 0xff;
        PpuBeginDrawing(g_ppu, s_video_pixels, (size_t)s_video_pitch, s_render_flags);
      }
      if (s_ws_bg_margins && s_ws_extra > 0 && s_ws_scratch_bg && snes->vPos > 0 &&
          snes->vPos <= kVideoHeight) {
        const size_t row = (size_t)(snes->vPos - 1) * (size_t)s_video_pitch;
        uint32_t *dst = (uint32_t *)(s_video_pixels + row);
        const uint32_t *src = (const uint32_t *)(s_ws_scratch_bg + row);
        for (int x = 0; x < s_ws_extra; x++) dst[x] = src[x];
        for (int x = s_video_w - s_ws_extra; x < s_video_w; x++) dst[x] = src[x];
      }
      if (s_ws_extra > 0 && s_ws_obj_clip && s_ws_scratch && snes->vPos > 0 &&
          snes->vPos <= kVideoHeight) {
        const size_t row = (size_t)(snes->vPos - 1) * (size_t)s_video_pitch;
        uint32_t *dst = (uint32_t *)(s_video_pixels + row);
        const uint32_t *src = (const uint32_t *)(s_ws_scratch + row);
        for (int x = 0; x < s_ws_extra; x++) dst[x] = src[x];
        for (int x = s_video_w - s_ws_extra; x < s_video_w; x++) dst[x] = src[x];
      }
      g_snes_ppu_dbg_layer_mask = 0xff;
      if(s_perf_detail) s_perf_raster_ms+=(SDL_GetPerformanceCounter()-raster_t0)*s_perf_clock_ms;
    }
    if (snes->vPos == 0) {
      /* The title sign's own entries, before anything reads OAM this frame.
       * Not a widescreen repair: the shiver is in the guest's 256 columns. */
      title_sign_align();
      /* Clamp the BG layers out of the widescreen margins on EVERY screen,
       * not only where the host map composes.
       *
       * Tied to the host map it only helped the city view, and a menu screen
       * ($01df 0/1/2/4) still tiled its background sideways -- reported from
       * play on a clean start, which sits at $01df == 4. The clamp belongs to
       * widescreen itself; whether the map is being replaced is a separate
       * question. Re-applied per frame, as the API requires. */
      /* Pick the renderer by BG MODE, every frame.
       *
       * The widescreen layer policies only exist on the new renderer, but the
       * new renderer does not draw mode 0 -- measured at AUTHENTIC width, so
       * this is not a widescreen problem:
       *
       *   savestate_1 title      mode 1   identical, 42060 non-black both
       *   savestate_8 gameplay   mode 1   identical, 54352 both
       *   savestate_3 scenario   mode 0   legacy 53264 vs new 863
       *   savestate_6 fax        mode 0   legacy 57130 vs new 176
       *
       * An earlier commit called the switch free on the strength of three
       * save states that all happened to be mode 1. They were not a sample.
       *
       * So: mode 0 keeps the legacy renderer and loses the policies, which
       * costs nothing on the two screens that use it -- the scenario selector's
       * BG1 is genuinely 64 columns, so widening it unclamped is correct. */
      if (g_ppu && s_ws_extra > 0) {
        const bool mode0 = PPU_mode(g_ppu) == 0;
        /* The new renderer gets HALVED colour math wrong. Use the legacy one
         * whenever the guest is dimming a scene behind an overlay.
         *
         * Measured on the advice popup, comparing the authentic 256 columns
         * against a plain 256-wide run of the same state: the new renderer is
         * wrong on 8736 pixels, the legacy one on 0. It loses whole rectangles
         * of the map -- the black bands above and below the panel, which a
         * 256-wide render shows as ordinary city.
         *
         * It is not a geometry effect: 8, 32 and 96 pixels of extra space
         * corrupt exactly the same 8736 pixels, so any widescreen at all
         * switches the path and the width is irrelevant.
         *
         * Nothing is lost by dropping to legacy here. The new renderer is
         * chosen for its widescreen layer policies, and a screen dimming
         * behind an overlay has every background clamped anyway, so there are
         * no policies left to apply. */
        const bool dim_overlay = PPU_halfColor(g_ppu) && PPU_addSubscreen(g_ppu);
        s_render_flags = (!mode0 && !s_force_legacy && !dim_overlay) ? 1u : 0u;
        /* Lift the per-scanline sprite limit while widened.
         *
         * The title's light row is four 64 px sprites, which is already 32
         * 8x8 tiles -- the hardware ceiling for one line. Adding the sprites
         * that carry it into the margins therefore pushed existing ones out,
         * and 128 pixels per line vanished from INSIDE the authentic picture
         * on rows 199..220. Measured, not guessed.
         *
         * kPpuRenderFlags_NoSpriteLimits removes that ceiling. It is a
         * departure from hardware, but only on a picture that is already wider
         * than hardware ever drew, and it is the difference between extending
         * the row and corrupting the row. At authentic width nothing sets it. */
        if (s_ws_widen_lights) s_render_flags |= 8u;   /* NoSpriteLimits */
        /* Push them. PpuBeginDrawing is what copies renderFlags into the PPU,
         * and in the default configuration this host called it exactly once,
         * at startup -- the host-map and sprite-clip paths call it per line,
         * but both are off by default. So every per-frame decision above was
         * being computed and thrown away. */
        static uint32_t pushed = 0xffffffffu;
        if (pushed != s_render_flags) {
          pushed = s_render_flags;
          PpuBeginDrawing(g_ppu, s_video_pixels, (size_t)s_video_pitch,
                          s_render_flags);
        }
        /* Mode 0 has no working clamp, so a 32-column layer WILL tile. The fax
         * showed three copies of the briefing side by side. The runner has a
         * purpose-built answer: PpuSetExtraSpaceCentered renders only the
         * authentic 256 and keeps the centring budget, leaving the caller to
         * black the margins -- "used for bounded screens where there is no
         * valid BG past 256 to show", which is exactly this case.
         *
         * So on mode 0, widen only if EVERY enabled background is 64 columns.
         * The scenario selector qualifies (BG1 alone, 64 wide) and gets the
         * full picture; the fax does not (BG1/2/3 all 32) and gets pillarbox
         * instead of tiling. */
        bool all_wide = true;
        for (int L = 0; L < 4; L++)
          if (((g_ppu->screenEnabled[0] >> L) & 1) && !PPU_bgTilemapWider(g_ppu, L))
            all_wide = false;
        /* Mode 0 has no working clamp, so a 32-column layer tiles. Rather
         * than pillarbox the whole picture, widen it and take the MARGINS from
         * a pass that draws only the backdrop layer.
         *
         * The fax is the case that motivates it. Its BG3 is the wooden desk --
         * a genuinely repeating block, 8 tiles wide on a 16-row cycle, tile
         * = $20 + (row%8)*$10 + ((row/8)%2)*8 + col%8 -- so a 256 px wrap on
         * that layer is seamless. What must not tile is BG1, the briefing
         * text, and BG2, the paper. Widening their tilemaps is not an option:
         * both are high=1, 32x64 maps already spanning two pages each, so a
         * 64-column version needs four pages apiece and only four are free in
         * total.
         *
         * Taking the margins from a backdrop-only pass sidesteps all of it and
         * costs no VRAM. Reported from play: the wood pattern is complete and
         * visible before the paper rises. */
        s_ws_bg_margins = mode0 && !all_wide;
        s_ws_margin_layer = 0;
        if (s_ws_bg_margins)
          for (int L = 3; L >= 0; L--)          /* highest enabled BG = backdrop */
            if ((g_ppu->screenEnabled[0] >> L) & 1) { s_ws_margin_layer = L; break; }
        PpuSetExtraSpace(g_ppu, (uint8_t)s_ws_extra);
      }
      title_ws_update();
      memset(s_oam_right_hints, 0, sizeof s_oam_right_hints);
      memset(s_oam_left_hints, 0, sizeof s_oam_left_hints);
      widen_wood_bg();
      /* AFTER widen_wood_bg(), which clears the flag for the frame. */
      if (title_ws_live() && s_ws_widen_title && s_ws_extra > 0)
        s_bg3_widened = true;
      widen_title_lights();
      selector_after_upload();
      selector_sylt_sprites();
      /* BG3 is hard-clamped independent of wsLayerClamp:
       *
       *     if (layer != 2) return extra;
       *     return (ppu->wsBg3WidenY && y >= ppu->wsBg3WidenY) ? extra : 0;
       *
       * because BG3 usually carries a status bar that must not tile sideways.
       * The main menu is BG3-only, so nothing else could widen it -- measured,
       * its margins stayed at 0 pixels with the tilemap already 64 columns and
       * the clamp mask showing BG3 clear.
       *
       * Set EVERY frame, not once inside widen_wood_bg(): the field is sticky
       * and PpuResetLayerPolicies() does not clear it, so opening it on the
       * menu would leave BG3 widened on the gameplay HUD afterwards -- exactly
       * the tiling this default exists to prevent. */
      if (g_ppu)
        PpuSetWidescreenBg3Widen(g_ppu, s_bg3_widened ? 1 : 0);
      if (s_ws_extra > 0) {
        /* Clamp exactly the layers that CANNOT be widened, derived from the
         * live registers rather than a per-screen table.
         *
         * A BG tilemap is 32 or 64 columns; 32 is 256 px, so widening one
         * always wraps and the picture tiles sideways -- reported from play as
         * the title's foreground repeating. 64 columns covers 512 px, more
         * than the 448 the maximum widescreen renders, so those carry real
         * content all the way out.
         *
         * Surveyed across the screens: only the title's BG1 ($6000) and the
         * scenario selector's BG1 ($3000) are 64 wide. Everything else -- main
         * menu, map select, name entry, gameplay HUD, tax -- is entirely
         * 32-column layers, and clamping them shows backdrop in the margins
         * instead of a wrapped copy.
         *
         * SC_WS_CLAMP overrides with a fixed mask when experimenting. */
        uint8_t clamp = s_ws_clamp_auto ? 0 : s_ws_clamp;
        if (s_ws_clamp_auto)
          for (int L = 0; L < 4; L++)
            if (!PPU_bgTilemapWider(g_ppu, L)) clamp |= (uint8_t)(1u << L);
        /* The title is the exception: wrapping its 32-column backgrounds is
         * wanted, not a fault. BG3 is the sky -- rows of uniformly tile $0000
         * with scattered stars -- and BG2 the far skyline, so a 256 px wrap is
         * invisible on the one and reads as more skyline on the other.
         * Clamping them stops the gradient dead at the authentic edge, which
         * is exactly what it did once the flags below started reaching the PPU
         * per frame and the clamp became live for the first time. */
        if (s_ws_clamp_auto && title_ws_live() && s_ws_widen_title)
          clamp &= (uint8_t)~0x06;   /* BG2 | BG3 */
        /* The wood-only margin pass needs its layer to REACH the margins, or
         * the pass renders 256 px of desk and nothing beyond. Its own map
         * wraps seamlessly, and the frame the player sees is taken from the
         * pass rather than from this one, so unclamping costs nothing here. */
        if (s_ws_clamp_auto && s_wood_pass_layer >= 0)
          clamp &= (uint8_t)~(1u << s_wood_pass_layer);
        /* SC_WS_MIRROR=<mask>: pad those layers by mirroring the authentic
         * 256 into the margins instead of clamping them. Needs no VRAM, which
         * matters on the screens that have none free -- map select and name
         * entry both have a single spare 2KB page and no consecutive pair, so
         * the menu's relocate-and-fill is impossible there. */
        clamp &= (uint8_t)~s_ws_mirror;
        PpuSetWidescreenLayerMirror(g_ppu, s_ws_mirror);
        PpuSetWidescreenLayerClamp(g_ppu, clamp);
        s_ws_clamp_now = clamp;
        /* Sprites are NOT covered by that mask -- PpuWidescreenLayerExtra()
         * only consults it for layer < 4 -- so they reach the margins however
         * the BGs are clamped. Measured on the title: BG2 and BG3 clamp to 0
         * margin pixels while OBJ still puts 1623 there.
         *
         * Most of those are not real. A raw OAM X in [256, 256+extraRight) is
         * ambiguous: either a sprite the widescreen host meant to place in the
         * right margin, or one the game parked off-screen-LEFT at x-512, which
         * hardware never shows. The runner's default keeps the positive decode
         * for both, so parked sprites ghost in -- reported from play as a
         * small blinking rope along the bottom of the title.
         *
         * NOTE the setter's polarity: PpuWsSetOamRightHints(ppu, NULL) sets
         * wsOamRightHintStrict to ZERO -- permissive -- and only a non-NULL
         * pointer turns strict on. An earlier version passed NULL and a
         * comment claiming it enabled strict; it did the opposite, which left
         * parked sprites ghosting into the right margin. Reported from play as
         * a second green selection marker blinking on the scenario screen.
         *
         * A zeroed hint array is therefore "strict, nothing marked": every
         * ambiguous slot wraps negative exactly as hardware does.
         * SC_WS_OAM=0 restores the permissive decode. */
        /* THE TITLE HAS NO AMBIGUOUS PARKED SPRITES, so the strict wrap costs
         * it something for nothing.
         *
         * A raw OAM X in [256, 256+extraRight) is normally ambiguous: either a
         * sprite entering the right margin, or one parked off-screen-LEFT at
         * x-512. Strict assumes parked and wraps it negative, so an object
         * arriving from the right stays hidden until it crosses x=256 and then
         * appears all at once -- reported from play as the sign "stepping" in
         * on the right while it now leaves smoothly on the left.
         *
         * Dumping the title's OAM settles which it is here: exactly ONE sprite
         * is in the band (slot 125, raw 277, tile 120 -- the row of blinking
         * lights along the bottom), and every parked entry sits at raw 384 or
         * beyond, outside it. So on this screen the positive decode is the
         * right one for everything in the band.
         *
         * Marked per-slot rather than by turning strict off, so the decode
         * stays strict everywhere else -- the scenario selector's parked
         * sprites DO land in the band, which is what the strict default is
         * for. SC_WS_TITLE_OAM_RIGHT=0 restores the wrap. */
        /* The city view wants the positive decode as well.
         *
         * Traffic really does enter the right margin: slot 109 runs raw X
         * 256 -> 348 at 4 px a frame, about 24 frames inside the ambiguous
         * band, before going on to 352..396 which decodes to -160..-116 and
         * is correctly invisible. Strict wraps the whole run away, which is
         * the locomotive missing from the margin.
         *
         * This is only half of it -- host_map_compose() fills those columns
         * from a BG-only render, so the margin OBJ pass below is the other
         * half. Neither shows anything alone.
         *
         * SC_WS_CITY_OAM_RIGHT=0 restores the wrap. */
        if (s_ws_oam_strict && g_ram[0x14] == 0x00 && host_map_screen_live()) {
          static int on = -1;
          if (on < 0) { const char *e = getenv("SC_WS_CITY_OAM_RIGHT");
                        on = (e && *e) ? (*e != '0') : 1; }
          /* Only the slots that are MOVING.
           *
           * Hinting the whole band was too coarse: the city view parks HUD
           * sprites there too, and the blanket hint decoded them positive
           * and printed the date -- "1902 JA" -- across the right margin.
           * That is exactly the ambiguity the strict default exists for.
           *
           * The classifier already separates the two: traffic steps 4 px a
           * frame and carries motion grace, while parked HUD text does not
           * move at all. So hint per slot on that. */
          if (on && g_ppu) {
            for (int s = 0; s < 128; s++)
              if (g_ppu->wsOamMotionGrace[s])
                s_oam_right_hints[s >> 3] |= (uint8_t)(1u << (s & 7));
          }
        }
        if (s_ws_oam_strict && title_ws_live() && s_ws_widen_title) {
          static int on = -1;
          if (on < 0) {
            const char *e = getenv("SC_WS_TITLE_OAM_RIGHT");
            on = (e && *e) ? (*e != '0') : 1;
          }
          if (on) memset(s_oam_right_hints, 0xff, sizeof s_oam_right_hints);
          /* The LEFT margin is the sign's, by name.
           *
           * Hinting every left slot was tried and reverted: it let the sign
           * leave at the true edge, but it also let it SIT there -- the game
           * stops emitting it for half of its round and its slots keep the
           * last position, which showed as the sign stuck to the left border
           * for twenty seconds. The margin's motion heuristic was the other
           * way round: it admitted the sign while it moved and faded it out
           * where it stopped, which is not how it leaves on hardware either.
           *
           * src/sc_titlesign.c settles it without guessing: the sign is an
           * animation channel, so its record, its x and its six sprites are
           * all readable, and it says nothing at all while the game is not
           * showing it. Those slots are claimed, and the heuristic is off
           * here -- nothing else on the title travels through a margin. */
          title_sign_place();
        }
        /* SC_WS_SCEN_OAM_LEFT=1 (experiment): release the left hints on the
         * scenario selector ($14 = 0b). Widescreen reveals two card columns
         * the authentic 256 view never shows, and the game parks their won-
         * mark sprites at negative X (slots seen at rawX 472/480, tile 76 --
         * the same X tiles the on-screen marks use). Hardware clips those
         * entirely; a 96 px margin does not. Whether they land on the right
         * cards is the thing to look at. */
        /* The scenario selector's margin sprites are NOT a job for the
         * motion classifier.
         *
         * The won-mark drawer at 03:ded0 puts each mark at $df30,Y MINUS the
         * smooth-scroll $16. Scrolled to the ninth column, $16 is $50, so the
         * marks for bits 0 and 3 -- column 0, San Francisco and Detroit --
         * land at x = 14 - 80 = -66, inside the left margin. They are genuine
         * margin content, and with the classifier deciding, they appeared
         * only while the screen was moving and vanished when it stopped.
         * Reported from play exactly that way. The same grace also let the
         * parked second selection bracket back in as fragments during a
         * scroll -- the ghost this file already fought once.
         *
         * So on this screen the fallback is switched off and the pins and
         * marks are claimed by position instead -- every sprite of them
         * recomputed from the ROM's own records and matched against OAM
         * (selector_hint_margin_sprites). Nothing else in the margins is
         * claimed, so the hidden bracket stays hidden.
         *
         * The first version matched each mark at its record's BASE, where no
         * sprite of it sits -- the four sit 17..33 px right and 25..41 down --
         * and never matched at all, and it left the pins out. So the margin
         * cards showed neither. */
        if (s_ws_oam_strict && selector_on_screen() && g_ppu) {
          g_ppu->wsOamMotionGraceOn = 0;
          selector_hint_margin_sprites();
        } else if (g_ppu) {
          /* Off on the title too: the sign is hinted by name above. */
          g_ppu->wsOamMotionGraceOn = title_ws_live() ? 0 : 1;
        }
        /* Releasing ALL the left hints on the selector was tried once, and
         * it is wrong: slots 8-21 are the selection bracket, which the blink
         * hides by setting X bit 8 on its off phase, and releasing them paints
         * that bracket into a margin. The note that stood here went on to
         * conclude the margin cards' marks were not in OAM at all; they are
         * (at x9 463/479 for column 0 at scroll $50), the search for them just
         * looked in the wrong place. Claiming by exact position, above, takes
         * the pins and marks and leaves the bracket alone. */
        if (s_ws_oam_strict) {
          /* Strict, with only the slots this host placed itself marked. */
          PpuWsSetOamRightHints(g_ppu, s_oam_right_hints);
          PpuWsSetOamLeftHints(g_ppu, s_oam_left_hints);

        } else {
          PpuWsSetOamRightHints(g_ppu, NULL);
          PpuWsSetOamLeftHints(g_ppu, NULL);
        }
      }
      /* SC_PPU_LAYOUT=1: one line per screen, printed when $14 changes.
       * Widening a screen means knowing which BG carries its background and
       * whether that tilemap has anything in the columns the extra width
       * would expose -- the same question SC_SELECTOR_PPU answered for the
       * scenario screen, asked everywhere. */
      /* SC_TITLE_DUMP=<path>: write BG1's two tilemap pages plus the live
       * hScroll, once, for offline analysis of which cells hold what. */
      if (getenv("SC_TITLE_DUMP") && g_ppu && g_ram[0x14] == 0x01) {
        static int dumped = 0;
        if (!dumped && s_frames > 8) {
          dumped = 1;
          const unsigned m = (unsigned)PPU_bgTilemapAdr(g_ppu, 0);
          FILE *f = fopen(getenv("SC_TITLE_DUMP"), "wb");
          if (f) {
            uint16_t hdr[3];
            hdr[0] = (uint16_t)g_ppu->hScroll[0];
            hdr[1] = (uint16_t)m;
            hdr[2] = (uint16_t)PPU_bgTileAdr(g_ppu, 0);
            uint16_t hdr2[3];
            hdr2[0] = (uint16_t)PPU_objTileAdr1(g_ppu);
            hdr2[1] = (uint16_t)PPU_objTileAdr2(g_ppu);
            hdr2[2] = (uint16_t)PPU_objSize(g_ppu);
            fwrite(hdr, 2, 3, f);
            fwrite(hdr2, 2, 3, f);
            fwrite(g_ppu->vram, 2, 0x8000, f);   /* whole VRAM */
            fwrite(g_ppu->oam, 2, 0x100, f);     /* OAM low  */
            fwrite(g_ppu->highOam, 1, 32, f);    /* OAM high */
            fclose(f);
          }
        }
      }
      /* SC_OAM_BAND=1: list every OAM slot whose raw X lands in the
       * ambiguous band [256, 256+extraRight), for whatever screen is up.
       *
       * That band is the whole question the strict decode answers by
       * assuming "parked". On the title the assumption is wrong and the
       * slots are hinted permissive; on the scenario selector it is right
       * for some slots and wrong for others, which is why the selector needs
       * this rather than a blanket switch either way. Y is printed because
       * a parked sprite is usually parked in Y as well (>= 224), which
       * separates the two cases without guessing. */
      /* High OAM packs FOUR sprites per byte, two bits each: byte i>>2,
       * shift (i&3)*2. Read as i>>3 with shift (i&7)*2 -- as this did --
       * the shift runs off the end of the byte and the 9th X bit comes
       * back as zero for most slots. That is not cosmetic: it made every
       * sprite look confined to x < 256 and produced a confident, wrong
       * conclusion that the game culls its objects at the view edge. */
      if (getenv("SC_OAM_BAND") && g_ppu) {
        static int done = 0;
        if (!done && s_frames > 20) {
          done = 1;
          fprintf(stderr, "[oamband] $14=%02x extraRight=%d\n",
                  g_ram[0x14], s_ws_extra);
          for (int i = 0; i < 128; i++) {
            const unsigned lo = g_ppu->oam[i * 2];
            const unsigned hi = g_ppu->oam[i * 2 + 1];
            const unsigned hbits = g_ppu->highOam[i >> 2];
            const unsigned x9 = (lo & 0xff) | (((hbits >> ((i & 3) * 2)) & 1) << 8);
            const unsigned y = (lo >> 8) & 0xff;
            const unsigned tile = hi & 0xff;
            const unsigned attr = (hi >> 8) & 0xff;
            const int all = atoi(getenv("SC_OAM_BAND"));
            if (y < 224 && (all > 1 ||
                (x9 >= 256 && x9 < 256u + (unsigned)s_ws_extra)))
              fprintf(stderr, "  slot %3d  rawX=%3u y=%3u tile=%3u attr=%02x\n",
                      i, x9, y, tile, attr);
          }
        }
      }
      /* SC_HDMA_DIAG=1: which HDMA channels are live, per frame. The merge
       * moved HDMA onto the LLE beam timeline (upstream b8ef573), so when a
       * screen glitches one frame in four the first question is whether HDMA
       * is involved at all. */
      if (getenv("SC_HDMA_DIAG") && g_snes && g_snes->dma) {
        unsigned mask = 0;
        for (int i = 0; i < 8; i++)
          if (g_snes->dma->channel[i].hdmaActive) mask |= (1u << i);
        fprintf(stderr, "[hdma] f=%llu active=%02x\n",
                (unsigned long long)s_frames, mask);
      }
      /* SC_OAM_TRACK=1: every frame, every OAM slot that is on-screen
       * vertically and sits anywhere near the right edge. The question it
       * answers is whether the game CULLS its sprites at x=255: if it does,
       * slots walk up to the edge and vanish; if it does not, a slot keeps
       * moving smoothly through [256, 352) and widescreen could show it. */
      if (getenv("SC_OAM_TRACK") && g_ppu) {
        for (int i = 0; i < 128; i++) {
          const unsigned lo = g_ppu->oam[i * 2];
          const unsigned hi = g_ppu->oam[i * 2 + 1];
          const unsigned hb = g_ppu->highOam[i >> 2];
          const unsigned x9 = (lo & 0xff) | (((hb >> ((i & 3) * 2)) & 1) << 8);
          const unsigned y = (lo >> 8) & 0xff;
          if (y < 224 && x9 >= 180 && x9 < 400)
            fprintf(stderr, "[oamtrk] f=%llu slot=%d x=%u y=%u tile=%u\n",
                    (unsigned long long)s_frames, i, x9, y, hi & 0xff);
        }
      }
      if (getenv("SC_PPU_LAYOUT")) {
        static uint8_t last = 0xff;
        if (g_ram[0x14] != last) {
          last = g_ram[0x14];
          fprintf(stderr, "[layout]   obsel=%02x objbase1=%04x objbase2=%04x size=%d\n",
                  g_ppu->obsel, (unsigned)PPU_objTileAdr1(g_ppu),
                  (unsigned)PPU_objTileAdr2(g_ppu), (int)PPU_objSize(g_ppu));
          fprintf(stderr, "[layout] $14=%02x $01df=%u mode=%d main=%02x sub=%02x\n",
                  g_ram[0x14], g_ram[0x1df], (int)PPU_mode(g_ppu),
                  g_ppu->screenEnabled[0], g_ppu->screenEnabled[1]);
          for (int L = 0; L < 4; L++)
            if ((g_ppu->screenEnabled[0] >> L) & 1)
              fprintf(stderr, "[layout]   BG%d map=%04x wide=%d high=%d chr=%04x hs=%d vs=%d\n",
                      L + 1, (unsigned)PPU_bgTilemapAdr(g_ppu, L),
                      PPU_bgTilemapWider(g_ppu, L) ? 1 : 0,
                      PPU_bgTilemapHigher(g_ppu, L) ? 1 : 0,
                      (unsigned)PPU_bgTileAdr(g_ppu, L),
                      g_ppu->hScroll[L], g_ppu->vScroll[L]);
        }
      }
      host_map_arm_captures();
      snes->inVblank = false; snes->inNmi = false;
      /* Real "HDMA init": (re)latch each currently-enabled channel's table
       * pointer once per frame. This replaces an old `dma_startDma(dma, 0,
       * true)` call here that unconditionally zeroed every channel's
       * hdmaActive flag every frame -- harmless while nothing consumed that
       * flag, but exactly backwards for SimpleHdma_Init, which needs to see
       * whatever the game's own $420C write last set it to. */
      for (int i = 0; i < 8; i++)
        if (s_host_hdma) hdma_init_channel(&s_hdma[i], &snes->dma->channel[i]);
    } else if (snes->vPos == 225) {
      startingVblank = !ppu_checkOverscan(g_ppu);
    } else if (snes->vPos == 240) {
      if (!snes->inVblank) startingVblank = true;
    }
    if(free_ui_cursor) {
      memcpy(g_ppu->oam,mouse_oam,sizeof mouse_oam);memcpy(g_ppu->highOam,mouse_high,sizeof mouse_high);
    }
    if (startingVblank) {
      ppu_handleVblank(g_ppu);
      /* SC_VRAM_DUMP=<path>: one snapshot of VRAM plus the BG1 tilemap
       * address, taken at vblank. Written to settle the font mapping --
       * which byte the dialog renderer turns into which CHR tile -- by
       * correlation against the offline tileset, rather than by guessing
       * a stride. One-shot: the first frame that asks for it. */
      /* SC_VRAM_DUMP_ON=<screen id> waits for that screen instead of firing
       * on the first frame, so a capture of the scenario selector ($0b)
       * does not depend on hitting a key at the right moment. */
      { static int done; const char *vd = getenv("SC_VRAM_DUMP");
        const char *von = getenv("SC_VRAM_DUMP_ON");
        const int want = von && *von ? (int)strtol(von, NULL, 16) : -1;
        /* SC_VRAM_DUMP_WAIT=<n> holds the capture n frames past the first
         * frame the screen matches. Screens that draw their own text after
         * the transition -- the main menu writes its word strips a few
         * frames in -- otherwise get captured empty, which is how the first
         * menu capture came back as an untextured panel. */
        { const char *vw = getenv("SC_VRAM_DUMP_WAIT");
          const int wait = vw && *vw ? atoi(vw) : 0;
          static int seen = -1;
          if (vd && *vd && !done && g_ppu &&
              (want < 0 || g_ram[0x14] == (uint8_t)want)) {
            if (seen < 0) seen = 0; else seen++;
          }
          if (vd && *vd && !done && g_ppu && seen >= 0 && seen < wait)
            goto vram_dump_done;
        }
        if (vd && *vd && !done && g_ppu &&
            (want < 0 || g_ram[0x14] == (uint8_t)want)) {
          done = 1;
          FILE *f = fopen(vd, "wb");
          if (f) {
            fwrite(g_ppu->vram, 2, 0x8000, f);
            fclose(f);
            /* A sidecar, so a capture describes itself. The surface tool
             * needs each layer's map and CHR base, and hand-passing those
             * is exactly how a donor and a target get mismatched. */
            { char meta[520];
              snprintf(meta, sizeof meta, "%s.info", vd);
              FILE *mf = fopen(meta, "w");
              if (mf) {
                fprintf(mf, "screen=%02x\nscroll=%04x\nscenario=%u\n"
                            "bgmode=%d\n",
                        g_ram[0x14],
                        (unsigned)(g_ram[0x16] | (g_ram[0x17] << 8)),
                        (unsigned)(g_ram[0x40] | (g_ram[0x41] << 8)),
                        (int)PPU_mode(g_ppu));
                for (int L = 0; L < 4; L++)
                  fprintf(mf, "bg%dmap=%04x\nbg%dchr=%04x\n",
                          L + 1, (unsigned)PPU_bgTilemapAdr(g_ppu, L),
                          L + 1, (unsigned)PPU_bgTileAdr(g_ppu, L));
                fclose(mf);
              } }
            fprintf(stderr, "[vram] dumped 64KB to %s  screen=$%02x scroll=$%04x "
                            "$40=%u bg1map=$%04x "
                            "bg1chr=$%04x bg3map=$%04x bg3chr=$%04x\n",
                    vd, g_ram[0x14],
                    (unsigned)(g_ram[0x16] | (g_ram[0x17] << 8)),
                    (unsigned)(g_ram[0x40] | (g_ram[0x41] << 8)),
                    (unsigned)PPU_bgTilemapAdr(g_ppu, 0),
                    (unsigned)PPU_bgTileAdr(g_ppu, 0),
                    (unsigned)PPU_bgTilemapAdr(g_ppu, 2),
                    (unsigned)PPU_bgTileAdr(g_ppu, 2));
          }
        } }
      vram_dump_done:
      /* SC_OAM_DUMP=<path> -- every sprite once, when SC_OAM_DUMP_ON names
       * the screen (hex $14), SC_OAM_DUMP_WAIT frames after it appears.
       *
       * SC_OAM_WATCH reports CHANGES, which is the wrong shape for a screen
       * that is composed once and then sits there: the main menu sets its
       * sprites on screen $02 and they persist unchanged into $03, so a
       * change-triggered capture of $03 sees nothing at all. A snapshot says
       * what is on screen rather than what just moved.
       *
       * X bit 8 and the size bit come from the high table at highOam[], two
       * bits per sprite, four sprites to a byte. */
      { static int done; const char *od = getenv("SC_OAM_DUMP");
        const char *oon = getenv("SC_OAM_DUMP_ON");
        const char *ow = getenv("SC_OAM_DUMP_WAIT");
        const int want = oon && *oon ? (int)strtol(oon, NULL, 16) : -1;
        const int wait = ow && *ow ? atoi(ow) : 0;
        static int seen;
        if (od && *od && !done && g_ppu &&
            (want < 0 || g_ram[0x14] == (uint8_t)want)) {
          if (seen++ >= wait) {
            done = 1;
            FILE *f = fopen(od, "w");
            if (f) {
              fprintf(f, "# screen=%02x frame=%llu obsel=%02x objbase1=%04x objbase2=%04x\n",
                      g_ram[0x14], (unsigned long long)s_frames, g_ppu->obsel,
                      (unsigned)PPU_objTileAdr1(g_ppu),
                      (unsigned)PPU_objTileAdr2(g_ppu));
              fprintf(f, "# spr x y tile attr size\n");
              for (int i = 0; i < 128; i++) {
                const uint16_t lo = g_ppu->oam[i * 2], hi = g_ppu->oam[i * 2 + 1];
                const uint8_t hb = g_ppu->highOam[i >> 2];
                const int sh = (i & 3) * 2;
                const unsigned x = (unsigned)(lo & 0xff) |
                                   (((hb >> sh) & 1) ? 0x100u : 0u);
                fprintf(f, "%3d %4u %3u $%03x $%02x %d\n", i, x,
                        (unsigned)(lo >> 8), (unsigned)(hi & 0x1ff),
                        (unsigned)((hi >> 8) & 0xfe),
                        ((hb >> (sh + 1)) & 1));
              }
              fclose(f);
              fprintf(stderr, "[oamdump] 128 sprites -> %s  screen=$%02x\n",
                      od, g_ram[0x14]);
            }
          }
        }
      }
      /* SC_OAM_WATCH=1 -- per-frame diff of OAM, as sprite entries.
       *
       * The main map's building labels turned out not to be on any
       * background at all. In game the PPU runs mode 0 with CHR bases at
       * $2000 and $0000, and the label artwork sits at VRAM word $6600 --
       * which no background can reach, since the tile index would be 2240
       * and the field is ten bits. Cycling every tool in the toolbar changed
       * not one tilemap cell, which agrees. They are sprites, so this is
       * where the per-label tile runs have to be read from. */
      { static uint16_t shadow_oam[0x100]; static uint8_t shadow_hi[0x20];
        static int on = -1, primed;
        if (on < 0) { const char *e = getenv("SC_OAM_WATCH");
                      on = (e && *e && *e != '0'); }
        if (on && g_ppu) {
          int shown = 0, changed = 0;
          for (int i = 0; i < 0x80; i++) {
            const uint16_t lo = g_ppu->oam[i * 2], hi = g_ppu->oam[i * 2 + 1];
            if (lo == shadow_oam[i * 2] && hi == shadow_oam[i * 2 + 1]) continue;
            changed++;
            if (primed && shown < 24) {
              fprintf(stderr, "[oam] f%llu $%02x  spr%-3d x=%-3u y=%-3u tile=$%03x attr=$%02x\n",
                      (unsigned long long)s_frames, g_ram[0x14], i,
                      (unsigned)(lo & 0xff), (unsigned)(lo >> 8),
                      (unsigned)(hi & 0x1ff), (unsigned)((hi >> 8) & 0xfe));
              shown++;
            }
            shadow_oam[i * 2] = lo; shadow_oam[i * 2 + 1] = hi;
          }
          for (int i = 0; i < 0x20; i++) shadow_hi[i] = g_ppu->highOam[i];
          if (primed && changed > shown)
            fprintf(stderr, "[oam] f%llu  ... and %d more sprites\n",
                    (unsigned long long)s_frames, changed - shown);
          primed = 1;
        }
      }
      /* SC_VRAM_WATCH=<hex word addr>[+<count>] -- per-frame diff of a VRAM
       * range, plus every layer's scroll when it moves.
       *
       * Written for the main menu, where the usual routes all came up dry:
       * its tilemap ($05ABF1) and its artwork are both packets, both already
       * identified, and the German artwork renders correct German words
       * against the very same map. What differs between the regions is
       * therefore WHERE each line is taken from, and that is neither in a
       * packet nor in any text table -- so the thing to watch is the writes
       * and the scroll, not the data. SNESRECOMP_DMA_LOG is inert in this
       * target (ppudma_record_dma is stubbed off the AOT tier), which is why
       * this exists rather than reusing it. */
      { static uint16_t *shadow; static int wcount = -1; static unsigned wbase;
        static uint16_t last_h[4], last_v[4]; static int scroll_seen;
        if (wcount < 0) {
          const char *e = getenv("SC_VRAM_WATCH");
          wcount = 0;
          if (e && *e) {
            char *end = NULL;
            wbase = (unsigned)strtoul(e, &end, 16);
            wcount = (end && *end == '+') ? atoi(end + 1) : 64;
            if (wbase + (unsigned)wcount > 0x8000u) wcount = (int)(0x8000u - wbase);
            shadow = (uint16_t *)calloc((size_t)(wcount > 0 ? wcount : 1),
                                        sizeof(uint16_t));
            if (!shadow) wcount = 0;
            else fprintf(stderr, "[watch] VRAM $%04x..$%04x\n",
                         wbase, wbase + (unsigned)wcount - 1);
          }
        }
        /* The cap was 12, which hid exactly the interesting part: a label
         * redraw is ~16 cells, so the bulk arrived as "... and N more".
         * SC_VRAM_WATCH_MAX raises it. */
        static int cap = -1;
        if (cap < 0) { const char *e = getenv("SC_VRAM_WATCH_MAX");
                       cap = (e && *e) ? atoi(e) : 40; if (cap < 1) cap = 1; }
        if (wcount > 0 && g_ppu && shadow) {
          int shown = 0;
          for (int i = 0; i < wcount; i++) {
            const uint16_t now = g_ppu->vram[wbase + (unsigned)i];
            if (now == shadow[i]) continue;
            if (shown < cap)
              fprintf(stderr, "[watch] f%llu $%02x  $%04x: $%04x -> $%04x\n",
                      (unsigned long long)s_frames, g_ram[0x14],
                      wbase + (unsigned)i, shadow[i], now);
            shadow[i] = now; shown++;
          }
          if (shown > cap)
            fprintf(stderr, "[watch] f%llu  ... and %d more cells\n",
                    (unsigned long long)s_frames, shown - cap);
          /* Scroll is opt-in. Left on it buried the cell changes: a run
           * came back 5780 lines long with 240 of them the ones I wanted. */
          if (!getenv("SC_VRAM_WATCH_SCROLL")) { scroll_seen = 1; goto watch_done; }
          for (int L = 0; L < 4; L++) {
            const uint16_t h = g_ppu->hScroll[L], v = g_ppu->vScroll[L];
            if (scroll_seen && h == last_h[L] && v == last_v[L]) continue;
            fprintf(stderr, "[watch] f%llu $%02x  BG%d scroll h=%u v=%u\n",
                    (unsigned long long)s_frames, g_ram[0x14], L + 1, h, v);
            last_h[L] = h; last_v[L] = v;
          }
          scroll_seen = 1;
        watch_done: ;
        }
      }
      apply_surfaces();
      /* Every frame the selector is up, not once on entry: the game draws
       * its own cards as the screen fades in, so a single placement at
       * entry is painted over. Reported from play as the cards being right
       * for a moment and then gone. */
      ws_hide_backdrop_furniture();
      ws_fill_flat_margins();
      ws_fill_margins();
      if (getenv("SC_PASS_DIAG") && s_ws_scratch) {
        static int nf;
        long long diff = 0, tot = 0;
        int first_y = -1, first_x = -1;
        for (int y = 0; y < kVideoHeight; y++) {
          const uint32_t *a =
              (const uint32_t *)(s_ws_scratch + (size_t)y * s_video_pitch);
          const uint32_t *b =
              (const uint32_t *)(s_video_pixels + (size_t)y * s_video_pitch);
          for (int x = 0; x < s_video_w; x++) {
            tot++;
            if (a[x] != b[x]) {
              diff++;
              if (first_y < 0) { first_y = y; first_x = x; }
            }
          }
        }
        if (++nf % 30 == 0)
          fprintf(stderr,
                  "[pass] f=%d extra pass vs MAIN render differ on %lld of %lld px"
                  " first at (%d,%d)\n",
                  nf, diff, tot, first_x, first_y);
      }
      /* SC_MAPGEN_VERIFY=1: sample the guest's PRNG state once per frame.
       *
       * There is no per-opcode hook in this target -- the interp816 core does
       * not call interp816_opcode_hook, and interp_bridge.c (which owns the
       * coverage hooks) is not compiled here. Both were tried and produced no
       * output at all.
       *
       * Sampling per frame is enough anyway: the state is 32 bits, so if
       * sc_mapgen_prng_step() is correct then consecutive samples must be
       * joined by a small number of iterations. A wrong step lands on the next
       * sample essentially never. $59/$5b are direct page in bank 0. */
      if (getenv("SC_MAPGEN_VERIFY")) {
        static uint16_t p0, p1;
        static int have;
        const uint16_t g0 = (uint16_t)(g_ram[0x59] | (g_ram[0x5a] << 8));
        const uint16_t g1 = (uint16_t)(g_ram[0x5b] | (g_ram[0x5c] << 8));
        if (!have || g0 != p0 || g1 != p1) {
          fprintf(stderr, "[prng] f=%llu %04X %04X\n",
                  (unsigned long long)s_frames, g0, g1);
          p0 = g0; p1 = g1; have = 1;
        }
      }
      ws_fix_scroll_seam(); /* before compose: it copies the guest columns */
      host_map_compose();   /* all 224 visible lines are drawn by now */
      snes->inVblank = true;
      snes->inNmi = true;
      if (snes->nmiEnabled) { cpu->nmiWanted = true; s_nmi_requests++; }
      if (snes->autoJoyRead) snes->autoJoyTimer = 4224;
    }
  } else if (snes->hPos == 1024) {
    if (!snes->inVblank) {
      dma_cycle(snes->dma);
      /* Per-line HDMA transfer, same timing as plain-DMA continuation
       * above: this fires during the current line's hblank, so the values
       * it writes take effect starting with the *next* line's render, at
       * this loop's hPos==0 branch. */
      if (s_host_hdma) for (int i = 0; i < 8; i++) hdma_do_line(&s_hdma[i]);
    }
  }

  snes->hPos += 2;
  if (snes->hPos == 1364) {
    snes->hPos = 0;
    snes->vPos++;
    if (snes->vPos == 262) {
      snes->vPos = 0;
      s_frames++;
      /* SC_FRAME_BANK_TRACE=<start>,<end>: print the CPU bank:PC at every
       * frame boundary in that range -- for finding what's actually
       * executing during "gap" frames a lower-priority per-frame task
       * (like the cursor dispatcher) doesn't get a turn on, instead of
       * guessing from PC-history (too shallow to span multiple frames). */
      { const char *fbt = getenv("SC_FRAME_BANK_TRACE");
        if (fbt && *fbt) {
          static uint64_t s_fbt_start, s_fbt_end;
          static bool s_fbt_parsed;
          if (!s_fbt_parsed) {
            sscanf(fbt, "%llu,%llu", (unsigned long long *)&s_fbt_start, (unsigned long long *)&s_fbt_end);
            s_fbt_parsed = true;
          }
          if (s_frames >= s_fbt_start && s_frames <= s_fbt_end)
            fprintf(stderr, "[framebank f=%llu] k=%02x pc=%04x\n",
                    (unsigned long long)s_frames, g_cpu->k, g_cpu->pc);
        }
      }
    }
  }
}

static void parse_addr_trace(const char *spec) {
  const char *at = strchr(spec, '@');
  char buf[256];
  size_t len = at ? (size_t)(at - spec) : strlen(spec);
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, spec, len);
  buf[len] = '\0';
  if (at) s_addr_trace_start_frame = strtoull(at + 1, NULL, 0);
  char *tok = strtok(buf, ",");
  while (tok && s_addr_trace_count < SC_ADDR_TRACE_MAX) {
    unsigned bank = 0, addr = 0;
    if (sscanf(tok, "%x:%x", &bank, &addr) == 2)
      s_addr_trace_pcs[s_addr_trace_count++] = (bank << 16) | addr;
    tok = strtok(NULL, ",");
  }
}

/* Self-arming gate for SC_ADDR_TRACE, mirroring SC_DEBUG_LIVE's: don't
 * start logging until $01ed (the cursor/scroll byte under investigation)
 * first changes, so imprecise timing getting into gameplay doesn't burn
 * through the 200-hit-per-address cap on dead frames. Only takes effect
 * when SC_ADDR_TRACE_ARM_ON_01ED is set; otherwise behaves as before. */
static bool s_addr_trace_armed = true;
static uint8_t s_addr_trace_last_ed = 0xff;

/* Auto-fast-forward during map/scenario generation. Found live (bsnes
 * trace, user-captured): 03:d862 is a 10-iteration loop (X counts 9..0)
 * calling the PRNG at 00:824b/00:824f, after seeding it from the map seed
 * ($0b27-$0b29) into a rolling pair of WRAM state words ($59/$5b/$5d).
 * Confirmed procedural map generation: 01:f1f1 draws a random byte from
 * that PRNG and branches ~34%/66% into five distinct terrain-feature
 * routines. (The iteration count is seed-derived, 1-32, not the fixed 10
 * this comment used to claim.) This is genuine, real computation, not a dumb
 * idle-delay loop (confirmed: our interpreter already finishes every
 * frame's work in far under the 16.67ms budget -- SC_FRAME_TIME never
 * fires -- so the "wait" is the ROM deliberately spreading this work
 * across many real seconds of paced frames, not CPU cost). Rather than
 * replacing the algorithm (risk: any mismatch could produce a different
 * generated map than the real ROM would), just detect execution passing
 * through it and apply the same frame-batching fast-forward uses,
 * automatically, without needing Tab held. Also covers the Nintendo
 * LC_LZ5-style decompressor at 00:90dd (confirmed live: fires repeatedly
 * during the same map/scenario-load wait, decompressing tile/text data --
 * see tools/extract_graphics.py and docs/REFERENCE_third_party_optimization_patch.md).
 *
 * DEFAULT OFF as of the settings-menu work -- this fired during ordinary
 * gameplay, not just the load screen, and the resulting intermittent 6x
 * bursts made the game feel rough and badly worsened the fast-forward
 * audio delay of the time (since fixed: the audio drain trims the backlog).
 * Measured with SC_ADDR_TRACE on the three trigger PCs against real
 * gameplay save states: on the classic map screen it fired sporadically
 * (~4 times in 2000 frames, each arming a 20-frame boost), but on the
 * View screen it fired roughly every 10-13 frames -- i.e. that screen sat
 * in effectively *continuous* turbo, since each hit re-arms the holdoff
 * before the previous one expires.
 *
 * Root cause of the false positives: 00:824b is the game's PRNG (see the
 * ROM_MAP entry -- an additive generator over $59/$5b, taking no input,
 * which is what rules out the "checksum" reading these comments used to
 * carry), so it is called constantly throughout ordinary simulation, not
 * just during map generation. Same conclusion as before -- it is a bad
 * trigger -- but for a much more obvious reason. The older wording below is
 * kept only because the decision it justified still stands:
 * 00:824b is the shared checksum/hash
 * accumulator, not map-generation-specific code -- the game calls it
 * during normal simulation too. It's also redundant as a trigger, since
 * 03:d862 (the map-gen loop that calls it) is itself already a trigger,
 * so it's dropped from the trigger set entirely rather than merely gated.
 * The two remaining triggers are genuinely load-specific. Toggle the
 * feature from the F10 settings menu ("AUTO TURBO ON LOAD"); Tab-held
 * manual fast-forward is unaffected either way. */
/* AUTO TURBO is DELETED, not merely defaulted off.
 *
 * Reported from play: it makes the sound laggy. That matches the long note
 * above -- intermittent 6x bursts during ordinary gameplay made the game feel
 * rough and worsened the fast-forward audio delay. A setting whose only honest
 * advice is "leave it alone" is worse than no setting, so the row and the flag
 * are gone.
 *
 * What it existed for became MAPGEN TURBO, and the WHOLE FAMILY is now gone.
 * The decompiled map generator (SC_MAPGEN_FAST, hooked at 01:f1ed) removes the
 * wait these were collapsing: measured, the map completes at frame 90 whether
 * the boost is 16x or off. The decompressor half went with it -- 00:90dd fires
 * on screen transitions and loads during ordinary play, so boosting it
 * advanced the SIMULATION, which is how the whole thing was noticed ("seems to
 * speed up the simulation, on the normal map, not the map generation").
 *
 * Nothing host-side now runs the guest faster except Tab-held fast-forward and
 * DRAG TURBO, both of which are explicit, held gestures. */
static unsigned long s_gen_trigger_hits;
static int s_bank_profile;
static unsigned long long s_bank_ops[256];
static unsigned long long s_b3_page[256];
static int s_pc_profile_page=-1;
static unsigned long long s_pc_profile_ops[256];
/* Which bank the per-page breakdown covers. Default 03 (the simulation),
 * but the overview-map load lives in banks 00/02, and a breakdown nailed
 * to one bank cannot see that. SC_BANK_PROFILE_PAGE=02. */
static int s_bank_page_sel = 3;
static uint64_t s_interpreter_bank_ops[256],s_interpreter_pages[256],s_interpreter_pc[256];

static unsigned long long s_tick_count, s_tick_frames_total, s_tick_ops_total;
static unsigned long long s_tick_frames_max, s_tick_start_frame, s_tick_start_ops;

/* Loading the stock save codec drops power flags. Rebuild the real network
 * after SRAM and the expanded map are restored, before zones can decline.
 * Truttle1's PowerBugPatch identified the original dropout; no patch bytes
 * are redistributed. The host solve also respects plant capacity and every
 * expanded-map cell instead of temporarily powering the stock 12,000 cells. */
static bool s_power_fix = true;
static bool s_mute_city_warnings;
static uint32_t s_power_fix_hits;
static void restore_loaded_power(void);
static void apply_power_fix(void) { restore_loaded_power(); }

/* Record one executed guest PC into the coverage bitmaps.
 *
 * Factored out of the per-opcode loop so the fiber host can feed the same
 * bitmaps through the bridge PC hook. Takes the PC and widths as arguments
 * rather than reading g_cpu, because in fiber mode g_cpu never executes. */
static void sc_note_executed_pc(uint32_t pc24, int mf, int xf) {
  /* SC_INTERP_PROFILE=1: where does interpreted time actually go?
   *
   * With SC_FIBER the AOT tier runs compiled bodies, but a 600-frame qualify
   * still interprets ~2.8M opcodes against 5263 bounces -- so the interpreter
   * carries most of the work, and that mass is what a native HLE would remove.
   * This buckets interpreted PCs by 256 bytes so the hot routines can be
   * ranked and picked off. Only fires in AOT-linked targets; the interp816
   * build never calls this hook. */
  {
    static int prof = -1;
    if (prof < 0) { const char *e = getenv("SC_INTERP_PROFILE"); prof = (e && *e) ? 1 : 0; }
    if (prof) {
      static uint32_t bucket[256 * 256];   /* bank<<8 | (addr>>8) */
      static unsigned long long total;
      const unsigned idx = ((pc24 >> 16) & 0xffu) << 8 | ((pc24 >> 8) & 0xffu);
      bucket[idx]++;
      if (++total % 2000000ULL == 0) {
        /* Pick the top 12 by repeated max, EXCLUDING what is already picked.
         * The insertion version this replaced never excluded, so one hot
         * bucket filled most of the list -- a profile showing the same page
         * and the same count a dozen times, which is nonsense that looks like
         * data. */
        unsigned top[12];
        int ntop = 0;
        for (int k = 0; k < 12; k++) {
          unsigned best = 0; uint32_t bestv = 0;
          for (unsigned i = 0; i < 256u * 256u; i++) {
            if (!bucket[i]) continue;
            int taken = 0;
            for (int j = 0; j < ntop; j++) if (top[j] == i) { taken = 1; break; }
            if (taken) continue;
            if (bucket[i] > bestv) { bestv = bucket[i]; best = i; }
          }
          if (!bestv) break;
          top[ntop++] = best;
        }
        fprintf(stderr, "[profile] %llu interpreted opcodes; hottest 256-byte pages:\n", total);
        for (int k = 0; k < ntop; k++)
          fprintf(stderr, "   %02X:%02Xxx  %9u  %5.1f%%\n",
                  top[k] >> 8, top[k] & 0xff, bucket[top[k]],
                  100.0 * (double)bucket[top[k]] / (double)total);
      }
    }
  }
  const uint8_t bank = (uint8_t)((pc24 >> 16) & 0xff);
  const uint16_t pc  = (uint16_t)(pc24 & 0xffff);
  if (s_pc_bitmap_bank >= 0 && bank == s_pc_bitmap_bank && pc >= 0x8000 &&
      s_frames >= s_pc_bitmap_start_frame) {
    uint32_t idx = pc - 0x8000;
    s_pc_bitmap[idx >> 3] |= (uint8_t)(1u << (idx & 7));
  }
  if (s_pc_bitmap_bank == -2 && pc >= 0x8000 && bank < 64 &&
      s_frames >= s_pc_bitmap_start_frame) {
    uint32_t idx = pc - 0x8000;
    s_pc_bitmap_all[bank][idx >> 3] |= (uint8_t)(1u << (idx & 7));
  }
  if (s_mx_bitmap && pc >= 0x8000 && bank < 64) {
    uint32_t idx = pc - 0x8000;
    int mx = ((mf ? 1 : 0) << 1) | (xf ? 1 : 0);
    s_mx_bitmap[mx][bank][idx >> 3] |= (uint8_t)(1u << (idx & 7));
  }
  if (bank < 64) s_banks_seen |= (1ULL << bank);
}

/* Compiled bodies ENTERED, as manifest-style keys (pc24:MmXn).
 *
 * Deliberately not folded into the bitmaps. A bounce reports an entry, not an
 * extent -- the body then runs an unknown number of opcodes silently. The
 * manifest min_pc24/max_pc24 cannot fill that in either: those bounds swallow
 * nested routines and stop short of a truncated return (docs/OPEN_QUESTIONS.md
 * F5), so expanding them would manufacture coverage that never executed --
 * the same class of error as contaminating a union with SC_FREEZE runs.
 *
 * Reported separately so a tool can JOIN on the key, which is exactly how the
 * program manifest is indexed. */
#define SC_AOT_VARIANTS_MAX 4096
static uint32_t s_aot_variant[SC_AOT_VARIANTS_MAX];  /* (pc24<<2)|(m<<1)|x */
static uint32_t s_aot_variant_hits[SC_AOT_VARIANTS_MAX];
static int      s_aot_variant_count;
static uint64_t s_aot_variant_overflow;

static void sc_note_aot_entry(uint32_t pc24, int mf, int xf) {
  uint32_t key = (pc24 << 2) | ((mf ? 1u : 0u) << 1) | (xf ? 1u : 0u);
  for (int i = 0; i < s_aot_variant_count; i++)
    if (s_aot_variant[i] == key) { s_aot_variant_hits[i]++; return; }
  if (s_aot_variant_count >= SC_AOT_VARIANTS_MAX) { s_aot_variant_overflow++; return; }
  s_aot_variant[s_aot_variant_count] = key;
  s_aot_variant_hits[s_aot_variant_count] = 1;
  s_aot_variant_count++;
}

#ifdef SC_AOT_TIER
/* â”€â”€ SC_FIBER=1: run the guest inside the fiber (migration step 3d) â”€â”€â”€â”€â”€â”€â”€
 *
 * The driver itself lives in src/sc_fiberdrive.c, because it needs
 * cpu_state.h and that header declares a global `CpuState g_cpu` which
 * collides with this file's `Interp816 *g_cpu`. Keeping it in its own
 * translation unit is cheaper than renaming a symbol used several hundred
 * times here.
 *
 * Strictly opt-in. Without SC_FIBER the interpreter path below is untouched,
 * because that path is this project's correctness baseline and every
 * `--qualify` number rests on it. */
static bool s_fiber_mode;
#endif
/* SC_BEAM_LEGACY=1: leave the beam to snes.c's own DMA and $4212 steps, as
 * before sc_own_the_beam() -- for A/B comparisons only. */
static bool s_own_beam = true;
#ifdef SC_AOT_TIER
/* What was asked for, and whether a human asked. The difference matters
 * only on a non-US ROM: an explicit SC_FIBER=1 there is an error worth
 * stopping for, while the mere default quietly steps down to the
 * interpreter so every region stays playable. */
static int  s_fiber_want;
static int  s_fiber_explicit;

/* One host frame in the fiber model: advance the PPU and devices for a whole
 * frame (so raster effects still work line by line, per MIGRATION_step3 Â§4),
 * release the vblank wait the way the NMI handler would, then let the guest
 * run until its vblank HLE hands the frame back. */
/* Beam advance, exposed to the frame driver (src/sc_fiberdrive.c).
 * handle_pos_stuff() is static and deeply tied to this file, so the driver
 * calls in rather than duplicating the device model. */
/* One beam step, catching the APU up on the same cadence the per-opcode path
 * uses.
 *
 * snes_catchupApu() clamps apuCatchupCycles to 10000 SPC cycles as a runaway
 * guard (snes.c). The interpreter never trips it, because it catches up after
 * every opcode -- ~48 master cycles, about 2 SPC cycles. A beam advance that
 * runs a whole frame and then catches up once does trip it, hard: one frame is
 * 357368 master, i.e. ~17046 SPC cycles, so the clamp silently discarded 41%
 * of every frame's audio. That was the entire fiber-host audio shortfall --
 * 220 samples/frame measured against the real 534, failing --qualify's
 * >=500/frame bar while logic and video were both fine.
 *
 * Catching up every 24 steps (48 master) reproduces an average opcode's
 * cadence, which keeps the accumulator three orders of magnitude below the
 * clamp. */
unsigned long g_beam_steps;
static void sc_beam_step(void) {
  static unsigned steps;
  g_beam_steps++;
  handle_pos_stuff();
  g_snes->apuCatchupCycles += 2.0 * kApuCyclesPerMaster;
  if (++steps >= 24) { steps = 0; sc_catchup_apu(g_snes); }
}
void sc_advance_beam_one_frame(void) {
  uint64_t before = s_frames;
  unsigned guard = 0;
  while (s_frames == before && guard++ < 400000) {
    sc_beam_step();
  }
  sc_catchup_apu(g_snes);
}

/* Advance until the auto-joypad read has finished.
 *
 * This is what makes a per-frame host viable for this ROM at all. $4212 bit 0
 * is literally `autoJoyTimer > 0` (snes.c), armed with 4224 at vblank start
 * and counted down by the beam. The game waits on it every frame at 00:9280
 * (`LDA $4212 ; AND #$01 ; BNE`), from both 00:8151 and 00:8201. If the guest
 * gets the frame while that timer is still running, it spins forever, because
 * nothing advances the beam while the guest holds the CPU -- which is exactly
 * how the first fiber attempt deadlocked.
 *
 * Draining it here costs ~4224 master cycles at the top of the frame and needs
 * no HLE and no per-routine knowledge: the host simply does not hand over a
 * frame whose input latch is still busy. */
void sc_advance_until_input_ready(void) {
  unsigned guard = 0;
  while (g_snes->autoJoyTimer && guard++ < 40000) {
    sc_beam_step();
  }
  sc_catchup_apu(g_snes);
}

/* Catch a vblank entry that happened inside the bridge rather than in this
 * host's beam loop.
 *
 * Both sides move snes->hPos/vPos (see MIGRATION_step3 21), and only one of
 * them raises NMI. handle_pos_stuff() detects the entry by SAMPLING the beam at
 * hPos==0 on the overscan line; snes_advance_beam() (snes.c) just assigns
 * `inVblank = v >= 225` as it goes. So whenever the guest's own execution
 * carried the beam across line 225, the host's sample for that frame never
 * occurred: no ppu_handleVblank, no NMI, no auto-joypad arm. Measured cost --
 * 425 NMIs against the per-opcode host's 592 over 600 frames, a 28% shortfall,
 * which is the whole of the two hosts' phase divergence.
 *
 * inNmi is the discriminator: handle_pos_stuff() sets it on a processed entry
 * and snes_advance_beam() never touches it, so `inVblank && !inNmi` means
 * exactly "the beam is in vblank and nobody processed getting there". */
static void sc_catch_missed_vblank(void) {
  Snes *snes = g_snes;
  if (!snes->inVblank) { snes->inNmi = false; return; }
  if (snes->inNmi) return;
  ppu_handleVblank(g_ppu);
  snes->inNmi = true;
  if (snes->nmiEnabled) { g_cpu->nmiWanted = true; s_nmi_requests++; }
  if (snes->autoJoyRead) snes->autoJoyTimer = 4224;
}
/* Master cycles from the current beam position to the frame boundary.
 * 1364 per scanline, 262 lines per frame. */
static uint64_t sc_cycles_to_frame_end(void) {
  int64_t c = (int64_t)(262 - (int)g_snes->vPos) * 1364 - (int64_t)g_snes->hPos;
  return c > 0 ? (uint64_t)c : 0;
}

/* One host frame.
 *
 * The frame boundary -- the instant that decides what gets presented -- must
 * fall while the guest is STOPPED. That is the whole content of this function,
 * and it took three attempts to get right.
 *
 * The bridge advances the beam by the guest's own master cycles as it runs
 * (interp_bridge.c's per-opcode snes_sync_master_clock moves the same
 * hPos/vPos this host's beam loop does). So the original shape -- run the beam
 * all the way round, present, then hand the guest a flat 357368 cycles -- let
 * vPos WRAP MID-BURST, presenting a screen sampled from part-way through the
 * guest's update. Those are frames the per-opcode host never renders, because
 * it interleaves guest writes across the scanlines as they are drawn, and they
 * are what made already-fixed widescreen defects appear to come back.
 *
 * Two things that did NOT work, both measured, both recorded in
 * docs/TODO_fiber_rendering.md: resizing the flat budget (worse in both
 * directions, 0/17 frames identical at every value tried against 9/17 at
 * exactly one frame), and an earlier two-slice split that double-counted the
 * guest clock and fired vblank detection twice (0/17, master cycles nearly
 * doubled).
 *
 * What works is to size each slice from the ACTUAL BEAM POSITION rather than
 * guess, and to cross the boundary in between:
 *
 *   park at vblank entry, guest stopped   -> active display rendered cleanly
 *   NMI
 *   slice A, bounded by cycles-to-wrap    -> guest cannot carry the beam over
 *   host crosses the boundary, guest stopped  -> THE PRESENT
 *   slice B, the rest of the frame budget -> guest's main-loop work
 *
 * The guest still receives one frame of cycles per host frame, so pacing is
 * unchanged. It simply is not holding the CPU at the instant that matters. */
static bool run_one_frame_fiber(void) {
  uint64_t before = s_frames;
  bool ok = true;
  const uint64_t kFrameCycles = 357368u;

  /* Render the active display with the guest quiescent in its 00:9311 wait,
   * which is the hardware order: scan out, then vblank, then let the game
   * write the PPU while nothing is being scanned. */
  { unsigned guard = 0;
    while (!g_snes->inVblank && s_frames == before && guard++ < 400000)
      sc_beam_step();
    sc_catchup_apu(g_snes); }

  /* Hand the host's NMI request to the guest rather than faking its effect --
   * handle_pos_stuff() raises it on g_cpu, the Interp816, which never executes
   * in fiber mode. Forging g_ram[$b9]=1 released 00:930d's wait but skipped
   * the rest of 00:80B2, i.e. the per-frame PPU work. */
  sc_catch_missed_vblank();
  bool nmi_pending = false;
  if (g_cpu->nmiWanted) { g_cpu->nmiWanted = false; nmi_pending = true; }

  { static uint64_t last_guest_master;
    const uint64_t guest_start = ScFiberDrive_MasterCycles();

    /* Slice A: bounded so the beam stops short of the wrap. */
    { uint64_t a = sc_cycles_to_frame_end();
      if (a > kFrameCycles) a = kFrameCycles;
      if (a < 1364u) a = 1364u;
      ok = ScFiberDrive_RunGuestSlice(s_frames, nmi_pending, a); }

    /* Cross the boundary with the guest stopped. This is the present. */
    { unsigned guard = 0;
      while (s_frames == before && guard++ < 400000) sc_beam_step();
      sc_catchup_apu(g_snes); }

    /* Slice B: the remainder of this frame's budget, capped so the beam
     * cannot reach the NEXT boundary either. No NMI -- one per frame. */
    { const uint64_t used = ScFiberDrive_MasterCycles() - guest_start;
      if (used < kFrameCycles) {
        uint64_t b = kFrameCycles - used;
        uint64_t room = sc_cycles_to_frame_end();
        if (room && b > room) b = room;
        if (b >= 1364u)
          ok = ScFiberDrive_RunGuestSlice(s_frames, false, b) && ok;
      } }

    s_nmi_serviced = ScFiberDrive_NmiDelivered();

    /* Mirror the guest clock into the host counter ONCE per frame. Doing this
     * per slice is how the earlier attempt double-counted. */
    uint64_t now = ScFiberDrive_MasterCycles();
    if (now > last_guest_master) {
      uint64_t delta = now - last_guest_master;
      g_master_cycles += delta;
      /* Pace the APU off GUEST time as well as host beam time.
       *
       * The beam is advanced by both sides: this host's sc_beam_step(), and
       * the bridge's per-opcode snes_sync_master_clock() (interp_bridge.c),
       * which moves the same snes->hPos/vPos by the guest's master delta. So
       * a frame is split between them in a ratio that varies per frame, and
       * whichever side moves the beam, the elapsed wall time is the same.
       *
       * Pacing the SPC off host beam steps alone therefore starved it exactly
       * in proportion to how much of the frame the guest had consumed. It was
       * not a small effect: measured per-frame DSP production was ~540 on
       * host-heavy frames and *0* on guest-heavy ones, about 30% of frames,
       * dragging the average to 418/frame against --qualify's >=500 bar.
       * (SC_APU_DIAG=1 prints that ledger.)
       *
       * The bridge does not cover this itself: bridge_apu_flush() takes the
       * absolute-timeline early-out, which clears its pending master count
       * without advancing the SPC because it expects an RtlRunFrame host to
       * do the sync. This host does not call RtlRunFrame, so the guest's
       * share of the frame reached the APU from nowhere at all.
       *
       * Chunked because snes_catchupApu() clamps the accumulator at 10000 SPC
       * cycles as a runaway guard, and a whole frame is ~17046 -- the same
       * clamp sc_beam_step() has to stay under. 4096 master is ~195 SPC. */
      while (delta) {
        uint32_t chunk = delta > 4096u ? 4096u : (uint32_t)delta;
        g_snes->apuCatchupCycles += (double)chunk * kApuCyclesPerMaster;
        sc_catchup_apu(g_snes);
        delta -= chunk;
      }
    }
    last_guest_master = now; }

  sc_catch_missed_vblank();
  return ok;
}
#endif /* SC_AOT_TIER */

/* s_fiber_mode only exists in the AOT build. */
#ifdef SC_AOT_TIER
static bool sc_fiber_active(void) { return s_fiber_mode; }
#else
static bool sc_fiber_active(void) { return false; }
#endif

static int      s_disaster_bit = -1;
static uint64_t s_disaster_frame;

static void scenario_event_tick(void);            /* defined with the menu */
static void arm_scenario_event(unsigned idx, uint16_t countdown, const char *what);
static void service_disaster_menu8(void);

/* SC_SCENARIO_EVENT=<meltdown|ufo>@<frame>: the headless twin of the F10
 * MELTDOWN / UFO rows, so the trigger can be verified without a human at the
 * window. */
static int      s_scen_event_idx = -1;
static uint16_t s_scen_event_cd;
static uint64_t s_scen_event_frame;
static const char *s_scen_event_name;

static void sc_maybe_trigger_scenario_event(void) {
  if (s_scen_event_idx < 0 || s_frames < s_scen_event_frame) return;
  arm_scenario_event((unsigned)s_scen_event_idx, s_scen_event_cd, s_scen_event_name);
  s_scen_event_idx = -1;
}

/* Zero the cursor step-delay countdown every frame.
 *
 * 01:c0dd gates the cursor step on $01f3: `LDA $01f3 ; BEQ +4 ; DEC $01f3 ;
 * RTS` -- while it is nonzero the step is skipped, which is what makes holding
 * a button act once per few frames instead of continuously (bulldozing "only
 * step by step").
 *
 * The US build fixes this by patching the two `STA $01f3` immediates from 3 to
 * 0 in ROM. $01f3 is WRAM, so zeroing it here does the same thing without a
 * byte patch -- and therefore works on EVERY region, including the ones whose
 * ROM sites we have never located. 16-bit, so both halves. */
static bool s_fast_ticks = true;

/* RCI development cadence multiplier; city time is unchanged. */
static int s_development_override; /* 0 = use the saved city default */
#define s_development_speed (s_development_override?s_development_override:(int)ScWorldDevelopmentSpeed(&s_world))
static ScMouseDialog s_mouse_dialog;
static const int kDevelopmentSpeeds[] = {0, 1, 2, 3, 5, 10, 20, 50};
static const char *const kDevelopmentSpeedNames[] = {"OFF", "X1", "X2", "X3", "X5", "X10", "X20", "X50"};
static ScDevelopment s_development;
static ScDevelopmentBatches *s_development_batches;
static bool s_development_batch_reference;
static bool s_native_bind_eager;
static uint64_t s_native_bind_deferred,s_native_bind_fallback;
static ScPopulation s_population;
static ScPopulationCensus *s_population_census;
static ScRefreshClock s_population_clock;
static ScPowerRefresh s_power_refresh;
static bool s_native_power_active;
static int s_population_game_speed=-1;
static unsigned s_population_clock_cells;
static unsigned s_map_cycle_remainder; /* fractional spatial work, below two master clocks */
static void reset_refresh_clocks(void) {
  ScDevelopmentBatchesReset(s_development_batches);
  ScRefreshClockReset(&s_population_clock,200);
  ScPowerRefreshReset(&s_power_refresh);
  s_native_power_active=false; s_population_game_speed=-1;s_population_clock_cells=0;
  s_map_cycle_remainder=0;
}
static int s_pop_override = -1;
static void population_saved_city(bool save);
static char s_population_path[1100];

static void population_saved_city(bool save) {
  if (!ScSram_Active() || !*s_population_path || g_snes->cart->ramSize < 0x8000) {
    if (!save) ScPopulationImport(&s_population, g_ram);
    return;
  }
  uint8_t data[SC_POPULATION_CITIES_BYTES];
  ScPopulationCitiesInit(data);
  FILE *f = fopen(s_population_path, "rb");
  if (f) {
    bool ok = fread(data, 1, sizeof data, f) == sizeof data;
    ok = fgetc(f) == EOF && ok; fclose(f);
    if (!ok || !ScPopulationCitiesValid(data, sizeof data)) ScPopulationCitiesInit(data);
  }
  unsigned flag_addr = save ? 0x423 : 0x421;
  unsigned slot = !save && s_city_loading?s_loading_slot:(g_ram[flag_addr] | (g_ram[flag_addr+1]<<8)) == 1 ? 0 : 1;
  if (!save) {
    if (!ScPopulationCityLoad(&s_population, data, sizeof data, g_snes->cart->ram, slot))
      ScPopulationImport(&s_population, g_ram);
    else ScPopulationMirror(&s_population, g_ram);
    return;
  }
  if (g_snes->cart->ram[5+slot] != 1) return; /* failed native save */
  ScPopulationCitySave(data, g_snes->cart->ram, slot, &s_population);
  /* The native city save is complete at this PC. Flush it before metadata;
   * a stale sidecar never matches a newly changed city after an interrupted save. */
  ScSram_Flush();
  char temporary[1120]; snprintf(temporary, sizeof temporary, "%s.tmp", s_population_path);
  f = fopen(temporary, "wb");
  bool ok = f && fwrite(data, 1, sizeof data, f) == sizeof data;
  if (f) ok = fclose(f) == 0 && ok;
#ifdef _WIN32
  if (ok) ok = MoveFileExA(temporary, s_population_path,
    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  if (ok) ok = rename(temporary, s_population_path) == 0;
#endif
  if (!ok) { remove(temporary); fprintf(stderr, "population: could not save %s\n", s_population_path); }
}

/* Guest frames per host frame while a mouse button is held. 1 = off. */
static int s_drag_turbo = 1;
static void world_saved_city(bool save) {
  if (!ScSram_Active() || !*s_world_path || g_snes->cart->ramSize<0x8000) return;
  size_t size=ScWorldCitiesSize(); uint8_t *data=malloc(size);
  if (!data) { fprintf(stderr,"world: save metadata allocation failed\n"); return; }
  ScWorldCitiesInit(data);
  FILE *f=fopen(s_world_path,"rb");
  if (f) {
    fseek(f,0,SEEK_END);long old_size=ftell(f);rewind(f);
    uint8_t *old=old_size>0 && (size_t)old_size<=size?malloc(old_size):NULL;
    bool ok=old && fread(old,1,old_size,f)==(size_t)old_size;fclose(f);
    if(!ok || !ScWorldCitiesUpgrade(data,old,old_size)) ScWorldCitiesInit(data);
    free(old);
  }
  unsigned addr=save?0x423:0x421;
  unsigned slot=!save && s_city_loading?s_loading_slot:(g_ram[addr]|(g_ram[addr+1]<<8))==1?0:1;
  if (!save) {
    if (ScWorldCityLoad(&s_world,data,size,g_snes->cart->ram,slot)) {
      fprintf(stderr,"world: loaded city slot %u (%s)\n",slot+1,s_world.active?(s_world.mega?"3840x3200":s_world.colossal?"1920x1600":s_world.giant?"960x800":s_world.huge?"480x400":"240x200"):"120x100");
      if (s_world.active) {
        bool hud=(g_ram[0x1d7]|g_ram[0x1d8])!=0;
        ram_set_w(0x1c5,ScWorldWidth(&s_world)-(hud?25:30));
        ram_set_w(0x1c9,ScWorldHeight(&s_world)-(hud?22:26));
      }
    }
    free(data); return;
  }
  if (g_snes->cart->ram[5+slot]!=1 || !ScWorldCitySave(data,g_snes->cart->ram,slot,&s_world)) { free(data); return; }
  ScSram_Flush();
  char temporary[1120]; snprintf(temporary,sizeof temporary,"%s.tmp",s_world_path);
  f=fopen(temporary,"wb"); bool ok=f && fwrite(data,1,size,f)==size;
  if (f) ok=fclose(f)==0 && ok;
#ifdef _WIN32
  if (ok) ok=MoveFileExA(temporary,s_world_path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
  if (ok) ok=rename(temporary,s_world_path)==0;
#endif
  if (!ok) { remove(temporary); fprintf(stderr,"world: could not save %s\n",s_world_path); }
  free(data);
}
/* Use the native codec on a private cartridge image. Neither an interrupted
 * load/save nor the half-second SRAM writer may publish it into slots 1/2. */
static bool test_city_private_sram(const uint8_t *native) {
  if(s_test_swap || g_snes->cart->ramSize!=sizeof s_test_native_backup)return false;
  memcpy(s_test_native_backup,g_snes->cart->ram,sizeof s_test_native_backup);
  memcpy(s_test_names_backup,g_ram+0xb65,sizeof s_test_names_backup);
  s_test_flags_backup=ram_w(0x44);
  ScSram_Suspend(true);s_test_swap=true;
  if(native)memcpy(g_snes->cart->ram,native,sizeof s_test_native_backup);
  else g_snes->cart->ram[5]=0; /* failed native compression cannot look saved */
  return true;
}
static void test_city_restore_sram(void) {
  if(!s_test_swap)return;
  memcpy(g_snes->cart->ram,s_test_native_backup,sizeof s_test_native_backup);
  memcpy(g_ram+0xb65,s_test_names_backup,sizeof s_test_names_backup);
  ram_set_w(0x44,s_test_flags_backup);
  s_test_swap=false;ScSram_Suspend(false);
}
static bool test_city_begin_load(void) {
  uint32_t n;const uint8_t *record=ScSram_Extra(&n);
  if(!n) {
    s_test_generate_pending=true;
    s_city_present_pending=true;s_city_fade_started=s_city_black_seen=false;
    return true;
  }
  ScWorld *world=malloc(sizeof *world);ScPopulation population;
  bool valid=world && ScTestCityDecode(record,n,world,&population);free(world);
  if(!valid) {fprintf(stderr,"[test city] City 3 record is invalid; saved file preserved\n");return false;}
  if(!test_city_private_sram(ScTestCityNativeSave(record,n)))return false;
  s_test_load_pending=true;ram_set_w(0x421,1);
  s_city_present_pending=true;s_city_fade_started=s_city_black_seen=false;
  return true;
}
static void test_city_finish_save(void) {
  size_t n=ScTestCityRecordSize();uint8_t *record=malloc(n);
  bool ok=record && ScTestCityEncode(record,n,g_snes->cart->ram,&s_world,&s_population);
  test_city_restore_sram();s_test_saving=false;
  ok=ok && ScSram_SetExtra(record,(uint32_t)n);free(record);
  if(ok) {ScSram_Flush();ok=ScSram_Stored();}
  if(ok)fprintf(stderr,"[test city] saved City 3 in SRAM; normal slots preserved\n");
  else fprintf(stderr,"[test city] could not save City 3; previous saves preserved\n");
}
static const int kDragTurbos[] = { 1, 2, 3, 4, 6 };

/* SC_NINTH=1: a ninth scenario slot, and the wider scroll it needs.
 *
 * The selector is a 4x2 grid bounded in three places (all 8-bit A):
 *
 *   03:ddba  LDA #$02 ; LDX $42 ; BPL +1 ; INC A ; STA $79
 *              $79 = max column: 2, or 3 once every scenario is beaten
 *   03:de00  CMP #$03 ... LDA #$06 / LDA #$07
 *              column 3 maps to index 6 (row 0) or 7 (row 1)
 *   03:de27  CPX #$0003 ; LDA #$0050 ; STA $22
 *              column 3 scrolls the view to $50
 *
 * The ninth map already exists: the pointer table at 03:ce70 carries NINE
 * entries and index 8 decodes cleanly (docs/ROM_MAP.md). Only the selector
 * caps out. What does NOT exist is a ninth seed -- the per-scenario tables at
 * 03:cec9 are eight entries, so index 8 would read past them into whatever
 * follows.
 *
 * Done with PC hooks rather than ROM patches: each of the three sites is a
 * value the host can simply overwrite the instant the ROM has written it,
 * which needs no free ROM space and leaves every byte of the image intact. */
/* Exact-fingerprint match against the pristine US image. Every ROM-address
 * hook in this file is a US address, so all of them have to be gated on it:
 * the other four regions are the same game at different offsets, where the
 * same PC is some unrelated instruction. */

static uint32_t s_rom_fnv;      /* FNV-1a of the ROM file, see main() */
static uint32_t s_tr_off, s_tr_len;  /* SC_TRANSLATION, for the recheck */
/* Translated scenario briefings, keyed by the address they decompress FROM.
 * The game unpacks each briefing through 00:90dd, so the substitution goes
 * in at the decompressor's exit -- the same hook Sylt's own briefing has
 * used all along, generalised from one address to the twelve. */
enum { kMaxBriefs = 16 };
static uint32_t s_brief_src[kMaxBriefs], s_brief_len[kMaxBriefs];
static uint8_t *s_brief_data[kMaxBriefs];
static int      s_brief_count;
static uint32_t s_brief_decomp_src, s_brief_out;
/* Scenario picture tiles -- the cards and the HUD word-strips. Artwork, not
 * text, so a translation of them is a different set of pixels rather than a
 * different string. Substituted at the same decompressor exit. */
static uint8_t *s_scen_tiles; static uint32_t s_scen_tiles_len;
/* Accent glyphs the US font has no drawing for. Each is a tile index into
 * the decompressed font plus its 16 stored bytes; the dialog renderer
 * indexes that font by character code, so the index IS the code. */
enum { kMaxGlyphs = 32, kFontTile = 16 };
static uint16_t s_glyph_idx[kMaxGlyphs];
static uint8_t  s_glyph_px[kMaxGlyphs][kFontTile];
static int      s_glyph_count;
/* Translated briefing pages, carried as STRINGS and composed here the way
 * sylt_write_brief_tilemap() composes Sylt's. Nothing about the donor's
 * tilemap travels: bases, space aliases, punctuation layout and page
 * pairing all stop mattering, because this side does the drawing.
 *
 * The title uses a different glyph bank than the body ($000/$030 against
 * $690/$6c0) -- that is what makes it a different colour on screen. */
enum { kBpPages = 16, kBpLines = 56, kBpChars = 33 };
static uint32_t s_bp_src[kBpPages];
static char     s_bp_title[kBpPages][kBpChars];
static uint8_t  s_bp_nlines[kBpPages];
static uint8_t  s_bp_row[kBpPages], s_bp_col[kBpPages];
static char     s_bp_line[kBpPages][kBpLines][kBpChars];
static int      s_bp_count;
/* Accented glyphs for the BRIEFING bank -- a different typeface from the
 * dialog font, so separate from the 16 font accents. */
static uint16_t s_sg_idx[kMaxGlyphs];
static uint8_t  s_sg_px[kMaxGlyphs][16];
static int      s_sg_count;
/* Translated scenario cards. Each is an 8x9 tilemap rectangle plus the art
 * of every tile it references -- the shipped cards, unlike Sylt's, are not
 * a consecutive run. Placed at the selector, the same moment and the same
 * way sylt_place_card() places the ninth. */
enum { kMaxCards = 12, kCardTiles = 64 };
static uint8_t  s_card_col[kMaxCards], s_card_row[kMaxCards];
static uint8_t  s_card_w[kMaxCards], s_card_h[kMaxCards];
static uint16_t s_card_map[kMaxCards][72];
static uint16_t s_card_tid[kMaxCards][kCardTiles];
static uint8_t  s_card_px[kMaxCards][kCardTiles][32];
static uint8_t  s_card_nt[kMaxCards];
static int      s_card_count;
/* Surfaces: the general form of the card placement -- a tilemap region plus
 * the art of every tile it uses, written when a given screen appears. Kept
 * as the raw file and walked at apply time. */
enum { kMaxSurf = 4 };
static uint8_t *s_surf[kMaxSurf]; static uint32_t s_surf_len[kMaxSurf];
static int      s_surf_count;
static bool s_ninth_scenario;
/* Declared here rather than beside load_sylt_map(): the arm is set from the
 * selector hook and the swap runs in the opcode loop, both of which come
 * earlier in this file. */
static uint8_t *s_sylt_map;
static long s_sylt_map_len;
static bool s_sylt_map_armed;   /* only when the ninth column is confirmed */
/* Set once Sylt's map is actually in place, cleared on the way back to the
 * selector. Index 8 is the practice map as well, so "$0040 == 8" alone
 * cannot tell the two apart after the arm has been consumed. */
static bool s_sylt_city;
static int  s_ninth_scroll = 0xA0;   /* $22 target for the new column.
                                      * Past the tilemap's own 359 px, which
                                      * selector_extend_tilemap() fills in. */

/* One character of the stored city name, for SC_SCEN_DIAG. */
static char nmc(unsigned i) {
  const unsigned v = g_ram[0x0b5b + 1 + i];
  return (v >= 0x0a && v <= 0x23) ? (char)(0x41 + v - 0x0a) : 0x2e;
}

static void ninth_scenario_hook(unsigned bank, unsigned pc) {
  if (!s_ninth_scenario || !s_rom_is_us || bank != 0x03) return;
  switch (pc) {
    case 0xddb6:   /* selector entry, before anything reads the cursor.
                    * Coming back from a scenario the ROM re-derives $52/$54
                    * from $0040, and it has no case for index 8: measured,
                    * the cursor came back as col=0 row=226 where a stock
                    * column 3 came back as col=3 row=0. Pin it instead, which
                    * also puts the cursor back on the ninth entry rather than
                    * anywhere else. */
      if ((g_ram[0x40] | (g_ram[0x41] << 8)) == 8) {
        g_ram[0x52] = 4;
        g_ram[0x54] = 0;
      }
      /* Reaching the selector clears the arm; 03:de4d re-sets it later in this
       * same routine if the ninth column is confirmed. Backing out of Sylt
       * without starting it therefore cannot leave it armed for the tutorial. */
      s_sylt_map_armed = false;
      s_sylt_city = false;
      selector_extend_tilemap();
      /* Cards first, Sylt second. The donor cards reference tiles that fall
       * in Sylt's CHR run at $2e0, so placing them after sylt_place_card()
       * overwrote its art -- reported from play as Sylt going black. */
      place_translated_cards();
      sylt_place_card();
      break;
    case 0xde4d:   /* B accepted on the selector (03:de4d is the JSR $e574 /
                    * INC $14 path). Arm the map swap only for the ninth
                    * column, and clear it for every other choice so a stale
                    * arm can never hand Sylt's map to another scenario --
                    * index 8 is the PRACTICE map, so that matters. */
      s_sylt_map_armed = (g_ram[0x52] == 4);
      break;
    case 0xdebb:   /* 03:deb2 and 03:deb8 have just stored the selection
                    * cursor's x and y. They come from the EIGHT-entry tables
                    * at 03:df10 and 03:df00, and 03:dea8 indexes them with
                    * $0040*2 -- so index 8 reads 16 bytes past 03:df10, which
                    * is the win-mark table, and the blinking border lands on
                    * the wood instead of the ninth card.
                    *
                    * X runs 0010/0060/00b0 for grid columns 0..2 and 0100 for
                    * column 3: eighty apart, so column 4 is $150 = 336, which
                    * is tilemap column 42 x 8 -- the card's own position.
                    * Y is $27 on the top row, $7f on the bottom. */
      if ((g_ram[0x40] | (g_ram[0x41] << 8)) == 8) {
        const uint16_t scroll = (uint16_t)(g_ram[0x16] | (g_ram[0x17] << 8));
        const uint16_t cx = (uint16_t)(0x150u - scroll);   /* 03:deb0 SBC $16 */
        g_ram[0x025d] = (uint8_t)(cx & 0xff);
        g_ram[0x025e] = (uint8_t)(cx >> 8);
        g_ram[0x025f] = 0x27; g_ram[0x0260] = 0x00;
      }
      break;
    case 0xddc3:   /* 03:ddc1 STA $79 has just run -- widen the max column.
                    * Hooks fire BEFORE the opcode at pc, so this has to sit
                    * on the instruction after the store, not on it. */
      /* Only once the ROM has unlocked column 3 itself.
       *
       * 03:ddba loads 2 and increments it to 3 when bit 15 of $42 is set --
       * the "every scenario beaten" flag 03:e31c writes. Forcing 4 flat
       * overrode that gate, so on a fresh save with nothing won the player
       * could still scroll to columns 3 and 4. Reported from play. Widening
       * only the already-widened value keeps the ninth entry behind exactly
       * the same condition the eighth is. */
      if (g_ram[0x79] == 3) g_ram[0x79] = 4;
      break;
    case 0xde1c:   /* after 03:de1a STA $40, and also the target of the
                    * 03:ddc7 branch taken when no direction is pressed, so
                    * the index stays right on idle frames too */
      if (g_ram[0x52] == 4) {
        g_ram[0x40] = 8;
        g_ram[0x41] = 0;
        /* One row only. Without this, Down on the ninth column runs 03:de0e's
         * row*3+col and lands on 7 -- the Free card -- while the cursor
         * sprite sits on an empty second row that has no card at all. */
        g_ram[0x54] = 0;
      }
      break;
    case 0xde31:   /* after 03:de2f STA $22 -- and necessarily here rather
                    * than on it, because for column 4 the 03:de2a BNE skips
                    * that store completely and it never executes */
      if (g_ram[0x52] == 4) {
        g_ram[0x22] = (uint8_t)(s_ninth_scroll & 0xff);
        g_ram[0x23] = (uint8_t)((s_ninth_scroll >> 8) & 0xff);
      }
      break;
    /* ---- The ninth scenario's win/lose rules ------------------------
     *
     * Every per-scenario table the ROM indexes with $0040 has EIGHT entries,
     * and they are laid out back to back, so index 8 reads the first entry of
     * whatever table follows. 03:cec8 below already repairs the seed. These
     * three cases repair the rest, all of it reached only in scenario mode --
     * $3e == 3, which 03:c502 and 03:e2f5 test for themselves. The practice
     * map shares index 8 and is excluded by that same test.
     *
     * Reported from play as "Sylt is losing after a short time", and it was
     * losing every time, immediately and unavoidably. */
    case 0xce2e:   /* entry to the map loader, for EVERY map */
      /* Clear the latch here, not just on the way to the selector.
       *
       * It was cleared at 03:ddb6 alone, which the tutorial never reaches --
       * it is started from the main menu, not the scenario selector. So a
       * latch left set by a Sylt session survived into the next practice map
       * and renamed it. Reported from play: "now the Practice map shows
       * Sylt".
       *
       * Both maps load through here, so clearing on entry and setting again
       * at the swap below makes the latch describe THIS load and nothing
       * earlier -- and it does so whichever way round the seed and the swap
       * happen to run. */
      s_sylt_city = false;
      break;
    case 0xcf31:   /* 03:cf19 has just copied the city name to $0b5b */
      /* The name table at $03cf32 is one of the few tables indexed by $0040
       * that is NOT short -- it has a real ninth entry, $cf79, and that entry
       * is the practice map's own name. So Sylt loads correctly named
       * PRACTICE, and that name is what the fast-travel minimap and the view
       * mode draw. Reported from play.
       *
       * $0b5b holds a length byte then the characters, in the same A = 0x0a
       * alphabet the briefing uses. Rewritten in place; the table itself is
       * ROM and repointing it would change the fingerprint.
       *
       * Gated on the arm OR the latch so the order of the seed against the
       * map swap does not matter: whichever ran first, one of the two is set
       * for Sylt and neither is for the tutorial, which shares index 8 and
       * has to keep its own name. */
      if ((g_ram[0x40] | (g_ram[0x41] << 8)) == 8 &&
          (s_sylt_map_armed || s_sylt_city)) {
        static const uint8_t kSylt[] = { 0x04, 0x1c, 0x22, 0x15, 0x1d };  /* SYLT */
        for (unsigned i = 0; i < sizeof kSylt; i++)
          g_ram[0x0b5b + i] = kSylt[i];
        /* Blank what the longer name left behind. The length byte is
         * what gets drawn, so this is not visible either way, but a
         * buffer reading SYLTTICE is a trap for the next reader. */
        for (unsigned i = sizeof kSylt; i <= 8; i++)
          g_ram[0x0b5b + i] = 0;
      }
      break;
    case 0xc518:   /* 03:c515 LDA $c5b3,Y has just read the deadline year */
      /* The deadline table at $03c5b3 ends at index 7 (the $ffff free-play
       * sentinel); index 8 falls into the countdown table at $03c5c3 and
       * reads 5. Against a start year of 2047 the SBC at 03:c519 borrows,
       * 03:c51e clamps the remainder to 0, and the countdown at $0ccb walks
       * all six of its steps in six calls -- so the scenario reaches its
       * verdict almost at once. $0deb is 1 there against the CMP #$0004 at
       * 03:c54b, so the verdict is always a loss.
       *
       * 2057 is Rio's deadline, which is the right one to borrow: the Sylt
       * patch repoints index 5, so the rest of its seed is Rio's too, and it
       * matches the ten-year limit Las Vegas uses. */
      if (g_cpu && g_ram[0x3e] == 3 &&
          (g_ram[0x40] | (g_ram[0x41] << 8)) == 8)
        g_cpu->a = 2057;
      break;
    case 0xc548:   /* the countdown has expired: verdict time */
      /* Sylt decides its own, ahead of the ROM's gate.
       *
       * 03:c54b applies CMP #$0004 to $0deb before any per-scenario objective
       * is looked at, and the ladder at 03:81d8 prices city class 4 at
       * 100,000 inhabitants. So every stock scenario secretly requires 100k.
       * Sylt starts at 3,400 on a small island -- the real one holds about
       * 18,000 -- so under that gate it could never be won at all, whatever
       * objective it was given, and a score test bolted on afterwards would
       * only have been strictly harder.
       *
       * Its rule instead: the city score back to where it started, and a
       * size floor an island can actually reach. $0ded is the score, set to
       * exactly 500 at 03:b485 when the evaluation counters are cleared, and
       * three stock scenarios already win on ">= 500". Class 2 is 10,000
       * people. Both are the ROM's own measures, not invented ones.
       *
       * Jumping straight to the ROM's own store keeps the result in one
       * place: c5a7 loads 2 (win), c5ac loads 1 (lose), both fall into the
       * STA $0d87 at c5af. */
      if (g_cpu && (g_ram[0x40] | (g_ram[0x41] << 8)) == 8 && g_ram[0x3e] == 3) {
        const unsigned cls   = g_ram[0x0deb] | ((unsigned)g_ram[0x0dec] << 8);
        const unsigned score = g_ram[0x0ded] | ((unsigned)g_ram[0x0dee] << 8);
        g_cpu->pc = (cls >= 2u && score >= 500u) ? 0xc5a7 : 0xc5ac;
      }
      break;
    case 0xe30a:   /* the win-mark setter, ORA $e334,Y */
      /* Its mask table is eight entries as well; index 8 reads 0xbb22, which
       * 03:e326 would commit to SRAM $700007 -- scattering win marks across
       * scenarios that were never played and setting bit 15, the game's own
       * "every scenario beaten" flag. Bit 8 is free: the six scenarios own
       * bits 0-5, Las Vegas and free play 6-7, and the all-beaten flag is 15.
       *
       * Do the OR here and step over the instruction. The ROM's own drawer at
       * 03:ded0 walks only eight bits, so bit 8 paints no mark on the card --
       * a cosmetic gap, against corrupting the six that do. */
      if (g_cpu && g_cpu->y == 16 && g_ram[0x3e] == 3) {
        g_cpu->a = (uint16_t)(g_cpu->a | 0x0100u);
        g_cpu->pc = 0xe30d;
      }
      break;
    case 0xcec8:   /* 03:ce8b has just seeded from its 8-entry tables */
      if ((g_ram[0x40] | (g_ram[0x41] << 8)) == 8) {
        /* Index 8 reads past the eight-entry tables, so the seed is supplied
         * here. These are the values the Sylt patch produces: it repoints
         * index 5, so Sylt inherits Rio's entries except where the patch
         * overrides them -- class 0004 -> 0001 and population 25341 -> 3400.
         * Year 2047 is Rio's and is what the card says. */
        g_ram[0x0c0d] = 0x02; g_ram[0x0c0e] = 0x01;   /* event countdown 258 */
        g_ram[0x0b53] = 0xff; g_ram[0x0b54] = 0x07;   /* year 2047 */
        g_ram[0x0deb] = 0x01; g_ram[0x0dec] = 0x00;   /* city class 1 */
        g_ram[0x0ca5] = 0x01; g_ram[0x0ca6] = 0x00;
        g_ram[0x0ba5] = 0x48; g_ram[0x0ba6] = 0x0d;   /* population 3400 */
        g_ram[0x0ba7] = 0x00; g_ram[0x0ba8] = 0x00;
      }
      break;
    default: break;
  }
}

/* --------------------------------------------------------------------------
 * REPLAY MENU -- STANDARD / FREE when a beaten scenario is chosen.
 *
 * A finished scenario is worth replaying two ways: again as a scenario, or
 * just as a city on that map. The ROM only offers the first. The second turns
 * out to be a one-word change once the city is up, because $003e is the mode
 * byte and every reader of it is in-game logic rather than the loader:
 *
 *   03:b863  == 3   scripted-disaster dispatcher   | scenario
 *   03:c502  == 3   win/lose objective check       | only
 *   03:e2f5  == 3   win-mark setter                |
 *   03:b916  == 1   free play's random-disaster threshold  | free play
 *   03:c3d5  == 1   two further simulation branches        | only
 *   03:c476  == 1                                          |
 *   03:cd96         save path: $3e -> SRAM $70006c
 *
 * Nothing on the map-load path looks at it -- the map is picked by $0040
 * alone (03:ce2e) -- so a free replay loads the scenario exactly as always
 * and only the rules differ. $3e = 1 is not a synthetic value either: it is
 * what save states 0/2/3/4, ordinary free-play cities, actually hold.
 *
 * Because 03:cd96 writes $3e into SRAM, a free replay saved to a slot
 * reloads as free play. That is the intent, but the choice does stick.
 *
 * Which scenarios are finished comes from $42, the completion mask the
 * win-mark drawer at 03:ded0 walks one LSR per scenario, bit N = scenario N
 * (03:e30a builds it, 03:e326 commits it to SRAM $700007). So the menu
 * offers itself on exactly the entries that already show a mark.
 */
/* Last frame on which the selector's per-frame handler ran, so host
 * overlays can tell they are on that screen without a $01df gate. */
static uint64_t s_selector_frame = ~0ull;
static uint32_t s_sylt_decomp_src;
static bool s_replay_menu = true;   /* SC_REPLAY_MENU=0 to disable */
static bool s_replay_open;
static int  s_replay_sel;           /* 0 = STANDARD, 1 = FREE */
static int  s_replay_free;          /* 0 off, 1 armed, 2 active */

/* The grid index under the cursor, recomputed the way 03:de0e does rather
 * than read back from $40: 03:ddc7 branches past the whole index computation
 * when no direction is pressed, so $40 is stale on a confirm that follows no
 * cursor movement -- which is precisely the common case. */
static int replay_index_now(void) {
  int col = g_ram[0x52], row = g_ram[0x54] & 1;
  if (s_ninth_scenario && col == 4) return 8;
  if (col == 3) return 6 + row;
  return row * 3 + col;
}

static bool replay_finished(int idx) {
  if (idx < 0 || idx > 7) return false;   /* the ninth is never "beaten" */
  return (g_ram[0x42] & (1u << idx)) != 0;
}

/* Hooked at the selector's per-frame entry, ahead of both the direction read
 * at 03:ddc3 and the confirm read at 03:de46, so blanking the new-press word
 * here freezes the cursor as well as the confirm. A hook at the confirm alone
 * would leave the selection moving underneath the open menu. */
static void replay_menu_hook(unsigned bank, unsigned pc) {
  if (bank != 0x03 || pc != 0xddb6) return;
  uint8_t lo = g_ram[0xc9], hi = g_ram[0xca];  /* $c9 new-press, read 16-bit */
  { static bool seen;
    if (!seen) { seen = true;
      fprintf(stderr, "[replay] selector reached, $42=%04x col=%u row=%u idx=%d\n",
              g_ram[0x42] | (g_ram[0x43] << 8), g_ram[0x52], g_ram[0x54],
              replay_index_now()); } }
  if (s_replay_open) {
    g_ram[0xc9] = 0; g_ram[0xca] = 0;
    if (hi & 0x08) s_replay_sel = 0;           /* Up   */
    if (hi & 0x04) s_replay_sel = 1;           /* Down */
    if (lo & 0x80) {                           /* A -- back out */
      s_replay_open = false;
    } else if (hi & 0x80) {                    /* B -- confirm */
      s_replay_open = false;
      s_replay_free = (s_replay_sel == 1) ? 1 : 0;
      fprintf(stderr, "[replay] scenario %d: %s\n", replay_index_now(),
              s_replay_free ? "FREE" : "STANDARD");
      g_ram[0xca] = 0x80;                      /* hand the press to the ROM */
    }
    return;
  }
  /* Reaching the selector with a free replay still latched means the player
   * has left that city, so drop it before it can colour the next start. */
  if (s_replay_free == 2) s_replay_free = 0;
  if (hi & 0x80)
    fprintf(stderr, "[replay] B on idx=%d finished=%d $42=%04x\n",
            replay_index_now(), (int)replay_finished(replay_index_now()),
            g_ram[0x42] | (g_ram[0x43] << 8));
  if ((hi & 0x80) && replay_finished(replay_index_now())) {
    s_replay_open = true;
    s_replay_sel = 0;
    fprintf(stderr, "[replay] menu opened on beaten scenario %d ($42=%04x)\n",
            replay_index_now(), g_ram[0x42] | (g_ram[0x43] << 8));
    g_ram[0xc9] = 0; g_ram[0xca] = 0;          /* swallow the opening press */
  }
}

/* Armed -> active on the frame the game declares scenario mode, rather than
 * at a chosen store site: no store to $003e is reachable in banks 00-07 apart
 * from the SRAM load at 03:ca57, so waiting for the value is the only route
 * that does not depend on finding the writer. Stays active afterwards so a
 * later rewrite cannot put the scenario rules back. */
static void replay_free_tick(void) {
  if (!s_replay_free) return;
  if ((g_ram[0x3e] | (g_ram[0x3f] << 8)) != 3) return;
  if (s_replay_free == 1)
    fprintf(stderr, "[replay] free play engaged on scenario %d at frame %llu"
            " ($3e 3->2, $0c0d %u->0)\n", g_ram[0x40],
            (unsigned long long)s_frames,
            (unsigned)(g_ram[0x0c0d] | (g_ram[0x0c0e] << 8)));
  s_replay_free = 2;
  /* 2, not 1. $3e == 1 is the PRACTICE map, not free play generally:
   * 03:b919 CMP #$0001 / BEQ $b967 jumps PAST the random-disaster
   * threshold, and it also fires the Dr. Wright tutorial intro. Setting 1
   * put a free replay into practice mode -- reported as the advice popup
   * appearing on a freshly started free scenario. 2 is what ordinary
   * free-play cities hold (save states 1/7/8/9) and still fails every
   * == 3 scenario gate. */
  g_ram[0x3e] = 2; g_ram[0x3f] = 0;
  g_ram[0x0c0d] = 0; g_ram[0x0c0e] = 0;   /* scripted-event countdown off */
}

/* Fire the scripted SC_DISASTER trigger once, at its frame. */
static void sc_maybe_trigger_disaster(void) {
  if (s_disaster_bit < 0 || s_frames < s_disaster_frame) return;
  g_ram[0x0197] |= (uint8_t)(1u << s_disaster_bit);
  fprintf(stderr, "[disaster] set $0197 bit %d -> $0197=%02x at frame %llu\n",
          s_disaster_bit, g_ram[0x0197], (unsigned long long)s_frames);
  s_disaster_bit = -1;
}

/* â”€â”€ 00:90dd, the stream decompressor â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * 48% of the overview-map load (docs/ROM_MAP.md). src/sc_decomp.c does
 * the same work on the host.
 *
 * SC_DECOMP_VERIFY=1 is the important mode, and it exists because the map
 * generator taught the lesson: that generator was WRONG for a whole session
 * while looking plausible, and what caught it was comparing against the
 * guest rather than eyeballing output. So this runs the C into a scratch
 * copy of WRAM, lets the ROM run as normal, and compares at the RTS. It
 * changes nothing -- it only reports. Only once it reports clean is
 * SC_DECOMP_FAST worth turning on.
 */
static uint8_t sc_decomp_bus_read(void *ctx, uint32_t addr) {
  (void)ctx;
  return (uint8_t)snes_read(g_snes, addr);
}

static uint8_t *s_dec_ref;             /* scratch WRAM image for verify */
static ScDecompResult s_dec_exp;       /* what the C predicted */
static int      s_dec_pending;         /* a call is in flight */
static int      s_dec_armed;           /* 00:90dd seen, 00:90ee not yet */
static int      s_dec_trace = -1;      /* SC_DECOMP_TRACE, resolved once */
static uint16_t s_dec_start_x;
static unsigned long s_dec_ok, s_dec_mismatch, s_dec_declined, s_dec_fast;

static int sc_decomp_mode(void) {
  static int mode = -1;                /* 0 off, 1 verify, 2 replace */
  if (mode < 0) {
    const char *f = getenv("SC_DECOMP_FAST");
    const char *v = getenv("SC_DECOMP_VERIFY");
    /* On by default, like SC_MAPGEN_FAST, and for the same reason: the
     * substitution is verified byte-exact against the guest rather than
     * judged by eye. SC_DECOMP_FAST=0 turns it off; SC_DECOMP_VERIFY=1
     * re-runs the comparison instead of replacing anything. */
    if (v && *v && *v != '0')   mode = 1;
    else if (f && *f)           mode = (*f != '0') ? 2 : 0;
    else                        mode = 2;
  }
  return mode;
}

/* -- screen-packet patches (SC_PACKET_PATCH) ------------------------------
 *
 * The scenario selector keeps its whole 64x32 tilemap and its card artwork
 * as two compressed packets in ROM ($05A5A1 and $0444DB), unpacked through
 * 00:90DD like everything else. A patch file (tools/text_tool.py packets)
 * carries spans to lay over the DECOMPRESSED bytes, so the translated screen
 * is uploaded by the game's own DMA at the game's own moment.
 *
 * That is the whole point of doing it here. Five earlier attempts wrote the
 * cards into VRAM from the host -- at the selector hook, at the frame top,
 * before and after Sylt -- and every one lost a race with the NMI DMA that
 * follows: names came out fragmented, one card's text continuing onto the
 * next. An offline replay of those same writes reproduced the German screen
 * exactly (384 name cells, zero differing), which ruled the DATA correct and
 * left only the timing. Patching the source removes the timing question
 * instead of trying to win it. */
typedef struct ScpkSpan { uint32_t off; uint16_t len; uint8_t *data; } ScpkSpan;
typedef struct ScpkEntry { uint32_t src, outlen; uint16_t nspans;
                           int reported; ScpkSpan *spans;
                           uint8_t nscreens, screens[8]; } ScpkEntry;
static ScpkEntry *s_scpk;
static int s_scpk_count = -1;          /* -1 = not looked for yet */
static uint32_t s_scpk_src, s_scpk_out;

static uint8_t *read_file(const char *path, uint32_t *size_out);

static void scpk_load(void) {
  s_scpk_count = 0;
  const char *path = getenv("SC_PACKET_PATCH");
  if (!path || !*path) return;
  uint32_t n = 0;
  uint8_t *b = read_file(path, &n);
  if (!b) { fprintf(stderr, "packet patch: cannot read %s\n", path); return; }
  if (n < 8 || memcmp(b, "SCPK", 4) != 0 || b[4] < 1 || b[4] > 2) {
    fprintf(stderr, "packet patch: %s is not an SCPK v1 or v2 file\n", path);
    free(b); return;
  }
  const int cnt = b[6] | (b[7] << 8);
  s_scpk = (ScpkEntry *)calloc((size_t)(cnt ? cnt : 1), sizeof(ScpkEntry));
  if (!s_scpk) { free(b); return; }
  uint32_t p = 8;
  for (int i = 0; i < cnt; i++) {
    if (p + 4 > n) break;
    ScpkEntry *e = &s_scpk[s_scpk_count];
    e->src = ((uint32_t)b[p] << 16) | b[p + 1] | ((uint32_t)b[p + 2] << 8);
    /* v2 carries the screens an entry is for, right after the address; an
     * empty list means every screen, which is all v1 could say. */
    uint32_t q = p + 3;
    e->nscreens = 0;
    if (b[4] >= 2) {
      const uint8_t ns = b[q++];
      if (q + ns > n) break;
      for (uint8_t s = 0; s < ns; s++)
        if (e->nscreens < sizeof e->screens) e->screens[e->nscreens++] = b[q + s];
      q += ns;
    }
    if (q + 6 > n) break;
    e->outlen = (uint32_t)b[q] | ((uint32_t)b[q + 1] << 8) |
                ((uint32_t)b[q + 2] << 16) | ((uint32_t)b[q + 3] << 24);
    e->nspans = (uint16_t)(b[q + 4] | (b[q + 5] << 8));
    p = q + 6;
    e->spans = (ScpkSpan *)calloc((size_t)(e->nspans ? e->nspans : 1),
                                  sizeof(ScpkSpan));
    if (!e->spans) break;
    int ok = 1;
    for (int k = 0; k < e->nspans; k++) {
      if (p + 6 > n) { ok = 0; break; }
      const uint32_t off = (uint32_t)b[p] | ((uint32_t)b[p + 1] << 8) |
                           ((uint32_t)b[p + 2] << 16) | ((uint32_t)b[p + 3] << 24);
      const uint16_t len = (uint16_t)(b[p + 4] | (b[p + 5] << 8));
      p += 6;
      if (p + len > n) { ok = 0; break; }
      uint8_t *d = (uint8_t *)malloc(len ? len : 1);
      if (!d) { ok = 0; break; }
      memcpy(d, b + p, len); p += len;
      e->spans[k].off = off; e->spans[k].len = len; e->spans[k].data = d;
    }
    if (!ok) break;
    s_scpk_count++;
  }
  free(b);
  for (int i = 0; i < s_scpk_count; i++)
    fprintf(stderr, "packet patch: $%02x:%04x  %u bytes out, %u spans\n",
            (unsigned)(s_scpk[i].src >> 16), (unsigned)(s_scpk[i].src & 0xffff),
            s_scpk[i].outlen, (unsigned)s_scpk[i].nspans);
  for (int i = 0; i < s_scpk_count; i++)
    if (s_scpk[i].nscreens)
      fprintf(stderr, "packet patch: $%02x:%04x  only while $14 = $%02x%s\n",
              (unsigned)(s_scpk[i].src >> 16), (unsigned)(s_scpk[i].src & 0xffff),
              s_scpk[i].screens[0], s_scpk[i].nscreens > 1 ? " (and more)" : "");
}

/* `out` is where the ROM put the packet: $8000 + X, in bank $7E. */
static void scpk_apply(uint32_t src, uint32_t out) {
  if (s_scpk_count < 0) scpk_load();
  for (int i = 0; i < s_scpk_count; i++) {
    ScpkEntry *e = &s_scpk[i];
    if (e->src != src) continue;
    /* The menu's sprite artwork ($09:A571) is not the menu's alone. The
     * scenario selector unpacks it again on screen $0a and draws its win marks
     * from it -- record $29, tiles $1B0 $1B2 $1D0 $1D2 -- and the new-city
     * screens unpack it on $04. A patch keyed only by source reached all of
     * them, so the composed menu letters landed on the marks: reported from
     * play as the red X turned into coloured fragments of STADT. An entry
     * that names its screens is applied on those screens only. */
    if (e->nscreens) {
      int hit = 0;
      for (int s = 0; s < e->nscreens; s++) hit |= e->screens[s] == g_ram[0x14];
      if (!hit) continue;
    }
    for (int k = 0; k < e->nspans; k++) {
      const uint32_t at = out + e->spans[k].off;
      if (at + e->spans[k].len > 0x20000u) continue;
      memcpy(&g_ram[at], e->spans[k].data, e->spans[k].len);
    }
    /* Once per ENTRY, not once overall: a single shared flag reported only
     * the first packet to be patched and left the others looking silent. */
    if (!e->reported) {
      e->reported = 1;
      fprintf(stderr, "packet patch: applied at $%02x:%04x\n",
              (unsigned)(src >> 16), (unsigned)(src & 0xffff));
    }
  }
}

/* The main map's building labels are not in a packet: they sit uncompressed
 * at file $034C00 and are copied to VRAM byte $CC00 one for one. So they are
 * patched in the cart IMAGE, the same way SC_TRANSLATION patches the message
 * block -- and for the same reason it does it here: after the fingerprint has
 * been taken, so a translated run keeps the host map renderer, SC_FIBER, the
 * cursor cadence patch and the view fix. */
static uint32_t s_scpk_rom_off, s_scpk_rom_len;   /* a witness for the recheck */
static void scpk_apply_rom(uint8_t *rom, uint32_t size) {
  if (s_scpk_count < 0) scpk_load();
  for (int i = 0; i < s_scpk_count; i++) {
    if (s_scpk[i].src != 0xfeffffu) continue;
    uint32_t n = 0;
    for (int k = 0; k < s_scpk[i].nspans; k++) {
      const uint32_t at = s_scpk[i].spans[k].off;
      if (at + s_scpk[i].spans[k].len > size) continue;
      memcpy(rom + at, s_scpk[i].spans[k].data, s_scpk[i].spans[k].len);
      if (!s_scpk_rom_len) { s_scpk_rom_off = at; s_scpk_rom_len = s_scpk[i].spans[k].len; }
      n += s_scpk[i].spans[k].len;
    }
    fprintf(stderr, "packet patch: %u bytes laid over the cart image\n", n);
  }
}

static int scpk_active(void) {
  if (s_scpk_count < 0) scpk_load();
  return s_scpk_count > 0;
}

static void sc_decomp_hook(Interp816 *cpu) {
  const int mode = sc_decomp_mode();
  if (!mode) return;

  /* Enter at 00:90ee, not 00:90dd, and leave via 00:9106 rather than by
   * emulating the RTS. Both details are load-bearing: the Sylt scenario
   * hack hooks PCs INSIDE this routine -- 00:90eb to capture the source
   * address, and 00:9106 to write its briefing tilemap. Skipping 90dd to
   * the return jumped straight over both, and Sylt vanished from the
   * scenario list. Reported from play; nothing in the qualify harness
   * would have caught it, because Sylt is this project's own addition.
   *
   * 00:90ee is the first instruction after setup (DB set from $000b, X
   * from $000e, $0011 cleared) and before any stream byte is consumed, so
   * the ROM does its own prologue and 90eb fires naturally. Setting PC to
   * 9106 lets the ROM run its real PLB/PLP/RTS, which also means the stack
   * unwinds itself instead of being unwound by hand.
   *
   * 90ee is also the loop-back target, so the arm flag keeps a declined
   * call from re-entering this on every command the ROM then decodes. */
  if (s_dec_trace < 0) {
    const char *t = getenv("SC_DECOMP_TRACE");
    s_dec_trace = (t && *t && *t != '0');
  }
  if (cpu->pc == 0x90dd) { s_dec_armed = 1; return; }

  if (cpu->pc == 0x90ee) {
    if (!s_dec_armed) return;
    s_dec_armed = 0;
    const uint8_t  sb = g_ram[0x0b];
    const uint16_t sy = (uint16_t)(g_ram[0x09] | (g_ram[0x0a] << 8));
    const uint16_t dx = (uint16_t)(g_ram[0x0e] | (g_ram[0x0f] << 8));

    if (mode == 2) {
      /* Decompress into scratch first, not straight into WRAM. That buys
       * the ability to BACK OUT: $e0 is the one command no capture has
       * ever exercised (see the cmd-hit counters), so if a stream uses it
       * this hands the work back to the ROM rather than trusting code no
       * measurement has ever confirmed. Everything else here is verified
       * byte-exact against the guest across a load and a cold boot. */
      if (!s_dec_ref) s_dec_ref = (uint8_t *)malloc(0x20000);
      if (!s_dec_ref) return;
      memcpy(s_dec_ref, g_ram, 0x20000);
      const unsigned long e0_before = g_sc_decomp_cmd_hits[7];
      ScDecompResult r;
      sc_decomp_run(s_dec_ref, sc_decomp_bus_read, NULL, sb, sy, dx, &r);
      /* SC_DECOMP_TRACE=1 prints one line per unpacked packet: the screen
       * that asked for it, where in ROM it came from, and how big it is.
       * This is how a screen's ROM assets get identified. The address it
       * prints converts to a file offset as bank*$8000 + (addr-$8000), which
       * is what tools/extract_graphics.py decompresses -- so a screen can be
       * patched at its source instead of by poking VRAM behind the game. */
      if (s_dec_trace)
        fprintf(stderr, "[decomp] screen $%02x  src $%02x:%04x  -> wram $%04x  %5u bytes%s\n",
                g_ram[0x14], sb, sy, dx, r.bytes_out,
                r.bad ? "  (declined)" : "");
      if (r.bad || g_sc_decomp_cmd_hits[7] != e0_before) {
        g_sc_decomp_cmd_hits[7] = e0_before;   /* keep the tally honest */
        s_dec_declined++;
        return;                        /* leave it to the ROM */
      }
      for (uint32_t i = dx; i != (uint32_t)r.x; i++)
        g_ram[0x8000 + (uint16_t)i] = s_dec_ref[0x8000 + (uint16_t)i];
      g_ram[0x09] = (uint8_t)r.src_y; g_ram[0x0a] = (uint8_t)(r.src_y >> 8);
      g_ram[0x0c] = (uint8_t)r.cd;    g_ram[0x0d] = (uint8_t)(r.cd >> 8);
      g_ram[0x10] = (uint8_t)r.flag;  g_ram[0x11] = 0;
      cpu->x = r.x;
      cpu->pc = 0x9106;   /* the ROM's own PLB/PLP/RTS, and Sylt's hook */
      s_dec_fast++;
      return;
    }

    if (!s_dec_ref) s_dec_ref = (uint8_t *)malloc(0x20000);
    if (!s_dec_ref) return;
    memcpy(s_dec_ref, g_ram, 0x20000);
    sc_decomp_run(s_dec_ref, sc_decomp_bus_read, NULL, sb, sy, dx, &s_dec_exp);
    s_dec_start_x = dx;
    s_dec_pending = 1;
    return;
  }

  /* 00:9108 -- the RTS. The guest has finished; compare. */
  if (cpu->pc == 0x9108 && s_dec_pending) {
    s_dec_pending = 0;
    unsigned long diff = 0;
    const uint16_t got_x = cpu->x;
    for (uint32_t i = s_dec_start_x; i != (uint32_t)s_dec_exp.x; i++)
      if (g_ram[0x8000 + (uint16_t)i] != s_dec_ref[0x8000 + (uint16_t)i]) diff++;
    const uint16_t got_y = (uint16_t)(g_ram[0x09] | (g_ram[0x0a] << 8));
    if (diff || got_x != s_dec_exp.x || got_y != s_dec_exp.src_y) {
      s_dec_mismatch++;
      if (s_dec_mismatch <= 8)
        fprintf(stderr, "[decomp] MISMATCH src=%02x:%04x dest=%04x  "
                        "bytes %u cmds %d  x %04x/%04x  y %04x/%04x  "
                        "%lu differing\n",
                s_dec_exp.src_bank, s_dec_exp.src_y, s_dec_start_x,
                s_dec_exp.bytes_out, s_dec_exp.commands,
                got_x, s_dec_exp.x, got_y, s_dec_exp.src_y, diff);
    } else {
      s_dec_ok++;
    }
  }
}

/* â”€â”€ 02:8b34, the overview-map cell classifier â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * 02:899b software-renders the whole city into a bitmap -- 120x100 = 12000
 * cells, one classifier call each, 46% of the load window. The classifier is
 * ~300 instructions of comparison ladder over tile ids, with a hardware
 * divider path and table reads (docs/ROM_MAP.md).
 *
 * It is NOT transcribed here, deliberately. It is memoised: the value is a
 * function of the 10-bit tile id plus a few globals, so there are at most
 * 1024 distinct answers and a real city uses far fewer. Letting the ROM
 * compute each one once and caching it is exact BY CONSTRUCTION -- the
 * numbers come from the ROM, not from my reading of it -- which is a much
 * better bargain than hand-porting a ladder whose every branch is a chance
 * to be subtly wrong. The map generator is the cautionary tale: transcribed
 * by hand, plausible-looking, and wrong for a session.
 *
 * Two things are deliberately NOT cached:
 *
 *   - tile ids $14-$25, the animated ones. 02:8b83 increments the animation
 *     counter $0b3b as a SIDE EFFECT and folds it into the table index, so
 *     the answer legitimately differs call to call. Those run the ROM.
 *   - anything at all, once $0d49 (the overlay selector), $3e or $40 change.
 *     The ladder branches on all three, so they are part of the key; the
 *     cache is flushed rather than keyed, since they change rarely.
 *
 * The skip enters at 8b36 (after the entry REP #$30, so the widths are
 * already what the ROM would leave) and exits by pointing PC at the real RTS
 * at 8b96 rather than unwinding the stack by hand -- the same shape the
 * decompressor uses at 00:9106, and for the same reason: the ROM does its own
 * return, and any PC the game hooks in between still executes.
 *
 * SC_MAPCLS=0 disables. SC_MAPCLS_VERIFY=1 caches but still runs the ROM and
 * compares, which is how the cache was shown to be exact.
 */
static uint16_t s_cls_cache[1024];
static uint8_t  s_cls_valid[1024];
static uint16_t s_cls_tile;            /* tile id of the call in flight */
static int      s_cls_pending;         /* 1 = fill, 2 = verify */
static unsigned s_cls_state = 0xffffffffu;
static unsigned long s_cls_hits, s_cls_fills, s_cls_mismatch;

static void sc_classifier_hook(Interp816 *cpu) {
  static int mode = -1;                /* 0 off, 1 verify, 2 replace */
  if (mode < 0) {
    const char *e = getenv("SC_MAPCLS");
    const char *v = getenv("SC_MAPCLS_VERIFY");
    if (v && *v && *v != '0')  mode = 1;
    else if (e && *e)          mode = (*e != '0') ? 2 : 0;
    else                       mode = 2;
  }
  if (!mode) return;

  const unsigned st = (unsigned)g_ram[0x0d49] |
                      ((unsigned)g_ram[0x3e] << 8) |
                      ((unsigned)g_ram[0x3f] << 12) |
                      ((unsigned)g_ram[0x40] << 16) |
                      ((unsigned)g_ram[0x41] << 20);
  if (st != s_cls_state) {
    memset(s_cls_valid, 0, sizeof s_cls_valid);
    s_cls_state = st;
  }

  if (cpu->pc == 0x8b36) {
    const uint16_t idx  = (uint16_t)(g_ram[0x0d63] | (g_ram[0x0d64] << 8));
    const uint16_t cell = s_world.active?ScWorldCell(&s_world,(ScWorldWidth(&s_world)/120)*(idx/2%120),(ScWorldWidth(&s_world)/120)*(idx/2/120)):
      (uint16_t)(g_ram[0x10200 + idx] | (g_ram[0x10201 + idx] << 8));
    const uint16_t tile = (uint16_t)(cell & 0x03ff);
    s_cls_tile = tile;
    s_cls_pending = 0;
    if (tile >= 0x14 && tile <= 0x25) return;   /* animated: $0b3b moves */
    if (!s_cls_valid[tile]) { s_cls_pending = 1; return; }
    if (mode == 1)          { s_cls_pending = 2; return; }
    cpu->a  = s_cls_cache[tile];
    cpu->pc = 0x8b96;                  /* the ROM's own RTS */
    s_cls_hits++;
    return;
  }

  /* 02:89b4 -- the ROM has returned and A holds the colour byte. */
  if (s_cls_pending == 1) {
    s_cls_cache[s_cls_tile] = (uint16_t)cpu->a;
    s_cls_valid[s_cls_tile] = 1;
    s_cls_fills++;
  } else if (s_cls_pending == 2) {
    if ((uint16_t)cpu->a != s_cls_cache[s_cls_tile]) s_cls_mismatch++;
    else s_cls_hits++;
  }
  s_cls_pending = 0;
}

/* Dynamic rate control for the audio output.
 *
 * The DSP makes 32040 samples per second of guest time, and the host paces
 * guest time to the wall clock -- but the audio device plays by its own
 * clock. Over RDP ("Remote Audio") that clock measured 0.76% slow: the queue
 * grew by about 240 samples a second and the sound fell further and further
 * behind the picture. A fixed cap would cut a gap into the sound every couple
 * of seconds. Instead each frame's samples are resampled, steered by how full
 * the device queue is: too full, the frame is played in slightly fewer
 * samples; too empty, in slightly more. The integral term learns the device's
 * drift so the queue settles on the target; the proportional term reacts to
 * stalls. The drift is clamped to 5%, as RetroArch's timing skew is: a real
 * sound card is off by well under 0.1%, but the RDP device here measured 1-3%
 * slow and varied between sessions -- which does shift the pitch audibly, but
 * is still better than sound that drifts away or drops out. */
enum { kAudioQueueTarget = 2048, kAudioOutMax = 2048 };
static double s_audio_drift;       /* learned device drift, for SC_AUDIO_DEBUG */

static int sc_audio_rate_control(const int16_t *in, int n, uint32_t queued,
                                 int16_t *out) {
  static double frac, level = -1.0;
  static int16_t prev[2];
  if (n <= 0) return 0;
  /* The queue level is read after each push and jumps by whole device
   * buffers as the device pulls; steer on its smoothed value. */
  level = level < 0 ? (double)queued : level + 0.05 * ((double)queued - level);
  double err = ((double)kAudioQueueTarget - level) / kAudioQueueTarget;
  if (err > 1.0) err = 1.0;
  if (err < -1.0) err = -1.0;
  s_audio_drift += 1e-5 * err;
  if (s_audio_drift > 0.05) s_audio_drift = 0.05;
  if (s_audio_drift < -0.05) s_audio_drift = -0.05;
  const double ratio = 1.0 + s_audio_drift + 0.02 * err;
  frac += n * ratio;
  int m = (int)frac;
  frac -= m;
  if (m > kAudioOutMax) m = kAudioOutMax;
  if (m <= 0) return 0;
  /* Output i sits at source position (i+1)*n/m - 1, so the last output lands
   * on the last input and the next frame continues from it (prev). */
  for (int i = 0; i < m; i++) {
    const double pos = (double)(i + 1) * n / m - 1.0;
    const int k = (int)(pos + 1.0) - 1;          /* floor, pos >= -1 */
    const double f = pos - k;
    for (int c = 0; c < 2; c++) {
      const double a = k < 0 ? prev[c] : in[k * 2 + c];
      const double b = k + 1 < n ? in[(k + 1) * 2 + c] : in[(n - 1) * 2 + c];
      const double v = a + (b - a) * f;
      out[i * 2 + c] = (int16_t)(v >= 0 ? v + 0.5 : v - 0.5);
    }
  }
  prev[0] = in[(n - 1) * 2];
  prev[1] = in[(n - 1) * 2 + 1];
  return m;
}

/* The beam belongs to handle_pos_stuff(), and snes.c has two ways round it.
 *
 * A DMA start ($420B) charges the transfer's guest time through a hook, and
 * with no hook installed snes.c moves hPos/vPos itself. The lines it crosses
 * are then never drawn by this host, their HDMA step never runs, a crossing of
 * the frame end is never counted, and the APU never gets the time. So the
 * time is walked through the host's own driver instead, like any opcode's.
 *
 * And every $4212 read adds a synthetic 64-clock step unless
 * g_interp_apu_driving says the caller already advances the beam per opcode,
 * which this loop does. That step can jump over h=1024: the line loses its
 * HDMA transfer and every later line of the frame is one line late. On the
 * scenario view that is the one-frame "jitter" -- every 4th scanline of the
 * skewed map off by a line -- once every few seconds
 * (docs/upstream/ISSUE_hdma_line_phase.md).
 *
 * The fiber tier keeps snes.c's own behaviour: its bridge sets and restores
 * the flag itself and syncs the beam from the CpuState clock. */
static void sc_advance_beam(uint64_t clocks) {
  static int reference=-1;
  if(reference<0) {const char *e=getenv("SC_BEAM_REFERENCE");reference=e && *e!='0';}
  /* The old two-clock driver samples 0 (scanout), 1024 (HDMA/vertical IRQ),
   * the programmed horizontal IRQ, and 1362 (line/frame wrap). Between those
   * events only beam position and the auto-joy countdown change. */
  clocks=(clocks+1)&~UINT64_C(1);
  while(clocks) {
    unsigned remaining=clocks>1364?1364:(unsigned)clocks;
    unsigned idle=reference?0:ScBeamIdleClocks(g_snes->hPos,g_snes->vPos,remaining,
        g_snes->hIrqEnabled,g_snes->vIrqEnabled,g_snes->hTimer,g_snes->vTimer);
    if(idle) {
      g_snes->autoJoyTimer=g_snes->autoJoyTimer<=idle?0:(uint16_t)(g_snes->autoJoyTimer-idle);
      g_snes->hPos+=idle;clocks-=idle;
    } else {handle_pos_stuff();clocks-=2;}
  }
}
static void sc_charge_master_cycles(Snes *snes, uint64_t clocks) {
#ifdef SC_AOT_TIER
  if (s_fiber_mode) {
    while (clocks) {
      const uint32_t chunk = clocks > 0xffffffffull ? 0xffffffffu : (uint32_t)clocks;
      snes_advance_master_cycles(snes, chunk);
      clocks -= chunk;
    }
    return;
  }
#endif
  g_master_cycles += clocks;
  sc_advance_beam(clocks);
  snes->apuCatchupCycles += (double)clocks * kApuCyclesPerMaster;
}

static void sc_own_the_beam(void) {
  extern int g_interp_apu_driving;   /* common_rtl.c, or this file */
  snes_set_master_clock_charge_hook(sc_charge_master_cycles);
#ifdef SC_AOT_TIER
  if (s_fiber_mode) return;
#endif
  g_interp_apu_driving = 1;
}

static bool refresh_live_population(const uint8_t *rom,size_t size) {
  static int reference=-1;
  if(reference<0) {const char *e=getenv("SC_POPULATION_CENSUS_REFERENCE");reference=e && *e=='1';}
  if(reference) return ScPopulationRefreshLive(&s_population,g_ram,&s_world,rom,size);
  if(!s_population_census) s_population_census=ScPopulationCensusCreate();
  return ScPopulationRefreshCached(s_population_census,&s_population,g_ram,&s_world,rom,size);
}

/* Connected C execution for the UI/driver banks. These are the host hook
 * boundaries in run_one_frame, rather than boundaries inferred from one
 * recorded city. World-coordinate hooks and mapped operands still run on
 * every edge inside the lane. Spatial bank 03 retains its larger native C
 * kernels and development scheduler. New bank 00/01 hooks must be added here. */
static bool sc_program_host_boundary(const Interp816 *cpu) {
  if(cpu->k==0) {
    if(s_world.test_city && (cpu->pc==0xc8e0 || cpu->pc==0xc987 || cpu->pc==0xca35))return true;
    if(ScSpriteOwns(cpu->pc) || ScWaitOwns(cpu->pc)) return true;
    switch(cpu->pc) {
    case 0x80b2:case 0x80c0: /* NMI observation / title and vehicle snapshot */
    case 0x90dd:case 0x90ee:case 0x9108:case 0x90eb:case 0x9106: /* decompression */
    case 0x9882: /* music command substitution */
    case 0xd19e:case 0xd1fc:case 0xd20a: /* mouse dialog observations */
    case 0xc019:case 0xc0f5:case 0xc154:case 0xbd41: /* vehicles */
      return true;
    }
    return false;
  }
  if(cpu->k==1) {
    if(s_save_dialog_active && cpu->pc==0xad54) return true;
    if(ScTileLookupOwns(cpu->pc)) return true;
    switch(cpu->pc) {
    case 0x8907: /* common city initialization before any HUD/palette uploads */
    case 0x8948: /* city entry fade-in */
    case 0xf1ed: /* map generation */
    case 0x8976:case 0x897f: /* warnings, construction, paste, census, power */
    case 0xe59b:case 0xa63c: /* Journey messages */
    case 0xcc1a:case 0xcc3a: /* gift dialog observation */
    case 0xa9c4: /* music enable */
    case 0xf11a:case 0xef29:case 0xef86: /* vehicles */
    case 0x8d26:case 0x89a0:case 0x89a3: /* accelerated scroll boundaries */
    case 0x8d2d:case 0x8d92:case 0x8dcd:
      return true;
    }
    return s_scroll_pass[0].repeating || s_scroll_pass[1].repeating;
  }
  return true;
}

static uint64_t s_program_lane_edges,s_program_lane_entries;
static uint64_t s_program_block_edges,s_program_block_entries;
typedef struct {uint64_t target;long *guard;unsigned edges;} ScProgramHostFlow;
static bool sc_program_flow_before(void *context,Interp816 *cpu) {
  ScProgramHostFlow *flow=context;
  if(sc_program_host_boundary(cpu) || !ScProgramAvailable(cpu)) return false;
  if(flow->edges) {
    if(s_frames>=flow->target || *flow->guard<=0) return false;
    --*flow->guard;
    if(s_bank_profile) {
      ++s_bank_ops[cpu->k];
      if(cpu->k==s_bank_page_sel) {
        ++s_b3_page[cpu->pc>>8];
        if((cpu->pc>>8)==s_pc_profile_page) ++s_pc_profile_ops[cpu->pc&255];
      }
    }
  }
  ScWorldGuestStepPrepared(&s_world,cpu,g_ram);
  ScWorldGuestVehiclesPrepared(&s_world,cpu,g_ram,g_snes->multiplyA);
  sc_note_executed_pc(((uint32_t)cpu->k<<16)|cpu->pc,cpu->mf,cpu->xf);
  ScWorldGuestBeginPrepared(&s_world_guest,&s_world,cpu,g_snes->cart->rom,g_snes->cart->romSize);
  return true;
}
static bool sc_program_flow_after(void *context,Interp816 *cpu,unsigned cost) {
  (void)cpu;ScProgramHostFlow *flow=context;unsigned master=cost*8;
  g_master_cycles+=master;
  sc_advance_beam(master);
  g_snes->apuCatchupCycles+=(double)master*kApuCyclesPerMaster;
  sc_catchup_apu(g_snes);
  ++flow->edges;
  return s_frames<flow->target && *flow->guard>0;
}
/* Opt-in coverage trace for native conversion. Record only an actual
 * interpreter fallback, before it changes PC/flags; ordinary C edges do no
 * file I/O or diagnostic clock measurements. */
static FILE *s_native_missing_file;
static int sc_interpreter_fallback(Interp816 *cpu) {
  static bool initialized;
  if(!initialized) {
    initialized=true;const char *path=getenv("SC_NATIVE_MISSING_PATH");
    if(path && *path) {
      s_native_missing_file=fopen(path,"w");
      if(s_native_missing_file) {
        setvbuf(s_native_missing_file,NULL,_IOFBF,8192);
        fputs("frame,pc,mf,xf,db,dp,sp,a,x,y,opcode,available\n",s_native_missing_file);
      }
    }
  }
  if(s_native_missing_file) {
    unsigned offset=(cpu->k&127)*32768+(cpu->pc&32767);
    unsigned opcode=offset<g_snes->cart->romSize?g_snes->cart->rom[offset]:0;
    fprintf(s_native_missing_file,"%llu,%02x%04x,%u,%u,%02x,%04x,%04x,%04x,%04x,%04x,%02x,%u\n",
        (unsigned long long)s_frames,cpu->k,cpu->pc,cpu->mf,cpu->xf,cpu->db,cpu->dp,cpu->sp,
        cpu->a,cpu->x,cpu->y,opcode,(unsigned)ScProgramAvailable(cpu));
  }
  return interp816_runOpcode(cpu);
}
static unsigned sc_program_flow_fallback(void *context,Interp816 *cpu) {
  (void)context;
  if(s_bank_profile) {
    ++s_interpreter_bank_ops[cpu->k];
    if(cpu->k==s_bank_page_sel) {
      ++s_interpreter_pages[cpu->pc>>8];
      if((cpu->pc>>8)==s_pc_profile_page) ++s_interpreter_pc[cpu->pc&255];
    }
  }
  int cost=sc_interpreter_fallback(cpu);return cost>0?(unsigned)cost:1;
}
static unsigned sc_run_program_lane(uint64_t target,long *guard) {
  static int reference=-1;
  if(reference<0) {const char *e=getenv("SC_PROGRAM_LANE_REFERENCE");reference=e && *e=='1';}
  Interp816 *cpu=g_cpu;Snes *snes=g_snes;
  if(reference || s_addr_trace_count || s_pc_capture_after!=-1 || s_dump_pc_armed ||
      sc_program_host_boundary(cpu) || !ScProgramAvailable(cpu)) return 0;
  ScProgramHostFlow flow={target,guard,0};
  unsigned block_edges=ScProgramRun(cpu,&flow,sc_program_flow_before,
      sc_program_flow_after,sc_program_flow_fallback);
  if(block_edges) {
    s_program_lane_edges+=block_edges;++s_program_lane_entries;
    s_program_block_edges+=block_edges;++s_program_block_entries;
    return block_edges;
  }
  unsigned edges=0;
  do {
    /* The enclosing loop already observed/profiled the first PC. Subsequent
     * PCs remain observable here, even when a span crosses a bank or width. */
    if(edges && s_bank_profile) {
      ++s_bank_ops[cpu->k];
      if(cpu->k==s_bank_page_sel) {
        ++s_b3_page[cpu->pc>>8];
        if((cpu->pc>>8)==s_pc_profile_page) ++s_pc_profile_ops[cpu->pc&255];
      }
    }
    ScWorldGuestStepPrepared(&s_world,cpu,g_ram);
    ScWorldGuestVehiclesPrepared(&s_world,cpu,g_ram,snes->multiplyA);
    sc_note_executed_pc(((uint32_t)cpu->k<<16)|cpu->pc,cpu->mf,cpu->xf);
    ScWorldGuestBeginPrepared(&s_world_guest,&s_world,cpu,snes->cart->rom,snes->cart->romSize);
    unsigned cost=ScProgramStep(cpu);
    if(!cost) {
      /* A live ROM patch may invalidate a compiled action after membership
       * admission. Its world hooks have already run: execute the original
       * edge here, without applying those hooks a second time. */
      if(s_bank_profile) {
        ++s_interpreter_bank_ops[cpu->k];
        if(cpu->k==s_bank_page_sel) {
          ++s_interpreter_pages[cpu->pc>>8];
          if((cpu->pc>>8)==s_pc_profile_page) ++s_interpreter_pc[cpu->pc&255];
        }
      }
      int fallback=sc_interpreter_fallback(cpu);cost=fallback>0?(unsigned)fallback:1;
    }
    unsigned master=cost*8;
    g_master_cycles+=master;
    sc_advance_beam(master);
    snes->apuCatchupCycles+=(double)master*kApuCyclesPerMaster;
    sc_catchup_apu(snes);
    ++edges;
    /* Beam processing can assert IRQ/NMI, begin DMA, or finish this frame.
     * Check the live CPU again before another C edge. Nothing is deferred. */
    if(s_frames>=target || *guard<=0 || sc_program_host_boundary(cpu) ||
        !ScProgramAvailable(cpu)) break;
    --*guard;
  } while(true);
  s_program_lane_edges+=edges;++s_program_lane_entries;
  return edges;
}

static uint64_t s_wait_lane_entries,s_wait_lane_spans;
/* Frame-wait execution has no city hooks. Keep its event-bounded native C
 * continuations in one scheduler lane instead of returning through the full
 * gameplay dispatcher at each line/HDMA boundary. Beam and APU retirement
 * remain per span, including an atomic instruction at an exact event. */
static unsigned sc_run_wait_lane(uint64_t target,long *guard,bool atomic_reference) {
  static int reference=-1;
  if(reference<0) {const char *e=getenv("SC_WAIT_LANE_REFERENCE");reference=e && *e=='1';}
  Interp816 *cpu=g_cpu;Snes *snes=g_snes;unsigned spans=0;
  while(!cpu->k && ScWaitOwns(cpu->pc) && !cpu->nmiWanted && !(cpu->irqWanted && !cpu->i)) {
    if(spans && (s_frames>=target || *guard<=0)) break;
    unsigned entry=cpu->pc;
    unsigned boundary=snes->hPos<1024?1024:1364;
    if(snes->hIrqEnabled && (!snes->vIrqEnabled || snes->vPos==snes->vTimer+1)) {
      unsigned irq=4*snes->hTimer;
      if(irq>=snes->hPos && irq<boundary) boundary=irq;
    }
    unsigned cost=0;
    /* hPos==0 can assert NMI/start HDMA. An atomic edge observes that event
     * at the same indivisible instruction boundary as the original driver. */
    if(snes->hPos && boundary>snes->hPos+2)
      cost=ScWaitDriverStep(cpu,g_ram,(boundary-snes->hPos-2)/8);
    if(!cost && !atomic_reference) cost=ScWaitInstructionStep(cpu,g_ram);
    if(!cost) break;
    if(spans) {
      --*guard;
      if(s_bank_profile) {
        ++s_bank_ops[0];
        if(!s_bank_page_sel) {
          ++s_b3_page[entry>>8];
          if((entry>>8)==s_pc_profile_page) ++s_pc_profile_ops[entry&255];
        }
      }
    }
    memset(&s_world_guest,0,sizeof s_world_guest);
    ++s_perf_spatial_cells;
    unsigned master=cost*8;
    g_master_cycles+=master;
    sc_advance_beam(master);
    snes->apuCatchupCycles+=(double)master*kApuCyclesPerMaster;
    sc_catchup_apu(snes);
    ++spans;
    if(reference) break;
  }
  if(spans) {++s_wait_lane_entries;s_wait_lane_spans+=spans;}
  return spans;
}

/* Development attempts run across districts at a bounded frame boundary. A loaded
 * legacy mid-attempt finishes its remaining native repetitions normally. */
static uint16_t prepare_development_pc(Interp816 *cpu) {
  if(s_development_batches && !s_development.repeating)
    ScDevelopmentBatchesObserve(s_development_batches,&s_world,cpu,g_ram);
  if(s_development_batches && !s_development.remaining && cpu->k==3) {
    if(cpu->pc==0x926f)return 0x92cc;
    if(cpu->pc==0x931b)return 0x9378;
    if(cpu->pc==0x93d1)return 0x9447;
  }
  return ScDevelopmentStepWorld(&s_development,&s_world,g_ram,cpu->pc,cpu->dp,cpu->sp,
      s_development_batches?1:s_development_speed);
}

static uint64_t s_development_lane_entries,s_development_lane_spans;
/* Extra zone attempts have no beam, calendar or audio retirement. Connect
 * their native capacity/decision/artwork/math helpers and compiled C edges
 * here instead of revisiting the complete city/UI dispatcher at each helper.
 * The ordinary attempt and final zone-frame return stay with that dispatcher. */
static bool sc_development_lane_site(const Interp816 *cpu) {
  if(cpu->k!=3 || cpu->db!=3 || cpu->waiting || cpu->stopped ||
     cpu->nmiWanted || (cpu->irqWanted && !cpu->i)) return false;
  unsigned pc=cpu->pc;
  if(pc==0x93b7 || pc==0x9301 || pc==0x9255)return false; /* Census ownership. */
  return (pc>=0x842f && pc<=0x84ea) || (pc>=0x9035 && pc<=0x90a6) ||
      (pc>=0x923d && pc<=0x9a92) || (pc>=0xa29a && pc<=0xa2f4) ||
      ScMathDivideOwns(pc);
}
static unsigned sc_run_development_lane(long *guard) {
  static int reference=-1,math_reference=-1,kernel_enabled=-1,extra_reference=-1;
  if(reference<0) {const char *e=getenv("SC_DEVELOPMENT_LANE_REFERENCE");reference=e && *e=='1';}
  if(math_reference<0) {const char *e=getenv("SC_MATH_REFERENCE");math_reference=e && *e=='1';}
  if(kernel_enabled<0) {const char *e=getenv("SC_SPATIAL_KERNELS");kernel_enabled=!e || *e!='0';}
  if(extra_reference<0) {const char *e=getenv("SC_EXTRA_BEAM_REFERENCE");extra_reference=e && *e=='1';}
  if(reference || extra_reference || !s_development.repeating || s_addr_trace_count ||
     s_pc_capture_after!=-1 || s_dump_pc_armed || !sc_development_lane_site(g_cpu))return 0;
  Interp816 *cpu=g_cpu;const uint8_t *rom=g_snes->cart->rom;size_t size=g_snes->cart->romSize;
  unsigned spans=0;
  while(s_development.repeating && sc_development_lane_site(cpu) && *guard>0) {
    /* Let main retire the final PLD/RTS at its ordinary clock. Admission of
     * that final boundary must not consume an extra profiling/guard edge. */
    if(cpu->pc==s_development.end && s_development.remaining<=1)break;
    if(spans) {
      --*guard;
      if(s_bank_profile) {
        ++s_bank_ops[3];
        if(s_bank_page_sel==3) {
          ++s_b3_page[cpu->pc>>8];
          if((cpu->pc>>8)==s_pc_profile_page)++s_pc_profile_ops[cpu->pc&255];
        }
      }
      ScWorldGuestStepPrepared(&s_world,cpu,g_ram);
      uint16_t next=prepare_development_pc(cpu);
      if(next!=cpu->pc)cpu->mf=cpu->xf=false;
      cpu->pc=next;
      if(!s_development.repeating || !sc_development_lane_site(cpu))break;
    }
    sc_note_executed_pc(((uint32_t)cpu->k<<16)|cpu->pc,cpu->mf,cpu->xf);
    /* Direct C helpers own their map accesses. Bind the compatibility bus
     * only if this edge falls through to compiled opcode execution. */
    if(s_native_bind_eager)ScWorldGuestBeginPrepared(&s_world_guest,&s_world,cpu,rom,size);
    else {s_world_guest.mapped=false;if(s_bank_profile)++s_native_bind_deferred;}
    unsigned cost=cpu->pc==0x9a3e?ScWorldGuestHousingStep(&s_world,cpu,g_ram,512):0;
    if(!cost && cpu->pc==0x987f)cost=ScWorldGuestHouseSiteStep(&s_world,cpu,g_ram,rom,size,512);
    if(!cost)cost=ScDevelopmentNativeBatch(&s_development,&s_world,cpu,g_ram,rom,size);
    if(cost && s_perf_detail)++s_perf_native_development_calls;
    if(!cost && !math_reference)cost=ScMathBatchStep(cpu,g_ram,4096);
    if(!cost && kernel_enabled && s_world.active)
      cost=ScWorldGuestBatchStep(&s_world,cpu,g_ram,rom,size,4096);
    if(!cost)cost=ScWorldGuestFastStep(&s_world,cpu,g_ram,rom,size);
    if(!cost)cost=ScWaitInstructionStep(cpu,g_ram);
    if(!cost && !math_reference)cost=ScMathInstructionStep(cpu,g_ram);
    if(!cost)cost=ScWorldGuestInstructionStep(&s_world,cpu,g_ram,rom,size);
    if(cost) {s_world_guest.mapped=false;++s_perf_spatial_cells;}
    else {
      if(!s_native_bind_eager) {ScWorldGuestBeginPrepared(&s_world_guest,&s_world,cpu,rom,size);
        if(s_bank_profile)++s_native_bind_fallback;}
      cost=ScProgramStep(cpu);
    }
    if(!cost)break; /* Unconverted/modified code keeps its original host path. */
    ++spans;
  }
  if(spans) {++s_development_lane_entries;s_development_lane_spans+=spans;}
  return spans;
}

static uint64_t s_city_lane_entries,s_city_lane_spans;
static bool city_lane_site(const Interp816 *c) {
    static int field_reference=-1;
    if(field_reference<0) {const char *e=getenv("SC_FIELD_LANE_REFERENCE");field_reference=e && *e=='1';}
    unsigned p=c->pc;
    if(c->k!=3 || c->db!=3 || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i))return false;
    if(p==0x9255 || p==0x9301 || p==0x93b7)return false;
    return (p>=0x8297 && p<=0x84ea) || (p>=0x88f3 && p<0x90a7) ||
        (p>=0x90c5 && p<=0x9a92) || (!field_reference && p>=0x9a93 && p<=0xa299) ||
        (p>=0xa29a && p<=0xa2b9) ||
        (p>=0xa493 && p<0xa6b8) || (p>=0xa70c && p<0xa7e0) || ScMathDivideOwns(p);
}
/* Keep the spatial simulation in its connected C lane until a beam event or
 * a host-owned census boundary. This removes the menu/save/construction
 * dispatcher from every tile, growth-helper and derived-field edge on the
 * largest maps. Prepared world hooks still run at each span, including GPU
 * submission, full-width centroid carries and redirected field iterators. */
static unsigned sc_run_city_lane(uint64_t target,long *guard) {
    static int reference=-1,diagnostic=-1;
    if(reference<0) {const char *e=getenv("SC_CITY_LANE_REFERENCE");reference=e && *e=='1';}
    if(diagnostic<0)diagnostic=getenv("SC_SPATIAL_KERNELS") || getenv("SC_MATH_REFERENCE");
    if(reference || !s_world.giant || s_addr_trace_count || s_pc_capture_after!=-1 || s_dump_pc_armed ||
       diagnostic || !city_lane_site(g_cpu))return 0;
    Interp816 *c=g_cpu;Snes *snes=g_snes;const uint8_t *rom=snes->cart->rom;size_t size=snes->cart->romSize;
    unsigned spans=0;
    while(s_frames<target && *guard>0 && city_lane_site(c)) {
        if(spans) {
            --*guard;
            if(s_bank_profile) {
                ++s_bank_ops[3];
                if(s_bank_page_sel==3) {++s_b3_page[c->pc>>8];if((c->pc>>8)==s_pc_profile_page)++s_pc_profile_ops[c->pc&255];}
            }
            ScWorldGuestStepPrepared(&s_world,c,g_ram);
            uint16_t next=prepare_development_pc(c);
            if(next!=c->pc)c->mf=c->xf=false;
            c->pc=next;
            if(!city_lane_site(c))break;
        }
        unsigned entry=c->pc;bool extra=s_development.repeating;
        unsigned boundary=snes->hPos<1024?1024:1364;
        if(snes->hIrqEnabled && (!snes->vIrqEnabled || snes->vPos==snes->vTimer+1)) {
            unsigned irq=4*snes->hTimer;if(irq>=snes->hPos && irq<boundary)boundary=irq;
        }
        unsigned scale=ScWorldGuestClockScale(&s_world,0x30000|entry),budget=0;
        if(extra)budget=4096;
        else if(snes->hPos && boundary>snes->hPos+2) {
            unsigned master=(boundary-snes->hPos-2)*scale,remainder=scale>1?s_map_cycle_remainder:0;
            if(master>remainder)budget=(master-remainder)/8;
        }
        if(s_native_bind_eager)ScWorldGuestBeginPrepared(&s_world_guest,&s_world,c,rom,size);
        else {s_world_guest.mapped=false;if(s_bank_profile)++s_native_bind_deferred;}
        unsigned cost=0;
        if(extra) {
            if(entry==0x9a3e)cost=ScWorldGuestHousingStep(&s_world,c,g_ram,512);
            if(!cost && entry==0x987f)cost=ScWorldGuestHouseSiteStep(&s_world,c,g_ram,rom,size,512);
            if(!cost)cost=ScDevelopmentNativeBatch(&s_development,&s_world,c,g_ram,rom,size);
        }
        if(!cost && ((entry>=0x9035 && entry<=0x90a6) || ScMathDivideOwns(entry)))
            cost=ScMathBatchStep(c,g_ram,budget);
        if(!cost && budget)cost=ScWorldGuestBatchStep(&s_world,c,g_ram,rom,size,budget);
        if(!cost && (c->pc!=0x9dca || (c->a&1023)<0x28 || budget>=192 || extra))
            cost=ScWorldGuestFastStep(&s_world,c,g_ram,rom,size);
        if(!cost)cost=ScMathInstructionStep(c,g_ram);
        if(!cost)cost=ScWorldGuestInstructionStep(&s_world,c,g_ram,rom,size);
        if(cost) {s_world_guest.mapped=false;++s_perf_spatial_cells;}
        else {
          if(!s_native_bind_eager) {ScWorldGuestBeginPrepared(&s_world_guest,&s_world,c,rom,size);
            if(s_bank_profile)++s_native_bind_fallback;}
          cost=ScProgramStep(c);
        }
        if(!cost)break;
        sc_note_executed_pc(0x30000|entry,c->mf,c->xf);++spans;
        if(!extra) {
            unsigned master=ScWorldGuestMasterCycles(&s_world,0x30000|entry,cost*8,&s_map_cycle_remainder);
            g_master_cycles+=master;sc_advance_beam(master);
            snes->apuCatchupCycles+=(double)master*kApuCyclesPerMaster;sc_catchup_apu(snes);
        }
    }
    if(spans) {++s_city_lane_entries;s_city_lane_spans+=spans;}
    return spans;
}

static bool run_one_frame(void) {
#ifdef SC_AOT_TIER
  if (s_fiber_mode) return run_one_frame_fiber();
#endif
  if(s_rom_is_us && !s_development_batch_reference && !s_development_batches && host_map_screen_live())
    s_development_batches=ScDevelopmentBatchesCreate();
  if(s_development_batches && host_map_screen_live() && !ram_w(0xd7) && !ram_w(0x379) &&
     !s_build_active && !s_build_pending && !s_clip_pending && !s_development.remaining) {
    unsigned done=ScDevelopmentBatchesRun(s_development_batches,&s_world,g_ram,
        g_snes->cart->rom,g_snes->cart->romSize,s_frames,s_development_speed,UINT_MAX,
        SDL_GetPerformanceCounter,SDL_GetPerformanceFrequency(),4.0,ScProgramStep);
    s_development.extra_attempts+=done;
  }
  unsigned population_cells=s_world.active?ScWorldCells(&s_world):12000;
  if (s_population_game_speed!=g_ram[0x193] || s_population_clock_cells!=population_cells) {
    s_population_game_speed=g_ram[0x193];
    s_population_clock_cells=population_cells;
    ScRefreshClockReset(&s_population_clock,s_population_game_speed==0?800:s_population_game_speed==1?400:200);
  }
  bool population_due=ScRefreshClockDue(&s_population_clock,s_frames,s_development_speed);
  if(s_development_batches && !(s_frames&7))population_due=true;
  if ((s_development_speed<=1 && !s_development_batches) || !s_rom_is_us || s_pop_override>=0) s_population.live=false;
  else if (host_map_screen_live() && !ram_w(0xd7) &&
      (!s_population.live || population_due)) {
    uint64_t prior=s_population.value;
    refresh_live_population(g_snes->cart->rom,g_snes->cart->romSize);
    if (getenv("SC_POPULATION_DIAG") && s_population.value!=prior)
      fprintf(stderr,"[population] frame %llu speed %d value %llu -> %llu\n",
          (unsigned long long)s_frames,s_development_speed,
          (unsigned long long)prior,(unsigned long long)s_population.value);
  }
  if (s_rom_is_us && host_map_screen_live() && !ram_w(0xd7)) refresh_fast_power(false);
  else {
    s_population_clock.observed=s_power_refresh.clock.observed=false;
    s_population_clock.previous_gap=s_power_refresh.clock.previous_gap=0;
  }
  if(s_population.valid && host_map_screen_live())
    ScJourneyObservePopulation(&s_world,s_population.value);
  if (s_fast_ticks) { g_ram[0x01f3] = 0; g_ram[0x01f4] = 0; }
  sc_maybe_trigger_disaster();
  replay_free_tick();
  sc_maybe_trigger_scenario_event();
  scenario_event_tick();
  service_disaster_menu8();
  Snes *snes = g_snes;
  Interp816 *cpu = g_cpu;
  uint64_t target = s_frames + 1;
  /* Read once: the loop below runs once per guest opcode, tens of thousands
   * of times a frame, and getenv() is a locked linear scan on Windows. */
  static int scen_diag = -1, brief_diag;
  if (scen_diag < 0) {
    scen_diag = getenv("SC_SCEN_DIAG") != NULL;
    brief_diag = getenv("SC_BRIEF_DIAG") != NULL;
  }
  long guard = 20000000L * s_development_speed;
  const bool power_diag=getenv("SC_POWER_DIAG")!=NULL;
  static int native_diag=-1;
  if(native_diag<0) native_diag=getenv("SC_NATIVE_DIAG")!=NULL;
  static int native_simulation=-1;
  if(native_simulation<0) {const char *e=getenv("SC_NATIVE_SIMULATION");native_simulation=!e || *e!='0';}
  static int atomic_reference=-1;
  if(atomic_reference<0) {const char *e=getenv("SC_NATIVE_ATOMIC_REFERENCE");atomic_reference=e && *e=='1';}
  /* The extra attempts are bounded but can share one host frame; give
   * them a proportional instruction allowance. Normal retains the old bar. */
  while (s_frames < target && guard-- > 0) {
    if(s_save_dialog_active && cpu->k==s_save_dialog_return.k &&
       cpu->pc==s_save_dialog_return.pc && cpu->sp==s_save_dialog_return.sp) {
      /* The stock save UI returns with working registers/flags changed.
       * Restore the interrupted city's caller, retaining live IRQ/NMI state. */
      cpu->a=s_save_dialog_return.a;cpu->x=s_save_dialog_return.x;cpu->y=s_save_dialog_return.y;
      cpu->dp=s_save_dialog_return.dp;cpu->db=s_save_dialog_return.db;
      interp816_setFlags(cpu,interp816_getFlags(&s_save_dialog_return));
      ram_set_w(0x1df,s_save_dialog_page);
      ram_set_w(0x1eb,s_save_dialog_x);ram_set_w(0x1ed,s_save_dialog_y);
      s_save_dialog_active=false;
      if(getenv("SC_SAVE_DIALOG_DIAG")) fprintf(stderr,"[escape save] returned to city at frame %llu\n",(unsigned long long)s_frames);
    }
    if(s_save_dialog_active && cpu->k==1 && cpu->pc==0xad54) {
      /* Enter Save on the stock file page, then close that page after the
       * slot dialog returns. The enclosing native menu restores the city
       * PPU, tilemaps and modal flags on both Save and Cancel. */
      if(s_save_dialog_phase==1) {s_save_dialog_phase=2;cpu->mf=false;cpu->a=1;cpu->pc=0xad8f;}
      else cpu->pc=0xae22;
    }
    if(s_save_dialog_pending && s_rom_is_us && cpu->k==1 && cpu->pc==0x8976 &&
       cpu->dp==0 && cpu->db==0 &&
       !ram_w(0xd7) && !ram_w(0x379) && !g_ram[0x391] && !g_ram[0xe3] &&
       !s_city_loading && !cpu->nmiWanted && !(cpu->irqWanted && !cpu->i)) {
      s_save_dialog_pending=false;s_save_dialog_active=true;s_save_dialog_return=*cpu;
      s_save_dialog_phase=1;
      s_save_dialog_page=ram_w(0x1df);s_save_dialog_x=ram_w(0x1eb);s_save_dialog_y=ram_w(0x1ed);
      ram_set_w(0x1df,4); /* 01:9dd5's indexed JSR selects the File page. */
      /* A real JSR frame enters the original file-menu transition; jumping
       * straight into the slot dialog skips its display setup. */
      unsigned return_pc=(cpu->pc-1)&65535;
      cpu->write(cpu->mem,cpu->sp--,return_pc>>8);
      cpu->write(cpu->mem,cpu->sp--,return_pc&255);
      cpu->pc=0x9d6b;
      sc_charge_master_cycles(g_snes,6*8); /* injected native JSR */
      if(getenv("SC_SAVE_DIALOG_DIAG")) fprintf(stderr,"[escape save] entering native file menu at frame %llu\n",(unsigned long long)s_frames);
    }
    if (cpu->k == 0x00 && cpu->pc == 0x80b2) s_nmi_serviced++;
    if(s_rom_is_us && ((cpu->k==0 && (cpu->pc==0xd19e || cpu->pc==0xd1fc || cpu->pc==0xd20a)) ||
                      (cpu->k==1 && (cpu->pc==0xcc1a || cpu->pc==0xcc3a))))
      ScMouseUiObserve(&s_mouse_dialog,g_ram,cpu->k,cpu->pc,cpu->sp);
    if(s_rom_is_us && s_world.test_city && cpu->k==0) {
      /* Once Save? is confirmed, City 3 has its own destination. */
      if(cpu->pc==0xc8e0)cpu->pc=0xc947; /* Save? instead of Where to save? */
      if(cpu->pc==0xc987) {ram_set_w(0x423,1);cpu->pc=0xc9bc;}
      if(cpu->pc==0xca35) {ram_set_w(0x423,1);cpu->pc=0xca6a;}
    }
    /* SC_BANK_PROFILE=1: opcodes executed per bank, and how many frames the
     * simulation tick spans. Answers "is the simulation worth replacing with
     * native code" with a number instead of an impression -- the map
     * generator was worth it because ~800 frames of wall clock were measured
     * first, not assumed. */
    if (s_bank_profile) {
      s_bank_ops[cpu->k]++;
      if (cpu->k == s_bank_page_sel) s_b3_page[cpu->pc >> 8]++;
      if(cpu->k==s_bank_page_sel && (cpu->pc>>8)==s_pc_profile_page) ++s_pc_profile_ops[cpu->pc&255];
      if (cpu->k == 0x03 && cpu->pc == 0x8000) {
        s_tick_count++;
        s_tick_start_frame = s_frames;
        s_tick_start_ops = s_bank_ops[3];
      }
      if (cpu->k == 0x03 && cpu->pc == 0x8026 && s_tick_start_ops) {
        const unsigned long long span = s_frames - s_tick_start_frame;
        s_tick_frames_total += span;
        s_tick_ops_total += s_bank_ops[3] - s_tick_start_ops;
        if (span > s_tick_frames_max) s_tick_frames_max = span;
        s_tick_start_ops = 0;
      }
    }
    /* The verified frame-wait path touches only WRAM. Execute its connected
     * C lane before the gameplay hook dispatcher, retaining every beam/APU
     * event and an atomic edge at line start, HDMA or IRQ deadlines. Address
     * tracing keeps the ordinary observer path. A pending interrupt always
     * goes through the original CPU dispatcher first. */
    if(native_simulation && s_rom_fnv==SC_ROM_FNV_US && !s_addr_trace_count &&
       !cpu->k && ScWaitOwns(cpu->pc) && !cpu->nmiWanted && !(cpu->irqWanted && !cpu->i)) {
      if(s_wait_lane_enabled) {
        if(sc_run_wait_lane(target,&guard,atomic_reference)) continue;
      } else {
        /* Ordinary play retains the preceding inlined driver: qualification
         * found a repeatable benefit for Tab, but a regression without it. */
        unsigned boundary=snes->hPos<1024?1024:1364;
        if(snes->hIrqEnabled && (!snes->vIrqEnabled || snes->vPos==snes->vTimer+1)) {
          unsigned irq=4*snes->hTimer;
          if(irq>=snes->hPos && irq<boundary) boundary=irq;
        }
        unsigned cost=0;
        if(snes->hPos && boundary>snes->hPos+2)
          cost=ScWaitDriverStep(cpu,g_ram,(boundary-snes->hPos-2)/8);
        if(!cost && !atomic_reference) cost=ScWaitInstructionStep(cpu,g_ram);
        if(cost) {
          memset(&s_world_guest,0,sizeof s_world_guest);
          ++s_perf_spatial_cells;++s_wait_lane_entries;++s_wait_lane_spans;
          unsigned master=cost*8;
          g_master_cycles+=master;
          sc_advance_beam(master);
          snes->apuCatchupCycles+=(double)master*kApuCyclesPerMaster;
          sc_catchup_apu(snes);
          continue;
        }
      }
    }
    if(native_simulation && s_rom_fnv==SC_ROM_FNV_US && sc_run_program_lane(target,&guard))
      continue;
    /* Unconditional now that AUTO TURBO is gone. This only opens the
     * generation/decompression window; whether anything speeds up is MAPGEN
     * TURBO's decision, and 1 means off. */
    /* Hold the boost for the WHOLE generation, not a fixed window after the
     * trigger.
     *
     * 03:d862 is the PRNG seeding loop -- 1 to 32 iterations, over in an
     * instant. The actual work is the two JSLs after it, 01:f1ed (terrain
     * features) and 02:923f. Measured with a 20-frame holdoff: 4 trigger hits,
     * 20 boosted frames, and the wait untouched, because the holdoff expired
     * long before the generator finished.
     *
     * 03:d871 is where execution resumes once both JSLs have returned, so the
     * pair bounds the generation exactly. The decompressor at 00:90dd keeps a
     * plain holdoff -- it has no equivalent end marker and is short. */
    if (cpu->k == 0x03 && cpu->pc == 0xd862) s_gen_trigger_hits++;
    if (s_rom_is_us && cpu->k == 0x02 && (cpu->pc == 0x8b36 || cpu->pc == 0x89b4))
      sc_classifier_hook(cpu);
    if (cpu->k <= 0x01 && s_ws_vehicles && ScVehicles_WantsPc(cpu->k, cpu->pc))
      ScVehicles_OnPc(cpu->k, cpu->pc, cpu->x, cpu->y, cpu->dp, cpu->db);
    /* The title sign's state with the shadow OAM it belongs to: read anywhere
     * else in the frame it is a pixel ahead on some frames and not on others,
     * which reads as a shiver (src/sc_titlesign.c). */
    if (cpu->k == 0x00 && cpu->pc == 0x80c0)
      ScTitleSign_Snapshot(g_ram, sc_bus_rom_read, NULL);
    if (s_rom_is_us && cpu->k == 0x00 && (cpu->pc == 0x90dd || cpu->pc == 0x90ee ||
                           cpu->pc == 0x9108))
      sc_decomp_hook(cpu);

    /* â”€â”€ Run the map generator natively, on the INTERPRETER path â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
     *
     * 01:f1ed is the whole generator, reached by JSL from 03:d869, and the
     * SNES CPU takes about 800 frames of wall clock to grind through it --
     * thirteen seconds of watching a map appear a few cells at a time.
     * src/sc_mapgen.c does the same work in well under a frame.
     *
     * This is the same substitution as the ScHle_MapGen HLE, but hooked
     * here rather than through hle_func, and that difference is the point:
     * hle_func only applies to AOT bodies, so it needs SC_FIBER, and the
     * fiber currently has rendering defects of its own
     * (docs/TODO_fiber_rendering.md). Hooking the interpreter delivers the
     * fast generation on the path that is actually correct.
     *
     * The GENERATOR is verified bit-exact -- three maps across both branches
     * reproduce the guest's map on all 12000 cells, consume its exact draw
     * count and leave the PRNG in its exact final state -- and the same
     * substitution is proven working through the fiber HLE.
     *
     * THIS HOOK IS NOW VERIFIED IN SITU, against the ROM's own generator on the
     * same save state: identical 5206 cells, identical kept index, identical
     * PRNG state 180B/1385, 100.00% of cells. The map completes at frame 90
     * against frame 690 for the ROM -- 600 frames, ten seconds, gone.
     *
     * Six further generations from an interactive session all returned to
     * 03:D86D, and three of them reproduce guest captures taken independently:
     * 4258 cells / 2DC4-8570, 9469 / 5B19-426F and 6626 / 346D-529F.
     *
     * A note for whoever verifies the next thing here: "gen: trigger_hits"
     * does NOT mean the generator ran. That counter is shared with the 00:90dd
     * decompressor, and a non-zero count while chasing this cost an hour.
     *
     * The RTL emulation below is the part most worth re-reading before
     * trusting this: get the pull order or the +1 wrong and it returns into
     * the middle of the caller.
     *
     * By the time execution reaches f1ed the caller has already seeded and
     * pre-stepped the PRNG (03:d84d through the d862 loop), so $59/$5b ARE the
     * starting point and there is no seeding to redo. */
    if (s_rom_is_us && cpu->k == 0x01 && cpu->pc == 0xf1ed) {
      static int fast = -1;
      if (fast < 0) {
        const char *e = getenv("SC_MAPGEN_FAST");
        fast = (e && *e) ? (*e != '0') : 1;   /* on; SC_MAPGEN_FAST=0 disables */
      }
      if (!fast && !s_large_maps) {ScWorldReset(&s_world);s_world.journey=s_journey_arming;}
      unsigned number=g_ram[0xb27]+g_ram[0xb28]*10+g_ram[0xb29]*100+s_map_number_high*1000;
      if (fast || s_large_maps || s_terrain_style || number==31337) {
        static ScMapGenState gs;
        ScMapGenPrng pr;
        pr.s0 = (uint16_t)(g_ram[0x59] | (g_ram[0x5a] << 8));
        pr.s1 = (uint16_t)(g_ram[0x5b] | (g_ram[0x5c] << 8));
        pr.t  = (uint16_t)(g_ram[0x5d] | (g_ram[0x5e] << 8));
        /* Preserve the cartridge's original three-digit seed path. The two
         * added digits extend its PRNG, rather than replace its terrain. */
        if(number>=1000) {
          uint32_t key=sc_mapgen_number_key(number/1000);
          pr.s0^=(uint16_t)key;pr.s1^=(uint16_t)(key>>16);
        }
        if (s_large_maps && !s_journey_arming && s_rom_fnv==SC_ROM_FNV_US) {
          ScWorldGenerateStyled(&s_world,s_large_maps,&pr,s_terrain_style);
          ScWorldApplyMapNumber(&s_world,number);ScWorldMirror(&s_world,g_ram);
        } else {
          ScWorldReset(&s_world);
          if(s_terrain_style)sc_mapgen_generate_style(&pr,&gs,0,s_terrain_style);
          else sc_mapgen_generate(&pr,&gs);
          sc_mapgen_apply_number(&gs,number);
          s_world.journey=s_journey_arming;
        }
        /* The map is at $7F0200 -- bank 7F, so 0x10200 into WRAM. */
        for (unsigned i = 0; !s_world.active && i < SC_MAPGEN_CELLS; i++) {
          g_ram[0x10200 + 2 * i]     = (uint8_t)(gs.map[i] & 0xff);
          g_ram[0x10200 + 2 * i + 1] = (uint8_t)((gs.map[i] >> 8) & 0xff);
        }
        if(s_world.active) {
          memcpy(gs.map,s_world.tiles,ScWorldCells(&s_world)*2);
        }
        sc_mapgen_preview_build(&s_custom_renderer.map_preview,gs.map,
            s_world.active?ScWorldWidth(&s_world):120,s_world.active?ScWorldHeight(&s_world):100,pr.s0);
        s_preview_expanded=s_preview_complete=false;
        s_map_number_dirty=false;
        /* Generation precedes the native Please wait panel. Start the reveal
         * only when map selection is visible, so that panel cannot consume it. */
        s_preview_started=0;
        if(getenv("SC_MAP_PREVIEW_DIAG")) fprintf(stderr,"[map preview] started frame %llu mode %u geometry %u,%u number %05u\n",
            (unsigned long long)s_frames,ram_w(0x14),s_world.active?ScWorldWidth(&s_world):120,
            s_world.active?ScWorldHeight(&s_world):100,number);
        g_ram[0x59] = (uint8_t)(pr.s0 & 0xff); g_ram[0x5a] = (uint8_t)(pr.s0 >> 8);
        g_ram[0x5b] = (uint8_t)(pr.s1 & 0xff); g_ram[0x5c] = (uint8_t)(pr.s1 >> 8);
        g_ram[0x5d] = (uint8_t)(pr.t  & 0xff); g_ram[0x5e] = (uint8_t)(pr.t  >> 8);

        /* Emulate the RTL that ends 01:f1ed: pull PCL, PCH, PBR, then PC+1.
         * The JSL at 03:d869 pushed three bytes; leaving them would return
         * into the middle of the caller. */
        { uint16_t sp = cpu->sp;
          uint8_t lo  = g_ram[(uint16_t)(sp + 1)];
          uint8_t hi  = g_ram[(uint16_t)(sp + 2)];
          uint8_t pbr = g_ram[(uint16_t)(sp + 3)];
          cpu->sp = (uint16_t)(sp + 3);
          cpu->k  = pbr;
          cpu->pc = (uint16_t)(((hi << 8) | lo) + 1); }

        if (getenv("SC_MAPGEN_FAST_DIAG")) {
          unsigned nz = 0;
          unsigned cells=s_world.active?ScWorldCells(&s_world):SC_MAPGEN_CELLS;
          for (unsigned i = 0; i < cells; i++)
            if ((s_world.active?ScWorldCell(&s_world,i%ScWorldWidth(&s_world),i/ScWorldWidth(&s_world)):gs.map[i]) & 0x3ff) nz++;
          fprintf(stderr, "[mapgen_fast] %u cells, %lu draws, prng %04X/%04X, "
                          "return %02X:%04X\n",
                  nz, g_sc_mapgen_prng_steps, (unsigned)pr.s0, (unsigned)pr.s1,
                  (unsigned)cpu->k, (unsigned)cpu->pc);
        }
      }
    }
    /* Post-load power fix -- see apply_power_fix(). 03:c8dd is reached with
     * the map already unpacked and SRAM already restored. */
    /* TWO hook points, because there are two ways a map arrives.
     *
     * 03:c8dd is the save-load path: 03:c8c8 has unpacked the map and
     * 03:c8cb has restored SRAM. Scenarios never go through it -- they load
     * via 03:ce2e, which unpacks with its own JSR $d15f at 03:ce5e and
     * returns to 03:ce61. So the power fix has been firing on loaded cities
     * and never on scenarios, which is exactly the "scenarios lose power at
     * start" report: the same post-load dropout, unpatched.
     *
     * 03:ce61 is the scenario equivalent -- map in place, about to return. */
    if (s_ninth_scenario) ninth_scenario_hook(cpu->k, cpu->pc);
    if (s_rom_is_us) {
      /* The city setup enables terrain before HUD uploads and the original
       * fade-in task are ready. Keep this transient view behind black. */
      if((cpu->k==3 && (cpu->pc==0xc621 || cpu->pc==0xc66a ||
            cpu->pc==0xc67f || cpu->pc==0xc8a1)) ||
          (cpu->k==1 && cpu->pc==0x8907 && !s_city_present_pending)) {
        s_city_present_pending=true;s_city_fade_started=s_city_black_seen=false;
        if(getenv("SC_CITY_ENTRY_DIAG"))fprintf(stderr,"[city entry] prepare frame %llu pc %02x:%04x\n",(unsigned long long)s_frames,cpu->k,cpu->pc);
      }
      if(s_city_present_pending && cpu->k==1 && cpu->pc==0x8948) {
        s_city_fade_started=true;
        if(getenv("SC_CITY_ENTRY_DIAG"))fprintf(stderr,"[city entry] fade frame %llu\n",(unsigned long long)s_frames);
      }
      if(s_test_revealed && cpu->k==2 && cpu->pc==0xbec6) {
        unsigned direction=ram_w(0xca)&12,choice=ram_w(0x421)%3;
        if(direction) {
          choice=(choice+((direction&4)?1:2))%3;ram_set_w(0x421,choice);g_ram[6]=7;
        }
        cpu->pc=0xbede; /* retain native hand publication and input yield */
      }
      if(s_test_revealed && cpu->k==2 && cpu->pc==0xbef2 && ram_w(0x421)==2)
        cpu->a=(cpu->a&0xff00)|ScSavedCityMenuY(2);
      if(s_test_revealed && cpu->k==3 && cpu->pc==0xe2a5 && ram_w(0x421)==2)
        cpu->pc=0xe2a9; /* generated City 3 is available even before first save */
      if(cpu->k==3 && cpu->pc==0xe2b8 && s_test_revealed && ram_w(0x421)==2) {
        if(test_city_begin_load())ram_set_w(0x421,0);
        else {cpu->pc=0xe2c4;ram_set_w(0x421,2);}
      }
      if(cpu->k==3 && cpu->pc==0xc5e5 && s_test_generate_pending)cpu->pc=0xc5eb;
      if(cpu->k==3 && cpu->pc==0xcafd && s_world.test_city) {
        if(test_city_private_sram(NULL)) {s_test_saving=true;ram_set_w(0x423,1);}
        else {fprintf(stderr,"[test city] save unavailable; normal slots preserved\n");cpu->pc=0xcc16;ram_set_w(0x48,1);}
      }
      /* Suppress only these notice/adviser publications. Their crime,
       * traffic and pollution calculations and all other events still run.
       * The original routine's RTS keeps the caller's stack intact. */
      if(s_mute_city_warnings && cpu->k==3 && !cpu->mf) {
        unsigned id=cpu->a;
        if(cpu->pc==0xc030 && (id==9 || id==10 || id==11 || id==27))
          cpu->pc=0xc085;
        else if((cpu->pc==0xbe0b || cpu->pc==0xc08b) &&
            (id==6 || id==7 || id==8 || id==21 || id==30 || id==40)) {
          if(getenv("SC_SETTINGS_DIAG")) fprintf(stderr,"[settings] suppressed city warning %u\n",id);
          cpu->pc=cpu->pc==0xbe0b?0xbe11:0xc094;
        }
      }
      if(s_mute_city_warnings && cpu->k==1 && cpu->pc==0x8976 &&
          cpu->dp==0 && cpu->db==0) {
        /* Also discard a pending publication restored from a save or queued
         * before F12 was opened. Already-open adviser panels keep their native
         * close path; do not interrupt a gift or milestone visit. */
        unsigned id=ram_w(0x397),notice=ram_w(0x389);
        if(!ram_w(0x391) && (id==6 || id==7 || id==8 || id==21 || id==30 || id==40)) {
          ram_set_w(0x395,0);ram_set_w(0x397,0);
        }
        if(notice==10 || notice==11 || notice==12 || notice==28) ram_set_w(0x389,0);
        notice=ram_w(0x381);
        if(ram_w(0x387) && (notice==9 || notice==10 || notice==11 || notice==27))
          ram_set_w(0x38b,0); /* ordinary notice expiry clears its panel */
      }
      /* The debug shortcut requests the ordinary Resume transition. Changing
       * $14 directly skips the native exit fade, clear and Load City setup. */
      if(s_test_menu_pending && cpu->k==3 && cpu->pc==0xd333) {
        ram_set_w(0x3e,0);g_ram[0xca]=0x90;
      }
      if(s_test_menu_pending && cpu->k==3 && cpu->pc==0xd337)cpu->pc=0xd33c;
      if(s_test_menu_pending && cpu->k==3 && cpu->pc==0xd369)s_test_menu_pending=false;
      if(cpu->k==3 && (cpu->pc==0xd306 || cpu->pc==0xd30f)) {
        if(getenv("SC_SETTINGS_DIAG"))fprintf(stderr,"[city setup] reset main entry %04x speed-page %d\n",cpu->pc,ScDevelopmentMenuActive());
        s_city_present_pending=s_city_fade_started=s_city_black_seen=false;
        s_size_selecting=s_speed_selecting=s_practice_size_pending=false;ScMapSizeMenuSet(false);
      }
      if((s_size_selecting || s_speed_selecting) && cpu->k==3 && cpu->pc==0xd337) cpu->pc=0xd33b;
      if((s_size_selecting || s_speed_selecting) && cpu->k==3 && cpu->pc==0xd333 && (g_ram[0xc9]&0x40)) {
        if(s_speed_selecting && s_size_game_choice!=3) {
          s_speed_selecting=false;s_size_selecting=true;ScMapSizeMenuSet(true);
          ram_set_w(0x3e,s_large_maps+1);
        } else {
          s_size_selecting=s_speed_selecting=false;ScMapSizeMenuSet(false);
          ram_set_w(0x3e,s_size_game_choice);
        }
        cpu->pc=0xd370;
      }
      if((ScMapSizeMenuActive() || ScDevelopmentMenuActive()) && cpu->k==2 && cpu->pc==0xbcbd) cpu->pc=0xbcd0;
      /* Resume uses its own cartridge text template, whose left edge is
       * twelve pixels to the right of the extended menu's other choices. */
      if(cpu->k==2 && cpu->pc==0xbcc1)ram_set_w(0x25d,124);
      /* Expand the decompressed panel BEFORE its first DMA. A VRAM-only
       * edit is overwritten by that pending upload during the entry fade. */
      if(cpu->k==2 && cpu->pc==0xbb89)
        ScJourneyMenuFrame((uint16_t *)(g_ram+0x8000),0x3000);
      /* Extend the real five-choice menu and its native hand sprite. */
      if(cpu->k==2 && cpu->pc==0xbcd6) {
        ram_set_w(0x25d,136);
        ScJourneyMenuFrame(g_ppu->vram,PPU_bgTilemapAdr(g_ppu,2));
      }
      if(cpu->k==2 && cpu->pc==0xbcfe) {
        unsigned selection=ScDevelopmentMenuActive() && !s_speed_selecting?s_speed_selection:
            ScMapSizeMenuActive() && !s_size_selecting?s_large_maps+1:
            ram_w(0x14)==2 || ram_w(0x14)==3 || ram_w(0x14)==18?ram_w(0x3e):s_journey_menu_selection;
        ram_set_w(0x25d,ScDevelopmentMenuActive()?58:42);
        cpu->a=(cpu->a&0xff00)|ScJourneyMenuY(!ScMapSizeMenuActive() && !ScDevelopmentMenuActive() && ram_w(0x44)!=0,selection);
        if(ScMapSizeMenuActive() && getenv("SC_SIZE_MENU_DIAG"))
          fprintf(stderr,"[map-size pointer] frame %llu choice %u y %u selecting %d action %u\n",
            (unsigned long long)s_frames,selection,cpu->a&255,s_size_selecting,ram_w(0x3e));
      }
      /* Native list input is reused for both setup pages. Intercept before
       * the original exit fade so the speed page stays in the same frame. */
      if(cpu->k==3 && cpu->pc==0xd366) {
        unsigned selected=ram_w(0x3e);
        if(getenv("SC_SETTINGS_DIAG"))fprintf(stderr,"[city setup] choice %u size %d speed %d\n",selected,s_size_selecting,s_speed_selecting);
        if(s_size_selecting) {
          s_large_maps=selected>=1 && selected<=6?selected-1:0;
          save_large_map_setting();s_size_selecting=false;s_speed_selecting=true;
          ScDevelopmentMenuSet(true);ram_set_w(0x3e,s_speed_selection);cpu->pc=0xd370;
        } else if(!s_speed_selecting && selected>=1 && selected<=3) {
          s_size_game_choice=selected;
          if(selected==3) { /* Journey always begins at the original size. */
            s_speed_selecting=true;ScDevelopmentMenuSet(true);ram_set_w(0x3e,s_speed_selection);
          } else {
            s_size_selecting=true;ScMapSizeMenuSet(true);ram_set_w(0x3e,s_large_maps+1);
          }
          cpu->pc=0xd370;
        }
      }
      if(cpu->k==3 && cpu->pc==0xd369) {
        unsigned selected=ram_w(0x3e);
        if(s_speed_selecting) {
          s_speed_selection=selected>=1 && selected<=6?selected:1;
          s_new_city_speed=kCityDevelopmentSpeeds[s_speed_selection-1];
          s_speed_selecting=false;selected=s_size_game_choice;ram_set_w(0x3e,selected);
          s_practice_size_pending=selected==1;
          if(getenv("SC_SETTINGS_DIAG"))fprintf(stderr,"[city speed] selected %ux for new city\n",s_new_city_speed);
        }
        s_journey_menu_selection=selected;s_journey_arming=selected==3;
        if(selected>=3) ram_set_w(0x3e,selected==3?2:3);
      }
      /* Keep the native Metropolis achievement/history, but let Journey's
       * border celebration supply its visit instead of opening two dialogs. */
      if(cpu->k==3 && cpu->pc==0xc112 && s_world.journey && ram_w(0xca5)==4) {
        ram_set_w(0x397,0);cpu->pc=0xc118;
      }
      /* The full cycle has returned from every spatial scan. Resizing here
       * cannot change a suspended scan's row pitch or flood-fill stack. */
      if(cpu->k==3 && cpu->pc==0x8016 && !ram_w(0xd7) && !ram_w(0x379) &&
          !s_build_pending && !s_build_active && !s_native_power_active && s_population.valid) {
        unsigned stage=ScJourneyExpand(&s_world,g_ram,s_population.value);
        if(stage) {
          reset_refresh_clocks();ScDevelopmentReset(&s_development);
          memset(&s_world_guest,0,sizeof s_world_guest);
          ScRendererResetHistory(&s_custom_renderer);
          fprintf(stderr,"[journey] frame %llu expanded to %ux%u at population %llu\n",
            (unsigned long long)s_frames,ScWorldWidth(&s_world),ScWorldHeight(&s_world),
            (unsigned long long)s_population.value);
        }
      }
      if(cpu->k==1 && cpu->pc==0x897f && cpu->dp==0 && cpu->db==0 &&
          host_map_screen_live() && !ram_w(0xd7) && !ram_w(0x379) &&
          !ram_w(0x391) && !ram_w(0x395) && !ram_w(0x397) &&
          s_world.journey_notice && !s_world.journey_announcing) {
        /* Message 4 supplies the native happy Wright animation and fanfare,
         * without a gift or scenario side effect. Its text is visit-scoped. */
        s_world.journey_announcing=true;ram_set_w(0x397,4);ram_set_w(0x395,1);
      }
      if(cpu->k==1 && cpu->pc==0xe59b && s_world.journey_announcing && ram_w(0x397)==4)
        cpu->a=0xfd00; /* virtual text record through the native 24-column writer */
      if(cpu->k==1 && cpu->pc==0xa63c && s_world.journey_announcing && ram_w(0x397)==4) {
        s_world.journey_notice=0;s_world.journey_announcing=false;
      }
    }
    if (s_rom_is_us && cpu->k==3 && cpu->pc==0x80eb) {
      ScPowerRefreshObserve(&s_power_refresh,s_frames,g_ram[0x193]);
      if (power_diag) fprintf(stderr,"[power native] frame %llu game speed %u tick %u\n",
          (unsigned long long)s_frames,g_ram[0x193],ram_w(0xb51));
    }
    if (s_rom_is_us && cpu->k==3 && cpu->pc==0xafb0) s_native_power_active=true;
    if (s_rom_is_us && cpu->k==3 && cpu->pc==0xb1a4) {
      s_native_power_active=false;
      if (s_development_speed>1) refresh_fast_power(true);
    }
    if (s_build_pending && !s_build_work && s_rom_is_us && cpu->k == 1 && cpu->pc == 0x897f &&
        cpu->dp == 0 && cpu->db == 0 && host_map_screen_live())
      commit_mouse_construction();
    if(s_build_work)break;
    if (s_clip_pending && s_rom_is_us && cpu->k==1 && cpu->pc==0x897f &&
        cpu->dp==0 && cpu->db==0 && host_map_screen_live()) commit_clipboard();
    if (s_population.live && s_rom_is_us && cpu->k==1 && cpu->pc==0x897f &&
        cpu->dp==0 && cpu->db==0 && host_map_screen_live() && !ram_w(0xd7))
      ScPopulationMirror(&s_population,g_ram);
    if (s_development_speed>1 && s_rom_is_us && cpu->k==1 && cpu->pc==0x897f &&
        cpu->dp==0 && cpu->db==0 && host_map_screen_live() && !ram_w(0xd7))
      refresh_fast_power(true);
    if(s_rom_is_us && cpu->k==3) {
      if(cpu->pc==0xd3b7) {s_map_number_high=0;s_map_number_dirty=true;}
      if(cpu->pc==0xd81c && s_map_number_dirty)cpu->pc=0xd834;
      if(cpu->pc==0xd625) {
        unsigned choice=sc_mapgen_number_nav(ram_w(0xb2d),g_ram[0xca]);
        ram_set_w(0xb2d,choice);g_ram[6]=7;
        cpu->pc=choice==1 && ram_w(0xb31)?0xd695:0xd65d;
      }
      if(cpu->pc==0xd3e0 && (g_ram[0xca]&128) && ram_w(0xb2d)>=2) {
        unsigned choice=ram_w(0xb2d),number=g_ram[0xb27]+g_ram[0xb28]*10+g_ram[0xb29]*100+s_map_number_high*1000;
        number=sc_mapgen_number_digit(number,(choice-2)/2,choice&1?-1:1);
        s_map_number_high=number/1000;g_ram[0xb27]=number%10;g_ram[0xb28]=number/10%10;g_ram[0xb29]=number/100%10;
        ram_set_w(0xb31,128);s_map_number_dirty=true;g_ram[6]=8;cpu->pc=0xd459;
      }
      if(cpu->pc==0xd4f7) {
        unsigned number=(g_ram[0xb27]+g_ram[0xb28]*10+g_ram[0xb29]*100+s_map_number_high*1000+1)%100000;
        s_map_number_high=number/1000;g_ram[0xb27]=number%10;g_ram[0xb28]=number/10%10;g_ram[0xb29]=number/100%10;
        s_map_number_dirty=true;cpu->pc=0xd51c;
      }
      if(cpu->pc==0xd7dd && ram_w(0xb2d)>=2) {
        unsigned choice=ram_w(0xb2d);
      ram_set_w(0x2000,SC_MAP_NUMBER_X+36-(choice-2)/2*8+((178+(choice&1)*8)<<8));
        ram_set_w(0x2002,0x3f9e);g_ram[0x2200]=2;cpu->pc=0xd7fc;
      }
    }
    if (s_rom_is_us && cpu->k == 3 && cpu->pc == 0xd3e0 &&
        (s_map_mouse_refresh_pending || s_map_mouse_accept_pending)) {
      if (g_ram[0x0b31]) {
        /* Keyboard navigation normally enters 03:d695 on reaching OK:
         * finish a changed map number before accepting this preview. */
        cpu->pc = 0xd695; cpu->mf = true; cpu->xf = true;
        s_map_mouse_refresh_pending = false;
      } else if (s_map_mouse_accept_pending && g_ram[0x0b2d]==1) {
        g_ram[0xca] |= 0x80;
        s_map_mouse_accept_pending = false;
        s_map_mouse_refresh_pending = false;
      }
    }
    if (scen_diag && cpu->k == 0x03 &&
        (cpu->pc == 0xc518 || cpu->pc == 0xc5a2 || cpu->pc == 0xe30a ||
         cpu->pc == 0xcf31 || cpu->pc == 0xce2e || cpu->pc == 0xce5e ||
         cpu->pc == 0xc548)) {
      fprintf(stderr, "[scen] pc=%04x $3e=%u $40=%u year=%u $0ccb=%u"
                      " $0deb=%u $0d87=%u A=%04x name=%c%c%c%c%c%c%c%c/%u\n",
              cpu->pc, g_ram[0x3e], g_ram[0x40] | (g_ram[0x41] << 8),
              g_ram[0x0b53] | (g_ram[0x0b54] << 8), g_ram[0x0ccb],
              g_ram[0x0deb] | (g_ram[0x0dec] << 8),
              g_ram[0x0d87] | (g_ram[0x0d88] << 8), (unsigned)cpu->a,
              nmc(0),
              nmc(1),
              nmc(2),
              nmc(3),
              nmc(4),
              nmc(5),
              nmc(6),
              nmc(7),
              g_ram[0x0b5b]);
    }
    if (cpu->k == 0x03 && cpu->pc == 0xddb6) s_selector_frame = s_frames;
    /* 0b:fbe7 is the free-play welcome block the ROM hands index 8. Keyed
     * on the source, so if it ever selects a different one this simply
     * does not fire rather than corrupting whatever did load. */
    if (s_ninth_scenario && s_rom_is_us && cpu->k == 0x03 && cpu->pc == 0xce5e &&
        s_sylt_map_armed && s_sylt_map) {
      /* 03:ce2e has decompressed the scenario's map to $7E8000 and is about
       * to unpack it. Swap in Sylt's, then disarm so the next scenario --
       * practice included -- loads its own. */
      s_sylt_map_armed = false;
      s_sylt_city = true;
      memcpy(&g_ram[0x8000], s_sylt_map, (size_t)s_sylt_map_len);
      fprintf(stderr, "[sylt] map swapped in at $7E8000 (%ld bytes)",
              s_sylt_map_len);
      fputc('\n', stderr);
    }
    /* Packet patches run on every unpack, independent of the briefing and
     * Sylt paths below: the selector's packets are not briefings, and they
     * must still be patched when no briefing blob is loaded. */
    if (cpu->k == 0x00) {
      if (cpu->pc == 0x90eb) {
        s_scpk_src = ((uint32_t)cpu->db << 16) | g_ram[0x09] |
                     ((uint32_t)g_ram[0x0a] << 8);
        s_scpk_out = 0x8000u + (g_ram[0x0e] | ((uint32_t)g_ram[0x0f] << 8));
      } else if (cpu->pc == 0x9106) {
        scpk_apply(s_scpk_src, s_scpk_out);
      }
    }
    if (s_ninth_scenario && s_rom_is_us && cpu->k == 0x00) {
      if (cpu->pc == 0x90eb)
        s_sylt_decomp_src = ((uint32_t)cpu->db << 16) | g_ram[0x09] |
                            ((uint32_t)g_ram[0x0a] << 8);
      else if (cpu->pc == 0x9106 && s_sylt_decomp_src == 0x0bfbe7u &&
               s_sylt_map_armed)
        /* Armed, NOT just "$0040 == 8". Index 8 is the practice map, so the
         * tutorial decompresses the very same welcome block with $0040 at 8
         * and was getting Sylt's briefing -- reported from play. The arm is
         * read here and consumed later by the map swap at 03:ce5e, which runs
         * after this. */
        sylt_write_brief_tilemap();
    }
    /* Translated briefings, same exit hook, generalised to the twelve
     * packets. Placed AFTER Sylt so its own briefing still wins on the
     * packet the two share (0B:FBE7, the free-play welcome block Sylt
     * borrows): a translation of the English text there would otherwise
     * overwrite the ninth scenario's own words. */
    if ((s_brief_count || s_scen_tiles_len || s_glyph_count || s_bp_count ||
         s_sg_count ||
         brief_diag) &&
        cpu->k == 0x00) {
      if (cpu->pc == 0x90eb) {
        s_brief_decomp_src = ((uint32_t)cpu->db << 16) | g_ram[0x09] |
                             ((uint32_t)g_ram[0x0a] << 8);
        s_brief_out = 0x8000u + (g_ram[0x0e] | ((uint32_t)g_ram[0x0f] << 8));
      } else if (cpu->pc == 0x9106) {
        /* SC_BRIEF_DIAG=1: every briefing-range unpack, with the screen
         * it happened on. Pairing the twelve packets across regions cannot
         * be done from the ROM -- there is no pointer table, and matching
         * layouts is defeated by translations being longer. Watching which
         * source the game asks for on which screen is the way to pair
         * them, and it needs no reading of the text at all. */
        if (brief_diag) {
          /* Keyed on source AND screen: one source serving two screens is
           * exactly the case that would make a briefing substitution appear
           * on a page it does not belong to. Sylt already shares 0B:FBE7
           * with the free-play welcome, so reuse is known to happen. */
          static uint32_t seen[64]; static uint8_t seens[64]; static int nseen;
          int dup = 0;
          for (int i = 0; i < nseen; i++)
            if (seen[i] == s_brief_decomp_src && seens[i] == g_ram[0x14]) dup = 1;
          if (!dup && nseen < 64) {
            seens[nseen] = g_ram[0x14];
            seen[nseen++] = s_brief_decomp_src;
            fprintf(stderr, "[brief] src=%06X out=$%05X screen=$%02x%s\n",
                    (unsigned)s_brief_decomp_src,
                    (unsigned)s_brief_out, g_ram[0x14],
                    g_ram[0x14] == 0x0b ? "  <-- SCENARIO SELECTOR" : "");
          }
        }
        const bool sylt_owns = s_ninth_scenario && s_sylt_map_armed &&
                               s_brief_decomp_src == 0x0bfbe7u;
        if (!sylt_owns) {
          if (s_bp_count &&
              (size_t)s_brief_out + SC_BRIEF_COLS * SC_BRIEF_ROWS * 2u
                  <= sizeof g_ram &&
              brief_compose_page(s_brief_decomp_src, &g_ram[s_brief_out])) {
            static int said; if (!said++)
              fprintf(stderr, "translation: briefing %06X composed from strings\n",
                      (unsigned)s_brief_decomp_src);
          }
          if (s_glyph_count && s_brief_decomp_src == 0x09C0FBu) {
            for (int i = 0; i < s_glyph_count; i++) {
              size_t at = (size_t)s_brief_out
                        + (size_t)s_glyph_idx[i] * kFontTile;
              if (at + kFontTile <= sizeof g_ram)
                memcpy(&g_ram[at], s_glyph_px[i], kFontTile);
            }
            { static int said; if (!said++)
                fprintf(stderr, "translation: %d accent glyphs written into "
                                "the font\n", s_glyph_count); }
          }
          /* Order matters: the wholesale tileset copy lands FIRST, then the
           * accented glyphs on top of it. The other way round the copy
           * simply erased them. */
          if (s_scen_tiles_len && s_brief_decomp_src == 0x09875Cu &&
              (size_t)s_brief_out + s_scen_tiles_len <= sizeof g_ram) {
            memcpy(&g_ram[s_brief_out], s_scen_tiles, s_scen_tiles_len);
            { static int said; if (!said++)
                fprintf(stderr, "translation: scenario tiles substituted\n"); }
          }
          if (s_sg_count && s_brief_decomp_src == 0x09875Cu) {
            for (int i = 0; i < s_sg_count; i++) {
              size_t at = (size_t)s_brief_out + (size_t)s_sg_idx[i] * 16u;
              if (at + 16u <= sizeof g_ram)
                memcpy(&g_ram[at], s_sg_px[i], 16);
            }
            { static int said; if (!said++)
                fprintf(stderr, "translation: %d briefing glyphs into the "
                                "scenario tileset\n", s_sg_count); }
          }
          for (int i = 0; i < s_brief_count; i++) {
            if (s_brief_src[i] != s_brief_decomp_src) continue;
            if ((size_t)s_brief_out + s_brief_len[i] <= sizeof g_ram)
              memcpy(&g_ram[s_brief_out], s_brief_data[i], s_brief_len[i]);
            { static int said; if (!said++)
                fprintf(stderr, "translation: briefing %06X substituted\n",
                        (unsigned)s_brief_decomp_src); }
            break;
          }
        }
      }
    }
    if (s_replay_menu) replay_menu_hook(cpu->k, cpu->pc);
    if (s_power_fix && cpu->k == 0x03 &&
        (cpu->pc == 0xc8dd || cpu->pc == 0xce61)) apply_power_fix();
    /* LC_LZ5 decompressor instrumentation -- see the SC_DECOMP_TRACE comment
     * above bus_read for the decoded calling convention and why the samples
     * are taken at 00:90eb / 00:9106 rather than at the JSR and the RTS. */
    if (s_decomp_trace && cpu->k == 0x00) {
      if (cpu->pc == 0x90eb) {
        s_decomp_active = true;
        s_decomp_src = ((uint32_t)cpu->db << 16) |
                       g_ram[0x09] | ((uint32_t)g_ram[0x0a] << 8);
        s_decomp_out_base = 0x7e8000u + (g_ram[0x0e] | ((uint32_t)g_ram[0x0f] << 8));
        s_decomp_wmin = 0xffffffffu;
        s_decomp_wmax = 0;
        s_decomp_wcount = 0;
      } else if (s_decomp_active && cpu->pc == 0x9106) {
        uint32_t src_end = ((uint32_t)cpu->db << 16) |
                           g_ram[0x09] | ((uint32_t)g_ram[0x0a] << 8);
        /* Compressed size has to be measured in LoROM *file* offsets, not by
         * subtracting the 24-bit addresses: 00:926d advances the pointer by
         * one bank per 32KB window ($8000..$ffff), so plain address
         * subtraction over-counts a bank crossing by $8000 and reports a
         * source longer than its own output. */
        uint32_t src_file = ((s_decomp_src >> 16) * 0x8000u) +
                            ((s_decomp_src & 0xffff) - 0x8000u);
        uint32_t end_file = ((src_end >> 16) * 0x8000u) +
                            ((src_end & 0xffff) - 0x8000u);
        fprintf(stderr,
                "[decomp #%u f=%llu] src=%02x:%04x..%02x:%04x (file %06x, %u in) "
                "dst=%06x wrote=%u range=%06x..%06x\n",
                s_decomp_calls++, (unsigned long long)s_frames,
                (unsigned)(s_decomp_src >> 16), (unsigned)(s_decomp_src & 0xffff),
                (unsigned)(src_end >> 16), (unsigned)(src_end & 0xffff),
                src_file, (unsigned)(end_file - src_file), s_decomp_out_base,
                s_decomp_wcount,
                s_decomp_wcount ? s_decomp_wmin : 0,
                s_decomp_wcount ? s_decomp_wmax : 0);
        s_decomp_active = false;
      }
    }
    if (s_addr_trace_count && s_frames >= s_addr_trace_start_frame && s_addr_trace_armed) {
      uint32_t pc = ((uint32_t)cpu->k << 16) | cpu->pc;
      for (int i = 0; i < s_addr_trace_count; i++) {
        if (s_addr_trace_pcs[i] == pc && s_addr_trace_hits[i] < 200) {
          fprintf(stderr, "[addrtrace f=%llu] pc=%02x:%04x a=%04x x=%04x y=%04x "
                  "s=%04x d=%04x db=%02x m%s x%s p=%02x nmi=%u irq=%u e=%u wait=%u stop=%u world=%u hit#%u\n",
                  (unsigned long long)s_frames, cpu->k, cpu->pc, cpu->a, cpu->x,
                  cpu->y, cpu->sp, cpu->dp, cpu->db, cpu->mf ? "8" : "16",
                  cpu->xf ? "8" : "16", interp816_getFlags(cpu), (unsigned)cpu->nmiWanted,
                  (unsigned)cpu->irqWanted, (unsigned)cpu->e, (unsigned)cpu->waiting,
                  (unsigned)cpu->stopped, (unsigned)s_world.active, s_addr_trace_hits[i]);
          if (s_addr_trace_hits[i] < 40) {
            fprintf(stderr, "  pc history (oldest..newest, this pc last):\n");
            int n = s_pc_history_filled;
            for (int h = n - 1; h >= 0; h--) {
              int pos = ((s_pc_history_head - 1 - h) % SC_PC_HISTORY_SIZE + SC_PC_HISTORY_SIZE) % SC_PC_HISTORY_SIZE;
              uint32_t hpc = s_pc_history[pos];
              fprintf(stderr, "    %02x:%04x\n", (unsigned)(hpc >> 16) & 0xff, (unsigned)(hpc & 0xffff));
            }
          }
          s_addr_trace_hits[i]++;
        }
      }
    }
    /* Gated on s_addr_trace_start_frame too, not just s_addr_trace_count --
     * this ran on every single opcode (not just every frame) from frame 0
     * the moment SC_ADDR_TRACE was set at all, regardless of an @start
     * suffix, which made interactive play visibly slower for however long
     * it took to reach the frame actually being investigated. Now a
     * delayed start via SC_ADDR_TRACE=...@N keeps the game at full,
     * untraced speed until frame N. */
    if (s_addr_trace_count && s_frames >= s_addr_trace_start_frame) {
      s_pc_history[s_pc_history_head] = ((uint32_t)cpu->k << 16) | cpu->pc;
      s_pc_history_head = (s_pc_history_head + 1) % SC_PC_HISTORY_SIZE;
      if (s_pc_history_filled < SC_PC_HISTORY_SIZE) s_pc_history_filled++;
    }
    if (s_rom_fnv == SC_ROM_FNV_US && cpu->k == 2) {
      if (cpu->pc == 0xad69) ScPopulationReport(&s_population, g_ram, false);
      if (cpu->pc == 0xada9) ScPopulationReport(&s_population, g_ram, true);
    }
    if (s_rom_fnv == SC_ROM_FNV_US) {
      if(cpu->k==3 && cpu->pc==0xc8a1) {
        s_loading_slot=ram_w(0x421)==1?0:1;s_city_loading=true;
      }
      if(s_city_loading && cpu->k==3 && (cpu->pc==0xc8b3 || cpu->pc==0xc8e6))
        ram_set_w(0x421,s_loading_slot+1);
      if (cpu->k==3 && (cpu->pc==0xce2e || cpu->pc==0xc8c8)) {
        ScRendererResetCamera(&s_custom_renderer);
        ScRendererBeginMapLoad(&s_custom_renderer);
        reset_refresh_clocks();
        ScWorldReset(&s_world);
      }
      if (cpu->k==3 && cpu->pc==0xcf89) ScWorldMirror(&s_world,g_ram);
      bool power_writeback=cpu->k==3 && cpu->pc==0xb152 && s_world.active;
      ScWorldGuestStepPrepared(&s_world, cpu, g_ram);
      if(power_writeback && cpu->pc==0xb1a4) {
        s_native_power_active=false;
        if(s_development_speed>1) refresh_fast_power(true);
        else if(s_world.giant) {
          /* The guest's word-sized branch stack cannot cover these maps.
           * Publish the full-width C network on the same Normal power tick. */
          ScPowerRefreshRestore(&s_power_refresh,g_ram,&s_world,g_snes->cart->rom,
              g_snes->cart->romSize,s_frames);
        }
      }
      ScWorldGuestVehiclesPrepared(&s_world, cpu, g_ram, g_snes->multiplyA);
    }
    if (s_rom_fnv == SC_ROM_FNV_US && cpu->k == 3) {
      if(cpu->pc==0xcbe2 && s_test_saving)test_city_finish_save();
      else if(cpu->pc==0xcbe2 && !s_world.test_city) {population_saved_city(true);world_saved_city(true);}
      if(cpu->pc==0xc8ce) {
        if(s_test_load_pending) {
          uint32_t n;const uint8_t *record=ScSram_Extra(&n);
          bool ok=ScTestCityDecode(record,n,&s_world,&s_population);
          test_city_restore_sram();s_test_load_pending=false;
          if(ok) {
            ScPopulationMirror(&s_population,g_ram);ScWorldMirror(&s_world,g_ram);
            ram_set_w(0x1c5,ScWorldWidth(&s_world)-(ram_w(0x1d7)?25:30));
            ram_set_w(0x1c9,ScWorldHeight(&s_world)-(ram_w(0x1d7)?22:26));
            fprintf(stderr,"[test city] loaded saved City 3, population %llu\n",(unsigned long long)s_population.value);
          }
        } else {population_saved_city(false);world_saved_city(false);}
      }
      if (cpu->pc == 0xc8dd) s_city_loading=false;
      if(cpu->pc==0xc633 && s_test_generate_pending) {
        ScTestCityStats stats;
        bool ok=ScTestCityGenerate(&s_world,&s_population,g_ram,g_snes->cart->rom,g_snes->cart->romSize,&stats);
        s_test_generate_pending=false;s_practice_size_pending=false;s_journey_arming=false;
        reset_refresh_clocks();ScDevelopmentReset(&s_development);memset(&s_world_guest,0,sizeof s_world_guest);
        ScRendererResetHistory(&s_custom_renderer);
        if(!ok) {s_city_present_pending=false;fprintf(stderr,"[test city] generation failed\n");}
      }
      if(cpu->pc==0xc63c && s_world.test_city)ram_set_w(0x38,0);
      if(cpu->pc==0xc633 && s_practice_size_pending) {
        ScMapGenPrng pr={ram_w(0x59),ram_w(0x5b),ram_w(0x5d)};
        if(s_large_maps)ScWorldGenerateStyled(&s_world,s_large_maps,&pr,s_terrain_style);
        if(s_world.active) {
          ScWorldMirror(&s_world,g_ram);
          ram_set_w(0x1c5,ScWorldWidth(&s_world)-25);ram_set_w(0x1c9,ScWorldHeight(&s_world)-22);
        }
        s_world.development_speed=(uint8_t)s_new_city_speed;
        s_practice_size_pending=false;
      }
      if (cpu->pc == 0xce61) ScPopulationImport(&s_population, g_ram);
      if (cpu->pc == 0xc73c) {
        s_world.development_speed=(uint8_t)s_new_city_speed;
        ScPopulationImport(&s_population, g_ram);
        if(s_journey_arming) {s_world.journey=true;s_journey_arming=false;}
      }
      /* A native census must not replace the current fast count with its
       * pre-development sweep. Recount once at this safe calculation entry. */
      if (cpu->pc==0x81a3) {
        ScRefreshClockObserve(&s_population_clock,s_frames);
        if (s_population.live)
          refresh_live_population(g_snes->cart->rom,g_snes->cart->romSize);
      }
      uint16_t population_pc = ScPopulationStep(&s_population, g_ram, cpu->pc, cpu->dp);
      if(cpu->pc==0x81a3 && s_population.valid) ScJourneyObservePopulation(&s_world,s_population.value);
      if (population_pc != cpu->pc) {
        cpu->y = (uint16_t)ScPopulationClass(s_population.value);
        cpu->a = (uint16_t)((g_ram[0xba7] | (g_ram[0xba8]<<8)) - 7);
        cpu->c = s_population.value >= 500000;
        cpu->mf = false; cpu->xf = false;
      }
      cpu->pc = population_pc;
      uint16_t next_pc = prepare_development_pc(cpu);
      if (next_pc != cpu->pc) { cpu->mf = false; cpu->xf = false; }
      cpu->pc = next_pc;
    }
    if(native_simulation && !atomic_reference && s_rom_fnv==SC_ROM_FNV_US &&
       s_development.repeating && sc_run_development_lane(&guard))continue;
    if(native_simulation && !atomic_reference && s_rom_fnv==SC_ROM_FNV_US &&
       sc_run_city_lane(target,&guard))continue;
    sc_note_executed_pc(((uint32_t)cpu->k << 16) | cpu->pc,
                        cpu->mf ? 1 : 0, cpu->xf ? 1 : 0);
    if (s_pc_capture_after > 0) {
      fprintf(stderr, "[pctrace] pc=%02x:%04x a=%04x x=%04x y=%04x p=%02x%s%s\n",
              cpu->k, cpu->pc, cpu->a, cpu->x, cpu->y,
              interp816_getFlags(cpu), cpu->mf ? " m8" : " m16", cpu->xf ? " x8" : " x16");
      s_pc_capture_after--;
    }
    if (s_dump_pc_armed &&
        ((uint32_t)cpu->k << 16 | cpu->pc) == s_dump_pc24) {
      char path[512];
      snprintf(path, sizeof(path), "%s/wram_%010llu.bin", s_dump_pc_dir,
               (unsigned long long)s_dump_pc_frame);
      if (!write_wram_dump(path))
        fprintf(stderr, "failed to write WRAM dump to %s\n", path);
      s_dump_pc_armed = false;
    }
    /* Repeat only the native city cursor/scroll routine, including its
     * terrain staging and moving-object shifts. Extra passes consume no
     * guest clock, so Ctrl does not accelerate the calendar or simulation. */
    if(s_rom_is_us && cpu->k==1 &&
        (s_scroll_multiplier>1 || s_scroll_pass[0].repeating || s_scroll_pass[1].repeating) &&
        !cpu->nmiWanted && !(cpu->irqWanted && !cpu->i)) {
      for(unsigned pass=0;pass<2;++pass) {
        unsigned start=pass?0x89a0:0x8d26;
        bool end=pass?cpu->pc==0x89a3:
            cpu->pc==0x8d2d || cpu->pc==0x8d92 || cpu->pc==0x8dcd;
        if(cpu->pc==start && !s_scroll_pass[pass].repeating) {
          s_scroll_pass[pass].extra=(unsigned)(s_scroll_multiplier-1);
          s_scroll_pass[pass].sp=cpu->sp;
        } else if(end && cpu->sp==s_scroll_pass[pass].sp) {
          if(s_scroll_pass[pass].extra) {
            --s_scroll_pass[pass].extra;s_scroll_pass[pass].repeating=true;cpu->pc=(uint16_t)start;
          } else s_scroll_pass[pass].repeating=false;
        }
      }
    }
    const bool scroll_work=cpu->k==1 && (s_scroll_pass[0].repeating || s_scroll_pass[1].repeating) &&
      !cpu->nmiWanted && !(cpu->irqWanted && !cpu->i);
    const bool development_work = cpu->k == 3 && s_development.repeating &&
      !cpu->nmiWanted && !(cpu->irqWanted && !cpu->i);
    const uint32_t executed_pc=((uint32_t)cpu->k<<16)|cpu->pc;
    const bool interrupt_work=cpu->nmiWanted || (cpu->irqWanted && !cpu->i);
    /* The clean US driver's music command write (not its upload/SFX ports).
     * Keep its following acknowledgement store consistent with the SPC-zero
     * substitution. The ROM bytes and all other port writes remain original. */
    if(s_rom_is_us && !interrupt_work && cpu->k==0 && cpu->pc==0x9882 && cpu->mf && cpu->dp==0)
      cpu->a=(cpu->a&0xff00)|ScMusicCommand((uint8_t)cpu->a);
    if(s_rom_is_us && !interrupt_work && cpu->k==1 && cpu->pc==0xa9c4 && !cpu->mf)
      ScMusicEnabled((cpu->a&8)!=0);
    const bool defer_native_binding=!s_native_bind_eager && native_simulation &&
        s_rom_fnv==SC_ROM_FNV_US && cpu->k==3 && !interrupt_work;
    if(defer_native_binding) {s_world_guest.mapped=false;if(s_bank_profile)++s_native_bind_deferred;}
    else ScWorldGuestBeginPrepared(&s_world_guest,&s_world,cpu,g_snes->cart->rom,g_snes->cart->romSize);
    unsigned fast_cycles=0,fast_budget=0;
    static int extra_beam_reference=-1;
    if(extra_beam_reference<0) {const char *e=getenv("SC_EXTRA_BEAM_REFERENCE");extra_beam_reference=e && *e=='1';}
    const bool extra_native=development_work && native_simulation && !extra_beam_reference;
    if(native_simulation && s_rom_fnv==SC_ROM_FNV_US && !interrupt_work &&
       !cpu->k && ScSpriteOwns(cpu->pc)) {
      static int sprite_diag=-1;static unsigned sprite_reports;
      if(sprite_diag<0) {const char *e=getenv("SC_SPRITE_DIAG");sprite_diag=e && *e=='1';}
      if(sprite_diag && sprite_reports<30 && s_frames>=620 && cpu->pc==0x9080) {
        fprintf(stderr,"[sprite C] frame=%llu db=%x sp=%x pc=%x x=%x y=%x pointer=%x count=%x mf=%u xf=%u e=%u d=%u\n",
            (unsigned long long)s_frames,cpu->db,cpu->sp,cpu->pc,cpu->x,cpu->y,
            ram_w(cpu->sp+3),ram_w(0x287),cpu->mf,cpu->xf,cpu->e,cpu->d);++sprite_reports;
      }
      unsigned boundary=snes->hPos<1024?1024:1364;
      if(snes->hIrqEnabled && (!snes->vIrqEnabled || snes->vPos==snes->vTimer+1)) {
        unsigned irq=4*snes->hTimer;
        if(irq>=snes->hPos && irq<boundary) boundary=irq;
      }
      if(snes->hPos && boundary>snes->hPos+2)
        fast_cycles=ScSpriteStep(cpu,g_ram,(boundary-snes->hPos-2)/8);
      /* Exactly the original one-instruction overrun at the deadline. The
       * C edge only touches cartridge/WRAM, so its publication and clock
       * order are the same as the fallback opcode it replaces. */
      if(!fast_cycles) fast_cycles=ScSpriteInstructionStep(cpu,g_ram);
    }
    if(native_simulation && s_rom_fnv==SC_ROM_FNV_US && !interrupt_work &&
       cpu->k==1 && ScTileLookupOwns(cpu->pc)) {
      /* Tile lookup publishes scratch and hardware-multiply state at its
       * original boundaries, before the next scanout/HDMA/IRQ event. */
      unsigned boundary=snes->hPos<1024?1024:1364;
      if(snes->hIrqEnabled && (!snes->vIrqEnabled || snes->vPos==snes->vTimer+1)) {
        unsigned irq=4*snes->hTimer;
        if(irq>=snes->hPos && irq<boundary) boundary=irq;
      }
      if(snes->hPos && boundary>snes->hPos+2)
        fast_cycles=ScTileLookupStep(&s_world,cpu,g_ram,g_snes->cart->rom,
            g_snes->cart->romSize,(boundary-snes->hPos-2)/8);
      if(fast_cycles) s_world_guest.mapped=false;
    }
    if(native_simulation && development_work && s_rom_fnv==SC_ROM_FNV_US && !interrupt_work) {
      static bool native_layout_reported;
      if(!native_layout_reported && native_diag) {
        fprintf(stderr,"[native C] extra attempt pc=%x dp=%x sp=%x\n",cpu->pc,cpu->dp,cpu->sp);native_layout_reported=true;
      }
      if(cpu->pc==0x9a3e) fast_cycles=ScWorldGuestHousingStep(&s_world,cpu,g_ram,512);
      if(cpu->pc==0x987f) fast_cycles=ScWorldGuestHouseSiteStep(&s_world,cpu,g_ram,
          g_snes->cart->rom,g_snes->cart->romSize,512);
      if(!fast_cycles) fast_cycles=ScDevelopmentNativeBatch(&s_development,&s_world,cpu,g_ram,
          g_snes->cart->rom,g_snes->cart->romSize);
      if(fast_cycles && s_perf_detail) ++s_perf_native_development_calls;
    }
    if(!fast_cycles && native_simulation && s_rom_fnv==SC_ROM_FNV_US && !interrupt_work &&
       !cpu->k && (cpu->pc==0x9311 || cpu->pc==0x9313 || cpu->pc==0x9315)) {
      /* Frame-wait counters are observable by the original RNG. Advance the
       * exact original loop clocks and bytes; never skip to the next frame. */
      unsigned boundary=snes->hPos<1024?1024:1364;
      if(snes->hIrqEnabled && (!snes->vIrqEnabled || snes->vPos==snes->vTimer+1)) {
        unsigned irq=4*snes->hTimer;
        if(irq>=snes->hPos && irq<boundary) boundary=irq;
      }
      if(snes->hPos && boundary>snes->hPos+2)
        fast_cycles=ScWaitStep(cpu,g_ram,(boundary-snes->hPos-2)/8);
    }
    static int math_reference=-1;
    if(math_reference<0) {const char *e=getenv("SC_MATH_REFERENCE");math_reference=e && *e=='1';}
    if(!fast_cycles && native_simulation && !math_reference &&
       s_rom_fnv==SC_ROM_FNV_US && !interrupt_work && cpu->k==3 &&
       ((cpu->pc>=0xa32e && cpu->pc<=0xa342) || cpu->pc==0xa395 || ScMathDivideOwns(cpu->pc) || cpu->pc==0xa462 ||
        ScMathRngOwns(cpu->pc))) {
      /* Multiply/divide keeps the original clock on every map size. RNG is
       * inside the existing map-scaled spatial clock range ($88f3-$90a6),
       * so its deadline budget must include that same area and remainder. */
      unsigned boundary=snes->hPos<1024?1024:1364;
      if(snes->hIrqEnabled && (!snes->vIrqEnabled || snes->vPos==snes->vTimer+1)) {
        unsigned irq=4*snes->hTimer;
        if(irq>=snes->hPos && irq<boundary) boundary=irq;
      }
      if(extra_native) fast_cycles=ScMathBatchStep(cpu,g_ram,4096);
      else if(snes->hPos && boundary>snes->hPos+2) {
        unsigned budget=boundary-snes->hPos-2;
        bool rng_scaled=s_world.active && cpu->pc>=0x9035 && cpu->pc<=0x90a6;
        if(rng_scaled) budget*=ScWorldCells(&s_world)/12000;
        unsigned remainder=rng_scaled?s_map_cycle_remainder:0;
        if(budget>remainder) fast_cycles=ScMathBatchStep(cpu,g_ram,(budget-remainder)/8);
      }
    }
    static int kernel_enabled=-1;
    if(kernel_enabled<0) {const char *e=getenv("SC_SPATIAL_KERNELS");kernel_enabled=!e || *e!='0';}
    if(!fast_cycles && kernel_enabled && s_rom_fnv==SC_ROM_FNV_US &&
       (s_world.active || (cpu->k==3 && (ScPostpassOwns(cpu->pc) || ScSweepOwns(cpu->pc)))) && !interrupt_work) {
      /* Stop before scanout, HDMA, line wrap, or the programmable IRQ. No
       * batched cell can delay an interrupt or render a row after its time. */
      unsigned boundary=snes->hPos<1024?1024:1364;
      if(snes->hIrqEnabled && (!snes->vIrqEnabled || snes->vPos==snes->vTimer+1)) {
        unsigned irq=4*snes->hTimer;
        if(irq>=snes->hPos && irq<boundary) boundary=irq;
      }
      if(extra_native) {
        /* Extra attempts leave beam/calendar/audio clocks stationary. Their
         * C work can span complete helpers even at a scanline boundary;
         * the development scheduler still owns the final PLD/RTS. */
        fast_budget=4096;
        fast_cycles=ScWorldGuestBatchStep(&s_world,cpu,g_ram,g_snes->cart->rom,
            g_snes->cart->romSize,fast_budget);
        if(fast_cycles) s_world_guest.mapped=false;
      } else if(snes->hPos && boundary>snes->hPos+2) {
        unsigned scale=ScWorldGuestClockScale(&s_world,executed_pc);
        unsigned budget=(boundary-snes->hPos-2)*scale;
        unsigned remainder=scale>1?s_map_cycle_remainder:0;
        if(budget>remainder) {
          fast_budget=(budget-remainder)/8;
          fast_cycles=ScWorldGuestBatchStep(&s_world,cpu,g_ram,g_snes->cart->rom,
              g_snes->cart->romSize,fast_budget);
        }
        if(fast_cycles) s_world_guest.mapped=false;
      }
    }
    if(!fast_cycles && s_rom_fnv==SC_ROM_FNV_US &&
       (cpu->pc!=0x9dca || (cpu->a&1023)<0x28 || fast_budget>=192 || development_work))
      fast_cycles=ScWorldGuestFastStep(&s_world,cpu,g_ram,g_snes->cart->rom,g_snes->cart->romSize);
    if(!fast_cycles && native_simulation && !atomic_reference &&
       s_rom_fnv==SC_ROM_FNV_US && !interrupt_work) {
      fast_cycles=ScWaitInstructionStep(cpu,g_ram);
      if(!fast_cycles && !math_reference) fast_cycles=ScMathInstructionStep(cpu,g_ram);
      if(!fast_cycles) fast_cycles=ScWorldGuestInstructionStep(&s_world,cpu,g_ram,
          g_snes->cart->rom,g_snes->cart->romSize);
      if(fast_cycles) s_world_guest.mapped=false;
    }
    if(fast_cycles) ++s_perf_spatial_cells;
    if(!fast_cycles && defer_native_binding) {
      ScWorldGuestBeginPrepared(&s_world_guest,&s_world,cpu,g_snes->cart->rom,g_snes->cart->romSize);
      if(s_bank_profile)++s_native_bind_fallback;
    }
    if(!fast_cycles && native_simulation && !interrupt_work)
      fast_cycles=ScProgramStep(cpu);
    if(!fast_cycles && native_simulation)
      fast_cycles=ScProgramExecute(cpu);
    if(s_bank_profile && !fast_cycles) {
      ++s_interpreter_bank_ops[cpu->k];
      if(cpu->k==s_bank_page_sel) {
        ++s_interpreter_pages[cpu->pc>>8];
        if((cpu->pc>>8)==s_pc_profile_page) ++s_interpreter_pc[cpu->pc&255];
      }
    }
    int cyc = fast_cycles?fast_cycles:sc_interpreter_fallback(cpu);
    /* Extra development attempts are host work, like the native map
     * generator. They do not advance the SNES beam, audio clock or calendar
     * cadence. The original attempt and final PLD/RTS retain normal timing. */
    if (development_work || scroll_work) continue;
    if (cyc <= 0) cyc = 1;
    int master = cyc * 8;
    if (s_rom_fnv==SC_ROM_FNV_US && !interrupt_work)
      master=ScWorldGuestMasterCycles(&s_world,executed_pc,master,&s_map_cycle_remainder);
    g_master_cycles += (uint64_t)master;
    sc_advance_beam(master);
    snes->apuCatchupCycles += (double)master * kApuCyclesPerMaster;
    sc_catchup_apu(snes);
  }
  if (getenv("SC_ADDR_TRACE_ARM_ON_01ED")) {
    if (s_addr_trace_last_ed == 0xff) { s_addr_trace_last_ed = g_ram[0x01ed]; s_addr_trace_armed = false; }
    else if (!s_addr_trace_armed && g_ram[0x01ed] != s_addr_trace_last_ed) s_addr_trace_armed = true;
  }
  if(s_city_present_pending && g_ram[0x14]==0 && s_city_fade_started) {
    bool dark=PPU_forcedBlank(g_ppu) || !PPU_brightness(g_ppu);
    /* The native city fade also darkens CGRAM with INIDISP at full
     * brightness. Wait for that black frame, not just forced blank. */
    bool palette_dark=true;
    for(unsigned i=0;i<256;++i) if(g_ppu->cgram[i]&32767) {palette_dark=false;break;}
    dark|=palette_dark;
    if(dark) s_city_black_seen=true;
    else if(s_city_black_seen) {
      s_city_present_pending=false;
      if(getenv("SC_CITY_ENTRY_DIAG"))fprintf(stderr,"[city entry] reveal frame %llu zoom %g hud %u\n",(unsigned long long)s_frames,s_custom_renderer.map_zoom,ram_w(0x1d7));
    }
  }
  return guard > 0;
}

/* Is the picture at its settled brightness?
 *
 * The three framebuffer fills below all decide what to paint by MEASURING the
 * colours at the edge of the authentic picture -- is this strip flat, is this
 * margin uniform, is this row plain wood. Mid-fade those colours are moving,
 * so a row can qualify on one scanline and fail on the next, and the margins
 * come out banded. Reported from play on the way out of the stats pages:
 * stripes down both sides while the fade was still running, gone once it
 * finished.
 *
 * So they do not run unless the display is settled. A margin that stays
 * backdrop through a fade reads as a clean letterbox, and matches what the
 * tilemap-based margins do anyway -- those go through the PPU, so they fade
 * with everything else. */
/* Report the guest's colour-math setup once per change, under SC_WS_DIAG.
 *
 * The popup screens dim the city behind their panel, and the question is what
 * that dim actually is before trying to reproduce it in the margins. */
static void ws_trace_math(void) {
  if (!g_ppu || !getenv("SC_WS_DIAG")) return;
  static unsigned last = ~0u;
  const unsigned key = (unsigned)g_ppu->cgadsub << 16 | (unsigned)g_ppu->cgwsel << 8
                     | (unsigned)(g_ppu->fixedColor & 0xff);
  if (key == last) return;
  last = key;
  fprintf(stderr, "[math] $14=%02x cgadsub=%02x (layers=%02x half=%d sub=%d) "
                  "cgwsel=%02x (addSub=%d prevent=%d clip=%d) fixed=%d,%d,%d\n",
          g_ram[0x14], g_ppu->cgadsub, PPU_mathEnabled(g_ppu),
          (int)PPU_halfColor(g_ppu), (int)PPU_subtractColor(g_ppu),
          g_ppu->cgwsel, (int)PPU_addSubscreen(g_ppu),
          PPU_preventMathMode(g_ppu), PPU_clipMode(g_ppu),
          PPU_fixedColorR(g_ppu), PPU_fixedColorG(g_ppu), PPU_fixedColorB(g_ppu));
}

static bool ws_display_settled(void) {
  ws_trace_math();
  if (!g_ppu) return false;
  const bool ok = !PPU_forcedBlank(g_ppu) && PPU_brightness(g_ppu) == 0x0f;
  /* Log the EDGES of a fade, not every step. A fade walks brightness through
   * sixteen values, so a step-by-step trace emitted sixteen lines per
   * transition and buried everything else -- a play session came back as 16KB
   * of nothing but this, with the build stamp scrolled out of the capture. */
  { static int last = -3;
    const int now = PPU_forcedBlank(g_ppu) ? -2 : PPU_brightness(g_ppu);
    const int settled_now = now == 0x0f, was = last == 0x0f;
    if (getenv("SC_WS_DIAG") && last != -3 && settled_now != was)
      fprintf(stderr, "[fade] f=%llu $14=%02x %s\n",
              (unsigned long long)s_frames, g_ram[0x14],
              settled_now ? "settled" : "fading");
    last = now; }
  return ok;
}

/* Keep the backdrop layer's FURNITURE out of the margins -- fallback only.
 *
 * The margins on a mode-0 screen are taken from a backdrop-only pass, and on
 * the fax that layer carries the machine as well as the desk, so the machine
 * repeated into both margins along with the wood. This was the first answer:
 * the desk repeats on a 16-row cycle, so a row showing furniture borrows its
 * margin from 16 rows above and lands on the same phase, walking downward so a
 * borrowed row can itself be borrowed from.
 *
 * It does not work, and both reasons are visible on screen. It borrows 16
 * PIXELS where the cycle is 16 tile ROWS, so it repeats a 16 px band instead
 * of the real 128 px pattern; and it judges wood by colour on finished pixels,
 * which the machine's flat beige can pass, dragging the machine's own
 * structure sideways -- the leg-shaped smears reported from play.
 *
 * widen_wood_bg() now fixes the fax at the tilemap instead, where neither
 * failure is possible, so this runs only where that declined to act: a wood
 * screen with no free VRAM pair, or some other mode-0 screen with furniture on
 * its backdrop. Smeared wood still beats a copy of the machine. */
static void ws_hide_backdrop_furniture(void) {
  if (!ws_display_settled()) return;
  if (!g_ppu || s_ws_extra <= 0 || !s_ws_bg_margins || !s_ws_margin_fill) return;
  if (s_wood_widened) return;          /* real wood is out there already */
  const int right0 = s_video_w - s_ws_extra;
  for (int y = 16; y < kVideoHeight; y++) {
    uint32_t *row = (uint32_t *)(s_video_pixels + (size_t)y * s_video_pitch);
    const uint32_t *above =
        (const uint32_t *)(s_video_pixels + (size_t)(y - 16) * s_video_pitch);
    int woody = 0;
    for (int k = 0; k < 8; k++) {
      const uint32_t c = row[k];
      const unsigned r = (c >> 16) & 0xffu, g = (c >> 8) & 0xffu, b = c & 0xffu;
      if (r > g + 8u && r > b + 8u && r < 170u) woody++;
    }
    if (woody >= 6) continue;                              /* plain desk */
    for (int x = 0; x < s_ws_extra; x++) row[x] = above[x];
    for (int x = right0; x < s_video_w; x++) row[x] = above[x];
  }
}

/* Carry a flat backdrop field out into the margins.
 *
 * The tax, city evaluation, city overview and last-ten-events screens all sit
 * on one flat colour, and all four drew it only inside the authentic 256:
 * black bars either side, reported from play with the observation that the fix
 * is simply to run the green out to both edges. It is. Where the picture's own
 * edge is a flat field there is nothing to reconstruct, so the colour is
 * carried outward and that is the whole of it -- no pattern, no VRAM, no
 * tilemap.
 *
 * Measured, the margins on those screens are green only on rows 0..10 and
 * 220..223 and black on 11..219, so the backdrop is reaching them at the top
 * and bottom and something in the compositor is dropping it in between. This
 * paints over the symptom rather than fixing that, which is worth being
 * explicit about; the cause is the same compositor behaviour as the city
 * view's grey block.
 *
 * Two guards keep it away from everything else. The edge must be flat for
 * eight pixels -- measured across every screen, the four flat ones manage that
 * on every row, while the wood screens manage 2..8 and the city view 1..3 --
 * and the margin must already be a single colour, so any screen drawing real
 * content out there is skipped row by row. */
#define SC_WS_FLAT_RUN 8
static void ws_fill_flat_margins(void) {
  /* Runs DURING a fade as well, not only once the display has settled.
   *
   * ws_display_settled() demands brightness == 15, so through a fade this
   * was skipped and the margins kept the per-line blank's black while the
   * guest dimmed gradually. Measured on the city overview, dismissing it:
   * the panel walks 216 -> 15 over fourteen frames while the border drops
   * from 49 to 0 in ONE. Reported from play as the green border being drawn
   * to black too fast.
   *
   * Nothing here needs full brightness. The colour painted is the guest's
   * own edge pixel, which the PPU has already dimmed by the same amount, so
   * the margin tracks the fade for free. Force blank still bars it -- then
   * there is genuinely nothing to show, and the blank owns the margins. */
  if (!g_ppu || PPU_forcedBlank(g_ppu)) return;
  if (!g_ppu || s_ws_extra <= 0 || !s_ws_margin_fill) return;
  const int right0 = s_video_w - s_ws_extra;
  /* All rows or none: this is a property of the SCREEN, not of a row.
   *
   * Judging each row on its own looked reasonable and is wrong. The city view
   * is not a flat-background screen, but a handful of its rows happen to end
   * in a run of one colour -- a band of water or sand meeting the edge -- and
   * each of those got that colour smeared out to the last widescreen pixel.
   * Measured: 7 rows of 224 on the city view against 224 of 224 on the
   * evaluation page, so the two separate by a mile and a simple majority
   * settles it. */
  int flat_rows = 0;
  for (int y = 0; y < kVideoHeight; y++) {
    const uint32_t *row = (const uint32_t *)(s_video_pixels + (size_t)y * s_video_pitch);
    bool both = true;
    for (int side = 0; side < 2 && both; side++) {
      const uint32_t edge = side ? row[right0 - 1] : row[s_ws_extra];
      for (int k = 1; k < SC_WS_FLAT_RUN && both; k++)
        if (row[side ? right0 - 1 - k : s_ws_extra + k] != edge) both = false;
    }
    if (both) flat_rows++;
  }
  if (flat_rows * 4 < kVideoHeight * 3) return;   /* not a flat-background screen */
  /* Fill with the SCREEN's background colour, not each row's own edge pixel.
   *
   * Per-row is wrong wherever something real reaches the edge. On the tax
   * menu the panel very nearly touches the guest's right edge, so rows
   * 41..53 -- the TAX RATE row -- smeared the panel's colour across the
   * whole margin, and with the mouse cursor sitting there they smeared its
   * black. Reported from play as colour repeating to the border, and as a
   * black bar "because the last pixel of the cursor is black". The cursor
   * only ever supplied the colour: savestate_8 shows the same band
   * standing still, with no mouse involved.
   *
   * Found by snapshotting one margin pixel through the end-of-frame path:
   * row45[400] is 000000 after ws_hide_backdrop_furniture() and ffdeb5
   * after this function, which put the write here and nowhere else.
   *
   * The margin exists to continue a flat background, and the screen has
   * already had to prove it HAS one to reach here, so paint the colour the
   * rows agree on. Note the split: the flat run still qualifies a row using
   * that row's OWN edge, and only the colour painted comes from the
   * screen. Testing flatness against the background instead was tried and
   * is wrong in the other direction -- a row whose edge is the panel is
   * perfectly flat, just not in the background colour, so it failed, was
   * skipped, and kept the blank's black. That trades a coloured stripe for
   * a black one. */
  uint32_t bg_edge = 0; int bg_votes = -1;
  for (int y = 0; y < kVideoHeight; y++) {
    const uint32_t cand = ((const uint32_t *)(s_video_pixels +
                           (size_t)y * s_video_pitch))[right0 - 1];
    int v = 0;
    for (int y2 = 0; y2 < kVideoHeight; y2++)
      if (((const uint32_t *)(s_video_pixels +
            (size_t)y2 * s_video_pitch))[right0 - 1] == cand) v++;
    if (v > bg_votes) { bg_votes = v; bg_edge = cand; }
  }
  for (int y = 0; y < kVideoHeight; y++) {
    uint32_t *row = (uint32_t *)(s_video_pixels + (size_t)y * s_video_pitch);
    for (int side = 0; side < 2; side++) {
      const uint32_t row_edge = side ? row[right0 - 1] : row[s_ws_extra];
      const uint32_t edge = bg_edge;
      bool flat = true;
      for (int k = 1; k < SC_WS_FLAT_RUN && flat; k++)
        if (row[side ? right0 - 1 - k : s_ws_extra + k] != row_edge) flat = false;
      if (!flat) continue;
      const int x0 = side ? right0 : 0;
      const int x1 = side ? s_video_w : s_ws_extra;
      bool uniform = true;
      for (int x = x0 + 1; x < x1 && uniform; x++)
        if (row[x] != row[x0]) uniform = false;
      if (!uniform || row[x0] == edge) continue;   /* real content, or done */
      for (int x = x0; x < x1; x++) row[x] = edge;
    }
  }
}

/* Fill widescreen margins that nothing drew into, from the screen's own edge.
 *
 * Some screens cannot be widened at all. Map select and name entry each carry
 * their background on BG3 as a 32-column tilemap, and have a single spare 2KB
 * VRAM page with no consecutive pair, so the main menu's relocate-and-fill is
 * impossible. Mirroring the layer does work without VRAM, but it reflects the
 * screen's own label into the margin -- "MAP SELECT" came back as "AM" on the
 * left and "T" on the right.
 *
 * So the margins are filled here instead, from the leftmost columns of the
 * authentic picture, which on both screens are 16 columns of plain desk on
 * every row. Mirror-tiled, so there is no seam and no need for the source to
 * be a whole pattern period.
 *
 * Two guards keep this from touching anything it should not:
 *
 *   - a row is filled only if its margins are ENTIRELY backdrop, so any screen
 *     that legitimately reaches the margins (the title's BG1, the widened main
 *     menu, the scenario selector) is skipped row by row;
 *   - the source strip must itself be free of backdrop, so a blank or fading
 *     screen does not smear nothing across the margins.
 *
 * These screens do not scroll, which is what makes a static fill honest here --
 * an earlier framebuffer fill on the scrolling selector crawled, and that is
 * why the selector got real tiles instead. */
#define SC_WS_FILL_SRC 16
static void ws_fill_margins(void) {
  if (!ws_display_settled()) return;
  /* Not when every background is clamped.
   *
   * That case is now handled properly at the line level: nothing is entitled
   * to the margins, so they are blanked to the backdrop. Filling them again
   * here from the picture's own edge undoes it, and on the advice popup and
   * the graphs page it undid it with a mirror-tiled strip of city map -- the
   * very content those screens should not be showing out there. The flat
   * screens that do want their colour carried out are covered by
   * ws_fill_flat_margins(), which tests for a flat edge rather than a merely
   * plain-looking one. */
  if ((s_ws_clamp_now & 0x0fu) == 0x0fu) return;
  if (!g_ppu || s_ws_extra <= 0 || !s_ws_margin_fill) return;
  /* Pillarboxed screens are filled too. The margins there have just been
   * blacked by the caller, so there is nothing to preserve, and a screen that
   * cannot widen its tilemap can still show its own backdrop in the margins. */

  /* Remember the last usable source strip for this screen, and fall back to it
   * when the live edge is no longer plain.
   *
   * The fax is why. Its BG3 carries wood across the whole picture -- visible
   * before the paper rises, as reported from play -- but once the sheet is up
   * it covers the edge on most rows, so a live-only source fills the top and
   * leaves the rest black. Caching the strip while it IS clean keeps the whole
   * margin filled for as long as the screen lasts.
   *
   * Dropped whenever $14 changes, so one screen's wood can never leak into
   * another's margins. */
  static uint32_t cache[SC_WS_FILL_SRC * kVideoHeight];
  static bool cache_ok[kVideoHeight];
  static uint8_t cache_screen = 0xff;
  if (cache_screen != g_ram[0x14]) {
    cache_screen = g_ram[0x14];
    memset(cache_ok, 0, sizeof cache_ok);
  }

  const uint16_t bd = g_ppu->cgram[0];
  const uint32_t backdrop = ((uint32_t)g_ppu->brightnessMult[bd & 0x1f] << 16)
                          | ((uint32_t)g_ppu->brightnessMult[(bd >> 5) & 0x1f] << 8)
                          | (uint32_t)g_ppu->brightnessMult[(bd >> 10) & 0x1f];
  const int right0 = s_video_w - s_ws_extra;

  for (int y = 0; y < kVideoHeight; y++) {
    uint32_t *row = (uint32_t *)(s_video_pixels + (size_t)y * s_video_pitch);
    bool empty = true;
    for (int x = 0; x < s_ws_extra && empty; x++)
      if ((row[x] & 0x00FFFFFFu) != backdrop) empty = false;
    for (int x = right0; x < s_video_w && empty; x++)
      if ((row[x] & 0x00FFFFFFu) != backdrop) empty = false;
    if (!empty) continue;                       /* something drew here */

    const uint32_t *src = &row[s_ws_extra];     /* authentic left edge */
    /* The strip has to look like a BACKGROUND, not like content. "Not
     * backdrop" was too weak a test: on the gameplay screen the left edge is
     * map and toolbar, and tiling it produced vertical smears across the
     * margins.
     *
     * Measured over every screen, distinct colours in the 16-pixel strip and
     * the channel range across it separate the two cleanly:
     *
     *   title / menu / map select / name entry   1.8-1.9 colours, range ~71
     *   gameplay / tax                           5.7-6.6 colours, range ~215
     *
     * so a plain texture is at most four colours and a modest range. */
    unsigned distinct = 0, seen[SC_WS_FILL_SRC];
    unsigned lo = 255, hi = 0;
    bool usable = true;
    for (int k = 0; k < SC_WS_FILL_SRC; k++) {
      const uint32_t c = src[k] & 0x00FFFFFFu;
      if (c == backdrop) { usable = false; break; }
      unsigned j = 0;
      while (j < distinct && seen[j] != c) j++;
      if (j == distinct) seen[distinct++] = c;
      for (int sh = 0; sh < 24; sh += 8) {
        const unsigned ch = (c >> sh) & 0xffu;
        if (ch < lo) lo = ch;
        if (ch > hi) hi = ch;
      }
    }
    uint32_t *keep = &cache[(size_t)y * SC_WS_FILL_SRC];
    if (usable && distinct <= 4u && (hi - lo) <= 120u) {
      for (int k = 0; k < SC_WS_FILL_SRC; k++) keep[k] = src[k];
      cache_ok[y] = true;
    } else if (!cache_ok[y]) {
      continue;                       /* never had a clean source for this row */
    }

    for (int x = s_ws_extra - 1; x >= 0; x--) {
      const int d = (s_ws_extra - 1 - x) % (SC_WS_FILL_SRC * 2);
      row[x] = keep[d < SC_WS_FILL_SRC ? d : (SC_WS_FILL_SRC * 2 - 1 - d)];
    }
    for (int x = right0; x < s_video_w; x++) {
      const int d = (x - right0) % (SC_WS_FILL_SRC * 2);
      row[x] = keep[d < SC_WS_FILL_SRC ? d : (SC_WS_FILL_SRC * 2 - 1 - d)];
    }
  }
}

/* Extend the title's light marquee across the widescreen margins.
 *
 * The row along the bottom of the title is OBJ, not a background, and it
 * scrolls left. Measured at 448 wide it spans x = 48..335 with its left edge
 * advancing 3 px per frame -- so it leaves into the left margin correctly, but
 * its right end is pinned at authentic x = 239, where the ROM spawns each
 * light. For a 256-wide picture that is near enough the edge; widened, the
 * spawn point sits 112 px inside the right margin and the lights appear to pop
 * into existence mid-picture.
 *
 * Nothing can reveal them, because the sprites do not exist out there. Neither
 * the ambiguous-band decode nor PpuSetWsHudOamShift helps -- see
 * docs/ROM_MAP.md. The only thing that closes the gap is putting more of them
 * in OAM, which is what this does: it finds the row, works out its pitch, and
 * repeats the SAME sprite outward into free slots until the widened picture is
 * covered. Nothing is invented -- tile, palette, priority and size are copied
 * from the row's own members, and the extras move with it because they are
 * recomputed from the live row every frame.
 *
 * Slots 104..127 are the game's parked pool on this screen (X=128, Y=0, tile
 * and attributes all zero), so the extras go there, highest first.
 *
 * OAM layout, from PpuDecodeOamX: the index there is the WORD index, so sprite
 * i owns words 2i and 2i+1, and its two high bits live in highOam[(2i)>>3] at
 * bit (2i)&7 -- X bit 8 -- and the bit above it -- size. That is the ordinary
 * SNES arrangement of four sprites per high byte. */
#define SC_LIGHTS_FIRST_SPARE 104   /* the game's own slots end here */
#define SC_SIGN_FIRST_SLOT    104   /* six for the sign ... */
#define SC_LIGHTS_FLOOR       110   /* ... the rest for the light row */

static int oam_get_x(int i) {
  const int wi = i * 2;
  int x = g_ppu->oam[wi] & 0xff;
  x |= ((g_ppu->highOam[wi >> 3] >> (wi & 7)) & 1) << 8;
  return x >= 256 ? x - 512 : x;
}

static void oam_put(int i, int x, int y, int tile, int attr, int size) {
  const unsigned x9 = (unsigned)x & 0x1ffu;
  const int wi = i * 2;
  g_ppu->oam[wi]     = (uint16_t)(((unsigned)(y & 0xff) << 8) | (x9 & 0xffu));
  g_ppu->oam[wi + 1] = (uint16_t)(((unsigned)(attr & 0xff) << 8) | (unsigned)(tile & 0xff));
  uint8_t *hb = &g_ppu->highOam[wi >> 3];
  const int bit = wi & 7;
  *hb = (uint8_t)(*hb & ~(3u << bit));
  if (x9 & 0x100u) *hb = (uint8_t)(*hb | (1u << bit));
  if (size)        *hb = (uint8_t)(*hb | (2u << bit));
}

/* â”€â”€ The scenario selector's pins and win marks â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 *
 * Where each sprite is, the game's and Sylt's, is src/sc_selector.c's to say.
 * Here the classic renderer acts on it: the game's own sprites that lie in a
 * margin are claimed by exact position -- their OAM slot matched on X, Y and
 * tile -- so the strict decode shows them and still hides the bracket, and
 * Sylt's, which no OAM slot holds, are put into slots the selector leaves
 * parked. The first version of the claim matched each mark at its record's
 * BASE, where none of its four sprites sits, and never matched at all. */
static uint8_t sc_bus_rom_read(void *ctx, uint32_t addr) {
  (void)ctx;
  return (uint8_t)snes_read(g_snes, addr);
}

static bool selector_on_screen(void) {
  return ScSelector_OnScreen(g_ram[0x14]);
}

static unsigned selector_won(void) {
  return g_ram[0x42] | ((unsigned)g_ram[0x43] << 8);
}

/* Claim each of the game's pin and mark sprites that lies in a margin. */
static void selector_hint_margin_sprites(void) {
  const int scroll = ScSelector_Scroll(g_ppu, sc_bus_rom_read, NULL);
  if (scroll < 0) return;
  ScSelSprite want[SC_SEL_MAX_SPRITES];
  const int nw = ScSelector_Sprites(want, scroll, selector_won(), false,
                                    sc_bus_rom_read, NULL);
  static int diag = -1;
  if (diag < 0) diag = getenv("SC_MARK_DIAG") != NULL;
  for (int s = 0; s < 128; s++) {
    const unsigned lo = g_ppu->oam[s * 2], hi = g_ppu->oam[s * 2 + 1];
    const unsigned x9 = (lo & 0xffu) | (((g_ppu->highOam[s >> 2] >> ((s & 3) * 2)) & 1u) << 8);
    if (x9 < 256u) continue;                       /* on screen already */
    const unsigned y = (lo >> 8) & 0xffu;
    const unsigned tile = (hi & 0xffu) | ((hi >> 8) & 1u) << 8;
    for (int w = 0; w < nw; w++) {
      const unsigned wt = (unsigned)want[w].tile | ((unsigned)want[w].attr & 1u) << 8;
      if (((unsigned)want[w].x & 0x1ffu) != x9 ||
          ((unsigned)want[w].y & 0xffu) != y || wt != tile) continue;
      /* Past the ambiguous band it decodes negative: the LEFT hint admits
       * it. Inside the band it is the right margin's. */
      if (x9 >= 256u + (unsigned)g_ppu->extraRightCur)
        s_oam_left_hints[s >> 3] |= (uint8_t)(1u << (s & 7));
      else
        s_oam_right_hints[s >> 3] |= (uint8_t)(1u << (s & 7));
      if (diag)
        fprintf(stderr, "[mark] slot %d x9=%u y=%u tile=$%03x scroll=%d\n",
                s, x9, y, tile, scroll);
      break;
    }
  }
}

/* The title sign in the margins, from src/sc_titlesign.c's reading of its
 * animation rather than from OAM.
 *
 * The game's own sprites cannot serve the margins here: its x counts through
 * ten bits and OAM holds nine, so while the sign is still approaching from
 * the right its entries read as far-left ones, and for the half of the round
 * it is away they sit unchanged where it left. Both are what the margin used
 * to show -- the sign appearing at the wrong edge, and the copy that hung at
 * the left border until the motion heuristic faded it out.
 *
 * Its six sprites are placed here instead, at the position its own counter
 * gives them, in the slots below the light row's pool, and only where they
 * fall outside the guest's 256 columns -- those columns keep the game's own
 * sprites, put on this frame by title_sign_align() below, so both halves of
 * the board are at one position and meet exactly at the seam.
 *
 * SC_WS_TITLE_SIGN=0 turns it off. */
static void title_sign_place(void) {
  static const uint8_t kObjSizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32}
  };
  static int on = -1;
  if (on < 0) { const char *e = getenv("SC_WS_TITLE_SIGN");
                on = (e && *e) ? (*e != '0') : 1; }
  if (!on || !g_ppu || s_ws_extra <= 0) return;
  ScSelSprite sign[SC_SIGN_MAX_SPRITES];
  const int n = ScTitleSign_Sprites(sign, SC_SIGN_MAX_SPRITES,
                                    sc_bus_rom_read, NULL);
  int slot = SC_SIGN_FIRST_SLOT;
  for (int k = 0; k < n && slot < SC_LIGHTS_FLOOR; k++) {
    const int size = kObjSizes[PPU_objSize(g_ppu)][sign[k].large ? 1 : 0];
    const int x = sign[k].x;
    if (x + size <= -s_ws_extra || x >= kVideoWidth + s_ws_extra) continue;
    if (x >= 0 && x + size <= kVideoWidth) continue;   /* the guest's own */
    oam_put(slot, x, sign[k].y, sign[k].tile, sign[k].attr, sign[k].large);
    if (x < 0) s_oam_left_hints[slot >> 3] |= (uint8_t)(1u << (slot & 7));
    else if (x >= kVideoWidth)
      s_oam_right_hints[slot >> 3] |= (uint8_t)(1u << (slot & 7));
    slot++;
  }
}

/* The sign's own six sprites, put on the frame the rest of the picture is on.
 *
 * 05:942e runs the animation channel first -- the COP at 05:9448, which
 * writes the sprites into the shadow OAM -- and steps the scene after it:
 * INC $16 for the skyline's scroll and DEC $0277 for the sign, one after the
 * other at 05:9460. Both reach the PPU at the next NMI, the sprites from
 * before that pair and the scroll from after it, so the board is a frame
 * behind the building it is bolted to. The pan moves a pixel every second
 * frame, so the two never step together -- skyline, sign, skyline, sign --
 * and the board shivers a pixel against the tower for the whole crossing.
 * It does so on hardware too; it is still a frame of lag, not a design.
 *
 * src/sc_titlesign.c has the position the scroll of this frame implies, and
 * ScTitleSign_Lag() says when the uploaded copy is the step behind it. The
 * entries are found by tile, row and that older position -- so only the six
 * are touched, and only where the game really emitted them -- and moved the
 * pixel forward. The margins draw from the same position (title_sign_place),
 * which is why the two halves meet exactly at the seam.
 *
 * SC_SIGN_ALIGN=0 leaves the guest's copy where the hardware would have it. */
static void title_sign_align(void) {
  static int on = -1;
  if (on < 0) { const char *e = getenv("SC_SIGN_ALIGN"); on = (e && *e) ? (*e != '0') : 1; }
  if (!on || !g_ppu) return;
  const int lag = ScTitleSign_Lag();
  if (lag <= 0) return;
  ScSelSprite sign[SC_SIGN_MAX_SPRITES];
  const int n = ScTitleSign_Sprites(sign, SC_SIGN_MAX_SPRITES,
                                    sc_bus_rom_read, NULL);
  unsigned taken = 0;
  for (int s = 0; s < 128 && taken != (1u << n) - 1u; s++) {
    const unsigned lo = g_ppu->oam[s * 2], hi = g_ppu->oam[s * 2 + 1];
    const unsigned y = (lo >> 8) & 0xffu;
    const unsigned x9 = (lo & 0xffu) |
        (((g_ppu->highOam[s >> 2] >> ((s & 3) * 2)) & 1u) << 8);
    const unsigned tile = (hi & 0xffu) | (((hi >> 8) & 1u) << 8);
    for (int k = 0; k < n; k++) {
      if (taken & (1u << k)) continue;
      const unsigned wt = (unsigned)sign[k].tile | ((unsigned)sign[k].attr & 1u) << 8;
      if (wt != tile || ((unsigned)sign[k].y & 0xffu) != y) continue;
      if (x9 != ((unsigned)(sign[k].x + lag) & 0x1ffu)) continue;
      const unsigned nx = (unsigned)sign[k].x & 0x1ffu;
      g_ppu->oam[s * 2] = (uint16_t)((lo & 0xff00u) | (nx & 0xffu));
      uint8_t *hb = &g_ppu->highOam[s >> 2];
      const unsigned bit = 1u << ((s & 3) * 2);
      *hb = (uint8_t)((nx & 0x100u) ? (*hb | bit) : (*hb & ~bit));
      taken |= 1u << k;
      break;
    }
  }
}
/* Sylt's pin, and its mark once won, in slots the selector leaves parked. */
static void selector_sylt_sprites(void) {
  if (!s_ninth_scenario || !g_ppu || !selector_on_screen()) return;
  const int scroll = ScSelector_Scroll(g_ppu, sc_bus_rom_read, NULL);
  if (scroll < 0) return;
  ScSelSprite all[SC_SEL_MAX_SPRITES];
  const int n = ScSelector_Sprites(all, scroll, selector_won(), true,
                                   sc_bus_rom_read, NULL);
  /* Spare slots: the ones parked at X = 384, from the top down. */
  int slot = 127;
  for (int k = 0; k < n; k++) {
    if (!all[k].host) continue;
    while (slot >= 64) {
      const unsigned lo = g_ppu->oam[slot * 2];
      const unsigned x9 = (lo & 0xffu) |
          (((g_ppu->highOam[slot >> 2] >> ((slot & 3) * 2)) & 1u) << 8);
      if (x9 == 384u) break;
      slot--;
    }
    if (slot < 64) return;
    const int x = all[k].x;
    if (x < 256 + (s_ws_extra > 0 ? g_ppu->extraRightCur : 0)) {
      oam_put(slot, x, all[k].y, all[k].tile, all[k].attr, all[k].large);
      if (x >= 256) s_oam_right_hints[slot >> 3] |= (uint8_t)(1u << (slot & 7));
    }
    slot--;
  }
}

/* The title's widescreen settings have to outlive screen $01.
 *
 * Pressing Start runs INC $14 at 03:D301 on the very next frame, but the
 * picture does not leave then: screen $02 fades it out first, and only blanks
 * to upload the menu once it is dark. Every title policy in this file -- BG3
 * widened, BG2 and BG3 left unclamped, the right-margin OAM hints, the light
 * row carried outward -- keyed on $14 == $01, so all of them dropped at once
 * while the centre was still at full brightness. Reported from play as the
 * widescreen elements being deleted instead of fading.
 *
 * It only shows once the title has finished building up, which is how it
 * was reported. Measured over the whole frame with Start pressed at frame
 * 1200: on the frame after Start the margins dropped from 43.4 and 33.8
 * mean brightness to 32.1 and 18.5 while the centre stayed at 56.8, taking
 * the margin-to-centre ratio from 0.68 to 0.45, and to 0.39 once the fade
 * itself began. With the latch the ratio holds at 0.68 all the way to
 * black. Pressed early, while the city is still rising, it shows nothing,
 * because the margins hold little yet.
 *
 * So the policies stay latched through $02 until the display is dark --
 * brightness 0 or forced blank -- and let go there. Nothing of the menu can
 * be on screen before that, because the menu is uploaded during the blank.
 * The latch only arms on $01, so the menu's own fade-in on $02 runs with the
 * ordinary settings. */
static bool s_title_ws_latched;

static void title_ws_update(void) {
  if (g_ram[0x14] == 0x01) { s_title_ws_latched = true; return; }
  if (s_title_ws_latched &&
      (g_ram[0x14] != 0x02 || !g_ppu ||
       PPU_forcedBlank(g_ppu) || PPU_brightness(g_ppu) == 0))
    s_title_ws_latched = false;
}

static bool title_ws_live(void) {
  return g_ram[0x14] == 0x01 || s_title_ws_latched;
}

static void widen_title_lights(void) {
  if (!g_ppu || s_ws_extra <= 0 || !s_ws_widen_lights) return;
  if (!title_ws_live()) return;     /* the title, and its fade-out */

  /* The row is the largest group of sprites sharing a Y, tile and attribute in
   * the bottom of the picture. Found rather than hard-coded, so a different
   * frame of the animation cannot leave it half-extended. */
  int best_n = 0, best_y = 0, best_tile = 0, best_attr = 0, best_size = 0;
  for (int i = 0; i < SC_LIGHTS_FIRST_SPARE; i++) {
    const int y = g_ppu->oam[i * 2] >> 8;
    if (y < 150 || y > 215) continue;
    const int tile = g_ppu->oam[i * 2 + 1] & 0xff;
    const int attr = g_ppu->oam[i * 2 + 1] >> 8;
    int n = 0;
    for (int j = 0; j < SC_LIGHTS_FIRST_SPARE; j++)
      if ((g_ppu->oam[j * 2] >> 8) == y &&
          (g_ppu->oam[j * 2 + 1] & 0xff) == tile &&
          (g_ppu->oam[j * 2 + 1] >> 8) == attr) n++;
    if (n > best_n) {
      best_n = n; best_y = y; best_tile = tile; best_attr = attr;
      best_size = (g_ppu->highOam[(i * 2) >> 3] >> (((i * 2) & 7) + 1)) & 1;
    }
  }
  if (best_n < 3) return;              /* not a row; leave it alone */

  /* Pitch is the smallest positive gap between members, and lo/hi are the
   * row's extent -- measured ONLY over members the authentic viewport actually
   * shows.
   *
   * A member parked off-screen counts towards best_n but must not set the
   * extent. One such sprite sat at x = -255, which made lo = -255, so the
   * leftward loop below started at lo - pitch = -319 and its first condition
   * (x >= -extra - pitch) was already false: it placed nothing at all, ever.
   * Measured as 0 px of sprite in the left margin against 1293 in the right,
   * on every title frame, and reported from play as the lights being missing
   * on the left until the title starts moving -- at which point the game's own
   * sprites move into the margin and cover it up. */
  int lo = 0x7fff, hi = -0x7fff, pitch = 0x7fff;
  for (int i = 0; i < SC_LIGHTS_FIRST_SPARE; i++) {
    if ((g_ppu->oam[i * 2] >> 8) != best_y) continue;
    if ((g_ppu->oam[i * 2 + 1] & 0xff) != best_tile) continue;
    if ((g_ppu->oam[i * 2 + 1] >> 8) != best_attr) continue;
    const int x = oam_get_x(i);
    if (x <= -16 || x >= 256) continue;   /* parked, not part of the row */
    if (x < lo) lo = x;
    if (x > hi) hi = x;
    for (int j = 0; j < SC_LIGHTS_FIRST_SPARE; j++) {
      if ((g_ppu->oam[j * 2] >> 8) != best_y) continue;
      if ((g_ppu->oam[j * 2 + 1] & 0xff) != best_tile) continue;
      const int xj = oam_get_x(j);
      if (xj <= -16 || xj >= 256) continue;
      const int d = xj - x;
      if (d > 0 && d < pitch) pitch = d;
    }
  }
  if (lo > hi) return;                    /* nothing on screen to extend */
  if (pitch <= 0 || pitch > 128) return;

  int slot = 127;
  int placed_l = 0, placed_r = 0;
  for (int x = lo - pitch; x >= -s_ws_extra - pitch && slot >= SC_LIGHTS_FLOOR; x -= pitch) {
    oam_put(slot, x, best_y, best_tile, best_attr, best_size);
    /* Below 0 it is hardware-hidden unless this host claims it. */
    if (x < 0) s_oam_left_hints[slot >> 3] |= (uint8_t)(1u << (slot & 7));
    placed_l++;
    slot--;
  }
  for (int x = hi + pitch; x <= 256 + s_ws_extra && slot >= SC_LIGHTS_FLOOR; x += pitch) {
    oam_put(slot, x, best_y, best_tile, best_attr, best_size);
    /* Past 256 it lands in the ambiguous band, so claim it explicitly. */
    if (x >= 256) s_oam_right_hints[slot >> 3] |= (uint8_t)(1u << (slot & 7));
    placed_r++;
    slot--;
  }
  if (getenv("SC_LIGHTS_DIAG")) {
    static int nn;
    if (nn++ % 60 == 0)
      fprintf(stderr,
              "[lights] n=%d y=%d lo=%d hi=%d pitch=%d placed L=%d R=%d slot=%d\n",
              best_n, best_y, lo, hi, pitch, placed_l, placed_r, slot);
  }
}

/* The wooden desk, shared by the main menu and the fax.
 *
 * Both screens draw it from one sheet -- tiles $020..$11f, sixteen to a sheet
 * row. Confirmed rather than assumed: rendering a 16x4 tile patch from each
 * save state and comparing gives 0 of 4096 pixels different, same three
 * colours. Only the character base differs ($4000 on the menu, $0000 on the
 * fax), so a tile NUMBER means the same wood on either screen.
 *
 * tools/wood_pattern.py measures the periods off live PPU dumps and renders
 * the match -- the tilemap, the block it found, and that block tiled back --
 * so the answer can be checked by eye instead of trusted:
 *
 *   fax  BG3 $5000 / chr $0000    16 x 16 tiles    512/512 cells reproduced
 *   menu BG3 $3000 / chr $4000    32 x  8 tiles    256/256 cells reproduced
 *
 * The two differ because each screen lays the same sheet out its own way, so
 * neither period can stand in for the other. What IS common is how a row runs:
 * sixteen consecutive tiles, so walking sideways means walking the low four
 * bits and wrapping them, while the high bits pick the sheet row and stay put.
 * Grown that way off a screen's OWN edge tile, a margin inherits that screen's
 * phase without being told it -- the fax sits at phase 0, the menu at phase 8
 * on half its rows, and neither is written down anywhere here.
 *
 * This replaces an 8-wide table read off the menu. There is no 8-wide repeat
 * on either screen; folding the wood into one is what put the menu's widened
 * border out of step with the rest of the panel. */
#define SC_WOOD_LO 0x020u
#define SC_WOOD_HI 0x11fu

static bool wood_tile(uint16_t e) {
  const unsigned t = e & 0x3ffu;
  return t >= SC_WOOD_LO && t <= SC_WOOD_HI && !(e & 0xc000u);   /* no flips */
}

static uint16_t wood_grow(uint16_t e, int d) {
  const unsigned t = e & 0x3ffu;
  return (uint16_t)((e & ~0x3ffu) | (t & ~0x0fu) |
                    ((unsigned)((int)t + d) & 0x0fu));
}

/* A run of two consecutive wood tiles from c, walking by step.
 *
 * "The end tile is in the wood range" was too weak a test to grow a margin
 * from. On map select the scenario names are still in the tilemap off to both
 * sides, and their font tiles fall inside the same range, so growing from one
 * walked along the font and printed "Flooding" and "Coastal" into the right
 * margin. Letters are not consecutive tile numbers -- "Flooding" repeats an
 * o -- so asking for a real run rejects them while the wood still passes.
 *
 * Two, because two is what these screens actually leave exposed, and asking
 * for more throws away the rows that have least. Measured over every wood map,
 * counting rows whose margin would come out at a phase the tilemap disagrees
 * with:
 *
 *              run>=2   run>=3
 *   map select    1        1      (the one is $0f9, a cable tile, correctly
 *   View Mode     3       13       refused in both)
 *   selector      0        0
 *   fax           0        0
 *   menu          0        0
 *
 * View Mode is the case that decides it: its desk is isometric, and row 6 ends
 * $202e $202f with a non-wood tile beside them, so three was one too many. */
static bool wood_run_at(const uint16_t *map, int r, int c, int step) {
  for (int k = 0; k < 2; k++) {
    const uint16_t a = map[r * 32 + c + k * step];
    if (!wood_tile(a)) return false;
    if (k && a != wood_grow(map[r * 32 + c + (k - 1) * step], step)) return false;
  }
  return true;
}

static bool wood_row_ends(const uint16_t *map, int r) {
  return wood_run_at(map, r, 0, 1) && wood_run_at(map, r, 31, -1);
}

/* Is this map the wood sheet, laid out the way wood_grow() assumes?
 *
 * Everything below writes four kilobytes of VRAM, so it has to be sure of the
 * layout first. Being "mostly tiles in the wood range" is not enough -- what
 * wood_grow() relies on is a run of CONSECUTIVE tiles, which wood_row_ends()
 * now establishes at both ends of every row it accepts. On top of that: some
 * rows entirely wood, and a good share of the map wood overall.
 *
 * The thresholds are set from measurement, not taste. Across every save state
 * the three real wood maps score rows/full of 16/15 (menu), 20/20 (fax) and
 * 10/8 (map select and name entry, whose picker device covers most of the
 * screen -- an earlier bar of 600 wood cells excluded them, and they were the
 * two screens still reported as looking wrong). Everything else in VRAM either
 * scores rows = 0 or is a map no enabled layer points at. */
static bool wood_map_ok(const uint16_t *map) {
  int rows = 0, cells = 0, full = 0;
  for (int r = 0; r < 32; r++) {
    int w = 0;
    for (int c = 0; c < 32; c++) if (wood_tile(map[r * 32 + c])) w++;
    cells += w;
    if (w == 32) full++;
    if (wood_row_ends(map, r)) rows++;
  }
  return rows >= 8 && full >= 4 && cells >= 300;
}

/* The row a margin row grows from: itself, or the wood row it repeats.
 *
 * Rows carrying furniture have no wood at their ends to take a phase from --
 * the fax machine spans rows 19..30, and rows 20..30 hold no wood at all. Those
 * fall back to the row the wood repeats from, which is what the old "borrow
 * from 16 rows above" was reaching for. Two things were wrong with that. It
 * borrowed 16 PIXELS rather than 16 tile rows, so it smeared a 16 px band down
 * the screen instead of repeating the real 128 px pattern; and it worked on
 * finished pixels, so wherever its is-this-plain-wood test misfired on the
 * machine's flat beige it dragged the machine's own structure sideways -- the
 * leg-shaped smears reported from play. Working on tilemap entries makes both
 * failures impossible, and measuring the period makes the first one moot.
 *
 * Measured on the EDGE columns alone, because they are the only ones the fill
 * reads. Comparing whole rows needed both rows to be clean end to end, and on
 * map select no such pair exists at any spacing -- its device covers the
 * middle from row 6 to row 27 -- so the search found nothing and fell through
 * to 16. The edge columns are unobstructed on every row and repeat every 8
 * there, which is the answer the fill actually needs. */
static int wood_vperiod(const uint16_t *map) {
  for (int vp = 1; vp <= 16; vp <<= 1) {
    int tested = 0, bad = 0;
    for (int r = 0; r + vp < 32; r++) {
      for (int e = 0; e < 2; e++) {
        const int c = e ? 31 : 0;
        const uint16_t a = map[r * 32 + c], b = map[(r + vp) * 32 + c];
        if (!wood_tile(a) || !wood_tile(b)) continue;
        tested++;
        if (a != b) bad++;
      }
    }
    /* A tenth may disagree. Demanding perfection let a single stray tile
     * decide the answer: map select carries $0f9 at row 11 column 31, one cell
     * of the device's cable that happens to land in the wood range, and it
     * alone rejected the true period of 8 and sent the search to 16. */
    if (tested >= 16 && bad * 10 <= tested) return vp;
  }
  return 16;
}

/* Fill a widened map's second page with wood grown off the first page's ends.
 *
 * On a 64-column map with no scroll the right margin reads columns 32.., and
 * the left margin reads the map's far end -- 63, 62, ... -- because the fetch
 * wraps. So the page is filled from both directions: its first half continues
 * rightward out of column 31, its second half leads back into column 0. The
 * middle is never visible at any supported margin (96 px = 12 tiles) and is
 * filled anyway, rather than left as tile $0000. */
/* One line per CHANGE of decision, with the frame number.
 *
 * The version this replaces printed the first few frames, keyed per screen
 * index. That tells you what a screen settled on and nothing about how it got
 * there, which is backwards for faults that only happen during a transition --
 * and worse, a decline printed once per screen, so an alternation between
 * widening and declining looked identical to one settled decision. It hid the
 * very thing it was there to find. */
static void wood_trace(const char *what, int layer, unsigned src) {
  if (!getenv("SC_WS_DIAG")) return;
  static char last[128];
  char now[128];
  snprintf(now, sizeof now, "%s $14=%02x mode=%d BG%d src=%04x en=%02x/%02x",
           what, g_ram[0x14], g_ppu ? PPU_mode(g_ppu) : -1, layer + 1, src,
           g_ppu ? g_ppu->screenEnabled[0] : 0,
           g_ppu ? g_ppu->screenEnabled[1] : 0);
  if (!strcmp(now, last)) return;
  snprintf(last, sizeof last, "%s", now);
  fprintf(stderr, "[wood] f=%llu %s\n", (unsigned long long)s_frames, now);
}

/* The tile to grow a row's margin from, at one end.
 *
 * The row's own end when it can be trusted; otherwise the nearest row at the
 * same vertical phase that can be, with the OWN end's palette and flip bits
 * kept. That last part is not cosmetic: below the fax machine, rows 28..30
 * carry the desk in palette 1 while the rows they share a phase with use
 * palette 0, so taking the borrowed tile whole would have painted three rows
 * of margin in the wrong colours. */
static uint16_t wood_end(const uint16_t *page0, int r, int c, int step, int vp) {
  const uint16_t own = page0[r * 32 + c];
  if (wood_run_at(page0, r, c, step)) return own;
  for (int k = r % vp; k < 32; k += vp) {
    if (!wood_run_at(page0, k, c, step)) continue;
    const uint16_t v = page0[k * 32 + c];
    return wood_tile(own) ? (uint16_t)((v & 0x3ffu) | (own & ~0x3ffu)) : v;
  }
  return own;
}

/* The wood that belongs beyond each edge, one row at a time.
 *
 * Each side is grown from its own end, because the two are not always the same
 * cycle: the menu's row 0 runs $020..$02f and then $060..$06f, so the left edge
 * continues out of $020 and the right out of $06f, and using one for both puts
 * a visible step in the grain at one seam.
 *
 * Each end is read from a row at its OWN vertical phase -- r, or failing that
 * the nearest row vp apart that still has a clean run there. Deriving the
 * right end from the left instead is wrong on exactly the rows that look like
 * the menu's: map select rows 8..11, 16..19 and 24..27 hold $020 at column 0
 * and $06f at column 31, two different rows of the sheet side by side, so
 * growing 31 steps from $020 gives $02f and the right margin comes out a
 * quarter of the sheet off. Rows 0, 8, 16 and 24 all carry $06f, which is what
 * the phase search finds.
 *
 * A row with no usable end anywhere at its phase falls back to row 0. On the
 * fax that never happens; rows 20..30 are solid machine, but every one of them
 * shares a phase with a clean row above. */
static void wood_fill_page1(const uint16_t *page0, uint16_t *page1) {
  const int vp = wood_vperiod(page0);
  for (int r = 0; r < 32; r++) {
    /* The row's OWN end first, always. Going straight to the phase search
     * breaks View Mode, whose desk is drawn in isometric: each row is offset
     * by a tile from the one above, so no vertical period describes it and any
     * other row is the wrong answer even at the right phase. Its rows all have
     * clean ends, so they never reach the search. */
    const uint16_t l  = wood_end(page0, r, 0, 1, vp);
    const uint16_t rt = wood_end(page0, r, 31, -1, vp);
    for (int c = 0; c < 32; c++)
      page1[r * 32 + c] = (c < 16) ? wood_grow(rt, c + 1) : wood_grow(l, c - 32);
  }
}

/* Widen a wood-backed screen by lending its layer a wood-only map.
 *
 * The margins already come from a pass that renders one layer on its own, so
 * for the length of that pass the layer's map is replaced with a copy carrying
 * nothing but wood -- no menu box, no fax machine, no city, no furniture --
 * and put back before the picture proper is drawn. A 32-column map wrapping at
 * 256 px is seamless here because the sheet's period, 16 tiles, divides 32,
 * and the wood arrives through the real PPU with the real palette and
 * brightness. Costs two 2KB copies per scanline and NOT ONE BYTE of VRAM.
 *
 * It replaces a version that relocated the map to a spare 64-column region,
 * and the reason is worth keeping. Choosing that region can only test the
 * layers enabled RIGHT NOW, so a page belonging to a screen the player is not
 * currently on looks free -- and these screens share VRAM. From a play log:
 *
 *   frame=3220 $14=09 BG3 src=5800 dst=5000
 *   frame=3270 $14=07 BG3 src=5400 dst=5800
 *   frame=3312 $14=07 BG3 src=5000 dst=5800
 *
 * Screen 09 wrote its copy over $5000, which is screen 07's own map; screen 07
 * wrote over $5800, which is screen 09's. Each then had to be reloaded by the
 * game on the way back, and until it was, the wrong tiles were on screen.
 * Reported from play as the wood flickering for seconds after a screen change,
 * and no amount of latching or settling could fix it, because the destination
 * was never really free. Writing nothing at all cannot collide with anything,
 * and it retires the whole risk of scrambling VRAM along with it. */
/* Do the columns a margin will read from a WIDE map contain nothing?
 *
 * Returns true if either side's eight tile columns are entirely blank, which
 * is the case the "already reaches the margins" shortcut got wrong. */
static bool wood_wide_margin_blank(int L) {
  if (!g_ppu) return false;
  const unsigned m = (unsigned)PPU_bgTilemapAdr(g_ppu, L);
  if (m + 0x800u > 0x8000u) return false;      /* both pages must be in VRAM */
  const int firstcol = (int)(((unsigned)g_ppu->hScroll[L] & 0x1ffu) >> 3);
  int lblank = 0, rblank = 0;
  for (int i = 1; i <= 8; i++) {
    const int lc = (firstcol - i) & 63;
    const int rc = (firstcol + 32 + i - 1) & 63;
    const unsigned lo = m + (unsigned)(lc & 31) + ((lc & 32) ? 0x400u : 0u);
    const unsigned ro = m + (unsigned)(rc & 31) + ((rc & 32) ? 0x400u : 0u);
    if ((g_ppu->vram[lo] & 0x3ffu) == 0) lblank++;
    if ((g_ppu->vram[ro] & 0x3ffu) == 0) rblank++;
  }
  return lblank == 8 || rblank == 8;
}

static void widen_wood_bg(void) {
  s_bg3_widened = false;
  s_wood_widened = false;
  s_wood_pass_layer = -1;
  if (!g_ppu || s_ws_extra <= 0 || !s_ws_widen_menu) return;

  /* Find the wood on WHICHEVER layer is carrying it.
   *
   * It is not always BG3. The menu, both faxes and View Mode put it there, but
   * map select and name entry -- the two screens whose margin wood was
   * reported wrong longest -- can have it elsewhere, and keying on BG3
   * declined on exactly the screens that needed this most.
   *
   * A 32x64 map is two pages and a 64x64 one is four, so those are skipped
   * rather than guessed at; a 64-column layer already reaches the margins. */
  int layer = -1;
  unsigned src = 0;
  bool wide_patch = false;   /* wide map, blank margin columns to fill in */
  for (int L = 0; L < 4 && layer < 0; L++) {
    /* SC_WS_DIAG prints WHY each layer was passed over. "no-wood-layer" on its
     * own says only that nothing qualified, which is the least useful thing it
     * could say on a screen whose margins are wrong. */
    const char *why = NULL;
    if (!((g_ppu->screenEnabled[0] >> L) & 1) &&
        !((g_ppu->screenEnabled[1] >> L) & 1)) why = "disabled";
    else if (PPU_bgTilemapWider(g_ppu, L)) {
      why = "wide-map";
      /* "A 64-column layer already reaches the margins" -- true only if the
       * columns it reaches them WITH are drawn. Measured on the screen that
       * reported a missing left margin: BG1 is 64 columns, the window sits at
       * column 0, so the right margin reads columns 32..39 (real content on
       * page 1) and the left wraps to 56..63, which are entirely blank. The
       * assumption held for one side and failed for the other, which is
       * exactly what "wood on the right, black on the left" looks like.
       *
       * So a wide layer is no longer skipped outright. If the columns a margin
       * will read are blank, it gets the same page-1 stand-in the 32-column
       * case gets -- patched over the blank columns only, so the side that
       * already works is left alone. */
      /* AND IT MUST ACTUALLY BE WOOD. The first version of this checked only
       * that the margin columns were blank and took the layer on that alone,
       * which is not a test for wood at all -- it accepted the title screen's
       * scrolling background and grew "wood" out of its tiles, breaking the
       * title badly. wood_map_ok() is the check the 32-column path has always
       * applied; the wide path needs it just as much. */
      { const unsigned m = (unsigned)PPU_bgTilemapAdr(g_ppu, L);
        if (m + 0x800u > 0x8000u)              why = "wide-pages-off-vram";
        else if (!wood_map_ok(&g_ppu->vram[m])) why = "wide-map-not-woodlike";
        else if (!wood_wide_margin_blank(L))    why = "wide-margins-already-drawn";
        else { why = NULL; layer = L; src = m; wide_patch = true; } }
      /* The claim being tested: "a 64-column layer already reaches the
       * margins". Print what the margins would actually READ from it -- the
       * eight tile columns either side of the 32-column window -- because a
       * wide map whose extra columns are blank reaches them with nothing. */
      if (getenv("SC_WS_DIAG")) {
        static int said[4];
        if (!said[L]) {
          said[L] = 1;
          const unsigned m = (unsigned)PPU_bgTilemapAdr(g_ppu, L);
          const unsigned hofs = (unsigned)g_ppu->hScroll[L] & 0x1ffu;
          const int firstcol = (int)(hofs >> 3);
          int lblank = 0, rblank = 0;
          for (int i = 1; i <= 8; i++) {
            const int lc = (firstcol - i) & 63;
            const int rc = (firstcol + 32 + i - 1) & 63;
            /* page 1 lives 0x400 words on for columns 32..63 */
            const unsigned lo = m + (unsigned)((lc & 31)) + ((lc & 32) ? 0x400u : 0u);
            const unsigned ro = m + (unsigned)((rc & 31)) + ((rc & 32) ? 0x400u : 0u);
            if ((g_ppu->vram[lo] & 0x3ff) == 0) lblank++;
            if ((g_ppu->vram[ro] & 0x3ff) == 0) rblank++;
          }
          fprintf(stderr, "[wood] BG%d wide: hofs=%u firstcol=%d  "
                          "left 8 cols blank=%d/8, right 8 cols blank=%d/8\n",
                  L + 1, hofs, firstcol, lblank, rblank);
        }
      }
    }
    else if (g_ppu->bgXsc[L] & 0x02u)      why = "bgXsc-wide";
    else {
      const unsigned m = (unsigned)PPU_bgTilemapAdr(g_ppu, L);
      if (m + 0x400u > 0x8000u)            why = "map-off-vram";
      else if (!wood_map_ok(&g_ppu->vram[m])) why = "map-not-woodlike";
      else { layer = L; src = m; }
    }
    if (why && getenv("SC_WS_DIAG")) {
      static char seen[4][32];
      if (strcmp(seen[L], why)) {
        snprintf(seen[L], sizeof seen[L], "%s", why);
        fprintf(stderr, "[wood] BG%d skipped: %s (map=%04x)\n",
                L + 1, why, (unsigned)PPU_bgTilemapAdr(g_ppu, L));
      }
    }
  }
  if (layer < 0) { wood_trace("no-wood-layer", -1, 0); return; }

  /* The stand-in is exactly page 1 of the 64-column map this used to build.
   *
   * On a 64-column map the right margin reads columns 32.. and the left reads
   * 63, 62, ... downward; on a 32-column map that wraps, the right margin
   * reads columns 0, 1, ... and the left reads 31, 30, ... -- the same cells
   * in the same order. So the fill that was right for page 1 is right here,
   * and both seams keep the screen's own phase. */
  /* Rebuild only from a map that held still since last frame.
   *
   * Opening the scenario menu, the margins scrambled briefly while the screen
   * was still fading and then came right -- reported from play. The map is
   * being DMAd in over several frames, so a rebuild caught mid-write reads
   * half-written rows, and the phase grown from them is nonsense. Sampling the
   * anchors and requiring them to match the previous frame costs 32 compares
   * and defers the rebuild by one frame; until then the previous screen's wood
   * stays up, which during a fade is exactly what it should do. */
  { static uint16_t anchor[32];
    static unsigned anchor_src = ~0u;
    bool steady = anchor_src == src;
    for (int r = 0; r < 32; r++) {
      const uint16_t a = g_ppu->vram[src + (unsigned)r * 32u];
      if (a != anchor[r]) steady = false;
      anchor[r] = a;
    }
    anchor_src = src;
    if (steady || !s_wood_pass_ready) {
      wood_fill_page1(&g_ppu->vram[src], s_wood_pass_map);
      if (wide_patch) {
        /* Page 1 is REAL here and one margin is already reading it correctly.
         * Keep every column that has something in it and take only the blank
         * ones from the grown wood, or the working side breaks while fixing
         * the other. */
        const uint16_t *real1 = &g_ppu->vram[src + 0x400u];
        for (int r = 0; r < 32; r++)
          for (int c = 0; c < 32; c++) {
            const uint16_t v = real1[r * 32 + c];
            if ((v & 0x3ffu) != 0) s_wood_pass_map[r * 32 + c] = v;
          }
      }
      s_wood_pass_ready = true;
    } }
  s_wood_pass_layer = layer;
  /* The margins of a wide map read PAGE 1, so that is the page to stand in
   * for; a 32-column map wraps and reads page 0. */
  s_wood_pass_src = wide_patch ? src + 0x400u : src;
  s_ws_bg_margins = true;
  s_ws_margin_layer = layer;
  if (layer == 2) s_bg3_widened = true;   /* BG3 is clamped independently */
  s_wood_widened = true;
  wood_trace("wood-pass", layer, src);
}

/* Extend the selector background in the TILEMAP, not in the framebuffer.
 *
 * BG1 is the only layer on the main screen for this screen (measured:
 * mode 0, BG1 map $3000 wide=1 chr $0000, BG2/3/4 off), and wide=1 means the
 * tilemap is 64 tiles across -- two 32x32 pages, the second at map+$400 words.
 * The shipped screen fills columns 0..44 and leaves 45..63 as tile $0000,
 * which is why scrolling the ninth column into view showed black.
 *
 * The extension is grown from the live tilemap with wood_grow(), the same rule
 * the menu and the faxes use: a row of the wood sheet is sixteen consecutive
 * tiles, so walking sideways walks the low four bits and wraps them.
 *
 * This replaced a hardcoded 4-wide by 8-tall block read off columns 41..44.
 * There is no 4-wide repeat -- reported from play as the selector's wood being
 * the one still worth optimising -- and the loop was plain to see: row 0 ran
 * $029 $02a $02b $02c $029 $02a... where the sheet continues $02d $02e $02f
 * $020.
 *
 * Column 41 is the anchor, and it has to be. Column 44 would be the natural
 * choice as the last column the shipped screen fills, but with SC_NINTH the
 * Sylt card occupies columns 42..49 on rows 5..13, so 44 is card art on a
 * third of the screen. 41 is wood on every row, which is presumably why the
 * old table was read from there too. */

/* Written every frame the selector runs: the screen's own setup DMA lands
 * before this and would otherwise put the blank tiles back. sylt_place_card()
 * runs after it, so the card is laid back over columns 42..49. */
static void selector_extend_tilemap(void) {
  if (!g_ppu) return;
  ScPpuVramChanged();
  const unsigned map = (unsigned)PPU_bgTilemapAdr(g_ppu, 0);   /* BG1, in words */
  if (map + 0x800u > 0x8000u) return;
  /* Remember each row's anchor, and keep using the last good one while the
   * screen is loading.
   *
   * Arriving at the selector, column 41 has not been written yet on the first
   * frames, so the row was skipped and its columns 45..63 stayed as the blank
   * tiles the shipped map holds -- reported from play as the left wood
   * flickering briefly on the way in. Which end it shows up at is not a
   * coincidence: BG1 is 64 columns and the left margin reads the map's far
   * end, columns 60..63, so the columns this fills are exactly the ones the
   * left margin shows. */
  static uint16_t held[32];
  static unsigned held_map = ~0u;
  if (held_map != map) { held_map = map; memset(held, 0, sizeof held); }
  for (int row = 0; row < 32; row++) {
    /* Columns 32..63 live in the second 32x32 page, at map + $400 words. */
    const unsigned page1 = map + 0x400u + (unsigned)row * 32u;
    const uint16_t live = g_ppu->vram[page1 + (41u - 32u)];
    if (wood_tile(live)) held[row] = live;
    const uint16_t anchor = held[row];
    if (!wood_tile(anchor)) continue;      /* never had one -- leave the row */
    for (int col = 45; col < 64; col++)
      g_ppu->vram[page1 + (unsigned)(col - 32)] = wood_grow(anchor, col - 41);
  }
}

/* SC_HOST_MAP_DUMP=<file>: render the map host-side and write it as a PPM,
 * at whatever frame SC_DUMP_AT names.
 *
 * A verification hook, not a feature. tools/render_map.py was validated
 * against real play first; dumping the C port the same way lets the two be
 * diffed, so the port is checked against a known-good implementation rather
 * than only against itself. Nothing here touches presentation yet. */
/* SC_HOST_MAP=1: draw the map host-side and composite the game's own HUD and
 * sprites back on top.
 *
 * BG3 and OBJ are captured into transparent ARGB surfaces via the overlay
 * export (snesrecomp/docs/HOST_OVERLAY_EXTRACTION.md, ported from the ActRaiser
 * fork) with RemoveFromGame, so the guest frame comes out carrying only the
 * layers we are replacing. Real alpha, not a black key.
 *
 * The guest still computes everything -- this only draws the map differently,
 * which is the state/presentation line docs/PLAN_renderer.md sets out.
 *
 * Opt-in: with nothing bound and no capture configured the export is a
 * documented no-op, so the default build stays byte-identical. */
static uint8_t *s_ov_bg3, *s_ov_obj;
/* HUD-only pass: BG3 + OBJ rendered into their own buffer, independent of the
 * overlay export. The export arms cleanly and reports success but writes no
 * pixels, and every check on this side came back correct, so this route stops
 * depending on it entirely -- it needs nothing from the runner but the public
 * layer mask and a retargeted PpuBeginDrawing. */
static uint32_t s_backdrop_argb;
static int s_ov_pitch;

static void host_map_init(void) {
  if (!s_host_map || !g_ppu || s_ov_bg3) return;
  /* Pitch MUST match the render width, not the maximum allocation.
   *
   * PpuWriteOverlayRenderLine centres the authentic 256-wide capture inside
   * whatever surface it is given:
   *
   *     width         = pitch / 4
   *     texture_extra = max((width - 256) / 2, 0)
   *     dst[x + texture_extra] = ...
   *
   * A 448-wide surface therefore receives the HUD at columns 96..351 while a
   * composite reading from column 0 sees only the transparent left margin.
   * That is exactly why both surfaces came back with zero non-transparent
   * pixels while binding and arming reported success. */
  s_ov_pitch = s_video_pitch;
  s_hud_pixels = (uint8_t *)calloc((size_t)s_video_pitch, kVideoHeight);
  s_guest_pixels = (uint8_t *)calloc((size_t)s_video_pitch, kVideoHeight);
  s_hostmap_pitch = (s_video_w + 16) * 4;
  s_hostmap_px = (uint8_t *)calloc((size_t)s_hostmap_pitch, kVideoHeight + 16);
  s_ov_bg3 = (uint8_t *)calloc((size_t)s_ov_pitch, kVideoHeight);
  s_ov_obj = (uint8_t *)calloc((size_t)s_ov_pitch, kVideoHeight);
  if (!s_ov_bg3 || !s_ov_obj) { s_host_map = false; return; }
  PpuClearOverlayBindings(g_ppu);
  bool a = PpuBindOverlaySurface(g_ppu, kPpuOverlaySource_Bg3, s_ov_bg3, (size_t)s_ov_pitch);
  bool b = PpuBindOverlaySurface(g_ppu, kPpuOverlaySource_Obj, s_ov_obj, (size_t)s_ov_pitch);
  fprintf(stderr, "host map: overlay bind bg3=%d obj=%d, bgmode=%d\n",
          (int)a, (int)b, (int)PPU_mode(g_ppu));
}

/* Per frame, before any line renders -- a deliberate no-op.
 *
 * It used to arm the runner's overlay export for BG3 and OBJ with
 * kPpuOverlayFlag_RemoveFromGame, which takes those layers OUT of the game's
 * own render. BG3 does come back correctly -- the surface reports exactly the
 * pixels BG3 drew -- but OBJ never does (upstream #31), so arming it simply
 * deletes the sprites from the picture.
 *
 * Nothing needs it now. The guest's own 256 columns are kept verbatim below,
 * so there is nothing to take apart and reassemble. */
static void host_map_arm_captures(void) {
  if (!s_host_map || !g_ppu || !s_ov_bg3) return;
  if (!host_map_screen_live()) return;
  PpuClearOverlayCaptures(g_ppu);
}

/* True only when a city actually exists to draw.
 *
 * $14 == 0 is the city view -- but it is ALSO the state the machine sits in
 * for the first hundred-odd frames of a cold boot, before the title appears,
 * with no map in WRAM at all. Gating on $14 alone therefore drew the map over
 * the boot screen from whatever happened to be in VRAM and WRAM: reported from
 * play as the first frames showing garbage.
 *
 * $003e is the mode byte and is 0 until a game is running (1 practice, 2 free
 * play, 3 scenario). Measured at boot: $14 = 00 and $3e = 0 through frame 110+,
 * while savestates 7, 8 and 9 -- real city views -- all have $3e = 1. */
static bool host_map_screen_live(void) {
  if (g_ram[0x14] != 0x00 || (g_ram[0x3e] | (g_ram[0x3f] << 8)) == 0) return false;
  if (!g_ppu) return false;
  /* $14 == 0 is not only the city view. The tax, evaluation, overview and
   * history pages all report it, and so does View Mode -- View Mode with the
   * SAME enable bits and the SAME three map bases as the city view, so no
   * register separates those two at all.
   *
   * Two further tests do. BG2 is the map and the four menu pages do not enable
   * it, which excludes them; and View Mode is the one carrying the wooden
   * desk, which widen_wood_bg() has already found by reading the tilemap.
   * Without both, the host map painted terrain across all five. */
  if (!(((g_ppu->screenEnabled[0] | g_ppu->screenEnabled[1]) >> 1) & 1))
    return false;                       /* BG2 = the map, on either screen */
  /* The bank/loan screen is a full-screen art scene, not the city.
   *
   * It reports $14 == 0 with BG2 enabled on the SUBSCREEN, so the test above
   * passed it and the host map painted city terrain into both margins --
   * reported from play as the loan view being broken in widescreen. The
   * margin blank could not clean up after it either, since that is skipped
   * whenever this function says yes.
   *
   * Enable bits separate the three cases that reach here:
   *
   *   city    main=17 (BG1|BG2|BG3|OBJ)  sub=04    BG2 on MAIN
   *   advice  main=14 (BG3|OBJ)          sub=03    BG2 sub, BG1 sub
   *   loan    main=15 (BG1|BG3|OBJ)      sub=02    BG2 sub, BG1 MAIN
   *
   * So: the map only on the subscreen while BG1 holds the main screen means
   * the picture belongs to that BG1 scene, and the city is merely showing
   * through colour math. Testing "BG2 on main" instead would have caught the
   * loan screen too, and would also have dropped the advice page, whose
   * dimmed city in the margins is wanted. */
  if (!((g_ppu->screenEnabled[0] >> 1) & 1) &&
      ((g_ppu->screenEnabled[0] >> 0) & 1))
    return false;                       /* BG1 scene over a subscreen map */
  if (s_wood_widened) return false;     /* View Mode */
  return true;
}

/* After the guest frame: replace the picture with our map, then put the
 * captured HUD and sprites back over it using their real alpha. */
/* Passe-partout: never show the guest's outermost tile column.
 *
 * Every seam chased in this file lives in exactly those 8 px at each side.
 * The trailing one is the column the game rewrites while it is still on screen
 * behind you; the leading one is the column that becomes visible before the
 * game rewrites it. Both are the same geometry: a 32-column tilemap is 256 px
 * against a 256 px screen, so one column has to serve both edges at once and
 * cannot.
 *
 * On hardware those columns sat in CRT overscan and were never seen -- the game
 * is built on that assumption. So rather than repair them frame by frame, do
 * not display them: the host map, which draws the same terrain from WRAM,
 * covers the outermost column on each side permanently.
 *
 * This removes the fault by construction, and with it the whole repair
 * mechanism -- direction tracking, hold counters, staleness bookkeeping -- and
 * the cloned cursor and HUD that mechanism caused, which came from translating
 * composed pixels that included screen-fixed layers.
 *
 * Measured first: every edge of the guest picture is live map, not HUD. While
 * the map scrolls, columns 0-15 change 60-89%% (the toolbar starts at x~16),
 * columns 240-255 change 34-59%%, and the top and bottom rows change too -- the
 * status bar is a panel inside the picture, not a band across the edge. An 8 px
 * crop therefore takes map pixels only and clips no HUD anywhere.
 *
 * SC_PASSEPARTOUT=0 restores the guest's own edge columns and re-enables the
 * per-frame repair. */
enum { kPassePartout = 8 };
static bool ws_passepartout(void) {
  static int on = -1;
  if (on < 0) {
    const char *e = getenv("SC_PASSEPARTOUT");
    on = (e && *e) ? (atoi(e) != 0) : 1;
  }
  return on != 0;
}

/* Repair the scroll seam.
 *
 * The map tilemap is 32 columns -- 256 px, exactly the screen width -- and
 * serves as a circular buffer over a city far larger than it. Scrolling has to
 * rewrite the column about to appear at the LEADING edge, and because 32
 * columns wrap onto themselves that very column is still on screen at the
 * TRAILING edge. The incoming content therefore flashes in at the far side,
 * once per tile column of scroll: about fifteen times a second at the normal
 * 2 px/frame, and it reads as content from the opposite edge.
 *
 * Measured on the city view scrolling right: the leftmost 8 px mismatch a
 * correctly-scrolled previous frame by 64-88% while the middle of the screen
 * mismatches by 0.0%. Isolating layers puts it entirely on BG2 (87.8%); BG1
 * measures 4.6% because the game windows BG1 to x 0..247, masking its own copy
 * of the same artifact. BG2 carries no window at all.
 *
 * No VRAM trick can fix it -- one column must serve both edges in the same
 * frame with different content -- so the composed picture is patched instead.
 * Between frames the map is a rigid translation by the scroll delta, and the
 * previous frame held the correct content for that sliver, so prev[x + dx] is
 * exactly it. Patched only on frames where the trailing column really was
 * rewritten, which keeps a sprite sitting at the edge from smearing on every
 * frame that merely scrolls. */
static uint8_t *s_seam_prev;
static size_t s_seam_prev_size;
static uint16_t s_seam_map[0x400];
static bool s_seam_have_prev;
static int s_seam_hs_prev, s_seam_vs_prev;
/* How many pixels of the rewritten column are still on screen at the
 * trailing edge. The repair has to continue until that column has fully
 * scrolled off, otherwise the picture simply snaps to the new content one
 * frame later and the seam reappears displaced rather than removed. */
static int s_seam_hold_x, s_seam_hold_y;
/* Which way the map was last travelling, so a paused frame still knows
 * which edge is trailing, and how long it has been still. */
static int s_seam_dir_x = 1, s_seam_dir_y = 1, s_seam_idle_x, s_seam_idle_y;
/* The LEADING edge is a separate fault from the trailing one this file
 * mostly deals with. Scrolling right, a tile column becomes visible at the
 * right BEFORE the game rewrites it, so for two or three frames it still
 * holds the wrapped content from 256 px away -- measured in a play capture
 * as a spike on the right 16 px every fourth frame (one tile column at
 * 2 px/frame), 72-79%% 'correctly scrolled' against 84-86%% on quiet frames.
 *
 * It cannot be repaired from history the way the trailing edge is: the
 * correct pixels do not exist yet anywhere, because the game has not
 * written them. The host map has that terrain from WRAM, so the strip is
 * started a few pixels early to cover the sliver while it is wrong.
 *
 * On hardware this sliver sat in CRT overscan and was never seen; widescreen
 * put the guest's right edge in the middle of the picture, next to the join,
 * which is why it reads as a defect now. */
static int s_seam_lead_col = -1;
static bool s_seam_lead_dirty;
static int s_hostmap_adj_x, s_hostmap_adj_y;  /* mirrors, for SC_COMPOSE_DIAG */
static inline uint32_t sc_ext_sub(uint32_t c, int sr, int sg, int sb) {
  if (!(sr | sg | sb)) return c;
  int r = (int)((c >> 16) & 0xff) - sr; if (r < 0) r = 0;
  int g = (int)((c >> 8) & 0xff) - sg;  if (g < 0) g = 0;
  int b = (int)(c & 0xff) - sb;         if (b < 0) b = 0;
  return (c & 0xff000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
/* The margin's dimming, for sprites drawn into it after the terrain. */
typedef struct { bool halve; int r, g, b; } ScVehicleDim;
static uint32_t sc_vehicle_shade(uint32_t c, void *ctx) {
  const ScVehicleDim *d = (const ScVehicleDim *)ctx;
  if (d->halve) return (c & 0xFF000000u) | ((c >> 1) & 0x007F7F7Fu);
  return sc_ext_sub(c, d->r, d->g, d->b);
}
static int s_seam_lead_cover;      /* right edge  */
static int s_seam_lead_left;       /* left edge   */
static int s_seam_lead_top;        /* top edge    */
static int s_seam_lead_row = -1;
static bool s_seam_lead_rdirty;

static void ws_fix_scroll_seam(void) {
  /* With the passe-partout on BOTH edges there is nothing left to repair, and
   * the repair is what ghosts the HUD: it translates COMPOSED pixels, so any
   * screen-fixed layer inside its 16 columns is dragged along a frame behind.
   * That is the same root cause as the cloned cursor, and it is why this stands
   * down entirely rather than being narrowed again.
   *
   * There is no quality cost. An earlier note here claimed the left cover
   * mismatched a translated previous frame by 11-34%% where the repair was
   * exact; that was a flaw in the measurement, not the picture. The band test
   * shifted an 8 px window by the scroll delta, so it read two GUEST columns
   * and compared them against host content. Measured with the window kept
   * inside the cover, both edges are 100.0%% on every frame. */
  if (ws_passepartout()) {
    s_seam_lead_cover = 0;
    s_seam_lead_left = 0;
    s_seam_lead_top = 0;
    s_seam_hold_x = 0;
    s_seam_hold_y = 0;
    return;
  }
  static int enabled = -1;
  if (enabled < 0) {
    const char *e = getenv("SC_SEAM_FIX");
    enabled = (e && *e) ? (atoi(e) != 0) : 1;
  }
  if (!enabled || !s_video_pixels || !g_ppu) return;

  const size_t need = (size_t)s_video_pitch * kVideoHeight;

  /* Off the city view the history is meaningless -- drop it so returning to
   * the map cannot patch from a menu's pixels. */
  if (!host_map_screen_live()) {
    s_seam_have_prev = false;
    s_seam_lead_cover = 0;
    s_seam_lead_left = 0;
    s_seam_lead_top = 0;
    s_seam_lead_col = -1;
    s_seam_lead_row = -1;
    return;
  }

  if (s_seam_prev_size != need) {
    free(s_seam_prev);
    s_seam_prev = (uint8_t *)malloc(need);
    s_seam_prev_size = s_seam_prev ? need : 0;
    s_seam_have_prev = false;
  }
  if (!s_seam_prev) return;

  const int hs = g_ppu->hScroll[1] & 0x3ff;
  const int vs = g_ppu->vScroll[1] & 0x3ff;

  /* Which tilemap entries changed since the last frame. */
  const unsigned m = (unsigned)PPU_bgTilemapAdr(g_ppu, 1);
  bool col_changed[32] = {false}, row_changed[32] = {false};
  for (unsigned i = 0; i < 0x400u; i++) {
    const uint16_t v = g_ppu->vram[(m + i) & 0x7fffu];
    if (v != s_seam_map[i]) { col_changed[i & 31] = true; row_changed[i >> 5] = true; }
    s_seam_map[i] = v;
  }

  if (s_seam_have_prev) {
    /* Scroll registers are 10-bit and the map wraps at 256; keep the delta
     * small and signed so a wrap does not read as a huge jump. */
    int dx = ((hs - s_seam_hs_prev) & 0xff), dy = ((vs - s_seam_vs_prev) & 0xff);
    if (dx > 128) dx -= 256;
    if (dy > 128) dy -= 256;

    /* Is the column now at the LEADING edge still holding wrapped content?
     * It goes suspect the moment it becomes the leading column and is cleared
     * the moment the game rewrites it. Which edge leads depends on the
     * direction of travel: scrolling right it is the right edge, scrolling
     * left the left one. Same again for the rows, vertically. */
    { const int lc = dx > 0 ? (((hs + kVideoWidth - 1) >> 3) & 31)
                            : ((hs >> 3) & 31);
      if (lc != s_seam_lead_col) { s_seam_lead_col = lc; s_seam_lead_dirty = true; }
      if (col_changed[lc]) s_seam_lead_dirty = false;
      s_seam_lead_cover = (dx > 0 && s_seam_lead_dirty)
                              ? (((hs + kVideoWidth - 1) & 7) + 1) : 0;
      s_seam_lead_left = (dx < 0 && s_seam_lead_dirty)
                              ? (8 - (hs & 7)) : 0; }
    { const int lr = dy > 0 ? (((vs + kVideoHeight - 1) >> 3) & 31)
                            : ((vs >> 3) & 31);
      if (lr != s_seam_lead_row) { s_seam_lead_row = lr; s_seam_lead_rdirty = true; }
      if (row_changed[lr]) s_seam_lead_rdirty = false;
      s_seam_lead_top = (dy < 0 && s_seam_lead_rdirty)
                            ? (8 - (vs & 7)) : 0; }

    const int gx0 = s_ws_extra;              /* guest's left edge, render coords */
    const int gx1 = gx0 + kVideoWidth;

    /* Horizontal: trailing edge is the side the content is leaving by.
     *
     * A frame with no movement must NOT abandon the repair. Panning by pushing
     * the cursor against the edge starts and stops constantly, so dx==0 frames
     * are common mid-scroll; clearing the hold on one let the stale column pop
     * straight back into view. Reported from play as the seam still being heavy
     * when panning by cursor and on the diagonal. With no movement the strip
     * simply holds its previous pixels -- dx is 0, so nothing translates and
     * nothing decrements. */
    if (dx != 0) { s_seam_dir_x = dx > 0 ? 1 : -1; s_seam_idle_x = 0; }
    else if (++s_seam_idle_x > 12) s_seam_hold_x = 0;
    if (dx > -8 && dx < 8) {
      /* Check BOTH edge columns, not just the trailing one. The two coincide
       * only when the scroll sits off a tile boundary; exactly at a boundary
       * they differ by one, and testing the wrong one missed the rewrite --
       * measured as a 4% residual on the right edge when scrolling left. */
      const int left_col = (hs >> 3) & 31;
      const int right_col = ((hs + kVideoWidth - 1) >> 3) & 31;
      /* Sixteen pixels, not eight. The game rewrites TWO tilemap columns per
       * update ("wrote: 6 7" in the per-frame trace), and both land inside the
       * trailing sliver. An 8 px repair left the second column showing through:
       * measured x0-7 at 0% but x8-15 still at 56%, which is why the seam was
       * still plainly visible in play after the first attempt. */
      /* Only ARM a repair near actual movement. Letting dx==0 arm one meant
       * ordinary map animation armed it on a still screen. */
      if ((dx != 0 || s_seam_idle_x <= 3) &&
          (col_changed[left_col] || col_changed[right_col])) s_seam_hold_x = 16;
      if (s_seam_hold_x > 0) {
        const int wdt = s_seam_hold_x;
        const int x0 = s_seam_dir_x > 0 ? gx0 : gx1 - wdt;
        const int x1 = s_seam_dir_x > 0 ? gx0 + wdt : gx1;
        s_seam_hold_x -= dx > 0 ? dx : -dx;
        if (s_seam_hold_x < 0) s_seam_hold_x = 0;
        for (int y = 0; y < kVideoHeight; y++) {
          /* BOTH axes. This block used to translate by dx only, so on a
           * DIAGONAL pan it pulled pixels from the wrong row and the repair
           * itself painted a seam along the top and left -- reported from play
           * as the seam being clearly visible when scrolling right and down. */
          const int sy = y + dy;
          if (sy < 0 || sy >= kVideoHeight) continue;
          uint32_t *dst = (uint32_t *)(s_video_pixels + (size_t)y * s_video_pitch);
          const uint32_t *src =
              (const uint32_t *)(s_seam_prev + (size_t)sy * s_video_pitch);
          for (int x = x0; x < x1; x++) {
            const int sx = x + dx;
            if (sx >= gx0 && sx < gx1) dst[x] = src[sx];
          }
        }
      }
    } else {
      s_seam_hold_x = 0;
    }

    /* Vertical: 32 rows is 256 px against 224 visible, so there is a little
     * slack here that the horizontal axis does not have -- but the game still
     * rewrites a visible row often enough to show the same seam. */
    /* The vertical repair is OFF by default.
     *
     * Unlike the horizontal one, which touches 16 columns of map, this rewrites
     * 16 rows across the WHOLE guest width -- straight through the status bar --
     * so any HUD element in those rows is translated with the map and ghosts a
     * frame behind. Reported from play as HUD elements drawn one frame too slow.
     *
     * And it buys nothing. The tilemap is 32 rows against 224 visible lines, so
     * there are 4 spare rows to stage into and the game rewrites a row before it
     * is exposed: measured over 113 frames of downward scrolling in a play
     * capture, the leading edge is median 0.0%% and worst 12.8%%, against 24-28%%
     * spikes on the horizontal axis where there is no slack at all.
     *
     * SC_SEAM_FIX_V=1 restores it. */
    { static int von = -1;
      if (von < 0) { const char *e = getenv("SC_SEAM_FIX_V");
                     von = (e && *e) ? (atoi(e) != 0) : 0; }
      if (!von) dy = 0, s_seam_hold_y = 0; }
    if (dy != 0) { s_seam_dir_y = dy > 0 ? 1 : -1; s_seam_idle_y = 0; }
    else if (++s_seam_idle_y > 12) s_seam_hold_y = 0;
    if (dy > -8 && dy < 8) {
      /* Symmetric with the horizontal block: sixteen pixels, both edge rows.
       * This axis was left at eight and a single row when the horizontal one
       * was widened -- the bottom seam seen when panning down by cursor. */
      const int top_row = (vs >> 3) & 31;
      const int bot_row = ((vs + kVideoHeight - 1) >> 3) & 31;
      if ((dy != 0 || s_seam_idle_y <= 3) &&
          (row_changed[top_row] || row_changed[bot_row])) s_seam_hold_y = 16;
      if (s_seam_hold_y > 0) {
        const int hgt = s_seam_hold_y;
        const int y0 = s_seam_dir_y > 0 ? 0 : kVideoHeight - hgt;
        const int y1 = s_seam_dir_y > 0 ? hgt : kVideoHeight;
        s_seam_hold_y -= dy > 0 ? dy : -dy;
        if (s_seam_hold_y < 0) s_seam_hold_y = 0;
        for (int y = y0; y < y1; y++) {
          const int sy = y + dy;
          if (sy < 0 || sy >= kVideoHeight) continue;
          uint32_t *dst = (uint32_t *)(s_video_pixels + (size_t)y * s_video_pitch);
          const uint32_t *src =
              (const uint32_t *)(s_seam_prev + (size_t)sy * s_video_pitch);
          for (int x = gx0; x < gx1; x++) {
            const int sx = x + dx;   /* BOTH axes here too */
            if (sx >= gx0 && sx < gx1) dst[x] = src[sx];
          }
        }
      }
    } else {
      s_seam_hold_y = 0;
    }
  }

  memcpy(s_seam_prev, s_video_pixels, need);
  s_seam_have_prev = true;
  s_seam_hs_prev = hs;
  s_seam_vs_prev = vs;
}

static void host_map_compose(void) {
  if (!s_host_map || !s_ov_bg3) return;
  /* Only on the main map screen. $01df is the screen-mode index: 3 is the
   * city view, while 0/1/2 are the menu pages (measured across the save
   * states). Without this the map painted over the scenario select, the
   * disaster page and everything else -- reported from play as "menu broken",
   * and entirely my omission rather than a renderer fault. */
  /* Gate on $14, the screen index -- NOT on $01df.
   *
   * $01df was tried first and is genuinely unreliable: the same city view with
   * a tool palette open has been observed at 3, at 4 and at 0, and gating on it
   * made the map vanish. That note stood for a long time as "no gate is
   * needed", and it was wrong. Without one this draws the city map on EVERY
   * screen, decoding whatever CHR happens to be in VRAM as map tiles -- on the
   * title that is title graphics, and the result is a screenful of scrambled
   * tiles that looks like a failing cartridge. Reported from play, and
   * reproduced exactly by loading the title with SC_HOST_MAP=1.
   *
   * $14 separates cleanly, measured across every save state: the city view and
   * its in-view menus are 00 (savestates 7, 8 and 9, whose $01df differ), while
   * title is 01, main menu 03, map select 05, name entry 07, scenario select
   * 0b and the fax 0f. Boot sits at 01, so nothing draws before a city exists.
   *
   * The reasoning that follows still holds WITHIN the city view: the capture
   * pass takes every layer except BG2, so
   * whatever the guest draws on any other layer covers the map by itself.
   * That is the same property that made in-view menus work without a special
   * case, applied consistently. */
  if (!host_map_screen_live()) return;

  /* Keep the guest's own 256 columns EXACTLY; spend the extra width on host
   * terrain to the right of them.
   *
   * Earlier versions replaced the map inside the authentic picture as well and
   * then tried to key the guest's frame back over the top. That cannot work:
   * the host renderer is a reimplementation and agrees with the guest's own map
   * on 76.6% of pixels at its best alignment. The missing quarter is real
   * content -- the taller roofs that overlap the tile behind them, shoreline
   * decoration -- which a colour key then discards as "same as the map".
   *
   * Nothing has to be recovered if nothing is thrown away. Measured, this
   * leaves the authentic 256 columns 0 pixels different from what the game
   * draws, on every screen.
   *
   * Two details the render depends on:
   *
   *  - NATIVE cell size. This continues a picture the guest draws at 8 px per
   *    cell, so any other zoom draws terrain at the wrong size AND starts from
   *    the wrong cell: a band of mismatched tiles along the join that moves as
   *    you scroll.
   *  - ONE ROW UP. The host render sits a pixel low against the guest's map.
   *    Sweeping the offset, dx 0 / dy +1 scores 91.8% and nothing else comes
   *    within thirty points. ScMapView_Render takes whole cells, so the
   *    correction cannot go through the scroll. */
  if (!s_guest_pixels) return;
  /* Nothing to extend at authentic width. The result is identical either way --
   * the guest's columns are copied back over the whole frame -- so this only
   * skips the wasted render now that the host map is on by default. */
  if (s_ws_extra <= 0) return;
  memcpy(s_guest_pixels, s_video_pixels, (size_t)s_video_pitch * kVideoHeight);

  if (ScMapView_GetCellPx() != 8) ScMapView_SetCellPx(8);
  if (!s_hostmap_px) return;
  int sx = 0, sy = 0;
  ScMapView_GetScroll(&sx, &sy);

  /* SUB-CELL alignment. ScMapView_GetScroll reports whole map cells, but the
   * guest scrolls its map 2 px at a time, so a cell-aligned render only agrees
   * with it every fourth frame and drifts up to 7 px in between -- reported
   * from play as the extension running slightly fast and visibly coming apart
   * for a moment. Measured, the guest's BG2 register follows
   * `cell * 8 + fine (mod 256)` exactly, so the fine part is the correction.
   *
   * The constant row below is separate and not this: every state measured sits
   * at fine (0,0) at rest, yet the host render is still a pixel low. */
  const int fx = g_ppu->hScroll[1] & 7;
  const int fy = g_ppu->vScroll[1] & 7;

  /* Keep the strip's MOVEMENT equal to the guest's, without disturbing where
   * it sits.
   *
   * The coarse cell and the fine offset come from different places:
   * ScMapView_GetScroll reads the game's scroll in city cells, fx/fy above are
   * the PPU's BG2 register. Around a tile boundary the cell can lag the
   * register by a frame, and the strip then snaps a whole tile the wrong way --
   * measured over a 230-frame vertical pan as exactly one frame where the guest
   * moved dy=+4 and the strip moved dy=-4. Reported from play as the extension
   * jumping "one tile away" when panning with A.
   *
   * Forcing the cell to agree with the register outright is NOT the fix: the
   * two carry a standing offset that is perfectly normal, and overriding it
   * moved the terrain on five of ten save states at rest. So correct only the
   * discrepancy in MOTION -- how far the strip would travel this frame versus
   * how far the register actually travelled -- and carry it as whole cells.
   * The adjustment cancels itself once the cell catches up, so at rest it is
   * zero and the picture is untouched. */
  { static int prev_sy, prev_sx, prev_fy, prev_fx, prev_v, prev_h_, have;
    static int still;
    static int adj_x, adj_y;
    const int vpix = g_ppu->vScroll[1] & 0xff, hpix = g_ppu->hScroll[1] & 0xff;
    if (have) {
      int dv = (vpix - prev_v) & 0xff, dh = (hpix - prev_h_) & 0xff;
      if (dv > 128) dv -= 256;
      if (dh > 128) dh -= 256;
      /* Only track ordinary scrolling; a jump means a screen change, not a pan. */
      if (dv > -32 && dv < 32 && dh > -32 && dh < 32) {
        const int moved_y = (sy - prev_sy) * 8 + (fy - prev_fy);
        const int moved_x = (sx - prev_sx) * 8 + (fx - prev_fx);
        if ((dv - moved_y) % 8 == 0) adj_y += (dv - moved_y) / 8;
        if ((dh - moved_x) % 8 == 0) adj_x += (dh - moved_x) / 8;
        /* Bound it. The fault is a one-frame, one-cell lag, so a correction
         * beyond a single cell is not that fault -- it is the two sources
         * tracking differently, and letting it accumulate walked the terrain
         * right off its anchor (85662 pixels adrift on one save state). */
        if (adj_y > 1) adj_y = 1; else if (adj_y < -1) adj_y = -1;
        if (adj_x > 1) adj_x = 1; else if (adj_x < -1) adj_x = -1;
        /* Return to zero once the map genuinely stops.
         *
         * The comment above says "at rest it is zero", and that was simply
         * not true: nothing drove the adjustment back. It was cleared only
         * by a JUMP (>=32 px), so any standing discrepancy the correction
         * picked up stayed forever. Measured on savestate_6: the disaster
         * camera pans the view (sy 61 -> 71), adj_y latches to -1, and stays
         * -1 for 350+ frames with dv, dh, fx and fy all zero. Reported from
         * play as the extension sitting one tile off after the disaster cam
         * moves the map -- and ONLY after that, which is exactly the
         * signature of a latch rather than a tracking error.
         *
         * The correction exists for a ONE-FRAME lag between the cell and the
         * register while scrolling. With nothing moving there is no lag to
         * correct, so it must decay. Eight still frames is the threshold
         * because the guest scrolls 2 px at a time and a pan never goes that
         * long without moving -- so this cannot fire mid-pan and undo the
         * lag fix it is there to provide. */
        if (dv == 0 && dh == 0 && sx == prev_sx && sy == prev_sy) {
          if (++still >= 8) { adj_x = 0; adj_y = 0; }
        } else {
          still = 0;
        }
      } else {
        adj_x = adj_y = 0;
      }
    }
    s_hostmap_adj_x = adj_x; s_hostmap_adj_y = adj_y;
    prev_sy = sy; prev_sx = sx; prev_fy = fy; prev_fx = fx;
    prev_v = vpix; prev_h_ = hpix; have = 1;
    { static int use = -1;
      if (use < 0) { const char *e = getenv("SC_HOSTMAP_ADJ");
                     use = (e && *e) ? (*e != '0') : 1; }
      if (use) { sx += adj_x; sy += adj_y; } } }

  /* Dim the extension the same way the guest dims the city behind an overlay.
   *
   * The advisor pages compose backdrop plus subscreen, HALVED (cgadsub $60,
   * cgwsel $02). With the backdrop black that is arithmetically `city / 2`, so
   * the extension is halved too -- derived from the registers, not fitted to
   * the picture. Two earlier attempts to measure a ratio off the frame both
   * made things worse: a mean left the terrain 36% too dark, a median tinted
   * other pages.
   *
   * Only this exact shape. Subtractive math against the subscreen cannot be
   * reproduced here, because the value being subtracted is the subscreen and
   * this code does not have it. */
  /* Advisor pages are LEFT-ALIGNED, like every other screen.
   *
   * Centring them was tried and backed out. Moving the page means moving
   * the guest's whole 256 columns, because at this point panel and city
   * are already one picture -- so the toolbar left the left edge and the
   * margins changed with it. Reported from play, twice: first as the map
   * shifting, then as the widescreen itself shifting.
   *
   * The right fix is to composite the page from its OWN layer, and the
   * layer state says that is exactly how the game draws it: main = $14
   * (BG3 + OBJ, the page) and sub = $03 (BG1 + BG2, the city). The runner
   * even has the machinery -- PpuSetOverlayCapture accepted the capture,
   * armed bg3=1 obj=1.
   *
   * It exports nothing, and cannot. renderFlags reads 8 (NoSpriteLimits);
   * bit 0, NewRenderer, is clear, so ppu_runLine dispatches to
   * ppu_draw_whole_line_legacy, and ppu_legacy.c has ZERO overlay
   * references. SC_NEW_RENDERER=1 does not flip it either. Overlay
   * extraction is a new-renderer feature and this project runs the legacy
   * path -- the same reason the note on s_render_flags gives for the
   * widescreen clamp fields being dead.
   *
   * So centring waits on either the new renderer or an overlay
   * implementation in the legacy one. Do not retry it at this layer. */
  const bool advisor_page = PPU_mathEnabled(g_ppu) && PPU_halfColor(g_ppu) &&
                            PPU_addSubscreen(g_ppu) && !PPU_subtractColor(g_ppu) &&
                            (g_ppu->cgadsub & 0x20u) && g_ppu->cgram[0] == 0;
  /* Subtractive colour math, the shape the map screens use.
   *
   * The advisor pages are cgadsub $60 -- additive, halved -- and `halve`
   * below reproduces them. The LAND VALUE / map screens are cgadsub $a3:
   * SUBTRACT, not halved, operand = subscreen. `halve` does not fire, so the
   * extension stayed at full brightness while the guest darkened its own
   * city. Reported from play as the map screen's right side not being
   * darker the way the advisor pages are.
   *
   * The subscreen is not exposed by the runner, so it cannot be applied
   * directly. It does not have to be: the host strip spans the guest's OWN
   * columns, so at the same screen x both draw the same cell, and the
   * difference IS what the math did. Measured on savestate_4 at a clean
   * city-vs-city sample: host b5,94,73 -> guest 7b,5a,39, a difference of
   * exactly 58 on all three channels. A uniform subtrahend.
   *
   * So derive it per frame, per channel, as the MODE of (host - guest) over
   * the overlap. The mode matters: the overlap also contains HUD, the panel
   * and sprites, where the two legitimately differ, and a mean or median is
   * dragged around by them -- which is what sank two earlier attempts to fit
   * a ratio off the frame (36%% too dark, and a tint on other pages). The
   * modal offset is the value the majority of city pixels agree on, and it
   * is zero on every screen that does no subtraction, so this is inert
   * elsewhere -- in normal gameplay guest and host are pixel-identical.
   *
   * SC_EXT_SUB=0 disables it. */
  int dim_r = 0, dim_g = 0, dim_b = 0;
  { static int on = -1;
    if (on < 0) { const char *e = getenv("SC_EXT_SUB");
                  on = (e && *e) ? (*e != '0') : 1; }
    /* Only at FULL brightness.
     *
     * The subtrahend is derived from the difference between the guest's own
     * columns and the host render of the same cells. During a fade those two
     * do not track each other exactly, and the estimator reads the gap as
     * colour math -- so the margin was dimmed on top of the fade and lagged
     * behind it, then snapped level when the fade finished. Measured leaving
     * the loan screen: the margin/guest ratio falls 1.06 -> 0.42 across the
     * fade-in and jumps back to 1.06 the frame it completes. Reported from
     * play as the fading not being synchronous.
     *
     * Every screen that really does subtract sits at brightness 15, so
     * requiring that costs nothing and removes the whole class: with the
     * estimator off the ratio holds at 1.06 for every frame of the fade. */
    /* Not while the picture is CHANGING BRIGHTNESS.
     *
     * The subtrahend is derived from the difference between the guest's own
     * columns and the host render of the same cells. Mid-fade those two do
     * not track exactly, the estimator reads the gap as colour math, and the
     * margin gets dimmed on top of the fade -- so it lags and then snaps
     * level when the fade ends. Measured leaving the loan screen: the
     * margin/guest ratio falls 1.06 -> 0.42 across the fade-in and returns to
     * 1.06 the frame it completes. Reported from play as the fading not being
     * synchronous.
     *
     * Testing brightness == 15 does NOT catch it: this game fades per
     * scanline, so the register still reads 15 when the compositor runs.
     * What does catch it is the master brightness moving at all between
     * frames -- a screen that genuinely subtracts sits still. */
    static int last_bright = -1;
    const int bright_now = (int)g_ppu->inidisp;
    const bool settled = (last_bright == bright_now);
    last_bright = bright_now;
    if (on && settled &&
        PPU_mathEnabled(g_ppu) && PPU_subtractColor(g_ppu) &&
        PPU_addSubscreen(g_ppu)) {
      static int hr[256], hg[256], hb[256];
      memset(hr, 0, sizeof hr); memset(hg, 0, sizeof hg); memset(hb, 0, sizeof hb);
      long n = 0, eq = 0;
      for (int y = 0; y + 1 + fy < kVideoHeight; y += 2) {
        const uint32_t *g =
            (const uint32_t *)(s_guest_pixels + (size_t)y * s_video_pitch);
        const uint32_t *s = (const uint32_t *)(
            s_hostmap_px + (size_t)(y + 1 + fy) * s_hostmap_pitch);
        for (int x = 0; x < kVideoWidth; x += 2) {
          const uint32_t gc = g[x + s_ws_extra] & 0xffffffu;
          const uint32_t sc = s[x + fx + 8] & 0xffffffu;
          if (!sc) continue;                  /* nothing drawn there */
          if (gc == sc) { eq++; continue; }   /* untouched by the math */
          const int dr = (int)((sc >> 16) & 0xff) - (int)((gc >> 16) & 0xff);
          const int dg = (int)((sc >> 8) & 0xff) - (int)((gc >> 8) & 0xff);
          const int db = (int)(sc & 0xff) - (int)(gc & 0xff);
          if (dr < 0 || dg < 0 || db < 0) continue;   /* not a subtraction */
          /* Skip CLAMPED channels. Where the subtraction drove a channel to
           * zero the observed difference is smaller than the real subtrahend
           * -- savestate_0 shows host 00,31,ad -> guest 00,00,73, i.e. diffs
           * 0,49,58 where the true value is 58 on all three and green simply
           * ran out of range. Counting those biases each channel downward by
           * a different amount, which is a colour cast rather than a dimming.
           * Only an unclamped channel witnesses the true subtrahend. */
          if ((gc >> 16) & 0xff) { hr[dr]++; n++; }
          if ((gc >> 8) & 0xff)  { hg[dg]++; }
          if (gc & 0xff)         { hb[db]++; }
        }
      }
      if (n > 200) {
        int br = 0, bg = 0, bb = 0;
        for (int i = 1; i < 256; i++) {
          if (hr[i] > hr[br]) br = i;
          if (hg[i] > hg[bg]) bg = i;
          if (hb[i] > hb[bb]) bb = i;
        }
        /* Only accept a subtrahend the majority actually agrees on. Below
         * that the frame is not doing a uniform subtraction and guessing
         * would tint it. */
        /* A channel with too few unclamped witnesses cannot be estimated;
         * fall back to whichever channel does have agreement, since the
         * subtrahend measured here is uniform across channels. */
        /* If a large share of the overlap is IDENTICAL, the math is not
         * subtracting anything and the differing pixels are HUD, sprites and
         * seam noise. Their modal difference is meaningless -- measured, it
         * comes out at 107 on savestate_7, where the probe shows guest and
         * host pixel-identical and the true answer is zero. cgadsub having
         * the subtract bit set is NOT sufficient: savestate_7 is $b3 and
         * subtracts nothing. Only the frame can say. */
        if (eq * 4 > n) { dim_r = dim_g = dim_b = 0; }
        else {
        long tr = 0, tg = 0, tb = 0;
        for (int i = 0; i < 256; i++) { tr += hr[i]; tg += hg[i]; tb += hb[i]; }
        const int ok_r = tr > 200 && hr[br] * 4 > tr;
        const int ok_g = tg > 200 && hg[bg] * 4 > tg;
        const int ok_b = tb > 200 && hb[bb] * 4 > tb;
        if (ok_r || ok_g || ok_b) {
          const int best = ok_b ? bb : (ok_g ? bg : br);
          dim_r = ok_r ? br : best;
          dim_g = ok_g ? bg : best;
          dim_b = ok_b ? bb : best;
        }
        }
      }
    } }
  /* Force blank hides the extension too.
   *
   * The host map dims with a fade because pal_entry() runs its colours
   * through the PPU's brightnessMult table. That covers brightness, and
   * misses the OTHER way a SNES shows nothing: INIDISP bit 7.
   *
   * The game ends a fade-out by writing $8f -- force blank ON, brightness
   * restored to 15 -- so it can rebuild the screen unseen. Measured across
   * the Information -> View Mode transition: inidisp steps 09, 08 ... 01 with
   * both halves fading together, and then at the very moment the guest goes
   * black the extension jumps back to FULL brightness for 14 frames, because
   * brightnessMult[15] is exactly what it was before the fade started.
   * Reported from play as the widescreen fading to black, returning to full
   * brightness, and only then being overwritten by the wood.
   *
   * Brightness alone can never express this: the register says 15 and means
   * nothing is displayed. */
  const bool blanked = PPU_forcedBlank(g_ppu) != 0;
  uint32_t s_margin_backdrop;
  { const uint16_t bd = g_ppu->cgram[0];
    const uint8_t *bm = g_ppu->brightnessMult;
    s_margin_backdrop = 0xff000000u | ((uint32_t)bm[bd & 31] << 16) |
                        ((uint32_t)bm[(bd >> 5) & 31] << 8) | bm[(bd >> 10) & 31]; }
  const bool halve = advisor_page;   /* same test, computed above */
  { static int md = -1;
    if (md < 0) { const char *e = getenv("SC_MATH_DIAG"); md = (e && *e) ? 1 : 0; }
    if (md) { static int nf; if (++nf % 40 == 0)
      fprintf(stderr, "[math] halve=%d en=%d half=%d addsub=%d sub=%d cgadsub=%02x cgwsel=%02x bd=%04x\n",
              (int)halve, (int)(PPU_mathEnabled(g_ppu) != 0),
              (int)(PPU_halfColor(g_ppu) != 0),
              (int)(PPU_addSubscreen(g_ppu) != 0),
              (int)(PPU_subtractColor(g_ppu) != 0),
              g_ppu->cgadsub, g_ppu->cgwsel, g_ppu->cgram[0]); } }
  /* Cover the guest's leading sliver while it is showing wrapped content.
   * The host render already spans the guest's own columns, so this is just
   * a matter of where the strip starts. Zero on every frame the guest's own
   * edge is correct, which is most of them. */
  const int lead = (s_seam_lead_cover > 0 && s_seam_lead_cover <= 8)
                       ? s_seam_lead_cover : 0;
  const int x_from = kVideoWidth - lead;
  /* The same cover on the other two leading edges. Unlike the right edge --
   * which widescreen puts in open picture next to the join -- these sit where
   * the HUD lives, so they paint over the toolbar and the status bar for the
   * two or three frames they are active. SC_SEAM_LEAD_LT=0 turns them off
   * without disturbing the right edge. */
  static int lt_on = -1;
  /* Off by default. These were never reproduced, and they cover the two
   * edges where the HUD lives -- the toolbar and the status bar -- so when
   * they do fire they paint host terrain over it. Reported from play as
   * ghosting on HUD elements. SC_SEAM_LEAD_LT=1 re-enables them. */
  if (lt_on < 0) { const char *e = getenv("SC_SEAM_LEAD_LT");
                   lt_on = (e && *e) ? (atoi(e) != 0) : 0; }
  const int lead_l = (lt_on && s_seam_lead_left > 0 && s_seam_lead_left <= 8)
                         ? s_seam_lead_left : 0;
  const int lead_t = (lt_on && s_seam_lead_top > 0 && s_seam_lead_top <= 8)
                         ? s_seam_lead_top : 0;
  const int pp = ws_passepartout() ? kPassePartout : 0;
  const int left_cover = pp > lead_l ? pp : lead_l;
  const int x_start = pp ? (kVideoWidth - pp) : x_from;
  /* Render one cell further left than needed, and skip it when sampling.
   * The overlay pass draws a building's upper half one CELL up and left, so
   * the leftmost visible column needs a neighbour outside the window to
   * receive an overhang from. Without it, roofs pop in at the left edge as
   * cells scroll into the render -- measured as the left 8 px failing to
   * translate on 11-34%% of frames while the rest of the picture was exact. */
  /* SC_COMPOSE_DIAG: the compositor samples a CELL-granular render with a
   * fine offset. If the cell step and the fx wrap ever land on different
   * frames, the sampled window jumps 8 px and back at the tile cadence. */
  { static int cd = -1;
    if (cd < 0) { const char *e = getenv("SC_COMPOSE_DIAG"); cd = (e && *e) ? 1 : 0; }
    if (cd) { static int nf; static int psx = -9999;
      nf++;
      fprintf(stderr, "[compose] inidisp=%02x blank=%d bright=%d f=%d sx=%d sy=%d dsx=%d fx=%d fy=%d adj=%d,%d\n",
              g_ppu->inidisp, (int)(PPU_forcedBlank(g_ppu) != 0),
              (int)PPU_brightness(g_ppu),
              nf, sx, sy, psx == -9999 ? 0 : sx - psx, fx, fy,
              s_hostmap_adj_x, s_hostmap_adj_y);
      psx = sx; } }
  int use_sx = sx, use_sy = sy, use_fx = fx, use_fy = fy;
  /* Hold the margins on the city being replaced.
   *
   * Loading a city from the in-city dialog writes the new map into WRAM while
   * the dialog and the old city are still on screen; the game fades out, and
   * shows the new city only when it fades back in. This renderer reads the map
   * from WRAM, so it drew the new city into the margins at once, beside the
   * old one, for the thirty-odd frames before the fade. Reported from play as
   * the widescreen not waiting for the fade to black (savestate 3, loading the
   * practice city).
   *
   * The game writes the new map over several frames, 500 to 3000 cells each,
   * where play changes fewer than 50 (measured over 6000 frames of a
   * scenario). So a frame changing more than 300 cells raises a suspicion:
   * from then on the margins draw the map as it was before it, and once 4000
   * cells differ from that copy they are held on it -- still through the
   * current brightness, so they fade out with the picture -- until the screen
   * has been black (force blank or brightness 0) and lights up again. Ten
   * quiet frames short of 4000 drop the suspicion. A frame on which this
   * screen was not live ends either state, and so does a time limit.
   *
   * SC_SWAP_HOLD=0 turns it off; SC_SWAP_DIAG prints what it sees. */
  { static int on = -1, diag = 0;
    if (on < 0) { const char *e = getenv("SC_SWAP_HOLD");
                  on = (e && *e) ? (*e != '0') : 1;
                  const char *d = getenv("SC_SWAP_DIAG"); diag = (d && *d) ? 1 : 0; }
    static uint8_t base_map[SC_MAPVIEW_MAP_BYTES], base_pal[SC_MAPVIEW_PAL_BYTES];
    static uint8_t prev_map[SC_MAPVIEW_MAP_BYTES], scratch_pal[SC_MAPVIEW_PAL_BYTES];
    static int base_sx, base_sy, base_fx, base_fy;
    static bool have, suspect, hold, dark;
    static int quiet, held;
    static uint64_t last_frame = ~(uint64_t)0;
    enum { kSwapFrame = 300, kSwapTotal = 4000, kSwapQuiet = 10, kSwapMaxFrames = 900 };
    if (s_frames != last_frame + 1) { have = suspect = hold = false; }
    last_frame = s_frames;
    const bool black = blanked || PPU_brightness(g_ppu) == 0;
    if (!on) have = suspect = hold = false;
    else if (!have) {
      ScMapView_Snapshot(base_map, base_pal);
      ScMapView_Snapshot(prev_map, scratch_pal);
      base_sx = sx; base_sy = sy; base_fx = fx; base_fy = fy;
      have = true;
    } else {
      const int step = ScMapView_ChangedCells(prev_map);
      ScMapView_Snapshot(prev_map, scratch_pal);
      if (!hold) {
        if (step > kSwapFrame && !suspect) { suspect = true; quiet = 0; }
        if (suspect) {
          const int total = ScMapView_ChangedCells(base_map);
          if (diag)
            fprintf(stderr, "[swap] f=%llu step %d total %d%s\n",
                    (unsigned long long)s_frames, step, total,
                    total > kSwapTotal ? " -> hold" : "");
          if (total > kSwapTotal) { hold = true; suspect = false; dark = false; held = 0; }
          else if (step > kSwapFrame) quiet = 0;
          else if (++quiet >= kSwapQuiet) suspect = false;
        }
      } else {
        if (black) dark = true;
        else if (dark) hold = false;       /* lit again: the new city is showing */
        if (++held > kSwapMaxFrames) hold = false;
        if (diag && !hold)
          fprintf(stderr, "[swap] f=%llu hold released after %d frames\n",
                  (unsigned long long)s_frames, held);
      }
      if (!suspect && !hold) {
        memcpy(base_map, prev_map, sizeof base_map);
        memcpy(base_pal, scratch_pal, sizeof base_pal);
        base_sx = sx; base_sy = sy; base_fx = fx; base_fy = fy;
      }
    }
    /* The load moves the view as well, so the copy is drawn where it was. */
    const bool frozen = suspect || hold;
    ScMapView_SetSource(frozen ? base_map : NULL, frozen ? base_pal : NULL);
    if (frozen) { use_sx = base_sx; use_sy = base_sy; use_fx = base_fx; use_fy = base_fy; }
  }
  const int cols = (s_video_w + 8 + 7) / 8 + 1, rows = (kVideoHeight + 16 + 7) / 8;
  const bool drawn = ScMapView_Render(s_hostmap_px, s_hostmap_pitch, cols, rows,
                                      use_sx - 1, use_sy);
  ScMapView_SetSource(NULL, NULL);
  if (!drawn) {
    memcpy(s_video_pixels, s_guest_pixels, (size_t)s_video_pitch * kVideoHeight);
    return;
  }

  /* SC_DIM_PROBE: the host strip spans the guest's OWN columns, so at the
   * same screen x the two draw the same city cell. Any difference is what
   * the guest's colour math did to it -- measured, not inferred. */
  { static int dp = -1;
    if (dp < 0) { const char *e = getenv("SC_DIM_PROBE"); dp = (e && *e) ? 1 : 0; }
    if (dp) { static int nf; if (++nf % 60 == 0) {
      fprintf(stderr, "[dim] halve=%d cgadsub=%02x sub=%d,%d,%d  ",
              (int)halve, g_ppu->cgadsub, dim_r, dim_g, dim_b);
      for (int k = 0; k < 4; k++) {
        const int yy = 190 + k * 8, xx = 150 + k * 40;
        const uint32_t *g =
            (const uint32_t *)(s_guest_pixels + (size_t)yy * s_video_pitch);
        const uint32_t *s = (const uint32_t *)(
            s_hostmap_px + (size_t)(yy + 1 + use_fy) * s_hostmap_pitch);
        fprintf(stderr, "x%d g=%06x h=%06x  ", xx,
                g[xx + s_ws_extra] & 0xffffff, s[xx + use_fx + 8] & 0xffffff);
      }
      fputc(10, stderr);
    } } }
  for (int y = 0; y < kVideoHeight; y++) {
    uint32_t *dst = (uint32_t *)(s_video_pixels + (size_t)y * s_video_pitch);
    const uint32_t *gst =
        (const uint32_t *)(s_guest_pixels + (size_t)y * s_video_pitch);
    const uint32_t *src =
        (const uint32_t *)(s_hostmap_px + (size_t)(y + 1 + use_fy) * s_hostmap_pitch);
    memcpy(dst, gst + s_ws_extra, (size_t)kVideoWidth * 4);   /* guest */
    if (y < lead_t)
      for (int x = 0; x < kVideoWidth; x++) dst[x] = blanked ? 0xff000000u
                        : sc_ext_sub(src[x + use_fx + 8], dim_r, dim_g, dim_b);
    else if (halve)
      for (int x = 0; x < left_cover; x++) {
        const uint32_t c = blanked ? 0xff000000u : src[x + use_fx + 8];
        dst[x] = (c & 0xFF000000u) | ((c >> 1) & 0x007F7F7Fu);
      }
    else
      for (int x = 0; x < left_cover; x++) dst[x] = blanked ? 0xff000000u
                        : sc_ext_sub(src[x + use_fx + 8], dim_r, dim_g, dim_b);
    if (halve)
      for (int x = x_start; x < s_video_w; x++) {
        const uint32_t c = blanked ? 0xff000000u : src[x + use_fx + 8];
        dst[x] = (c & 0xFF000000u) | ((c >> 1) & 0x007F7F7Fu);
      }
    else
      for (int x = x_start; x < s_video_w; x++) dst[x] = blanked ? 0xff000000u
                        : sc_ext_sub(src[x + use_fx + 8], dim_r, dim_g, dim_b);
    /* Sprites the host map cannot draw. dst[x] past the guest's edge was
     * rendered by the PPU at x + s_ws_extra, and only s_ws_extra px of the
     * wider strip has PPU coverage. Transparency is RGB-only: the render
     * buffer leaves alpha clear, so comparing the whole word makes every
     * backdrop pixel look opaque and paints the margin solid black.
     *
     * From x_start, not from the edge: the leading-edge cover above paints
     * host terrain over the guest's last columns while it pans, and the
     * guest's sprites there went with it -- the ship came apart at the edge
     * for the length of a pan, in pieces the authentic picture never shows. */
    if (s_ws_obj_layer && s_margin_obj_on && !blanked) {
      const uint32_t *ol =
          (const uint32_t *)(s_ws_obj_layer + (size_t)y * s_video_pitch);
      int hi = kVideoWidth + s_ws_extra;
      if (hi > s_video_w) hi = s_video_w;
      for (int x = x_start < kVideoWidth ? x_start : kVideoWidth; x < hi; x++) {
        const uint32_t c = ol[x + s_ws_extra];
        if ((c & 0x00ffffffu) != (s_margin_backdrop & 0x00ffffffu))
          dst[x] = 0xff000000u | (c & 0x00ffffffu);
      }
    }
  }
  /* The vehicles the game dropped at its right edge, over everything in the
   * margin, dimmed like the terrain they stand on -- and in the last sprite
   * width before the edge, where a pan carries them back in before the game
   * places them again (src/sc_vehicles.c). */
  if (s_ws_vehicles && !blanked) {
    ScVehicleDim dim = { halve, dim_r, dim_g, dim_b };
    ScVehicles_Draw(s_video_pixels, (size_t)s_video_pitch, kVideoWidth - 16,
                    s_video_w, kVideoHeight, sc_vehicle_shade, &dim);
  }
}

static bool write_host_map_ppm(const char *path, int cols, int rows) {
  const int w = cols * 8, h = rows * 8;
  const int pitch = w * 4;
  uint8_t *buf = (uint8_t *)malloc((size_t)pitch * h);
  if (!buf) return false;
  { const char *z = getenv("SC_MAP_ZOOM");
    if (z && *z) ScMapView_SetCellPx(atoi(z)); }
  int sx = 0, sy = 0;
  ScMapView_GetScroll(&sx, &sy);
  if (!ScMapView_Render(buf, pitch, cols, rows, sx, sy)) {
    fprintf(stderr, "host map: render refused (non-US ROM, or PPU/ROM not ready)\n");
    free(buf); return false;
  }
  FILE *f = fopen(path, "wb");
  if (!f) { free(buf); return false; }
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int y = 0; y < h; y++) {
    const uint32_t *row = (const uint32_t *)(buf + (size_t)y * pitch);
    for (int x = 0; x < w; x++) {
      uint8_t rgb[3] = { (uint8_t)(row[x] >> 16), (uint8_t)(row[x] >> 8),
                         (uint8_t)row[x] };
      if (fwrite(rgb, 1, 3, f) != 3) { fclose(f); free(buf); return false; }
    }
  }
  fprintf(stderr, "host map: %dx%d from cell (%d,%d) -> %s\n",
          w, h, sx, sy, path);
  free(buf);
  return fclose(f) == 0;
}

static bool write_ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  /* Row-wise at the ACTIVE width, using the buffer's real stride. The buffer
   * is allocated for the widescreen maximum, so a flat index over
   * s_video_w * height would walk diagonally through it. */
  int width = s_custom_video.enabled ? s_custom_renderer.view.width : s_video_w;
  int height = s_custom_video.enabled ? s_custom_renderer.view.height : kVideoHeight;
  fprintf(f, "P6\n%d %d\n255\n", width, height);
  for (int y = 0; y < height; y++) {
    const uint32_t *row = s_custom_video.enabled ? s_custom_renderer.pixels + (size_t)y * width :
      (const uint32_t *)(s_video_pixels + (size_t)y * s_video_pitch);
    for (int x = 0; x < width; x++) {
      uint32_t pixel=s_custom_video.enabled?ScRendererPixel(&s_custom_renderer,x,y):row[x];
      uint8_t rgb[3] = { (uint8_t)(pixel >> 16), (uint8_t)(pixel >> 8), (uint8_t)pixel };
      if (fwrite(rgb, 1, 3, f) != 3) { fclose(f); return false; }
    }
  }
  if (s_custom_video.enabled && getenv("SC_RENDER_AUDIT")) {
    char audit_path[1100]; snprintf(audit_path,sizeof audit_path,"%s.json",path);
    FILE *audit=fopen(audit_path,"w");
    if (!audit) { fclose(f); return false; }
    fprintf(audit,"{\"core_x\":%d,\"core_y\":%d,\"city\":%s,\"advisor_centered\":%s,\"edge_repairs\":[",
            s_custom_renderer.view.core_x,s_custom_renderer.view.core_y,
            s_custom_renderer.city_frame ? "true" : "false",
            s_custom_renderer.advisor_frame ? "true" : "false");
    for (int y=0;y<224;++y) fprintf(audit,"%s%u",y ? "," : "",s_custom_renderer.repaired_edges[y]);
    fprintf(audit,"],\"wood_layer\":%d,\"screen\":%u,\"city_flag\":%u,\"ppu\":{\"mode\":%u,\"inidisp\":%u,\"main\":%u,\"sub\":%u,\"window_sub\":%u,\"windows\":%u,\"math\":%u,\"math_control\":%u}}\n",
            s_custom_renderer.wood_layer,g_ram[0x14],ram_w(0x3e),
            PPU_mode(g_ppu),g_ppu->inidisp,
            g_ppu->screenEnabled[0],g_ppu->screenEnabled[1],g_ppu->screenWindowed[1],
            g_ppu->windowsel,g_ppu->cgadsub,g_ppu->cgwsel);
    fclose(audit);
    if (s_custom_renderer.advisor_frame) {
      snprintf(audit_path,sizeof audit_path,"%s.panel.pgm",path);
      FILE *mask=fopen(audit_path,"wb");
      if (!mask) { fclose(f); return false; }
      fputs("P5\n256 224\n255\n",mask);
      for (int i=0;i<256*224;++i) fputc(s_custom_renderer.advisor_pixels[i] ? 255 : 0,mask);
      if (fclose(mask)) { fclose(f); return false; }
    }
  }
  return fclose(f) == 0;
}

/* Dumps the actual composited SDL renderer output (game frame + any host
 * overlay drawn on top, e.g. the settings menu) rather than just the raw
 * SNES framebuffer write_ppm() above captures -- used by SC_MENU_PREVIEW
 * (see main()) for headless visual verification of overlay UI, the same
 * kind of screenshot a windowed browser dev-tools check gives for web UI,
 * which this native SDL window otherwise has no equivalent of. */
static bool write_renderer_ppm(SDL_Renderer *renderer, const char *path) {
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(renderer, &w, &h);
  if (w <= 0 || h <= 0) return false;
  uint32_t *buf = (uint32_t *)malloc((size_t)w * (size_t)h * 4);
  if (!buf) return false;
  bool ok = false;
#if SNESRECOMP_SDL3
  /* SDL3 allocates and returns a surface instead of filling a caller buffer,
   * and its format follows the renderer rather than what this dumper wants,
   * so convert before copying. Row by row: the pitch is not necessarily w*4.
   *
   * This must run BEFORE SDL_RenderPresent -- on SDL3 the backbuffer contents
   * are undefined after present, so reading afterwards yields black. That is
   * the trap this function fell into. */
  { SDL_Surface *shot = SDL_RenderReadPixels(renderer, NULL);
    if (shot) {
      SDL_Surface *conv = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_ARGB8888);
      if (conv) {
        for (int y = 0; y < h && y < conv->h; y++)
          memcpy(buf + (size_t)y * w,
                 (const uint8_t *)conv->pixels + (size_t)y * conv->pitch,
                 (size_t)w * 4);
        SDL_DestroySurface(conv);
        ok = true;
      }
      SDL_DestroySurface(shot);
    } }
#else
  ok = SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                            buf, w * 4) == 0;
#endif
  if (ok) {
    FILE *f = fopen(path, "wb");
    if (f) {
      fprintf(f, "P6\n%d %d\n255\n", w, h);
      for (int i = 0; i < w * h; i++) {
        uint8_t rgb[3] = { (uint8_t)(buf[i] >> 16), (uint8_t)(buf[i] >> 8),
                           (uint8_t)buf[i] };
        if (fwrite(rgb, 1, 3, f) != 3) { ok = false; break; }
      }
      ok = fclose(f) == 0 && ok;
    } else ok = false;
  }
  free(buf);
  return ok;
}

static bool write_wram_dump(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  bool ok = fwrite(g_ram, 1, sizeof(g_ram), f) == sizeof(g_ram);
  return fclose(f) == 0 && ok;
}

/* SC_SRAM_DUMP_PATH=<file>: dump the 32KB cart SRAM window ($700000-$707fff)
 * at exit. SRAM is NOT part of g_ram -- it lives in the cart model -- so a
 * WRAM dump does not capture it and it has to be read back through the bus.
 * Wanted for the save-game/scenario-record work: the layout at $700000 is a
 * 14-byte header (magic "SIM", flags, checksum) followed by the per-city save
 * block, and the only practical way to check a field's meaning is to diff two
 * dumps. */
static bool write_sram_dump(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  bool ok = true;
  for (uint32_t i = 0; i < 0x8000 && ok; i++)
    ok = fputc(snes_read(g_snes, 0x700000 + i), f) != EOF;
  return fclose(f) == 0 && ok;
}

/* Decode of the SRAM header, printed alongside the dump -- see
 * apply_unlock_all() for where each field comes from in the ROM. */
static void report_sram_header(const char *when) {
  uint8_t h[16];
  for (int i = 0; i < 16; i++) h[i] = snes_read(g_snes, 0x700000 + i);
  uint16_t sum = 0;
  for (int i = 0; i < 14; i++) sum = (uint16_t)(sum + h[i]);
  uint16_t flags = (uint16_t)(h[7] | (h[8] << 8));
  fprintf(stderr, "[sram %s] magic=%c%c%c win=%04x (scenarios", when,
          h[0] >= 32 ? h[0] : '?', h[1] >= 32 ? h[1] : '?',
          h[2] >= 32 ? h[2] : '?', flags);
  for (int i = 0; i < 8; i++) if (flags & (1u << i)) fprintf(stderr, " %d", i);
  fprintf(stderr, "%s) stored_sum=%04x computed=%04x%s\n",
          (flags & 0x8000) ? ", ALL" : "",
          (uint16_t)(h[14] | (h[15] << 8)), sum,
          (uint16_t)(h[14] | (h[15] << 8)) == sum ? "" : "  MISMATCH");
}

/* â”€â”€ save states -- for reproducing a specific screen/input scenario (e.g.
 * "on the map, cursor visible, nothing else held") instantly and
 * deterministically, instead of re-navigating menus by hand or by guessed
 * --input timing every single test run. snes_saveload() already covers the
 * full device model (cpu/apu/dma/ppu/cart + WRAM, since g_ram is snes->ram);
 * interp816_saveload() separately covers the actual CPU registers this
 * Phase-1 host runs on (snes->cpu is an unused legacy AOT-tier struct, not
 * what interp816 drives). s_frames is saved too so frame-numbered tooling
 * (SC_ADDR_TRACE's @start-frame, --input's start:duration) stays meaningful
 * across a load instead of resetting to 0. Host-only UI state (mouse
 * toggle, etc.) is deliberately not saved -- reloading shouldn't change
 * host input mode out from under you. */
typedef struct { SaveLoadInfo base; FILE *f; bool ok; } FileSli;
static void file_sli_write(SaveLoadInfo *sli, void *data, size_t n) {
  FileSli *fs = (FileSli *)sli;
  if (fs->ok && fwrite(data, 1, n, fs->f) != n) fs->ok = false;
}
static void file_sli_read(SaveLoadInfo *sli, void *data, size_t n) {
  FileSli *fs = (FileSli *)sli;
  if (fs->ok && fread(data, 1, n, fs->f) != n) fs->ok = false;
}

/* A versioned format, from the adaptive-renderer PR (blackerking/
 * UrbanRecomp#1). The device snapshot holds the PPU's registers and
 * memories but not its CPU-port latches -- the VRAM pointer and VMAIN's
 * increment-on-high bit among them -- so a state loaded where the game was
 * mid-upload sent the following tile and palette uploads to the wrong place.
 * That is the old "a state taken on a report screen breaks up after loading".
 * The latches (vramPointer up to the widescreen fields), this host's master
 * clock and its HDMA walker are written after the old contents, behind a
 * header naming their sizes, so a state from a build whose layout differs is
 * refused rather than misread.
 *
 * States without the header still load, with a warning: they are what every
 * bug report so far was filed against. */
enum { kScPpuBusBytes = offsetof(Ppu, extraLeftCur) - offsetof(Ppu, vramPointer) };
static const uint32_t kScStateHeader[] = {0x54534353u /* "SCST" */, 4,
                                          kScPpuBusBytes, sizeof(s_hdma)};

static bool save_state(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  FileSli fs;
  fs.base.func = file_sli_write;
  fs.f = f;
  fs.ok = true;
  fs.base.func(&fs.base, (void *)kScStateHeader, sizeof(kScStateHeader));
  ScMusicLock();snes_saveload(g_snes, &fs.base);ScMusicUnlock();
  interp816_saveload(g_cpu, &fs.base);
  fs.base.func(&fs.base, &s_frames, sizeof(s_frames));
  fs.base.func(&fs.base, &g_ppu->vramPointer, kScPpuBusBytes);
  fs.base.func(&fs.base, &g_master_cycles, sizeof(g_master_cycles));
  fs.base.func(&fs.base, s_hdma, sizeof(s_hdma));
  uint32_t development_size = sizeof(s_development);
  fs.base.func(&fs.base, &development_size, sizeof(development_size));
  fs.base.func(&fs.base, &s_development, sizeof(s_development));
  uint8_t population_data[SC_POPULATION_BYTES];
  ScPopulationEncode(&s_population, population_data);
  fs.base.func(&fs.base, population_data, sizeof population_data);
  size_t world_size=ScWorldEncodedSize();
  uint8_t *world_data=malloc(world_size);
  if (!world_data || !ScWorldEncode(&s_world,world_data,world_size)) fs.ok=false;
  else fs.base.func(&fs.base,world_data,world_size);
  free(world_data);
  bool ok = fs.ok;
  return fclose(f) == 0 && ok;
}

static bool load_state(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  uint32_t header[4];
  bool versioned = fread(header, 1, sizeof(header), f) == sizeof(header) &&
                   header[0] == kScStateHeader[0];
  if (versioned && ((header[1] < 1 || header[1] > 4) ||
      header[2] != kScStateHeader[2] || header[3] != kScStateHeader[3])) {
    fprintf(stderr, "state: %s is from a build with another layout "
                    "(version %u, %u latch bytes, %u HDMA bytes); save it again "
                    "with this build\n", path, (unsigned)header[1],
            (unsigned)header[2], (unsigned)header[3]);
    fclose(f);
    return false;
  }
  if (!versioned) {
    fprintf(stderr, "state: %s is in the old format, without the PPU port "
                    "latches; uploads right after loading can come out wrong. "
                    "Save it again with this build.\n", path);
    fseek(f, 0, SEEK_SET);
  }
  s_ui_mouse_pointer=(ScMouseUiPointer){0};
  s_custom_renderer.menu_pointer_active=false;
  s_custom_renderer.map_preview.active=0;s_preview_started=0;
  s_map_number_high=0;s_map_number_dirty=false;
  s_preview_expanded=s_preview_complete=s_preview_left_down=s_preview_click_owned=s_preview_input_blocked=false;
  ScConstructionFree(s_build_work);s_build_work=NULL;
  FileSli fs;
  fs.base.func = file_sli_read;
  fs.f = f;
  fs.ok = true;
  test_city_restore_sram();s_test_load_pending=s_test_generate_pending=s_test_saving=s_test_menu_pending=false;
  ScSram_Hold();   /* the saved cities on disk stay the player's */
  s_mouse_dialog=SC_MOUSE_DIALOG_NONE;
  s_save_dialog_pending=s_save_dialog_active=false;
  s_escape_back_frames=0;
  ScMusicLock();snes_saveload(g_snes, &fs.base);ScMusicResetLocked();ScMusicUnlock();
  interp816_saveload(g_cpu, &fs.base);
  fs.base.func(&fs.base, &s_frames, sizeof(s_frames));
  ScDevelopmentReset(&s_development);
  reset_refresh_clocks();
  s_journey_arming=false;
  s_city_loading=s_size_selecting=s_speed_selecting=s_practice_size_pending=false;
  s_city_present_pending=s_city_fade_started=s_city_black_seen=false;
  ScMapSizeMenuSet(false);
  ScWorldReset(&s_world); memset(&s_world_guest,0,sizeof s_world_guest);
  ScPopulationImport(&s_population, g_ram);
  if (versioned) {
    fs.base.func(&fs.base, &g_ppu->vramPointer, kScPpuBusBytes);
    fs.base.func(&fs.base, &g_master_cycles, sizeof(g_master_cycles));
    fs.base.func(&fs.base, s_hdma, sizeof(s_hdma));
    if (header[1] >= 2) {
      uint32_t development_size = 0;
      fs.base.func(&fs.base, &development_size, sizeof(development_size));
      if (development_size != sizeof(s_development)) fs.ok = false;
      fs.base.func(&fs.base, &s_development, sizeof(s_development));
      if (s_development.remaining < 0 || s_development.remaining > 50) fs.ok = false;
    }
    if (header[1] >= 3) {
      uint8_t population_data[SC_POPULATION_BYTES];
      fs.base.func(&fs.base, population_data, sizeof population_data);
      if (!fs.ok || !ScPopulationDecode(&s_population, population_data, sizeof population_data)) fs.ok = false;
    }
    if (header[1] >= 4) {
      uint8_t head[48]={0};fs.base.func(&fs.base,head,sizeof head);
      uint32_t size=(uint32_t)head[20]|(uint32_t)head[21]<<8|(uint32_t)head[22]<<16|(uint32_t)head[23]<<24;
      uint8_t *data=fs.ok && size>=48 && size<=ScWorldEncodedSize()?malloc(size):NULL;
      if(!data) fs.ok=false;
      else {
        memcpy(data,head,48);fs.base.func(&fs.base,data+48,size-48);
        if(!fs.ok || !ScWorldDecode(&s_world,data,size)) fs.ok=false;
        free(data);
      }
    }
  }
  g_ppu->lastBrightnessMult = 0xff;   /* rebuild the brightness tables */
  ScRendererResetCamera(&s_custom_renderer);
  ScRendererResetHistory(&s_custom_renderer);
  memset(s_scroll_pass,0,sizeof s_scroll_pass);
  s_scroll_multiplier=1;s_keyboard_pan_latched=false;
  s_middle_pan=(ScMousePan){0};
  s_journey_menu_selection=ram_w(0x3e);
  ScSram_Release();
  ScVehicles_Reset();   /* host-side, not in the state; back within 4 frames */
  bool ok = fs.ok;
  fclose(f);
  /* Hand the restored registers to the fiber, HERE rather than at the call
   * sites.
   *
   * load_state restores g_snes and the Interp816, and knows nothing about the
   * separate CpuState the fiber actually executes. Without this the fiber
   * keeps running boot registers over mid-game WRAM and wedges spinning on the
   * BNE at $05935A in the block-copy region.
   *
   * That was already known and already fixed -- but only on the --load-state
   * command-line path. The interactive slot keys and the menu loader called
   * load_state directly and got the stale registers, so loading a state from
   * inside a running fiber session hung the game. Doing it inside load_state
   * means a new call site cannot miss it, which is exactly how this one was
   * missed. */
#ifdef SC_AOT_TIER
  if (ok && sc_fiber_active()) ScFiberDrive_AdoptInterpState(g_cpu);
#endif
  if (ok) {
    unsigned screen=ram_w(0x14);
    ScMusicLock();ScMusicRestoreLocked(g_ram[8],!host_map_screen_live() || (ram_w(0x195)&8)!=0);ScMusicUnlock();
    s_journey_arming=s_world.journey && ((screen>=4 && screen<=9) || screen==21 || screen==22);
    s_build_active = s_build_pending = s_build_cancelled = false;
    s_clip_tool=s_clip_pending=0;s_clip_drag=s_clip_hover=false;
    ScClipboardClear(&s_clipboard);
    s_map_mouse_accept_pending = false;
    s_map_mouse_refresh_pending = false;
  }
  return ok;
}

static uint8_t *read_file(const char *path, uint32_t *size_out) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n <= 0) { fclose(f); return NULL; }
  uint8_t *b = (uint8_t *)malloc((size_t)n);
  if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
  fclose(f);
  if (b) *size_out = (uint32_t)n;
  return b;
}

static int startup_sdl_failure(const char *operation) {
  char message[1200];
  snprintf(message,sizeof message,"%s: %s",operation,SDL_GetError());
  ScMacStartupError(message);
  return 1;
}

/* â”€â”€ synthetic input injection for headless repro (mirrors the --input
 * flag in snesrecomp/cosim/ref_driver.c) -- lets a specific controller
 * press be reproduced deterministically at an exact frame, instead of
 * guessing from an interactive session. Player 2 gets its own identical
 * array/parser (--input2) -- needed for the documented debug-menu code
 * entry, which is read on controller 2. â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
typedef struct InputEvent { uint64_t start, duration; uint16_t mask; } InputEvent;
static InputEvent s_input_events[64];
static uint32_t s_input_event_count;
static InputEvent s_input2_events[96];
static uint32_t s_input2_event_count;

static bool add_input_event_to(InputEvent *arr, uint32_t *count, uint32_t cap, const char *text) {
  unsigned long long start = 0, duration = 0;
  unsigned mask = 0;
  char trailing = '\0';
  if (*count >= cap ||
      sscanf(text, "%llu:%llu:%x%c", &start, &duration, &mask, &trailing) != 3 ||
      !duration || mask > 0xffffu)
    return false;
  arr[(*count)++] = (InputEvent){start, duration, (uint16_t)mask};
  return true;
}

static bool add_input_event(const char *text) {
  return add_input_event_to(s_input_events, &s_input_event_count, 64, text);
}

static bool add_input2_event(const char *text) {
  return add_input_event_to(s_input2_events, &s_input2_event_count, 96, text);
}

/* One-button macro for the documented debug-menu entry code (Peter's
 * SNES guide for the game, crediting Corey Miller/"ZaphodBee"): a fixed
 * 16-step sequence read on controller 2 while on the "Goodbye! See you
 * soon" quit-confirmation screen. It opens the debug menu -- confirmed in
 * play (2026-09-17), although static analysis had found no code reading a
 * second controller (no $421A/$421B or manual $4016/$4017 access). The
 * macro saves 16 hand-timed presses. Each step is held for
 * kP2StepHold frames with a kP2StepGap release between steps so the
 * game's edge-detection (if any) sees 16 distinct presses, not one held
 * button. */
enum { kP2StepHold = 6, kP2StepGap = 6, kP2StepFrames = kP2StepHold + kP2StepGap };
static bool queue_debug_menu_code(uint64_t start_frame) {
  static const uint16_t kSeq[] = {
    kPad_Left, kPad_A, kPad_Right, kPad_Y, kPad_Up, kPad_B, kPad_Down, kPad_X,
    kPad_Select, kPad_Start, kPad_Start, kPad_Select, kPad_R, kPad_R, kPad_L, kPad_L,
  };
  uint32_t n = (uint32_t)(sizeof(kSeq) / sizeof(kSeq[0]));
  if (s_input2_event_count + n > 96) return false;
  for (uint32_t i = 0; i < n; i++) {
    s_input2_events[s_input2_event_count++] = (InputEvent){
      start_frame + (uint64_t)i * kP2StepFrames, kP2StepHold, kSeq[i]
    };
  }
  fprintf(stderr, "queued debug-menu code on controller 2 starting frame %llu (%u steps, %d frames each)\n",
          (unsigned long long)start_frame, n, kP2StepFrames);
  return true;
}

/* SC_FREEZE=<addr>:<val>[,<addr>:<val>...] -- hold WRAM bytes at fixed
 * values every frame, the same thing a bsnes "freeze"/cheat does. Both
 * addresses and values are hex; addresses are WRAM offsets (so $7e01ed is
 * just `1ed`). Applied once per emulated frame, so it survives the ROM
 * rewriting the byte itself -- unlike a one-shot poke after --load-state.
 *
 * This exists because "is byte X the thing gating behaviour Y?" keeps
 * being the decisive question in this project's investigations, and until
 * now the only ways to answer it were a ROM byte-patch (changes the code,
 * not the state) or asking the user to drive bsnes by hand. */
#define SC_FREEZE_MAX 16
static struct { uint32_t addr; uint8_t val; } s_freezes[SC_FREEZE_MAX];
static int s_freeze_count;

static void parse_freezes(const char *spec) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s", spec);
  for (char *tok = strtok(buf, ","); tok && s_freeze_count < SC_FREEZE_MAX;
       tok = strtok(NULL, ",")) {
    unsigned a = 0, v = 0;
    if (sscanf(tok, "%x:%x", &a, &v) == 2 && a < sizeof(g_ram)) {
      s_freezes[s_freeze_count].addr = a;
      s_freezes[s_freeze_count].val = (uint8_t)v;
      s_freeze_count++;
      fprintf(stderr, "freeze: $%05x = %02x\n", a, v);
    } else {
      fprintf(stderr, "freeze: bad spec '%s' (want hexaddr:hexval)\n", tok);
    }
  }
}

/* Scenario override: 0 = off, otherwise hold $0040 (the scenario index)
 * at this value every frame. The Select Scenario screen can only produce
 * indices 0-5 -- 03:de0e computes `$40 = $54*3 + $52`, a 2x3 grid -- but
 * the ROM's own deadline table at $03c5b3 has EIGHT entries, and both
 * extra scenarios are genuinely present in this ROM: index 6 is Las Vegas
 * (start 2096, 10-year limit, deadline 2106), whose briefing screen,
 * artwork and full text all load correctly once the index is forced;
 * index 7 carries the sentinel deadline $ffff (no time limit) and the
 * win/lose evaluator at 03:c548 deliberately returns without writing a
 * result for it -- i.e. free play. Holding the index is exactly how the
 * hidden scenario was confirmed, and is far less invasive than
 * restructuring the selection grid. Set it, then start a scenario as
 * normal. */
static int s_scenario_override;
static const int kScenarioOverrides[] = { 0, 6, 7 };
static const char *const kScenarioOverrideNames[] = { "OFF", "LAS VEGAS", "FREE PLAY" };

/* Population override for exercising milestones and the extended display.
 * Assign the authoritative 64-bit value and its native compatibility mirror
 * every frame while active; the next simulation sweep otherwise recalculates
 * population from zone capacities. INT_MAX represents the maximum profile
 * in this int-valued menu; ScPopulationSet receives the actual 64-bit limit. */
static const int kPopOverrides[] = { -1, 0, 2000, 10000, 50000, 100000, 500000, 600000,
                                    1000000, 1000000000, INT_MAX };
static const char *const kPopOverrideNames[] = { "OFF", "0", "2000", "10000", "50000", "100000",
    "500000", "600000", "1000000", "1000000000", "9999999999999" };

/* City class override ($0deb, 0-5 = Village through Megalopolis). SET POP
 * now derives this as well; an explicit class override applies afterward to
 * let milestone tests use a class independently of the population profile. */
static int s_class_override = -1;
static const int kClassOverrides[] = { -1, 0, 1, 2, 3, 4, 5 };

/* The milestone messages are one-shot: each is guarded by a latch byte
 * that the trigger increments when it fires (03:c350 -> $0cbd, 03:c369 ->
 * $0cbf, 03:c3b4 -> $0cc1, 03:c396 -> $0cc3). Zeroing them re-arms every
 * milestone so a message can be made to fire again on demand -- otherwise
 * a population override only ever works once per session. */
static void menu_action_clear_milestones(void) {
  g_ram[0x0cbd] = 0; g_ram[0x0cbf] = 0;
  g_ram[0x0cc1] = 0; g_ram[0x0cc3] = 0;
  fprintf(stderr, "[menu] cleared milestone latches $0cbd/$0cbf/$0cc1/$0cc3\n");
}

/* Scenario completion ("win mark") flags, SRAM $700007.
 *
 * Setting these is what makes the SCENARIO OVR hack above redundant: the
 * scenarios stop needing to be reached by forcing $0040, because the game
 * itself offers them.
 *
 * The bitfield and everything around it were read off the ROM:
 *
 *   03:e30a  ORA $e334,Y     scenario index * 2 indexes a mask table at
 *                            03:e334 -- $0001, $0002, $0004 ... $0080, so
 *                            bit N marks scenario N complete
 *   03:e311  AND #$003f      ...and once the low SIX bits are all set,
 *   03:e315  CMP #$003f      03:e31c ORA #$8000 sets bit 15, the game's own
 *                            "every scenario beaten" flag
 *   03:e326  STA $700007     committed to SRAM
 *   03:e36c  STA $42         and read back the other way at init, SRAM into
 *                            the direct-page word $42
 *
 * Writing $700007 alone is not enough. SRAM carries a 14-byte header with a
 * magic and a checksum, both of which the game verifies at boot (03:e411's
 * sum loop and 03:e42d's 'S','I','M' test); a header that fails either is
 * restored from the backup copy at $707ff0 (03:e446), which would silently
 * undo this. So the full commit path from 03:e553 is reproduced here:
 * recompute the checksum over $700000-$70000d into $70000e, then mirror the
 * whole 16-byte header to $707ff0.
 *
 * Bits 0-6 are set, i.e. the six ordinary scenarios plus Las Vegas -- every
 * scenario, which is what makes reaching the hidden one a normal menu
 * selection -- and with the ninth scenario on, bit 8 for Sylt, where its win
 * is kept (03:e30a's hook) and its card's mark is read from. Index 7, free
 * play, and index 8 without Sylt, the tutorial, have nothing to win.
 *
 * In the F10 menu as "ALL SCENARIO WON", with the cheats: it writes the
 * save, and switching it off does not take the wins back.
 *
 * SRAM lives in the cart model (`cart->ram`), not in g_ram, so it has to go
 * through the bus rather than a direct array write. The windowed game keeps
 * SRAM on disk (src/sc_sram.c), so the unlock is saved like any city. */
#define kWinMarkBits 0x007fu   /* scenarios 0-6 */
static bool s_unlock_all;

static void apply_unlock_all(void) {
  /* Only ever modify an SRAM the game has already formatted. A header failing
   * the magic test at 03:e42d gets restored from backup or rewritten, so
   * flags written into a blank SRAM would simply be undone -- and measured,
   * SRAM really is still blank well into a run: none of 03:e360/03:e40e/
   * 03:e45b executes at all in 3600 frames from a cold boot, because the
   * whole SRAM subsystem is only reached through a real game session. The
   * alternative -- fabricating the magic ourselves -- would mean claiming a
   * formatted save whose body is zeroed, so wait for the game instead. */
  if (snes_read(g_snes, 0x700000) != 'S' ||
      snes_read(g_snes, 0x700001) != 'I' ||
      snes_read(g_snes, 0x700002) != 'M') {
    /* Say so once. This used to return in silence, which makes the toggle
     * look broken rather than pending: switching it on at the title screen
     * does nothing at all until a real session has formatted SRAM, and there
     * was no way to tell that from "it didn't work". */
    static bool warned;
    if (!warned) {
      warned = true;
      fprintf(stderr, "[unlock] waiting: SRAM not formatted yet (no SIM magic "
              "at $700000). Start a game once, then this applies by itself.\n");
    }
    return;
  }
  uint16_t flags = (uint16_t)(snes_read(g_snes, 0x700007) |
                              ((uint16_t)snes_read(g_snes, 0x700008) << 8));
  uint16_t want = (uint16_t)(flags | kWinMarkBits);
  if (s_ninth_scenario) want |= 0x0100u;          /* Sylt */
  if ((want & 0x003f) == 0x003f) want |= 0x8000;  /* 03:e31c */
  /* Idempotent: after the first application this is two bus reads a frame and
   * nothing else, so it never fights the game's own writes to the header. */
  if (want == flags) return;

  snes_write(g_snes, 0x700007, (uint8_t)want);
  snes_write(g_snes, 0x700008, (uint8_t)(want >> 8));

  uint16_t sum = 0;                                /* 03:e553 */
  for (int i = 0; i < 14; i++)
    sum = (uint16_t)(sum + snes_read(g_snes, 0x700000 + i));
  snes_write(g_snes, 0x70000e, (uint8_t)sum);
  snes_write(g_snes, 0x70000f, (uint8_t)(sum >> 8));

  for (int i = 0; i < 16; i++)                     /* 03:e484 */
    snes_write(g_snes, 0x707ff0 + i, snes_read(g_snes, 0x700000 + i));

  /* Keep the live direct-page copy in step, so this also takes effect after
   * 03:e36c has already run rather than only on a later re-read. */
  g_ram[0x42] = (uint8_t)want;
  g_ram[0x43] = (uint8_t)(want >> 8);

  fprintf(stderr, "[unlock] scenario win marks $700007: %04x -> %04x "
          "(checksum %04x)\n", flags, want, sum);
}

static void apply_freezes(void) {
  for (int i = 0; i < s_freeze_count; i++)
    g_ram[s_freezes[i].addr] = s_freezes[i].val;
  if (s_unlock_all) apply_unlock_all();
  if (s_scenario_override) g_ram[0x0040] = (uint8_t)s_scenario_override;
  if (s_pop_override >= 0) {
    ScPopulationSet(&s_population, g_ram,
      s_pop_override == INT_MAX ? SC_POPULATION_MAX : (uint64_t)s_pop_override);
  }
  if (s_class_override >= 0) g_ram[0x0deb] = (uint8_t)s_class_override;
}

static void apply_frame_input(uint64_t frame) {
  uint16_t input = 0;
  for (uint32_t i = 0; i < s_input_event_count; i++) {
    InputEvent *e = &s_input_events[i];
    if (frame >= e->start && frame - e->start < e->duration) input |= e->mask;
  }
  g_snes->input1_currentState = input;

  uint16_t input2 = 0;
  for (uint32_t i = 0; i < s_input2_event_count; i++) {
    InputEvent *e = &s_input2_events[i];
    if (frame >= e->start && frame - e->start < e->duration) input2 |= e->mask;
  }
  g_snes->input2_currentState = input2;
}

/* Desktop mouse coordinates follow the actual rendered viewport. The
 * guest input surface can be offset inside the wider canvas, and menus
 * use a different anchor from the city HUD. */
static bool s_mouse_enabled = true;
static ScMouseUiResult mouse_ui_point(uint8_t *ram,int x,int y,bool select,bool ninth) {
  if(s_test_revealed && ram_w(0x14)==17 && x>=48 && x<222 && y>=(int)ScSavedCityMenuY(2) && y<(int)ScSavedCityMenuY(2)+24) {
    if(select)ram_set_w(0x421,2);
    return (ScMouseUiResult){true,true};
  }
  if((s_size_selecting || s_speed_selecting) && (ram_w(0x14)==3 || ram_w(0x14)==18)) {
    ScMouseUiResult result={true,false};
    for(unsigned i=0;i<6u;++i) if(x>=48 && x<232 && y>=(int)ScJourneyMenuY(false,i+1) && y<(int)ScJourneyMenuY(false,i+1)+16) {
      result.hit=true;if(select) ram_set_w(0x3e,i+1);
    }
    return result;
  }
  if(s_city_loading) return (ScMouseUiResult){true,false};
  if(s_mouse_dialog!=SC_MOUSE_DIALOG_NONE)
    return ScMouseUiDialogPoint(s_mouse_dialog,ram,x,y,select);
  return ScMouseUiPoint(ram,x,y,select,ninth);
}

/* Fast D-pad cursor (opt-in, F9): rather than reverse-engineer and patch
 * the ROM's own throttled cursor cadence (see docs/INVESTIGATION_
 * cursor_cadence.md -- $01f3, $01ff, and the deeper bank-$03 simulation-
 * tick preemption that ultimately paces it), reuse the same host-side
 * bypass the mouse patch above already established: while a direction is
 * held, poke $01eb/$01ed directly via apply_mouse_delta() every frame,
 * completely independent of the ROM's own per-frame dispatcher. This is
 * strictly additive -- the normal D-pad bits are still sent to the game
 * as usual (menu navigation, edge-detected list movement, etc. are
 * untouched), this just adds extra host-driven displacement on top for
 * the main-map cursor specifically, so holding a direction moves it at
 * full host speed instead of whatever cadence the ROM's own cooperative
 * scheduler happens to allow it that frame. Same caveats as the mouse
 * patch it reuses (menu jank, no bounds-replication beyond the simple
 * clamp already in apply_mouse_delta) -- off by default. */
static bool s_fast_cursor_enabled;
/* Pixels/frame while a direction is held. Adjustable (F10 menu ->
 * "CURSOR SPEED") rather than fixed, because the stock cursor's real
 * pacing turns out to be the game's own cooperative scheduler and can't
 * be tuned ROM-side: traced live, bank $03 (the city simulation) holds
 * the CPU for ~4 consecutive frames at a time, during which the bank-1
 * cursor dispatcher never runs at all, so the cursor steps its 2 pixels
 * only on the bank-1 frames -- a 4-on/4-off duty cycle averaging ~1
 * px/frame (~4s to cross the screen). That is authentic behaviour, not a
 * recomp defect, and is presumably why the cartridge shipped with SNES
 * Mouse support. This host-side nudge is the practical remedy. */
/* Mouse sensitivity, as a percentage applied after the window-scale divide.
 * 100 = one SNES pixel per SNES pixel of pointer travel. */
static int s_mouse_sensitivity = 100;
/* Direction the host mouse last moved, fed to the pad while a mouse button is
 * held so the ROM runs its own cursor/drag path instead of only seeing a
 * teleported cursor. */
/* Map tiles the right-drag pan may advance per frame, per axis. */

static int s_pan_max_tiles = 1;
static const int kPanMaxTiles[] = { 1, 2, 3, 4, 6, 8 };

static uint16_t s_mouse_dir;
static int      s_mouse_dir_frames;
static const int kMouseSensitivities[] = { 50, 75, 100, 150, 200 };

static int s_fast_cursor_step = 4;
static const int kFastCursorSteps[] = { 2, 4, 8, 16 };

static void apply_mouse_delta(int dx, int dy) {
  if (dx > 127) dx = 127; else if (dx < -127) dx = -127;
  if (dy > 127) dy = 127; else if (dy < -127) dy = -127;

  int x = (int)g_ram[0x01eb] + dx;
  if (x > 0xff) x = 0xff;
  if (x < 0x00) x = 0x00;
  g_ram[0x01eb] = (uint8_t)x;

  int y = (int)g_ram[0x01ed] + dy;
  if (y > 0xdf) y = 0xdf; /* 223: patch clamps Y to the visible scanline range */
  if (y < 0x00) y = 0x00;
  g_ram[0x01ed] = (uint8_t)y;
}

/* â”€â”€ minimal in-game settings menu â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
 * Follows the pattern researched from ar-recomp (ActRaiser recomp)'s
 * settings_overlay.c/settings.c/config.c (see task #20/#34): a single
 * descriptor table (SettingDesc[]) drives a generic renderer/input
 * handler instead of hand-coding a screen per toggle, and the menu is a
 * pure host-side SDL overlay drawn after the game's own frame is already
 * composited -- it never touches SNES VRAM/PPU state directly (only the
 * settings' own target fields, which the game already reads every frame
 * regardless of whether this menu exists). While open, run_one_frame() is
 * skipped and the last rendered game frame is simply re-presented every
 * host iteration, the same freeze-and-redraw approach ar-recomp's own
 * overlay uses.
 *
 * ar-recomp's own overlay decodes the ROM's actual dialog font/frame
 * graphics for an in-theme look -- skipped here as purely cosmetic
 * ActRaiser-specific work (not something this game's ROM has an equivalent
 * of anyway). This uses a small hand-authored 3x5 bitmap font instead,
 * the same kind of fallback ar-recomp itself falls back to when ROM font
 * decoding isn't available. It only covers the character set this menu's
 * own labels currently use -- add glyphs to kFont as new labels need
 * them. Not yet ported from ar-recomp's design: the Int/Enum setting
 * kinds (nothing here needs a ranged/enumerated value yet), the
 * apply-kind taxonomy (every setting here is effectively PASSIVE --  the
 * game already polls these fields itself every frame), and settings.ini
 * persistence (all of these already persist their own way, e.g. save
 * states, or are meant to be session-only toggles like the cheats). */

/* kSettingHeader is a non-interactive section label. It carries no field and
 * no action; navigation skips over it so Up/Down still lands only on real
 * rows. Sections exist because the list grew past the point where a flat
 * column of twenty-odd entries reads as one undifferentiated block. */
typedef enum { kSettingBool, kSettingBit, kSettingAction, kSettingCycle,
               kSettingHeader } SettingKind;

typedef struct {
  const char *label;
  SettingKind kind;
  void *field;           /* bool* (Bool), uint8_t* (Bit), int* (Cycle) */
  uint8_t mask;           /* kSettingBit only */
  void (*action)(void);   /* kSettingAction only */
  const int *values;      /* kSettingCycle only: allowed values, cycled in order */
  int value_count;
  /* kSettingCycle only, optional: one label per entry of `values`. A bare
   * number is fine for a step size, but not for an index whose meaning is
   * arbitrary -- SCENARIO OVR showing "6" and "7" cost a whole play session,
   * because 7 is free play and looks like a perfectly reasonable next value
   * after Las Vegas. Name them and the mistake is unavailable. */
  const char *const *value_names;
} SettingDesc;

static bool setting_get(const SettingDesc *d) {
  switch (d->kind) {
    case kSettingHeader: return false;
    case kSettingBool: return *(bool *)d->field;
    case kSettingBit:  return (*(uint8_t *)d->field & d->mask) != 0;
    default: return false;
  }
}
static void save_large_map_setting(void) {
  ScSettings settings;
  if (ScSettingsLoad(&settings,kScSettingsPath)) {
    settings.large_maps=s_large_maps;
    if (!ScSettingsSave(&settings,kScSettingsPath)) fprintf(stderr,"settings: could not save larger-map preference\n");
  }
  fprintf(stderr,"new city map size: %s\n",s_large_maps==5?"3840x3200":s_large_maps==4?"1920x1600":s_large_maps==3?"960x800":s_large_maps==2?"480x400":s_large_maps?"240x200":"120x100");
}
static void menu_action_fit_screen(void) {s_fit_screen_requested=true;}
static void save_terrain_setting(void) {
  ScSettings settings;
  if(ScSettingsLoad(&settings,kScSettingsPath)) {
    settings.terrain_style=s_terrain_style;
    if(!ScSettingsSave(&settings,kScSettingsPath))fprintf(stderr,"settings: could not save terrain style\n");
  }
}

static void setting_adjust(SettingDesc *d,int direction) {
  switch (d->kind) {
    case kSettingHeader: break;
    case kSettingBool: *(bool *)d->field = direction?direction>0:!*(bool *)d->field; break;
    case kSettingBit:
      if(direction<0) *(uint8_t *)d->field &= (uint8_t)~d->mask;
      else if(direction>0) *(uint8_t *)d->field |= d->mask;
      else *(uint8_t *)d->field ^= d->mask;
      break;
    case kSettingAction: if (direction>=0 && d->action) d->action(); break;
    case kSettingCycle: {
      int *v = (int *)d->field;
      int i = 0;
      for (; i < d->value_count; i++) if (d->values[i] == *v) break;
      if(d->value_count<=0 || !d->values) break;
      if(i==d->value_count) i=direction<0?0:-1;
      *v = d->values[(i+(direction<0?-1:1)+d->value_count)%d->value_count];
      break;
    }
  }
  if (d->field==&s_large_maps) save_large_map_setting();
  if (d->field==&s_terrain_style)save_terrain_setting();
  if(d->field==&s_terrain_style && s_rom_is_us && ram_w(0x14)==5 &&
      s_custom_renderer.map_preview.active) {
    s_map_number_dirty=true;ram_set_w(0xb31,0x80);s_map_mouse_refresh_pending=true;
  }
  if(getenv("SC_SETTINGS_DIAG") && d->field)
    fprintf(stderr,"[settings] %s value %d direction %d\n",d->label,
        d->kind==kSettingCycle?*(int *)d->field:(int)setting_get(d),direction);
}
static void setting_activate(SettingDesc *d) {setting_adjust(d,0);}

static void menu_action_save_slot1(void) {
  if (save_state("savestate_1.bin"))
    fprintf(stderr, "[menu] saved slot 1 -> savestate_1.bin at frame %llu\n",
            (unsigned long long)s_frames);
  else
    fprintf(stderr, "[menu] failed to save slot 1\n");
}

static void menu_action_load_slot1(void) {
  if (load_state("savestate_1.bin"))
    fprintf(stderr, "[menu] loaded slot 1 <- savestate_1.bin, now at frame %llu\n",
            (unsigned long long)s_frames);
  else
    fprintf(stderr, "[menu] failed to load slot 1 (not saved yet?)\n");
}

/* Disaster triggers: $0197 is the pending-disaster bitfield serviced by the
 * six-arm ladder at 03:b8ae, which calls one handler per bit and then masks
 * that bit off. Setting a bit here is exactly what the game's own
 * disaster-selection page does, so these fire the real code path rather than
 * simulating anything.
 *
 * Named only where a single-disaster recording has actually attributed the
 * bit (see docs/ROM_MAP.md); bits 0 and 1 are still unidentified and are
 * labelled by number so the menu never asserts something unproven. Naming
 * them after a guess is how the $0199 mistake happened. */
/* ARM TRIGGERS: a safety catch in front of the disaster rows.
 *
 * Requested after a stray selection set one off mid-game. The rows sit right
 * under the cheats in a menu navigated with the D-pad, and firing an
 * earthquake by accident is not recoverable without a save state. Off by
 * default, so the triggers do nothing until deliberately armed. */
static bool s_disaster_armed;

static bool disaster_triggers_armed(const char *what) {
  if (s_disaster_armed) return true;
  fprintf(stderr, "[menu] %s ignored -- ARM TRIGGERS is off\n", what);
  return false;
}

static void trigger_disaster_bit(unsigned bit, const char *what) {
  g_ram[0x0197] |= (uint8_t)(1u << bit);
  fprintf(stderr, "[menu] set $0197 bit %u (%s) -> $0197=%02x, frame %llu\n",
          bit, what, g_ram[0x0197], (unsigned long long)s_frames);
}
/* Scenario events: MELTDOWN and UFO are NOT $0197 bits.
 *
 * They are dispatched from 03:b96f on the per-scenario countdown $0c0d,
 * gated on $003e == 3 (scenario mode) and keyed on $0040 (scenario index):
 *
 *   $0040 == 4 (Boston)     and $0c0d == 1          -> JSR $bac1  meltdown
 *   $0040 == 6 (Las Vegas)  and ($0c0d & 15) == 0   -> JSR $bcb8  UFO
 *
 * So a trigger has to set three words together, and two of them identify the
 * city -- leaving $003e/$0040 changed would tell the game it is playing a
 * different scenario, which would corrupt the win check and the next save.
 * Arm them, then restore as soon as the ROM has taken the countdown to zero
 * with its own DEC $0c0d at 03:b9c9. That is the ROM reporting the event has
 * fired, so the restore is self-timing rather than a guessed frame delay --
 * simulation ticks are many frames apart and vary with game speed.
 *
 * Deliberately NOT a freeze: the values are set once and the ROM is left to
 * consume them, so execution stays on paths the game really takes. */
/* The loaded ROM image, so the UFO population gate can be lifted for the
 * duration of a triggered event. Set in main() once the ROM is read. */
static uint8_t *s_rom_data;
static uint32_t s_rom_size;
static void commit_mouse_construction(void) {
  /* Suspend the guest at its idle input boundary until an atomic private
   * transaction is ready. Host events, presentation and music keep running. */
  if (ram_w(0xd7) || ram_w(0x020d) != s_build_plan.tool ||
      (int16_t)ram_w(0x01bd) != s_build_scroll_x || (int16_t)ram_w(0x01bf) != s_build_scroll_y) {
    s_build_pending=false;return;
  }
  s_build_started=SDL_GetPerformanceCounter();
  s_build_work=ScConstructionBegin(g_ram,&s_world,s_rom_data,s_rom_size,&s_build_plan);
  if(!s_build_work) {s_build_pending=false;g_ram[5]=2;}
}
static void poll_mouse_construction_until(uint64_t deadline) {
  if(!s_build_work)return;
  do {
    if(ScConstructionStep(s_build_work,4096)) {
      unsigned cost=0,completed=ScConstructionCompleted(s_build_work);
      ScBuildResult r=ScConstructionFinish(s_build_work,g_ram,&s_world,&cost);
      ScConstructionFree(s_build_work);s_build_work=NULL;s_build_pending=false;
      fprintf(stderr,"[mouse build] %u placements, evaluated %u, cost %u, %s\n",s_build_plan.count,completed,cost,
          r==SC_BUILD_OK?"committed":r==SC_BUILD_FUNDS?"insufficient funds":"rejected");
      if(getenv("SC_PERF"))fprintf(stderr,"[mouse latency] build %.2f ms\n",(SDL_GetPerformanceCounter()-s_build_started)*1000.0/SDL_GetPerformanceFrequency());
      if(r!=SC_BUILD_OK)g_ram[5]=2;
      return;
    }
  } while(SDL_GetPerformanceCounter()<deadline);
}
static void poll_mouse_construction(void) {
  poll_mouse_construction_until(SDL_GetPerformanceCounter()+SDL_GetPerformanceFrequency()/250);
}
static SDL_Texture *s_preview_texture;
static uint8_t *s_preview_cells,*s_preview_reveal;
static unsigned s_preview_width,s_preview_height,s_preview_revision;
static unsigned s_preview_frame=UINT_MAX;
static uint32_t s_preview_palette[38];
static bool map_selection_preview_live(void) {
  return s_custom_renderer.map_preview.active && s_custom_renderer.map_preview.source &&
      (ram_w(0x14)==5 || ram_w(0x14)==6) && !ram_w(0xb31);
}
static ScRect map_selection_preview_rect(ScViewport v,int dw,int dh) {
  double sx=(double)s_destination.w/v.width,sy=(double)s_destination.h/v.height;
  if(s_preview_expanded) {
    double w=fmin(dw,dh*1.2),h=w/1.2;
    return SC_RECT((dw-w)*.5,(dh-h)*.5,w,h);
  }
  return SC_RECT(s_destination.x+(v.core_x+48)*sx,s_destination.y+(v.core_y+88)*sy,120*sx,100*sy);
}
static void zoom_event_pointer(const SDL_Event *event,double *x,double *y) {
#if SNESRECOMP_SDL3
  if(event->type==SDL_MOUSEWHEEL) {*x=event->wheel.mouse_x;*y=event->wheel.mouse_y;return;}
  float mx,my;SDL_GetMouseState(&mx,&my);
#else
#if SDL_VERSION_ATLEAST(2,26,0)
  if(event->type==SDL_MOUSEWHEEL) {*x=event->wheel.mouseX;*y=event->wheel.mouseY;return;}
#endif
  int mx,my;SDL_GetMouseState(&mx,&my);
#endif
  *x=mx;*y=my;
}
static void city_zoom_at_pointer(SDL_Window *window,SDL_Renderer *renderer,
                                 const SDL_Event *event,double factor) {
  int ww,wh,dw,dh;SDL_GetWindowSize(window,&ww,&wh);SDL_GetRendererOutputSize(renderer,&dw,&dh);
  ScViewport current=s_custom_video.enabled?s_custom_renderer.view:ScVideoViewport(&s_custom_video,dw,dh);
  double mx,my,x,y;zoom_event_pointer(event,&mx,&my);
  /* Read the raw displayed canvas, not the SNES cursor or relocated HUD
   * hitboxes. Each event gets its own anchor, even within the same frame. */
  if(!ScVideoWindowToCanvas(current,ScVideoDestination(current,dw,dh),ww,wh,dw,dh,mx,my,&x,&y))return;
  if(!ScVideoZoom(&s_custom_video,current,dw,dh,factor))return;
  ScRendererZoomAt(&s_custom_renderer,s_custom_video.map_zoom,x,y);
  if(getenv("SC_MOUSE_PAN_DIAG"))fprintf(stderr,"[zoom anchor] canvas %.4f,%.4f zoom %.6f\n",x,y,s_custom_video.map_zoom);
  if(s_ws_extra) {
    s_ws_extra=0;s_host_map=false;s_video_w=kVideoWidth;s_video_pitch=kVideoWidth*4;
    PpuSetExtraSpace(g_ppu,0);PpuBeginDrawing(g_ppu,s_video_pixels,(size_t)s_video_pitch,s_render_flags);
  }
}
static void draw_map_selection_preview(SDL_Renderer *renderer,ScViewport v) {
  ScMapPreview *p=&s_custom_renderer.map_preview;
  if(!p->active || !p->source || !s_custom_renderer.map_preview_frame)return;
  double sx=(double)s_destination.w/v.width,sy=(double)s_destination.h/v.height;
  int dw,dh;SDL_GetRendererOutputSize(renderer,&dw,&dh);
  ScRect rect=map_selection_preview_rect(v,dw,dh);
  unsigned w=(unsigned)ceil(rect.w),h=(unsigned)ceil(rect.h);
  if(w<120)w=120;if(h<100)h=100;if(w>2048)w=2048;if(h>2048)h=2048;
  if(!s_preview_texture || w!=s_preview_width || h!=s_preview_height) {
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h);
    uint8_t *cells=malloc((size_t)w*h),*reveal=malloc((size_t)w*h);
    if(!texture || !cells || !reveal) {if(texture)SDL_DestroyTexture(texture);free(cells);free(reveal);return;}
    if(s_preview_texture)SDL_DestroyTexture(s_preview_texture);
    free(s_preview_cells);free(s_preview_reveal);
    s_preview_texture=texture;s_preview_cells=cells;s_preview_reveal=reveal;
    s_preview_width=w;s_preview_height=h;s_preview_revision=0;s_preview_frame=UINT_MAX;
    snesrecomp_sdl_set_texture_linear(texture,false);
  }
  bool upload=s_preview_revision!=p->revision || s_preview_frame!=p->frame ||
      memcmp(s_preview_palette,s_custom_renderer.preview_colors,sizeof s_preview_palette)!=0;
  if(s_preview_revision!=p->revision) {
    sc_mapgen_preview_raster(p,s_preview_cells,s_preview_reveal,w,h);
    s_preview_revision=p->revision;
  }
  if(upload) {
    void *data;int pitch;
    if(!(SDL_LockTexture(s_preview_texture,NULL,&data,&pitch) SC_SDL_OK))return;
    for(unsigned y=0;y<h;++y) {
      uint32_t *row=(uint32_t *)((uint8_t *)data+(size_t)y*pitch);
      for(unsigned x=0;x<w;++x) {
        unsigned at=y*w+x,ci=p->frame>=s_preview_reveal[at]?s_preview_cells[at]:0;
        row[x]=s_custom_renderer.preview_colors[ci<38?ci:0];
      }
    }
    SDL_UnlockTexture(s_preview_texture);
    s_preview_frame=p->frame;memcpy(s_preview_palette,s_custom_renderer.preview_colors,sizeof s_preview_palette);
  }
  if(s_preview_expanded) {
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
  }
  SDL_RenderCopy(renderer,s_preview_texture,NULL,&rect);
  /* Redraw the fixed-size hand above the detailed preview only. The native
   * buttons, frame and cursor elsewhere keep their original composition. */
  if(s_ui_mouse_pointer.active && ScMouseUiPointerScreen(g_ram) && !s_middle_pan.active) {
    SDL_Rect clip={(int)rect.x,(int)rect.y,(int)ceil(rect.w),(int)ceil(rect.h)};
    SDL_RenderSetClipRect(renderer,&clip);
    for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
      uint32_t ink=ScRendererHandPixel(g_ppu,x,y);if(!ink)continue;
      ScRect pixel=SC_RECT(s_preview_cursor_x+(x-SC_MOUSE_HAND_HOT_X)*sx,
        s_preview_cursor_y+(y-SC_MOUSE_HAND_HOT_Y)*sy,sx,sy);
      SDL_SetRenderDrawColor(renderer,ink>>16&255,ink>>8&255,ink&255,255);SDL_RenderFillRect(renderer,&pixel);
    }
    SDL_RenderSetClipRect(renderer,NULL);
  }
}
static void draw_build_preview(SDL_Renderer *renderer,ScViewport v) {
  if(!s_build_plan.count)return;
  const ScBuildCell first=s_build_plan.cells[0],last=s_build_plan.cells[s_build_plan.count-1];
  static const int large_steps[]={4,4,6,4,4};
  int step=s_build_tool>=10 && s_build_tool<=14?large_steps[s_build_tool-10]:s_build_tool>=5?3:1;
  int left=first.x<last.x?first.x:last.x,top=first.y<last.y?first.y:last.y;
  int right=(first.x>last.x?first.x:last.x)+step,bottom=(first.y>last.y?first.y:last.y)+step;
  int scroll_x=s_custom_video.enabled?s_custom_renderer.scroll_x+s_custom_renderer.scroll_adjust_x:s_build_scroll_x*8;
  int scroll_y=s_custom_video.enabled?s_custom_renderer.scroll_y+s_custom_renderer.scroll_adjust_y:s_build_scroll_y*8;
  double x0=left*8-scroll_x,y0=top*8-scroll_y,x1=right*8-scroll_x,y1=bottom*8-scroll_y;
  if(s_custom_video.enabled) {ScRendererProjectCity(&s_custom_renderer,&x0,&y0);ScRendererProjectCity(&s_custom_renderer,&x1,&y1);}
  double sx=(double)s_destination.w/v.width,sy=(double)s_destination.h/v.height;
  x0=s_destination.x+(v.core_x+x0)*sx;x1=s_destination.x+(v.core_x+x1)*sx;
  y0=s_destination.y+(v.core_y+y0)*sy;y1=s_destination.y+(v.core_y+y1)*sy;
  double dx=(x1-x0)*step/(right-left),dy=(y1-y0)*step/(bottom-top);
  SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer,255,230,60,220);
  int clip_top=(int)(s_destination.y+(v.core_y+46)*sy);
  if(clip_top<s_destination.y)clip_top=s_destination.y;
  SDL_Rect clip={(int)s_destination.x,clip_top,(int)s_destination.w,(int)(s_destination.y+s_destination.h-clip_top)};
  SDL_RenderSetClipRect(renderer,&clip);
  ScRect outer=SC_RECT(x0,y0,x1-x0,y1-y0);SDL_RenderDrawRect(renderer,&outer);
  /* Shared grid lines replace four draw calls per zone. At subpixel scale
   * the outer outline stays readable instead of becoming a solid yellow fill. */
  if(dx>=4) {
    int begin=(int)floor((clip.x-x0)/dx)+1;if(begin<1)begin=1;
    for(int i=begin;i<(right-left)/step && x0+i*dx<clip.x+clip.w;++i)
      SDL_RenderDrawLine(renderer,x0+i*dx,y0,x0+i*dx,y1);
  }
  if(dy>=4) {
    int begin=(int)floor((clip.y-y0)/dy)+1;if(begin<1)begin=1;
    for(int i=begin;i<(bottom-top)/step && y0+i*dy<clip.y+clip.h;++i)
      SDL_RenderDrawLine(renderer,x0,y0+i*dy,x1,y0+i*dy);
  }
  SDL_RenderSetClipRect(renderer,NULL);
}
static uint8_t clipboard_rom_read(void *ctx,uint32_t address) {
  const ScRenderer *r=ctx;
  size_t offset=(size_t)((address>>16)&127)*0x8000+(address&0x7fff);
  return offset<r->rom_size?r->rom[offset]:255;
}
static void commit_clipboard(void) {
  unsigned action=s_clip_pending;s_clip_pending=0;
  if(ram_w(0xd7) || ram_w(0x379) || ram_w(0x020d)!=s_clip_native_tool) return;
  if(action==1) {
    bool ok=ScClipboardCopy(&s_clipboard,g_ram,&s_world,s_rom_data,s_rom_size,
        s_clip_x0,s_clip_y0,s_clip_x1,s_clip_y1);
    if(ok) {
      s_clip_tool=2;
      fprintf(stderr,"[copy] %u cells, %dx%d, price %llu, %u reward buildings excluded\n",
          s_clipboard.count,s_clipboard.width,s_clipboard.height,
          (unsigned long long)s_clipboard.price,s_clipboard.excluded);
    } else {g_ram[5]=2;fprintf(stderr,"[copy] no eligible objects or invalid selection\n");}
  } else if(action==2) {
    ScBuildResult result=ScClipboardPaste(&s_clipboard,g_ram,&s_world,s_rom_data,s_rom_size,s_clip_x1,s_clip_y1);
    if(result==SC_BUILD_OK) {
      ScPowerRefreshRestore(&s_power_refresh,g_ram,&s_world,s_rom_data,s_rom_size,s_frames);
      refresh_live_population(s_rom_data,s_rom_size);
      ScPopulationMirror(&s_population,g_ram);
    } else g_ram[5]=2;
    fprintf(stderr,"[paste] at %d,%d: %s\n",s_clip_x1,s_clip_y1,
        result==SC_BUILD_OK?"committed":result==SC_BUILD_FUNDS?"insufficient funds":"occupied or outside city");
  }
}
static void restore_loaded_power(void) {
  bool ok=ScPowerRefreshRestore(&s_power_refresh,g_ram,&s_world,g_snes->cart->rom,
      g_snes->cart->romSize,s_frames);
  if(s_power_fix_hits++<8 || !ok)
    fprintf(stderr,"[powerfix] rebuilt %u cells after load: %s (frame %llu)\n",
        s_world.active?ScWorldCells(&s_world):12000,ok?"ready":"failed",
        (unsigned long long)s_frames);
}
static void refresh_fast_power(bool bitmap_available) {
  uint64_t power_t0=s_perf_detail?SDL_GetPerformanceCounter():0;
  bool solved=ScPowerRefreshStep(&s_power_refresh,g_ram,&s_world,g_snes->cart->rom,
      g_snes->cart->romSize,s_frames,s_development_speed,bitmap_available && !s_native_power_active);
  if(s_perf_detail) s_perf_power_ms+=(SDL_GetPerformanceCounter()-power_t0)*s_perf_clock_ms;
  if (solved && getenv("SC_POWER_DIAG"))
    fprintf(stderr,"[power refresh] frame %llu speed %d native period %.1f frames\n",
        (unsigned long long)s_frames,s_development_speed,s_power_refresh.clock.budget/2.0);
}

static struct {
  bool        armed;
  uint16_t    saved_3e, saved_40;
  uint16_t    armed_cd;
  bool        gate_lifted;
  const char *what;
} s_scenario_event;

static uint16_t ram_w(uint32_t a) { return (uint16_t)(g_ram[a] | (g_ram[a+1] << 8)); }
static void ram_set_w(uint32_t a, uint16_t v) {
  g_ram[a] = (uint8_t)(v & 0xff); g_ram[a+1] = (uint8_t)(v >> 8);
}

/* The UFO checks the city population before it will appear:
 *
 *   03:b9b3  LDA $0ba5 ; CMP #$4c08 ; LDA $0ba7 ; SBC #$0001
 *   03:b9bf  BCC $b9c4          ; under 84,488 -> skip the UFO
 *   03:b9c1  JSR $bcb8
 *
 * NOP the branch (90 03 -> EA EA) so the call is reached regardless. Patching
 * the CODE rather than writing a fake population is the conservative choice:
 * $0ba5/$0ba7 are live simulation state that taxes, milestones and the win
 * check all read, so faking them even for one tick would change the game in
 * ways nothing here could bound. Two bytes of branch, by contrast, affect
 * exactly this decision.
 *
 * Scoped to the armed window and reverted with the rest of the trigger, so a
 * real Las Vegas game still has its gate. Byte-checked before writing, the
 * same as the boot-time patches.
 *
 * Interpreter-tier only: a compiled body for 03:b96f would already have the
 * branch baked in, so this has no effect in the AOT build. The windowed build
 * this menu lives in is the interpreter, so that is not a limitation here. */
#define SC_UFO_GATE_OFF 0x1b9bfu    /* 03:b9bf, headerless LoROM file offset */

/* cart_init() does `cart->rom = malloc(); memcpy(...)`, so the cart holds its
 * OWN copy and the buffer read_file() returned is not what executes. The
 * boot-time patches above work only because they run before the cart is
 * built. Anything patched later has to go to cart->rom, or it silently does
 * nothing -- which is exactly what the first version of this did. */
static uint8_t *sc_live_rom(void) {
  if (g_snes && g_snes->cart && g_snes->cart->rom) return g_snes->cart->rom;
  return s_rom_data;
}

static bool lift_ufo_population_gate(void) {
  uint8_t *rom = sc_live_rom();
  if (!rom || SC_UFO_GATE_OFF + 1 >= s_rom_size) return false;
  uint8_t *p = rom + SC_UFO_GATE_OFF;
  if (p[0] != 0x90 || p[1] != 0x03) {
    fprintf(stderr, "[menu] UFO gate: unexpected bytes %02x %02x at 03:b9bf, not patching\n", p[0], p[1]);
    return false;
  }
  p[0] = 0xea; p[1] = 0xea;
  fprintf(stderr, "[menu] UFO gate lifted (03:b9bf BCC -> NOP NOP)\n");
  return true;
}

static void restore_ufo_population_gate(void) {
  uint8_t *rom = sc_live_rom();
  if (!rom || SC_UFO_GATE_OFF + 1 >= s_rom_size) return;
  uint8_t *p = rom + SC_UFO_GATE_OFF;
  p[0] = 0x90; p[1] = 0x03;
  fprintf(stderr, "[menu] UFO gate restored\n");
}

static void arm_scenario_event(unsigned idx, uint16_t countdown, const char *what) {
  if (s_scenario_event.armed) {
    fprintf(stderr, "[menu] %s: a scenario event is already armed\n", what);
    return;
  }
  s_scenario_event.saved_3e = ram_w(0x3e);
  s_scenario_event.saved_40 = ram_w(0x40);
  s_scenario_event.what     = what;
  s_scenario_event.armed    = true;
  ram_set_w(0x3e, 3);
  ram_set_w(0x40, (uint16_t)idx);
  ram_set_w(0x0c0d, countdown);
  s_scenario_event.armed_cd = countdown;
  s_scenario_event.gate_lifted = (idx == 6) ? lift_ufo_population_gate() : false;
  fprintf(stderr, "[menu] armed %s: $3e=3 $0040=%u $0c0d=%u (saved $3e=%u $0040=%u), frame %llu\n",
          what, idx, (unsigned)countdown,
          (unsigned)s_scenario_event.saved_3e, (unsigned)s_scenario_event.saved_40,
          (unsigned long long)s_frames);
}

/* Restore once the ROM has counted the event out. Called once per frame. */
static void scenario_event_tick(void) {
  if (!s_scenario_event.armed) return;
  /* Restore on the ROM's first DEC $0c0d, not on the countdown reaching
   * zero. Both arms decrement on the tick they fire, so this is one tick
   * either way for the meltdown (1 -> 0) but sixteen for the UFO
   * (16 -> 0), and leaving $0040 forced for sixteen ticks would have the
   * game think it is in the wrong scenario for most of a minute. */
  if (ram_w(0x0c0d) == s_scenario_event.armed_cd) return;
  ram_set_w(0x3e, s_scenario_event.saved_3e);
  ram_set_w(0x40, s_scenario_event.saved_40);
  if (s_scenario_event.gate_lifted) {
    restore_ufo_population_gate();
    s_scenario_event.gate_lifted = false;
  }
  s_scenario_event.armed = false;
  fprintf(stderr, "[menu] %s fired; restored $3e=%u $0040=%u at frame %llu\n",
          s_scenario_event.what, (unsigned)s_scenario_event.saved_3e,
          (unsigned)s_scenario_event.saved_40, (unsigned long long)s_frames);
}

/* SC_DISASTER_MENU8=1: put the meltdown and the UFO on the GAME'S OWN
 * disaster page, not just the F10 menu.
 *
 * The page (01:aa39, screen mode $01df == 2) walks $0197 as a checkbox list:
 *
 *   01:aa3e  ASL A ; ASL A     ; 2 shifts, so only bits 5..0 reach the walker
 *   01:aa45  LDY #$0005        ; 6 rows
 *   01:aa77  LDA $01a95c,X     ; bit-mask table
 *
 * Two things make this cheap. The mask table at 01:a95c already runs to
 * $0200, so bits 6 and 7 have masks sitting there unused; and the input path
 * does SBC #$0008 with only a negative check, so row indices 0-7 are already
 * accepted. Only the render side is capped at six.
 *
 * Dropping the two shifts lets all eight bits reach the walker, and bumping
 * the count to 8 draws two more checkboxes.
 *
 * The new bits are serviced HERE rather than by extending 03:b8ae. That
 * ladder is a fixed chain ending in PLD/RTS at 03:b914 with no room for two
 * more arms, and the meltdown and UFO are not ladder disasters anyway -- they
 * are the $0c0d scenario events, which already have a verified trigger above.
 * So the ROM patch only has to make the bits SETTABLE; the host reads them.
 *
 * OPT-IN because two things about it are unverified: whether rows 6 and 7 land
 * inside the menu box or on top of whatever is below it, and that they will
 * have no LABELS -- the row text comes from the page-setup dispatch
 * (01:aabf JSR ($9d1a,X)), not from the checkbox renderer, so the two new rows
 * draw a checkbox with nothing beside it until that is extended too. */
static bool s_disaster_menu8;

/* The disaster page cannot be widened in place -- slots 6 and 7 are IN USE.
 *
 * Attempted and reverted: raise the row count, then position the two new rows
 * by writing their bytes in the buffer at $7e2063 + row*16. Both failed, and
 * the second failed destructively.
 *
 * A clean dump shows slots 6/7 holding `e0 00 32 80` -- byte 3 a palette or
 * attribute byte -- and slot 8 holding different tiles again ($35/$33). The
 * buffer is shared with other UI elements, so writing checkbox tiles and
 * palettes there corrupts them wherever they appear. Reported from play as
 * "Speed, Options and Disasters are all colourful even when not selected",
 * which is exactly that.
 *
 * Two methodology notes, both of which cost real time here:
 *
 *   - Sweeping the position byte over $60-$88 rendered NOTHING at any value.
 *     Verifying that a WRAM write landed is not verifying a pixel changed.
 *   - Replaying a save state already parked on a page never re-runs that
 *     page setup, so it is blind to any setup-time change. Several
 *     screenshots taken that way proved nothing either way.
 *
 * The page is drawn by 01:d94f, which blits four 16-word rows from ROM tables
 * at 01:d8af/d8cf/d8ef/... into the tilemap at $7e2440. Adding entries means
 * authoring new table rows there, not moving bytes in the sprite buffer. */


static void service_disaster_menu8(void) {
  if (!s_disaster_menu8) return;
  uint8_t v = g_ram[0x0197];
  if (v & 0x40) { g_ram[0x0197] = (uint8_t)(v & ~0x40u);
                  arm_scenario_event(4, 1,  "nuclear meltdown (in-game menu)"); }
  else if (v & 0x80) { g_ram[0x0197] = (uint8_t)(v & ~0x80u);
                       arm_scenario_event(6, 16, "UFO (in-game menu)"); }
}

static void menu_trigger_meltdown(void) {
  if (disaster_triggers_armed("nuclear meltdown")) arm_scenario_event(4, 1, "nuclear meltdown");
}
/* The UFO additionally passes a population gate at 03:b9b3 -- a 32-bit
 * compare of ($0ba7:$0ba5) against $0001_4c08 -- so it will not appear in a
 * city under 84,488 people. Measured: on a small free-play city the arm is
 * reached and the gate rejects it, so the menu row is not broken, the city is
 * just too small. */
static void menu_trigger_ufo(void) {
  if (disaster_triggers_armed("UFO")) arm_scenario_event(6, 16, "UFO");
}

static void menu_trigger_fire(void) { if (disaster_triggers_armed("fire")) trigger_disaster_bit(0, "fire"); }
static void menu_trigger_flood(void) { if (disaster_triggers_armed("flood")) trigger_disaster_bit(1, "flood"); }
static void menu_trigger_plane(void) { if (disaster_triggers_armed("plane crash")) trigger_disaster_bit(2, "plane crash"); }
static void menu_trigger_tornado(void) { if (disaster_triggers_armed("tornado")) trigger_disaster_bit(3, "tornado"); }
static void menu_trigger_quake(void) { if (disaster_triggers_armed("earthquake")) trigger_disaster_bit(4, "earthquake"); }
static void menu_trigger_monster(void) { if (disaster_triggers_armed("monster")) trigger_disaster_bit(5, "monster"); }

/* This table is the whole "extension" mechanism, mirroring ar-recomp's own
 * randomizer/HD-replacements pattern: each row is one self-contained
 * feature plugged in via a single field pointer or action callback, with
 * no separate plugin/registration system needed. Adding a new toggle or
 * action means adding one row here -- render_settings_menu() below never
 * needs to change. */
static const int kTerrainStyles[]={0,1,2,3,4,5};
static const char *const kTerrainStyleNames[]={"NATIVE","PROCEDURAL","ISLANDS","LAKES","RIVERS","FRACTAL"};
static SettingDesc s_settings[] = {
  /* Labels are kept short enough that the longest one plus its ON/OFF
   * value still fits the menu box at the current font size -- see
   * render_settings_menu()'s width math. */
  { "QOL",                   kSettingHeader, NULL, 0, NULL, NULL, 0 },
  { "FIT TO SCREEN",         kSettingAction, NULL, 0, menu_action_fit_screen, NULL, 0 },
  { "GPU TERRAIN",           kSettingBool, &s_gpu_terrain_enabled, 0, NULL, NULL, 0 },
  { "MOUSE CURSOR",          kSettingBool, &s_mouse_enabled,       0,    NULL, NULL, 0 },
  { "LAND GENERATION",       kSettingCycle, &s_terrain_style, 0, NULL,
    kTerrainStyles, SC_TERRAIN_STYLES, kTerrainStyleNames },
  { "DEVELOPMENT SPEED",     kSettingCycle, &s_development_override, 0, NULL,
    kDevelopmentSpeeds, 8, kDevelopmentSpeedNames },
  { "FAST TICKS",            kSettingBool, &s_fast_ticks,          0,    NULL, NULL, 0 },
  { "DRAG TURBO",            kSettingCycle, &s_drag_turbo,          0,    NULL,
    kDragTurbos, (int)(sizeof(kDragTurbos) / sizeof(kDragTurbos[0])) },
  { "PAN SPEED",             kSettingCycle, &s_pan_max_tiles,       0,    NULL,
    kPanMaxTiles, (int)(sizeof(kPanMaxTiles) / sizeof(kPanMaxTiles[0])) },
  { "MOUSE SPEED",           kSettingCycle, &s_mouse_sensitivity,   0,    NULL,
    kMouseSensitivities, (int)(sizeof(kMouseSensitivities) / sizeof(kMouseSensitivities[0])) },
  { "FAST CURSOR",           kSettingBool, &s_fast_cursor_enabled, 0,    NULL, NULL, 0 },
  { "CURSOR SPEED",          kSettingCycle, &s_fast_cursor_step,   0,    NULL,
    kFastCursorSteps, (int)(sizeof(kFastCursorSteps) / sizeof(kFastCursorSteps[0])) },
  { "REPLAY MENU",           kSettingBool, &s_replay_menu,         0,    NULL, NULL, 0 },
  { "FIX POWER ON LOAD",     kSettingBool, &s_power_fix,           0,    NULL, NULL, 0 },
  { "CHEATS",                kSettingHeader, NULL, 0, NULL, NULL, 0 },
  { "MUTE CITY WARNINGS",     kSettingBool, &s_mute_city_warnings, 0, NULL, NULL, 0 },
  { "ALL SCENARIO WON",      kSettingBool, &s_unlock_all,          0,    NULL, NULL, 0 },
  { "CHEAT NO DISASTER",     kSettingBit,  &g_ram[0x0425],         0x01, NULL, NULL, 0 },
  { "CHEAT MONEY",           kSettingBit,  &g_ram[0x0425],         0x02, NULL, NULL, 0 },
  { "CHEAT VALVE MAX",       kSettingBit,  &g_ram[0x0425],         0x04, NULL, NULL, 0 },
  { "CHEAT WATER",           kSettingBit,  &g_ram[0x0425],         0x08, NULL, NULL, 0 },
  { "SET POP",               kSettingCycle, &s_pop_override,        0,    NULL,
    kPopOverrides, (int)(sizeof(kPopOverrides) / sizeof(kPopOverrides[0])), kPopOverrideNames },
  { "SET CLASS",             kSettingCycle, &s_class_override,      0,    NULL,
    kClassOverrides, (int)(sizeof(kClassOverrides) / sizeof(kClassOverrides[0])) },
  { "CLR MILESTONE",         kSettingAction, NULL, 0, menu_action_clear_milestones, NULL, 0 },
  { "DISASTER TRIGGER",      kSettingHeader, NULL, 0, NULL, NULL, 0 },
  { "ARM TRIGGERS",          kSettingBool, &s_disaster_armed,      0,    NULL, NULL, 0 },
  { "FIRE",                  kSettingAction, NULL, 0, menu_trigger_fire,     NULL, 0 },
  { "FLOOD",                 kSettingAction, NULL, 0, menu_trigger_flood,    NULL, 0 },
  { "PLANE CRASH",           kSettingAction, NULL, 0, menu_trigger_plane,    NULL, 0 },
  { "TORNADO",               kSettingAction, NULL, 0, menu_trigger_tornado,  NULL, 0 },
  { "EARTHQUAKE",            kSettingAction, NULL, 0, menu_trigger_quake,    NULL, 0 },
  { "MONSTER",               kSettingAction, NULL, 0, menu_trigger_monster,  NULL, 0 },
  { "MELTDOWN",              kSettingAction, NULL, 0, menu_trigger_meltdown, NULL, 0 },
  { "UFO",                   kSettingAction, NULL, 0, menu_trigger_ufo,      NULL, 0 },
  { "STATE",                 kSettingHeader, NULL, 0, NULL, NULL, 0 },
  { "SAVE STATE 1",          kSettingAction, NULL, 0, menu_action_save_slot1, NULL, 0 },
  { "LOAD STATE 1",          kSettingAction, NULL, 0, menu_action_load_slot1, NULL, 0 },
};
#define kSettingCount (sizeof(s_settings) / sizeof(s_settings[0]))

static bool s_menu_open;
static int s_menu_selected;

/* SC_MENU_PREVIEW=1: force the settings menu open from frame 1, let a
 * handful of iterations render (so the window/renderer is definitely
 * live), dump the composited output to menu_preview.ppm via
 * write_renderer_ppm(), then exit -- headless visual verification of the
 * overlay UI without needing to click into the actual window. */
static bool s_menu_preview;
static int s_menu_preview_countdown = 5;

/* 5x5 bitmap font, one row per byte (bit4=leftmost col .. bit0=rightmost).
 * Coarse but complete for A-Z/0-9, so a label can't silently render a
 * blank for a glyph nobody added yet.
 *
 * The width went 3 -> 4 -> 5 over three rounds of SC_MENU_PREVIEW
 * screenshot review, each time because letters with interior diagonal
 * strokes were unreadable at the narrower size and *only* the rendered
 * image showed it: at 3 wide 'N' read as an hourglass, and at 4 wide both
 * 'M' and 'W' collapsed into something indistinguishable from 'H' (so
 * "MOUSE" read as "HOUSE" and "MONEY" as "HONEY"). 5 is the first width
 * where M/N/W each get a real interior stroke with a blank column on
 * either side. Don't narrow this again without re-checking the preview. */
typedef struct { char ch; uint8_t rows[5]; } FontGlyph;
static const FontGlyph kFont[] = {
  {' ', {0,0,0,0,0}},
  {'0', {14,17,17,17,14}}, {'1', {4,12,4,4,14}},   {'2', {14,17,2,4,31}},
  {'3', {30,1,14,1,30}},   {'4', {17,17,31,1,1}},  {'5', {31,16,30,1,30}},
  {'6', {14,16,30,17,14}}, {'7', {31,1,2,4,8}},    {'8', {14,17,14,17,14}},
  {'9', {14,17,15,1,14}},
  {'A', {14,17,31,17,17}}, {'B', {30,17,30,17,30}}, {'C', {15,16,16,16,15}},
  {'D', {30,17,17,17,30}}, {'E', {31,16,30,16,31}}, {'F', {31,16,30,16,16}},
  {'G', {15,16,19,17,15}}, {'H', {17,17,31,17,17}}, {'I', {31,4,4,4,31}},
  {'J', {7,2,2,18,12}},    {'K', {17,18,28,18,17}}, {'L', {16,16,16,16,31}},
  {'M', {17,27,21,17,17}}, {'N', {17,25,21,19,17}}, {'O', {14,17,17,17,14}},
  {'P', {30,17,30,16,16}}, {'Q', {14,17,21,18,13}}, {'R', {30,17,30,18,17}},
  {'S', {15,16,14,1,30}},  {'T', {31,4,4,4,4}},     {'U', {17,17,17,17,14}},
  {'V', {17,17,17,10,4}},  {'W', {17,17,21,27,17}}, {'X', {17,10,4,10,17}},
  {'Y', {17,10,4,4,4}},    {'Z', {31,2,4,8,31}},
  /* Punctuation, added for the Sylt briefing -- without these the fax read
   * "SYLT  GERMANY" and "DUNES  STORM SURGES", because font_glyph_rows()
   * falls back to blank for anything it does not carry. */
  {'.', {0,0,0,0,4}},      {',', {0,0,0,4,8}},     {'-', {0,0,14,0,0}},
  {'!', {4,4,4,0,4}},      {'?', {14,17,2,0,4}},   {':', {0,4,0,4,0}},
  {'\'', {4,4,0,0,0}},    {'/', {1,2,4,8,16}},
};
#define kFontCount (sizeof(kFont) / sizeof(kFont[0]))

static const uint8_t *font_glyph_rows(char c) {
  for (size_t i = 0; i < kFontCount; i++)
    if (kFont[i].ch == c) return kFont[i].rows;
  return kFont[0].rows; /* unknown char -> blank */
}

/* Draws text at (x,y) in real renderer pixels, each font pixel drawn as a
 * `px`x`px` filled square. Uppercases input so call sites can write labels
 * in whatever case is convenient. */
static void draw_text(SDL_Renderer *renderer, int x, int y, int px, const char *s) {
  int cx = x;
  for (const char *p = s; *p; p++) {
    char c = (char)toupper((unsigned char)*p);
    const uint8_t *rows = font_glyph_rows(c);
    for (int row = 0; row < 5; row++)
      for (int col = 0; col < 5; col++)
        if (rows[row] & (1 << (4 - col))) {
          ScRect r = SC_RECT(cx + col * px, y + row * px, px, px);
          SDL_RenderFillRect(renderer, &r);
        }
    cx += 6 * px; /* 5 cols of glyph + 1 col of spacing */
  }
}

static int text_width(int px, const char *s) {
  int n = (int)strlen(s);
  return n > 0 ? n * 6 * px - px : 0;
}

typedef struct SettingsLayout { int px, line_h, pad, x, y, w, h; } SettingsLayout;
static SettingsLayout settings_layout(int out_w, int out_h) {
  const int lines = (int)kSettingCount + 6 + (s_menu_preview ? 4 : 0);
  int px = out_h / (6 * (lines + 1));
  if (px > 4) px = 4;
  if (px < 1) px = 1;
  SettingsLayout r = {px, 6*px, 3*px, 0, 0, out_w*3/4, 6*px*(lines+1)};
  r.x = (out_w-r.w)/2; r.y = (out_h-r.h)/2;
  return r;
}
static int settings_mouse_row(SDL_Window *window, SDL_Renderer *renderer, double wx, double wy) {
  int ww, wh, dw, dh;
  SDL_GetWindowSize(window, &ww, &wh); SDL_GetRendererOutputSize(renderer, &dw, &dh);
  if (ww<=0 || wh<=0 || dw<=0 || dh<=0) return -1;
  double x=wx*dw/ww, y=wy*dh/wh;
  SettingsLayout r=settings_layout(dw,dh);
  int first_y=r.y+r.pad+2*r.line_h;
  if (x<r.x+r.pad || x>=r.x+r.w-r.pad || y<first_y ||
      y>=first_y+(int)kSettingCount*r.line_h) return -1;
  int row=(int)((y-first_y)/r.line_h);
  return s_settings[row].kind==kSettingHeader ? -1 : row;
}

static void render_settings_menu(SDL_Renderer *renderer) {
  int out_w = 0, out_h = 0;
  SDL_GetRendererOutputSize(renderer, &out_w, &out_h);

  /* Font pixel size, in real screen pixels. Adaptive rather than a fixed 4:
   * the box is pad*2 + line_h*lines tall with line_h = 6*px and pad = 3*px,
   * i.e. 6*px*(lines+1), so a long enough list pushes menu_y negative and
   * silently clips the title off the top of the window. That is exactly what
   * adding the six disaster triggers did. Shrink to fit instead, capped at
   * the original 4 so short lists look unchanged. */
  SettingsLayout layout=settings_layout(out_w,out_h);
  int px=layout.px, line_h=layout.line_h, pad=layout.pad;
  int menu_w=layout.w, menu_h=layout.h, menu_x=layout.x, menu_y=layout.y;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
  ScRect bg = SC_RECT(menu_x, menu_y, menu_w, menu_h);
  SDL_RenderFillRect(renderer, &bg);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_RenderDrawRect(renderer, &bg);

  int ty = menu_y + pad;
  draw_text(renderer, menu_x + pad, ty, px, "SETTINGS");
  ty += line_h * 2;

  for (size_t i = 0; i < kSettingCount; i++) {
    SettingDesc *d = &s_settings[i];
    bool selected = ((int)i == s_menu_selected);
    bool header = (d->kind == kSettingHeader);
    if (header) {
      /* Section labels sit flush left in a dimmer grey; the rows under them
       * are indented, so the grouping is visible without needing rules or a
       * second font. */
      SDL_SetRenderDrawColor(renderer, 150, 150, 255, 255);
      draw_text(renderer, menu_x + pad, ty, px, d->label);
      ty += line_h;
      continue;
    }
    SDL_SetRenderDrawColor(renderer, 255, selected ? 255 : 255, selected ? 0 : 255, 255);
    draw_text(renderer, menu_x + pad + 4 * px, ty, px, d->label);
    if (d->kind != kSettingAction) {
      char numbuf[16];
      const char *val;
      if (d->kind == kSettingCycle) {
        int cv = *(int *)d->field;
        int idx = -1;
        for (int k = 0; k < d->value_count; k++) if (d->values[k] == cv) idx = k;
        if(d->field==&s_development_override && !cv) {
          snprintf(numbuf,sizeof numbuf,"OFF (CITY X%u)",ScWorldDevelopmentSpeed(&s_world));
          val=numbuf;
        } else if (d->value_names && idx >= 0) {
          val = d->value_names[idx];
        } else if (cv < 0) {
          val = "OFF"; /* negative sentinel, so 0 stays a real selectable value */
        } else {
          snprintf(numbuf, sizeof(numbuf), "%d", cv);
          val = numbuf;
        }
      } else {
        val = setting_get(d) ? "ON" : "OFF";
      }
      int label_w = text_width(px, d->label);
      draw_text(renderer, menu_x + pad + 4 * px + label_w + 8 * px, ty, px, val);
    }
    ty += line_h;
  }

  ty += line_h / 2;
  SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
  draw_text(renderer, menu_x + pad, ty, px - 1, "UP DOWN SELECT");
  ty += line_h - px;
  draw_text(renderer, menu_x + pad, ty, px > 1 ? px - 1 : 1, "ENTER OR CLICK TOGGLE");
  ty += line_h - px;
  draw_text(renderer, menu_x + pad, ty, px - 1, "F12 CLOSE");

  /* Under SC_MENU_PREVIEW only: render the full glyph set so a single
   * preview screenshot verifies every character, not just the ones the
   * current labels happen to use. Two missing glyphs ('P', then 'B')
   * already shipped as blanks precisely because nothing exercised them
   * until a label needed them. */
  if (s_menu_preview) {
    ty += line_h;
    draw_text(renderer, menu_x + pad, ty, px - 1, "ABCDEFGHIJKLM");
    ty += line_h - px;
    draw_text(renderer, menu_x + pad, ty, px - 1, "NOPQRSTUVWXYZ");
    ty += line_h - px;
    draw_text(renderer, menu_x + pad, ty, px - 1, "0123456789");
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

/* The Sylt map, supplied at run time instead of patched into the ROM.
 *
 * The map arrives as an IPS that drops a compressed map at $108000 and
 * repoints scenario index 5 -- Rio -- at it, so applying it plainly would
 * replace Rio. The companion patch exists to relocate Rio first. Neither is
 * applied here:
 *
 *   * patching the ROM changes its FNV, and the check in main() matches that
 *     exactly to set s_rom_is_us. That flag gates the host map renderer,
 *     SC_FIBER, the cursor-cadence patch and the view fix, so a patched image
 *     quietly loses all four.
 *   * index 8 is the PRACTICE map, not a spare slot. Overriding its data
 *     unconditionally would hand Sylt to the tutorial.
 *
 * So the map is written into WRAM only when the ninth entry was actually
 * chosen. 03:ce2e decompresses a scenario's map to $7E8000 and 03:ce5e unpacks
 * it with JSR $d15f; replacing the buffer just before that JSR hands the game
 * its own intermediate form and lets its own unpacker do the work. Rio keeps
 * its slot, practice keeps its map, the ROM is untouched, and Sylt still loads
 * through the ROM's own path. */

/* A data file shipped with the program (sylt_graphics/...): from the working
 * directory, where a portable folder keeps it, else from the program's own
 * directory -- where an installed build keeps it, since there the working
 * directory is the player's data folder instead. */
static FILE *sc_fopen_data(const char *rel) {
  FILE *f = fopen(rel, "rb");
  if (f) return f;
#if SNESRECOMP_SDL3
  const char *base = SDL_GetBasePath();
#else
  char *base = SDL_GetBasePath();
#endif
  if (!base) return NULL;
  char path[1024];
  snprintf(path, sizeof path, "%s%s", base, rel);
#if !SNESRECOMP_SDL3
  SDL_free(base);
#endif
  return fopen(path, "rb");
}

static void load_sylt_map(void) {
  const char *path = getenv("SC_SYLT_MAP");
  FILE *f = path ? fopen(path, "rb")
                 : sc_fopen_data(path = "sylt_graphics/sylt_map.bin");
  if (!f) return;                       /* absent is not an error */
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
  const long n = ftell(f);
  /* $7E8000 upward is the decompression scratch; the largest shipped map
   * unpacks from 10068 bytes, so anything near that is plausible and anything
   * far past it is not. */
  if (n <= 0 || n > 0x4000) {
    fprintf(stderr, "[sylt] %s: implausible map size %ld, ignored", path, n);
    fputc('\n', stderr);
    fclose(f);
    return;
  }
  rewind(f);
  s_sylt_map = (uint8_t *)malloc((size_t)n);
  if (!s_sylt_map) { fclose(f); return; }
  if (fread(s_sylt_map, 1, (size_t)n, f) != (size_t)n) {
    free(s_sylt_map); s_sylt_map = NULL; fclose(f); return;
  }
  fclose(f);
  s_sylt_map_len = n;
  fprintf(stderr, "[sylt] loaded %s (%ld bytes, unpacked by the ROM's own "
          "JSR $d15f)", path, n);
  fputc('\n', stderr);
}

/* The Sylt card, as real BG1 tiles.
 *
 * Everything here was measured off the live selector rather than assumed:
 *
 *   layer      mode 0, BG1 the only layer on the main screen,
 *              tilemap $3000, wide=1 (64 columns), character base $0000
 *   cards      8 columns x 9 rows of body, at columns 12/22/32 -- ten apart,
 *              so the fifth lands on column 42 -- rows 5..13, with a drop
 *              shadow of tile $0010 down the right edge and along the bottom
 *   palette    2, whose entries are black / #94948b / #eeeecd / #73736a.
 *              That is the exact range the shipped card art uses, so a
 *              greyscale drawing needs no per-tile palette juggling
 *   free CHR   tiles $24b..$2d5 and $2e0..$3ff are blank in VRAM AND
 *              unreferenced by the tilemap -- 427 slots, against the 72 a
 *              card needs
 *
 * The card sits off-screen at the stock column-3 scroll ($50 shows columns
 * 10..41) and fully on-screen at the ninth column's $a0 (columns 20..51), so
 * it can only appear when it should. The tilemap wraps at 64 columns, which
 * caps any scroll at 256 before column 0 would reappear on the right; $a0 is
 * well inside that.
 *
 * Card names on the shipped cards are pre-rendered word strips placed as
 * sprites, and there is no "Sylt" strip to borrow -- the strips are whole
 * words, not glyphs, so one cannot be composed either. The drawn card carries
 * its own caption instead, which is why the whole 64x72 block comes from the
 * artwork rather than just the thumbnail. */
#define SC_SYLT_TILE_BASE 0x2e0u   /* start of the 288-slot free run */
#define SC_SYLT_COL 42
#define SC_SYLT_ROW 5
#define SC_SYLT_PAL 2u
#define SC_SYLT_SHADOW 0x0010u

static uint16_t *s_sylt_tiles;      /* tiles_w * tiles_h * 8 words */
static int s_sylt_tw, s_sylt_th;

/* Sylt's card is this project's own artwork and draws its disaster line as
 * pixels -- "Coastal" on row 6, "Flooding" on row 7 -- so the selector packet
 * patch, which only repoints tilemap cells at shipped strips, cannot reach
 * it. When a patch is loaded it carries the donor's word for the same
 * disaster (the strip Rio's card uses) as six tiles of artwork. Row 7 is
 * blanked from this card's own background rather than the donor's, so the
 * one-line German word does not leave "Flooding" underneath it. */
/* -- SC_WRAM_WATCH: name the instruction that writes a WRAM range ---------
 *
 * SC_WRAM_WATCH="LO:HI" (hex g_ram offsets, HI optional) prints every write
 * in that range with the 65816 PC of the instruction that made it.
 *
 * This exists because none of the runtime's own watchpoints work in this
 * target. cpu_trace's WRAM watch, SNESRECOMP_WRITE_WATCH and
 * SNESRECOMP_WRAM_WATCH all sit on the AOT/CpuState write path
 * (cpu_write8/16), and this host executes through the interp816 core, so
 * those functions are never called -- a watch on $7E:2000, written every
 * frame, stayed silent. See docs/ROM_MAP.md.
 *
 * What DOES reach the interpreter's stores is snes.c's own hook: all three
 * direct-WRAM store sites call snes_note_direct_wram_write(), which forwards
 * to a settable function pointer. So this needs no submodule change at all;
 * it just installs a callback the runtime already offers.
 *
 * The PC is g_interp816_cur_pc, the interpreter's current instruction -- so
 * a hit is not "something changed this", it is the address of the store. */
extern uint32_t g_interp816_cur_pc;
static uint32_t s_ww_lo, s_ww_hi; static long s_ww_hits, s_ww_cap = 400;
/* SC_WRAM_WATCH_FROM=<frame>: the boot clear loop writes every byte of WRAM
 * at frame 2 and would otherwise spend the whole budget saying so. */
static unsigned long long s_ww_from;

static void sc_wram_write_probe(uint32_t off, uint8_t val, const char *via) {
  if (off < s_ww_lo || off > s_ww_hi || s_ww_hits >= s_ww_cap) return;
  if (s_frames < s_ww_from) return;
  fprintf(stderr, "[wramwrite] f%llu  $%05X = %02X  by %02X:%04X  via %s\n",
          (unsigned long long)s_frames, off, val,
          (unsigned)((g_interp816_cur_pc >> 16) & 0xff),
          (unsigned)(g_interp816_cur_pc & 0xffff), via ? via : "-");
  s_ww_hits++;
}

/* -- SC_DMA_VRAM: every VRAM DMA, with the WRAM buffer it came from -------
 *
 * SC_DMA_VRAM=1 lists each VRAM transfer as source -> destination. Optional
 * SC_DMA_VRAM_AT=<hex vmadd> reports only transfers covering that VRAM word.
 *
 * For finding where a screen's text is composed. The status/advisor block is
 * ROM text (docs/ROM_MAP.md) but how the game indexes it is unknown, and
 * unlike the building labels it draws to a tilemap rather than to the sprite
 * staging buffer -- so SC_WRAM_WATCH has nothing to watch until the WRAM
 * buffer behind that tilemap is known. This names it: the DMA source IS that
 * buffer, and SC_WRAM_WATCH pointed there then names the writing routine. */
static uint32_t s_dv_at = 0xffffffffu;
static bool s_dv_on;                 /* SC_DMA_VRAM: log them */

static void sc_dma_vram_probe(uint8_t aBank, uint16_t aAdr,
                              uint16_t vmadd, uint16_t size) {
  const uint32_t words = size ? (uint32_t)size / 2u : 0x8000u;
  if (s_dv_at != 0xffffffffu &&
      !(vmadd <= s_dv_at && s_dv_at < vmadd + words)) return;
  fprintf(stderr, "[dmavram] f%llu $%02x  $%02X:%04X -> vram $%04X  %u bytes  (%u words)\n",
          (unsigned long long)s_frames, g_ram[0x14], aBank, aAdr, vmadd,
          size ? size : 0x10000u, words);
}

/* -- SC_VRAM_WRITE_WATCH: name the instruction that writes a VRAM range ---
 *
 * SC_VRAM_WRITE_WATCH="LO:HI" (hex VRAM WORD addresses) prints each CPU
 * write through $2118/$2119 in that range with the 65816 PC behind it.
 *
 * The counterpart of SC_WRAM_WATCH, and needed because the status text box
 * has no WRAM staging buffer to watch: SC_DMA_VRAM showed there is no
 * per-frame tilemap DMA in game at all, so the text is written straight to
 * VRAM. The debug server does carry a VRAM trace, but it is gated on
 * SNESRECOMP_REVERSE_DEBUG, allocates gigabytes of ring, and records
 * g_last_recomp_func -- an AOT function name, empty on the interp816 path
 * this host runs. So ppu.c got a settable hook beside its existing
 * debug_server_on_vram_write() calls, the same shape snes.c already had for
 * WRAM, and this installs a callback on it. */
static uint32_t s_vw_lo, s_vw_hi; static long s_vw_hits, s_vw_cap = 400;
static unsigned long long s_vw_from;
static int s_vw_nz;
static int s_vw_scr = -1;

static void sc_vram_write_probe(uint32_t byte_addr, uint8_t value) {
  const uint32_t word = byte_addr >> 1;
  if (word < s_vw_lo || word > s_vw_hi || s_vw_hits >= s_vw_cap) return;
  if (s_frames < s_vw_from) return;
  /* SC_VRAM_WRITE_WATCH_NZ=1 drops zero writes. Screens are blanked with a
   * clear loop before anything is drawn, and it spends the whole budget
   * in one frame -- 600 hits at 00:869D, all of them zeros. */
  if (s_vw_nz && value == 0) return;
  /* SC_VRAM_WRITE_WATCH_SCREEN=<hex $14>: only that screen. The title
   * animates constantly and spends any budget before a later screen draws
   * anything -- 4000 hits at 00:8D43 without ever reaching the menu. */
  if (s_vw_scr >= 0 && g_ram[0x14] != (uint8_t)s_vw_scr) return;
  fprintf(stderr, "[vramwrite] f%llu  $%04X%s = %02X  by %02X:%04X\n",
          (unsigned long long)s_frames, word, (byte_addr & 1) ? "h" : "l",
          value, (unsigned)((g_interp816_cur_pc >> 16) & 0xff),
          (unsigned)(g_interp816_cur_pc & 0xffff));
  s_vw_hits++;
}

static void sc_vram_write_watch_install(void) {
  const char *e = getenv("SC_VRAM_WRITE_WATCH");
  if (!e || !*e) return;
  char *end = NULL;
  s_vw_lo = (uint32_t)strtoul(e, &end, 16);
  s_vw_hi = (end && *end == ':') ? (uint32_t)strtoul(end + 1, NULL, 16)
                                 : s_vw_lo + 0x3f;
  { const char *m = getenv("SC_VRAM_WRITE_WATCH_MAX");
    if (m && *m) s_vw_cap = strtol(m, NULL, 0); }
  { const char *f = getenv("SC_VRAM_WRITE_WATCH_FROM");
    if (f && *f) s_vw_from = strtoull(f, NULL, 0); }
  { const char *z = getenv("SC_VRAM_WRITE_WATCH_NZ");
    s_vw_nz = (z && *z && *z != '0'); }
  { const char *sc = getenv("SC_VRAM_WRITE_WATCH_SCREEN");
    if (sc && *sc) s_vw_scr = (int)strtol(sc, NULL, 16); }
  ppu_set_vram_write_log_hook(sc_vram_write_probe);
  fprintf(stderr, "vram write watch: $%04X..$%04X, from frame %llu, up to %ld hits\n",
          s_vw_lo, s_vw_hi, (unsigned long long)s_vw_from, s_vw_cap);
}

/* The selector's tiles and map arriving, on its fade-in screen $0A.
 *
 * $0A covers the fax (or menu) fading out, one DMA of the selector's whole
 * set -- $7E:8000 to VRAM $0000, 28 KB, map at $3000 included -- and the
 * selector fading in. Its own additions -- the wood past column 44, the
 * translated cards, Sylt's card -- were made at 03:ddb6, which runs once the
 * fade-in is over, so the fade showed the shipped map: black on the right,
 * no Sylt, and Sylt's pin and mark hanging over the black. Flagged here at the
 * DMA and made at the next frame's start (selector_after_upload), when the
 * transfer is done. */
static bool s_sel_upload_seen;

static void sc_dma_vram_notify(uint8_t aBank, uint16_t aAdr, uint16_t vmadd,
                               uint16_t size) {
  if (s_dv_on) sc_dma_vram_probe(aBank, aAdr, vmadd, size);
  const uint32_t words = size ? (uint32_t)size / 2u : 0x8000u;
  if (g_ram[0x14] == 0x0a && vmadd <= 0x3000u && vmadd + words >= 0x3800u)
    s_sel_upload_seen = true;
}

/* Every frame of $0A after that DMA, while BG1 is the selector's map ($3000,
 * 64 columns wide): the same three 03:ddb6 makes, so they are there from the
 * fade-in's first light. Repeating them is harmless -- 03:ddb6 does them
 * again anyway -- and covers a map row the upload finishes late. */
static void selector_after_upload(void) {
  if (g_ram[0x14] != 0x0a) { s_sel_upload_seen = false; return; }
  if (!s_sel_upload_seen || !g_ppu || !s_ninth_scenario || !s_rom_is_us) return;
  if (PPU_bgTilemapAdr(g_ppu, 0) != 0x3000 || !PPU_bgTilemapWider(g_ppu, 0))
    return;
  selector_extend_tilemap();
  place_translated_cards();   /* cards first: see 03:ddb6 */
  sylt_place_card();
}

static void sc_dma_vram_install(void) {
  dma_set_vram_notify_hook(sc_dma_vram_notify);
  const char *e = getenv("SC_DMA_VRAM");
  if (!e || !*e || *e == '0') return;
  s_dv_on = true;
  { const char *at = getenv("SC_DMA_VRAM_AT");
    if (at && *at) s_dv_at = (uint32_t)strtoul(at, NULL, 16); }
  if (s_dv_at != 0xffffffffu)
    fprintf(stderr, "dma vram probe: only transfers covering vram $%04X\n",
            s_dv_at);
  else
    fprintf(stderr, "dma vram probe: every VRAM transfer\n");
}

static void sc_wram_watch_install(void) {
  const char *e = getenv("SC_WRAM_WATCH");
  if (!e || !*e) return;
  char *end = NULL;
  s_ww_lo = (uint32_t)strtoul(e, &end, 16);
  s_ww_hi = (end && *end == ':') ? (uint32_t)strtoul(end + 1, NULL, 16)
                                 : s_ww_lo + 0x27;
  { const char *m = getenv("SC_WRAM_WATCH_MAX");
    if (m && *m) s_ww_cap = strtol(m, NULL, 0); }
  { const char *f = getenv("SC_WRAM_WATCH_FROM");
    if (f && *f) s_ww_from = strtoull(f, NULL, 0); }
  snes_set_wram_write_log_hook(sc_wram_write_probe);
  fprintf(stderr, "wram watch: $%05X..$%05X, from frame %llu, up to %ld hits\n",
          s_ww_lo, s_ww_hi, (unsigned long long)s_ww_from, s_ww_cap);
}

static void sylt_apply_translated_line(void) {
  if (!s_sylt_tiles || s_sylt_tw != 8 || s_sylt_th < 8) return;
  if (s_scpk_count < 0) scpk_load();
  const ScpkEntry *e = NULL;
  for (int i = 0; i < s_scpk_count; i++)
    if (s_scpk[i].src == 0xffffffu) { e = &s_scpk[i]; break; }
  if (!e) return;
  /* Both drawn lines go first, from each row's own leftmost tile, which is
   * card background. Blanking both and letting the patch choose where to
   * land keeps that choice in the tool rather than split across the two. */
  for (int row = 6; row <= 7; row++) {
    const int base = row * s_sylt_tw;
    for (int tx = 1; tx < s_sylt_tw; tx++)
      memcpy(&s_sylt_tiles[(size_t)(base + tx) * 8],
             &s_sylt_tiles[(size_t)base * 8], 8 * sizeof(uint16_t));
  }
  int n = 0;
  for (int k = 0; k < e->nspans; k++) {
    const uint32_t slot = e->spans[k].off / 16u;
    if (e->spans[k].len != 16 ||
        slot >= (uint32_t)(s_sylt_tw * s_sylt_th)) continue;
    for (int j = 0; j < 8; j++)
      s_sylt_tiles[slot * 8 + j] =
          (uint16_t)(e->spans[k].data[j * 2] | (e->spans[k].data[j * 2 + 1] << 8));
    n++;
  }
  fprintf(stderr, "[sylt] disaster line replaced from the packet patch (%d tiles)\n", n);
}

static void load_sylt_card(void) {
  const char *path = getenv("SC_SYLT_CARD");
  FILE *f = path ? fopen(path, "rb")
                 : sc_fopen_data(path = "sylt_graphics/sylt_card.bin");
  if (!f) return;                       /* absent is not an error */
  uint16_t hdr[4];
  if (fread(hdr, sizeof hdr, 1, f) != 1) { fclose(f); return; }
  const int tw = hdr[0], th = hdr[1];
  if (tw <= 0 || th <= 0 || tw > 16 || th > 16) {
    fprintf(stderr, "[sylt] %s: implausible size %dx%d tiles, ignored", path, tw, th);
    fputc('\n', stderr);
    fclose(f);
    return;
  }
  const size_t words = (size_t)tw * th * 8u;
  s_sylt_tiles = (uint16_t *)malloc(words * sizeof(uint16_t));
  if (!s_sylt_tiles) { fclose(f); return; }
  for (size_t i = 0; i < words; i++) {
    int lo = fgetc(f), hi = fgetc(f);
    if (lo < 0 || hi < 0) { free(s_sylt_tiles); s_sylt_tiles = NULL; fclose(f); return; }
    s_sylt_tiles[i] = (uint16_t)(lo | (hi << 8));
  }
  fclose(f);
  s_sylt_tw = tw; s_sylt_th = th;
  sylt_apply_translated_line();
  fprintf(stderr, "[sylt] loaded %s (%dx%d tiles -> CHR $%03x..$%03x)", path,
          tw, th, SC_SYLT_TILE_BASE, SC_SYLT_TILE_BASE + tw * th - 1);
  fputc('\n', stderr);
}

/* BOTH the character data and the tilemap are rewritten every frame.
 *
 * The character data was uploaded once behind a static flag, and that was
 * wrong: leaving the selector and coming back re-runs the screen's own setup,
 * which reloads VRAM and wipes the tiles at $2e0 while the tilemap entries
 * still point at them -- so the card came back pitch black. Reported from play
 * on returning from the fax. 72 tiles is 576 words a frame, which is nothing. */
static void sylt_place_card(void) {
  if (!s_sylt_tiles || !g_ppu) return;
  ScPpuVramChanged();
  const unsigned map = (unsigned)PPU_bgTilemapAdr(g_ppu, 0);

  for (int i = 0; i < s_sylt_tw * s_sylt_th; i++) {
    const unsigned dst = (SC_SYLT_TILE_BASE + (unsigned)i) * 8u;
    if (dst + 8u > 0x8000u) break;
    for (int k = 0; k < 8; k++)
      g_ppu->vram[dst + k] = s_sylt_tiles[i * 8 + k];
  }

  for (int ty = 0; ty < s_sylt_th; ty++)
    for (int tx = 0; tx < s_sylt_tw; tx++) {
      const int col = SC_SYLT_COL + tx, row = SC_SYLT_ROW + ty;
      if (col > 63 || row > 31) continue;
      const unsigned idx = map + (col < 32 ? 0u : 0x400u)
                         + (unsigned)row * 32u + (unsigned)(col & 31);
      if (idx >= 0x8000u) continue;
      g_ppu->vram[idx] = (uint16_t)((SC_SYLT_PAL << 10)
                       | (SC_SYLT_TILE_BASE + (unsigned)(ty * s_sylt_tw + tx)));
    }

  /* The same drop shadow the other cards carry: down the right edge, then
   * along the bottom. Without it the ninth card floats where the rest sit. */
  for (int ty = 1; ty <= s_sylt_th; ty++) {
    const int col = SC_SYLT_COL + s_sylt_tw, row = SC_SYLT_ROW + ty;
    if (col > 63 || row > 31) continue;
    g_ppu->vram[map + (col < 32 ? 0u : 0x400u) + (unsigned)row * 32u
                + (unsigned)(col & 31)] = SC_SYLT_SHADOW;
  }
  for (int tx = 1; tx <= s_sylt_tw; tx++) {
    const int col = SC_SYLT_COL + tx, row = SC_SYLT_ROW + s_sylt_th;
    if (col > 63 || row > 31) continue;
    g_ppu->vram[map + (col < 32 ? 0u : 0x400u) + (unsigned)row * 32u
                + (unsigned)(col & 31)] = SC_SYLT_SHADOW;
  }
}

/* The Sylt briefing, as a real tilemap handed to the ROM's own fax renderer.
 *
 * Index 8 falls off the eight-entry seed tables, so the ROM decompresses the
 * free-play welcome (group-1 block 6, file 0x05FBE7) for it -- the wrong text
 * for a scenario. Rather than paint over the fax, this overwrites the block
 * after it lands in WRAM, so the game types it out, fades it and dismisses it
 * exactly like the other eight briefings. Nothing is patched in the ROM.
 *
 * Each briefing is a 32x64 tilemap of 16-bit entries at $7E8000, decompressed
 * by 00:90eb. The character encoding was read off the shipped blocks -- the
 * Las Vegas title row decodes as "Las Vegas, U.S.A. 2096" against it:
 *
 *   title font   upper $000 + (c-'A')   lower $030 + (c-'a')
 *   body font    upper $690 + (c-'A')   lower $6C0 + (c-'a')
 *                -- bit 10 is the palette select. Dropping it renders the
 *                body in the title's red; the shipped blocks set it.
 *   ','  base+$1C     '.'  base+$1D     apostrophe base+$1E
 *   digits  base+$20 + d                blank  $3FF
 *
 * KNOWN: the fax renderer drops the first character of every word, so this
 * will show as "ylt, ermany 047" exactly as the shipped briefings show as
 * "as egas, .S.A. 096". The stored tilemaps are complete -- decoding block 7
 * gives "Las Vegas, the world's largest gambling city," in full -- so that is
 * a display bug, not a data one, and it is not this feature's to fix. */

static void brief_put(uint8_t *dst, int row, int col, const char *s,
                      unsigned up, unsigned lo) {
  for (; *s; s++, col++) {
    if (col < 0 || col >= SC_BRIEF_COLS) continue;
    unsigned t = SC_BRIEF_BLANK;
    /* unsigned: the accented characters arrive as Latin-1 bytes >= $80,
     * which a signed char would make negative and never match. */
    const unsigned char c = (unsigned char)*s;
    if (c >= 'A' && c <= 'Z')      t = up + (unsigned)(c - 'A');
    else if (c >= 'a' && c <= 'z') t = lo + (unsigned)(c - 'a');
    else if (c >= '0' && c <= '9') t = up + 0x20u + (unsigned)(c - '0');
    else if (c == ',')             t = up + 0x1Cu;
    else if (c == '.')             t = up + 0x1Du;
    else if (c == 39)              t = up + 0x1Eu;   /* apostrophe, unescaped */
    /* Slots outside the two letter banks, identified from the German
     * words they sit inside. Fixed tile numbers rather than up-relative:
     * the four accented ones are blank in the US tileset, so the donor's
     * artwork is copied in at the same numbers; the hyphen the US draws
     * already. */
    else if (c == 0xFCu)          t = 0x6F1u;   /* u-umlaut */
    else if (c == 0xE4u)          t = 0x6F4u;   /* a-umlaut */
    else if (c == 0xF6u)          t = 0x704u;   /* o-umlaut */
    else if (c == 0xDFu)          t = 0x70Bu;   /* sharp s   */
    /* NOT $69D: that is the German slot number, and on the US side $69D is
     * up+13 -- the letter N, which is what it drew. The donor's hyphen is
     * copied into this free slot instead. */
    else if (c == '-')            t = 0x6ECu;
    else if (c == 0xE9u)          t = 0x6EDu;   /* e-acute, French */
    else if (c == 0xE8u)          t = 0x6EEu;   /* e-grave, French */
    else if (c == 0xE2u)          t = 0x6EFu;   /* a-circumflex     */
    else if (c == 0xEFu)          t = 0x6F0u;   /* i-diaeresis      */
    /* space, and anything with no glyph, stays blank */
    const size_t i = (size_t)(row * SC_BRIEF_COLS + col) * 2u;
    dst[i]     = (uint8_t)(t & 0xffu);
    dst[i + 1] = (uint8_t)(t >> 8);
  }
}

/* Laid out on the same rows the shipped briefings use: title on row 2, body
 * from row 4, both indented four columns. */
/* 24 columns is the ROM's own limit: decoding the nine shipped blocks, text
 * occupies columns 4..27 and never runs past it. The first cut went to 28 and
 * spilled off the right edge of the paper. */
static const char *const kSyltBody[] = {
  "The North Sea has taken",
  "the dunes. Storm surges",
  "break over the marsh at",
  "every spring tide, and",
  "the ferry harbour floods",
  "twice a year. The",
  "islanders have voted to",
  "build rather than leave.",
  "",
  "Raise a working town on",
  "the sand within 5 years.",
};

static void sylt_write_brief_tilemap(void) {
  uint8_t *dst = &g_ram[0x8000];          /* $7E8000 */
  for (int i = 0; i < SC_BRIEF_COLS * SC_BRIEF_ROWS; i++) {
    dst[i * 2]     = (uint8_t)(SC_BRIEF_BLANK & 0xffu);
    dst[i * 2 + 1] = (uint8_t)(SC_BRIEF_BLANK >> 8);
  }
  brief_put(dst, 2, 5, "Sylt, Germany 2047", 0x000u, 0x030u);
  for (int i = 0; i < (int)(sizeof(kSyltBody) / sizeof(kSyltBody[0])); i++)
    brief_put(dst, 4 + i, 4, kSyltBody[i], 0x690u, 0x6C0u);
  fprintf(stderr, "[sylt] briefing tilemap written to $7E8000");
  fputc('\n', stderr);
}

/* Apply any surface whose screen is the one now showing. A surface carries
 * its tiles already REMAPPED to indices the target screen left free, so
 * writing them cannot land on artwork that is already correct -- which is
 * what the naive same-index card copy did to the card names.
 *
 * Applied once per visit to the screen, not once ever: the game rebuilds
 * these tilemaps each time it enters. */
static void apply_surfaces(void) {
  static int last_screen = -1;
  if (!s_surf_count || !g_ppu) return;
  const int scr = g_ram[0x14];
  if (scr == last_screen) return;
  last_screen = scr;
  ScPpuVramChanged();
  for (int i = 0; i < s_surf_count; i++) {
    const uint8_t *s = s_surf[i];
    if (s[6] != (uint8_t)scr) continue;
    const unsigned tmap = (unsigned)(s[7] | (s[8] << 8));
    const unsigned nw = s[9];
    const unsigned nrows = s[10];
    const uint8_t *rows = s + 11;
    const unsigned ncols = rows[nrows];
    const uint8_t *cols = rows + nrows + 1;
    const uint8_t *p = cols + ncols;
    const unsigned ntiles = (unsigned)(p[0] | (p[1] << 8)); p += 2;
    const uint8_t *ents = p;
    const uint8_t *art = ents + (size_t)nrows * ncols * 2u;
    if ((size_t)(art - s) + (size_t)ntiles * (2u + nw * 2u) > s_surf_len[i])
      continue;                       /* truncated: leave the screen alone */
    for (unsigned t = 0; t < ntiles; t++) {
      const uint8_t *e = art + (size_t)t * (2u + nw * 2u);
      const unsigned idx = (unsigned)(e[0] | (e[1] << 8));
      if ((size_t)idx * nw + nw > 0x8000u) continue;
      for (unsigned k = 0; k < nw; k++)
        g_ppu->vram[idx * nw + k] =
            (uint16_t)(e[2 + k * 2] | (e[3 + k * 2] << 8));
    }
    /* Only the listed columns. Writing a whole row would replace the
     * background, which is the target's and not the donor's. */
    for (unsigned r = 0; r < nrows; r++)
      for (unsigned ci = 0; ci < ncols; ci++) {
        const unsigned c = cols[ci];
        const size_t k = ((size_t)r * ncols + ci) * 2u;
        const unsigned v = (unsigned)(ents[k] | (ents[k + 1] << 8));
        const unsigned at = tmap + (c < 32 ? 0u : 0x400u)
                          + (unsigned)rows[r] * 32u + (c & 31u);
        if (at < 0x8000u) g_ppu->vram[at] = (uint16_t)v;
      }
    { static int said; if (!said++)
        fprintf(stderr, "translation: surface applied on screen $%02x\n", scr); }
  }
}

/* Write the translated scenario cards into VRAM: the art of each tile the
 * card references, then its 8x9 tilemap. Same operation and same moment as
 * sylt_place_card(), which places the ninth card -- these are the other
 * eight. Column 42 is never carried in the blob, so Sylt is untouched. */
static void place_translated_cards(void) {
  if (!s_card_count || !g_ppu) return;
  /* A packet patch puts the same cards in through the game's own DMA, and
   * doing both means this one races the other and loses. */
  if (scpk_active()) return;
  ScPpuVramChanged();
  const unsigned map = (unsigned)PPU_bgTilemapAdr(g_ppu, 0);
  for (int i = 0; i < s_card_count; i++) {
    for (int t = 0; t < s_card_nt[i]; t++) {
      const unsigned base = (unsigned)s_card_tid[i][t] * 16u;
      if (base + 16u > 0x8000u) continue;
      for (int k = 0; k < 16; k++)
        g_ppu->vram[base + k] = (uint16_t)(s_card_px[i][t][k * 2] |
                                          (s_card_px[i][t][k * 2 + 1] << 8));
    }
    if (s_card_h[i] == 0) continue;   /* tiles only: no tilemap write */
    for (int y = 0; y < s_card_h[i]; y++)
      for (int x = 0; x < s_card_w[i]; x++) {
        const int col = s_card_col[i] + x, row = s_card_row[i] + y;
        if (col > 63 || row > 31) continue;
        const unsigned idx = map + (col < 32 ? 0u : 0x400u)
                           + (unsigned)row * 32u + (unsigned)(col & 31);
        if (idx >= 0x8000u) continue;
        g_ppu->vram[idx] = s_card_map[i][y * s_card_w[i] + x];
      }
  }
  { static int said; if (!said++)
      fprintf(stderr, "translation: %d scenario cards placed\n", s_card_count); }
}

/* Compose a translated briefing page from strings -- the same two-bank
 * layout sylt_write_brief_tilemap() uses just above, so the title keeps
 * its own colour. Returns false when this page is not one we carry. */
static bool brief_compose_page(uint32_t src, uint8_t *dst) {
  for (int i = 0; i < s_bp_count; i++) {
    if (s_bp_src[i] != src) continue;
    for (int k = 0; k < SC_BRIEF_COLS * SC_BRIEF_ROWS; k++) {
      dst[k * 2]     = (uint8_t)(SC_BRIEF_BLANK & 0xffu);
      dst[k * 2 + 1] = (uint8_t)(SC_BRIEF_BLANK >> 8);
    }
    if (s_bp_title[i][0])
      brief_put(dst, 2, 5, s_bp_title[i], 0x000u, 0x030u);
    /* the page's OWN origin, not a fixed row 4 column 4: the pages do
     * not share one layout. */
    for (int k = 0; k < s_bp_nlines[i]; k++)
      brief_put(dst, s_bp_row[i] + k, s_bp_col[i], s_bp_line[i][k],
                0x690u, 0x6C0u);
    return true;
  }
  return false;
}

/* The STANDARD / FREE box. Deliberately small and centred rather than styled
 * like the ROM's own dialogs: this is a host overlay drawn after the frame is
 * copied to the renderer, exactly like the settings menu above, so it shares
 * that font and needs nothing from the guest's text system. */
static void render_replay_menu(SDL_Renderer *renderer) {
  int out_w = 0, out_h = 0;
  SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
  int px = out_h / 120;
  if (px > 4) px = 4;
  if (px < 1) px = 1;
  const int line_h = 6 * px, pad = 4 * px;
  static const char *const kRows[2] = { "STANDARD", "FREE" };
  static const char *const kTitle = "REPLAY SCENARIO";

  int inner = text_width(px, kTitle);
  for (int i = 0; i < 2; i++) {
    int w = text_width(px, kRows[i]) + 4 * px;   /* rows are indented */
    if (w > inner) inner = w;
  }
  const int menu_w = inner + pad * 2;
  const int menu_h = pad * 2 + line_h * 5;
  const int menu_x = (out_w - menu_w) / 2;
  const int menu_y = (out_h - menu_h) / 2;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 210);
  ScRect bg = SC_RECT(menu_x, menu_y, menu_w, menu_h);
  SDL_RenderFillRect(renderer, &bg);
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
  SDL_RenderDrawRect(renderer, &bg);

  int ty = menu_y + pad;
  SDL_SetRenderDrawColor(renderer, 150, 150, 255, 255);
  draw_text(renderer, menu_x + pad, ty, px, kTitle);
  ty += line_h * 2;
  for (int i = 0; i < 2; i++) {
    bool sel = (i == s_replay_sel);
    /* Same yellow-when-selected convention as the settings rows. */
    SDL_SetRenderDrawColor(renderer, 255, 255, sel ? 0 : 255, 255);
    draw_text(renderer, menu_x + pad + 4 * px, ty, px, kRows[i]);
    ty += line_h;
  }
  ty += line_h - px;
  SDL_SetRenderDrawColor(renderer, 170, 170, 170, 255);
  draw_text(renderer, menu_x + pad, ty, px - 1, "B PICK   A BACK");
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

/* â”€â”€ generic activity qualification (--qualify N): the same pass/fail bar
 * as snesrecomp/cosim/ref_driver.c's standalone mode -- "goes through the
 * attract demo without logic, video, or audio errors" made concrete and
 * automatable, with zero game-specific WRAM knowledge required. â”€â”€â”€â”€â”€â”€â”€ */
static void write_mx_bitmap_dump(void) {
  if (!s_mx_bitmap || !s_mx_bitmap_path) return;
  FILE *f = fopen(s_mx_bitmap_path, "wb");
  if (!f) { fprintf(stderr, "SC_MX_BITMAP: cannot write %s\n", s_mx_bitmap_path); return; }
  fwrite(s_mx_bitmap, 1, 4 * sizeof(s_pc_bitmap_all), f);
  fclose(f);
  unsigned n[4] = {0,0,0,0};
  for (int mx = 0; mx < 4; mx++)
    for (int b = 0; b < 64; b++)
      for (int i = 0; i < 4096; i++)
        for (int k = 0; k < 8; k++)
          if (s_mx_bitmap[mx][b][i] & (1u << k)) n[mx]++;
  fprintf(stderr, "[mxbitmap] m0x0=%u m0x1=%u m1x0=%u m1x1=%u -> %s\n",
          n[0], n[1], n[2], n[3], s_mx_bitmap_path);
}

/* SC_AOT_VARIANTS=<file>: compiled variants entered during the run, one per
 * line as `pc24:MmXn hits=N` -- the same key the program manifest uses, so a
 * tool can join the two directly. Empty for a per-opcode run, which enters no
 * compiled bodies at all. */
static void write_aot_variants(void) {
  const char *path = getenv("SC_AOT_VARIANTS");
  if (!path || !s_aot_variant_count) return;
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "SC_AOT_VARIANTS: cannot write %s\n", path); return; }
  fprintf(f, "# compiled bodies ENTERED. An entry is not an extent: the body\n");
  fprintf(f, "# then runs an unknown number of opcodes without reporting them,\n");
  fprintf(f, "# so this must NOT be expanded into an executed-PC bitmap.\n");
  for (int i = 0; i < s_aot_variant_count; i++) {
    uint32_t k = s_aot_variant[i];
    fprintf(f, "%06x:M%dX%d hits=%u\n", (unsigned)(k >> 2),
            (int)((k >> 1) & 1), (int)(k & 1), (unsigned)s_aot_variant_hits[i]);
  }
  fclose(f);
  fprintf(stderr, "[aotvariants] %d distinct compiled variants entered\n",
          s_aot_variant_count);
  if (s_aot_variant_overflow)
    fprintf(stderr, "[aotvariants] WARNING: %llu entries dropped (table full)\n",
            (unsigned long long)s_aot_variant_overflow);
}

/* SC_PPU_DUMP_DIR=<dir> [+ SC_PPU_DUMP_INTERVAL, SC_PPU_DUMP_START]: dump the
 * PPU side of the machine -- VRAM, CGRAM and OAM -- so two runs can be
 * compared on what is actually available to draw with, not just on which code
 * executed. Written for the UFO question: the renderer runs identically on a
 * practice map and on Las Vegas, so the difference has to be in this data. */
static void write_ppu_dump(uint64_t frame) {
  const char *dir = getenv("SC_PPU_DUMP_DIR");
  if (!dir || !g_ppu) return;
  char path[512];
  snprintf(path, sizeof(path), "%s/ppu_%010llu.bin", dir, (unsigned long long)frame);
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "SC_PPU_DUMP_DIR: cannot write %s\n", path); return; }
  fwrite(g_ppu->vram,  2, 0x8000, f);   /* 64KB VRAM  */
  fwrite(g_ppu->cgram, 2, 0x100,  f);   /* 512B CGRAM */
  fwrite(g_ppu->oam,   2, 0x100,  f);   /* 512B OAM   */
  fclose(f);
}

static void write_pc_bitmap_dump(void) {
  write_mx_bitmap_dump();
  write_aot_variants();
  if (s_pc_bitmap_bank == -1) return;
  const char *path = getenv("SC_PC_BITMAP_PATH");
  if (!path) return;
  FILE *f = fopen(path, "wb");
  if (!f) return;
  if (s_pc_bitmap_bank == -2)
    fwrite(s_pc_bitmap_all, 1, sizeof(s_pc_bitmap_all), f);
  else
    fwrite(s_pc_bitmap, 1, sizeof(s_pc_bitmap), f);
  fclose(f);
}

/* Distinct-PC histogram for SC_MAP_WRITE_TRACE, emitted at exit alongside the
 * PC bitmap so both windowed and --qualify runs report it. */
static void write_map_trace_summary(void) {
  if (!s_map_write_trace) return;
  fprintf(stderr, "[mapwrite] %u total writes into $%06x..$%06x from %d distinct PCs\n",
          s_map_write_hits, kMapBufStart, kMapBufEnd - 1, s_map_write_pc_count);
  for (int i = 0; i < s_map_write_pc_count; i++)
    fprintf(stderr, "[mapwrite]   %02x:%04x  %u writes\n",
            (unsigned)(s_map_write_pcs[i].pc >> 16) & 0xff,
            (unsigned)(s_map_write_pcs[i].pc & 0xffff), s_map_write_pcs[i].count);
}

static int run_qualification(uint64_t frames) {
  uint64_t logic_changes = 0, video_changes = 0, audio_active_frames = 0;
  uint64_t last_ram_hash = 0, last_video_hash = 0;
  uint32_t last_sample_write = 0;
  int16_t audio_buf[1024 * 2];
  FILE *state_trace = NULL;
  { const char *p = getenv("SC_STATE_TRACE"); if (p) state_trace = fopen(p, "w"); }

  uint64_t stall_run = 0, stall_max = 0;
  for (uint64_t f = 0; f < frames; f++) {
    /* Qualification-only construction fixture, through the same safe-boundary
     * queue used by an actual mouse release. No direct mid-simulation writes. */
    const char *build_test = getenv("SC_BUILD_TEST");
    if (build_test && s_rom_is_us) {
      unsigned long long at; unsigned tool; int x0,y0,x1,y1;
      if (sscanf(build_test,"%llu:%u:%d:%d:%d:%d",&at,&tool,&x0,&y0,&x1,&y1)==6 && f==at &&
          host_map_screen_live() && ScConstructionPlanWorld(&s_build_plan,&s_world,tool,x0,y0,x1,y1)) {
        ram_set_w(0x020d,(uint16_t)tool);
        s_build_scroll_x=(int16_t)ram_w(0x01bd); s_build_scroll_y=(int16_t)ram_w(0x01bf);
        s_build_pending=true;
      }
    }
    apply_frame_input(f);
    apply_freezes();
    if (!run_one_frame()) {
      fprintf(stderr, "qualify: opcode guard tripped at frame %llu (hang/runaway)\n",
              (unsigned long long)f);
      return 1;
    }

    while(s_build_work)poll_mouse_construction();

    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < sizeof(g_ram); i++) { h ^= g_ram[i]; h *= 1099511628211ULL; }
    if (f > 1 && h != last_ram_hash) { logic_changes++; stall_run = 0; }
    else if (f > 1) { stall_run++; if (stall_run > stall_max) stall_max = stall_run; }
    last_ram_hash = h;
    if (state_trace) fprintf(state_trace, "%llu %016llx %02x%04x %04x %04x %04x %04x %02x %llu\n",
      (unsigned long long)f, (unsigned long long)h, g_cpu->k, g_cpu->pc,
      g_cpu->a, g_cpu->x, g_cpu->y, g_cpu->sp, interp816_getFlags(g_cpu),
      (unsigned long long)g_master_cycles);
    { const char *at = getenv("SC_SAVE_AT"), *path = getenv("SC_SAVE_PATH");
      if (at && path && f == strtoull(at, NULL, 0)) save_state(path); }
    static int s_dbg_interval = -1;
    if (s_dbg_interval < 0) {
      const char *e = getenv("SC_DEBUG");
      s_dbg_interval = e ? (atoi(e) > 0 ? atoi(e) : 60) : 0;
    }
    if (s_dbg_interval > 0 && (f % (uint64_t)s_dbg_interval) == 0) {
      fprintf(stderr, "[dbg f=%llu] cpu.pc=%02x:%04x ram_stall_run=%llu input=%04x "
              "$011b=%02x $12=%02x $01f5=%02x%02x $01c1=%02x%02x $d7=%02x "
              "$01df=%02x%02x $0c0f=%02x%02x $c9=%02x $ca=%02x\n",
              (unsigned long long)f, g_cpu->k, g_cpu->pc,
              (unsigned long long)stall_run, g_snes->input1_currentState,
              g_ram[0x011b], g_ram[0x0012],
              g_ram[0x01f6], g_ram[0x01f5], g_ram[0x01c2], g_ram[0x01c1],
              g_ram[0x00d7],
              g_ram[0x01e0], g_ram[0x01df], g_ram[0x0c10], g_ram[0x0c0f],
              g_ram[0x00c9], g_ram[0x00ca]);
    }

    uint64_t vh = 1469598103934665603ULL;
    /* Active area only -- the buffer is sized for the widescreen maximum, and
     * hashing the unused tail would dilute the signal. */
    for (size_t i = 0; i < (size_t)s_video_pitch * kVideoHeight; i++)
      { vh ^= s_video_pixels[i]; vh *= 1099511628211ULL; }
    if (f > 1 && vh != last_video_hash) video_changes++;
    last_video_hash = vh;

    {
      s_loop_frame = f;
      const char *dump_at = getenv("SC_DUMP_AT");
      const char *dump_path = getenv("SC_DUMP_PATH");
      if (dump_at && dump_path && f == strtoull(dump_at, NULL, 0)) {
        { const char *hm = getenv("SC_HOST_MAP_DUMP");
          if (hm && *hm) {
            int hc = 32, hr = 28;
            { const char *e = getenv("SC_HOST_MAP_CELLS");
              if (e && *e) sscanf(e, "%d,%d", &hc, &hr); }
            write_host_map_ppm(hm, hc, hr);
          } }
        if (write_ppm(dump_path))
          fprintf(stderr, "dumped frame %llu to %s\n", (unsigned long long)f, dump_path);
        else
          fprintf(stderr, "failed to write frame dump to %s\n", dump_path);
      }
      const char *wram_path = getenv("SC_WRAM_DUMP_PATH");
      if (dump_at && wram_path && f == strtoull(dump_at, NULL, 0)) {
        if (write_wram_dump(wram_path))
          fprintf(stderr, "dumped WRAM at frame %llu to %s\n", (unsigned long long)f, wram_path);
        else
          fprintf(stderr, "failed to write WRAM dump to %s\n", wram_path);
      }
      /* SC_DUMP_DIR + SC_DUMP_INTERVAL [+ SC_DUMP_START]: same idea as the
       * SC_WRAM_DUMP_DIR family below, but for video (.ppm) frames -- lets
       * a single --qualify run capture a whole navigation sequence (e.g.
       * every frame of a menu transition) for offline visual inspection,
       * instead of needing one run per SC_DUMP_AT frame. */
      const char *dump_dir = getenv("SC_DUMP_DIR");
      const char *dump_interval_s = getenv("SC_DUMP_INTERVAL");
      if (dump_dir && dump_interval_s) {
        uint64_t interval = strtoull(dump_interval_s, NULL, 0);
        const char *start_s = getenv("SC_DUMP_START");
        uint64_t start = start_s ? strtoull(start_s, NULL, 0) : 0;
        if (interval > 0 && f >= start && (f - start) % interval == 0) {
          char path[512];
          snprintf(path, sizeof(path), "%s/frame_%010llu.ppm", dump_dir, (unsigned long long)f);
          if (!write_ppm(path))
            fprintf(stderr, "failed to write frame dump to %s\n", path);
        }
      }
      /* SC_WRAM_DUMP_DIR + SC_WRAM_DUMP_INTERVAL [+ SC_WRAM_DUMP_START]:
       * repeatedly dump WRAM every <interval> frames starting at <start>
       * (default 0), one file per dump named wram_<frame>.bin in <dir> --
       * for bulk automation (e.g. stepping through many Map Select
       * screens in a single --qualify run) where a single SC_DUMP_AT
       * frame isn't enough. */
      const char *wram_dir = getenv("SC_WRAM_DUMP_DIR");
      const char *wram_interval_s = getenv("SC_WRAM_DUMP_INTERVAL");
      if (wram_dir && wram_interval_s) {
        uint64_t interval = strtoull(wram_interval_s, NULL, 0);
        const char *start_s = getenv("SC_WRAM_DUMP_START");
        uint64_t start = start_s ? strtoull(start_s, NULL, 0) : 0;
        if (interval > 0 && f >= start && (f - start) % interval == 0) {
          if (s_dump_pc24 != 0xffffffffu && !sc_fiber_active()) {
            /* Defer: fire at the guest PC instead. See SC_WRAM_DUMP_PC. */
            s_dump_pc_armed = true; s_dump_pc_frame = f;
            snprintf(s_dump_pc_dir, sizeof(s_dump_pc_dir), "%s", wram_dir);
          } else {
            char path[512];
            snprintf(path, sizeof(path), "%s/wram_%010llu.bin", wram_dir, (unsigned long long)f);
            if (!write_wram_dump(path))
              fprintf(stderr, "failed to write WRAM dump to %s\n", path);
          }
          write_ppu_dump(f);
        }
      }
    }

    Dsp *dsp = g_snes->apu->dsp;
    uint32_t available = dsp->sampleWrite - dsp->sampleRead;
    bool active = false;
    uint32_t inspect = available < DSP_SAMPLE_RING ? available : DSP_SAMPLE_RING;
    for (uint32_t i = 0; i < inspect; i++) {
      uint32_t idx = (dsp->sampleRead + i) & (DSP_SAMPLE_RING - 1);
      if (dsp->sampleBuffer[idx * 2] || dsp->sampleBuffer[idx * 2 + 1]) { active = true; break; }
    }
    if (active) audio_active_frames++;
    /* The headless drain takes 534 a frame whenever that many exist, a hair
     * more than the 533.125 the DSP makes, so the ring never fills here.
     * dsp_getSamples() takes exactly what it is asked for and never more
     * than exists (runner/src/snes/dsp.c). */
    /* SC_APU_DIAG=1: per-frame audio ledger -- what the DSP produced, what
     * was queued, and what the drain took. Cheap and env-gated. */
    { static int diag = -1;
      if (diag < 0) diag = getenv("SC_APU_DIAG") ? 1 : 0;
      if (diag) fprintf(stderr, "[apu f=%llu] write=%u avail=%u produced=%d\n",
                        (unsigned long long)f, dsp->sampleWrite, available,
                        (int)(dsp->sampleWrite - last_sample_write));
#ifdef SC_AOT_TIER
      /* Beam-step ledger: only the fiber host has a host-side beam loop.
       * This is what showed the beam being advanced from two places at
       * once -- guest-heavy frames need only ~300 host steps instead of
       * 178684, because the bridge already moved hPos/vPos itself. */
      if (diag) { extern unsigned long g_beam_steps;
                  static unsigned long prev_steps;
                  fprintf(stderr, "[beam f=%llu] steps=%lu vPos=%d hPos=%d sf=%llu joy=%d\n",
                          (unsigned long long)f, g_beam_steps - prev_steps,
                          (int)g_snes->vPos, (int)g_snes->hPos,
                          (unsigned long long)s_frames,
                          (int)g_snes->autoJoyTimer);
                  prev_steps = g_beam_steps; }
#endif
    }
    if (available >= 534) dsp_getSamples(dsp, audio_buf, 534);
    last_sample_write = dsp->sampleWrite;
  }

  int rc = 0;
  if (frames >= 120) {
    if (!logic_changes) {
      fprintf(stderr, "qualify: FAIL -- logic did not progress across %llu frames\n",
              (unsigned long long)frames);
      rc = 1;
    }
    if (last_sample_write < frames * 500 || !audio_active_frames) {
      fprintf(stderr, "qualify: FAIL -- audio did not produce active continuous output "
              "(samples=%u active_frames=%llu)\n",
              last_sample_write, (unsigned long long)audio_active_frames);
      rc = 1;
    }
    if (!video_changes) {
      fprintf(stderr, "qualify: FAIL -- rendered video stayed frozen\n");
      rc = 1;
    }
  }
#ifdef SC_AOT_TIER
  /* Did the guest actually run COMPILED code? Without this the wall-clock
   * comparison in OPEN_QUESTIONS B2 is unreadable: a fiber run that quietly
   * interpreted everything would look exactly like a slow AOT tier. Tier-downs
   * are only reachable FROM a compiled body, so a nonzero count is positive
   * evidence that compiled code executed. */
  { extern long interp_tier_hit_count(void);
    extern void interp_tier2_stats(int *sites, unsigned long long *clean,
                                   unsigned long long *bail);
    int sites = 0; unsigned long long clean = 0, bail = 0;
    interp_tier2_stats(&sites, &clean, &bail);
    extern unsigned long long g_interp_bridge_bounces;
    extern unsigned long long g_interp_bridge_steps;
    extern unsigned ScFiberDrive_GuestS(void);
    extern unsigned ScFiberDrive_ResumePC(void);
    if (s_fiber_mode) fprintf(stderr, "guest: S=%04X resume=%06X\n",
                              ScFiberDrive_GuestS(), ScFiberDrive_ResumePC());
    else fprintf(stderr, "guest: S=%04X pc=%02X:%04X\n",
                         (unsigned)g_cpu->sp, (unsigned)g_cpu->k, (unsigned)g_cpu->pc);
    fprintf(stderr, "aot: bounces=%llu interp_steps=%llu tier_downs=%ld gap_sites=%d clean=%llu bail=%llu",
            g_interp_bridge_bounces, g_interp_bridge_steps,
            interp_tier_hit_count(), sites, clean, bail);
    fprintf(stderr, "\n"); }
#endif
  fprintf(stderr, "gen: 03:d862 seeding hits=%lu\n", s_gen_trigger_hits);
  if (s_dec_fast || s_dec_declined)
  /* SC_LABEL_TRACE: who writes the main map's building labels?
   *
   * The labels are sprites -- OAM slots 90..97, which the game stages in
   * WRAM at $7E:2168 before DMAing it. Every attempt to find their
   * placement statically has failed: the run starts, the lengths, their
   * deltas and several strides are all absent from the ROM, and the
   * generated code addresses the slots computationally rather than by any
   * literal this can be grepped for. So ask the runtime instead. The
   * watch fires inside cpu_write8/16 and the ring keeps the most recent
   * function entry, which names the routine.
   *
   * Needs a build with SNESRECOMP_TRACE=1; in the normal build this is
   * a no-op and the dump prints nothing. */
#if defined(SNESRECOMP_TRACE) && SNESRECOMP_TRACE
  if (getenv("SC_LABEL_TRACE"))
    cpu_trace_dump_wram("label writes", 64);
#endif

    fprintf(stderr, "decomp: replaced=%lu declined=%lu\n",
            s_dec_fast, s_dec_declined);
  if (s_cls_hits || s_cls_fills || s_cls_mismatch)
    fprintf(stderr, "mapcls: cached=%lu distinct=%lu mismatched=%lu\n",
            s_cls_hits, s_cls_fills, s_cls_mismatch);
  if (s_dec_ok || s_dec_mismatch)
    fprintf(stderr, "decomp: verified=%lu mismatched=%lu\n",
            s_dec_ok, s_dec_mismatch);
  if (s_dec_ok || s_dec_mismatch || s_dec_fast) {
    fprintf(stderr, "decomp: cmd hits");
    for (int i = 0; i < 8; i++)
      fprintf(stderr, " %02x=%lu", i << 5, g_sc_decomp_cmd_hits[i]);
    fprintf(stderr, "  bank-wraps=%lu\n", g_sc_decomp_bank_wraps);
  }
  if (s_bank_profile) {
    unsigned long long tot = 0;
    for (int b = 0; b < 256; b++) tot += s_bank_ops[b];
    fprintf(stderr, "bankprof: total opcodes=%llu\n", tot);
    for (int b = 0; b < 256; b++)
      if (s_bank_ops[b] * 200 > tot)
        fprintf(stderr, "  bank %02x: %12llu  %5.1f%%\n", b,
                s_bank_ops[b], 100.0 * (double)s_bank_ops[b] / (double)tot);
    { unsigned long long b3 = s_bank_ops[s_bank_page_sel];
      fprintf(stderr, "  bank-%02x hot pages:\n", s_bank_page_sel);
      for (int k = 0; k < 10; k++) {
        int best = -1; unsigned long long bv = 0;
        for (int i = 0; i < 256; i++)
          if (s_b3_page[i] > bv) { bv = s_b3_page[i]; best = i; }
        if (best < 0 || !bv) break;
        fprintf(stderr, "    %02x:%02x00-%02xff  %12llu  %5.1f%%\n",
                s_bank_page_sel, best, best, bv, b3 ? 100.0*(double)bv/(double)b3 : 0.0);
        s_b3_page[best] = 0;
      } }
    if (s_tick_count)
      fprintf(stderr, "  sim ticks=%llu  avg frames/tick=%.2f  max=%llu  "
                      "avg bank-03 opcodes/tick=%llu\n",
              s_tick_count, (double)s_tick_frames_total / (double)s_tick_count,
              s_tick_frames_max, s_tick_ops_total / s_tick_count);
  }
  fprintf(stderr,
          "qualify: %s frames=%llu master=%llu logic_changes=%llu "
          "logic_stall_max=%llu audio_samples=%u audio_active_frames=%llu "
          "video_changes=%llu nmi_requests=%llu nmi_serviced=%llu "
          "final_pc=%02x:%04x\n",
          rc == 0 ? "PASS" : "FAIL",
          (unsigned long long)frames, (unsigned long long)g_master_cycles,
          (unsigned long long)logic_changes, (unsigned long long)stall_max,
          last_sample_write, (unsigned long long)audio_active_frames,
          (unsigned long long)video_changes,
          (unsigned long long)s_nmi_requests, (unsigned long long)s_nmi_serviced,
          g_cpu->k, g_cpu->pc);
  fprintf(stderr, "banks_seen=%016llx\n", (unsigned long long)s_banks_seen);
  write_pc_bitmap_dump();
  write_map_trace_summary();
  { const char *p = getenv("SC_WRAM_MAP"); if (s_wram_map && p) write_wram_map(p); }
  { const char *p = getenv("SC_SRAM_DUMP_PATH");
    if (p && *p) {
      report_sram_header("exit");
      fprintf(stderr, write_sram_dump(p) ? "dumped SRAM to %s\n"
                                         : "failed to write SRAM dump to %s\n", p);
    } }
  if (state_trace) fclose(state_trace);
  return rc;
}

int main(int argc, char **argv) {
  if(!ScMacPreparePaths(argc,argv))return 2;
  for (int i = 1; i + 1 < argc; ++i)
    if (!strcmp(argv[i], "--video-config")) s_video_config = argv[++i];
  if (!ScVideoLoad(&s_custom_video, s_video_config)) {
    fprintf(stderr, "Invalid widescreen settings: %s\n", s_video_config); return 2;
  }
  bool explicit_size = false, native_only = false, fullscreen_given = false;
  /* SC_MAPGEN_SELFTEST=<index>: run the decompiled generator for one map index
   * and write the 12000-cell result to SC_MAPGEN_OUT, then exit. No ROM, no
   * emulation -- this is the native generator alone, so a match against a map
   * dumped from the guest means the decompilation is right.
   *
   * SC_MAPGEN_CARRY and SC_MAPGEN_A exist because 03:d840's entry carry and
   * its entry A are genuinely unknown -- the disassembly cannot show either --
   * so they are swept rather than assumed. */
  /* SC_MAPGEN_SWEEP=1 with SC_MAPGEN_GOLD=<wram dump>: brute-force 03:d840's
   * two unknown entry values against a map dumped from the guest. Sweeping is
   * the only way to settle them -- the disassembly cannot show either, and a
   * backward walk of the PRNG from a sampled state produced only chance hits.
   *
   * This is decisive in BOTH directions. If the decompilation is right, the
   * true pair reproduces the map and stands far above every other; if nothing
   * rises above chance, the seeding is not what is wrong and no amount of
   * guessing at it will help. It came out the second way, twice.
   *
   * SC_MAPGEN_GOLD MUST BE A COMPLETED GENERATION. 03:d840 runs the whole map
   * as one synchronous JSL, but the SNES CPU needs ~800 frames of wall clock
   * to finish it, so a capture that stops earlier catches a part-built map --
   * which is what the first reference here did, invalidating every number
   * measured against it. The completion marker is $0b2a-2c (03:d873 copies the
   * selection there only after the generation returns) matching $0b27-29, with
   * the PRNG frozen. */
  { const char *sw = getenv("SC_MAPGEN_SWEEP");
    const char *gp = getenv("SC_MAPGEN_GOLD");
    if (sw && *sw && gp && *gp) {
      static uint16_t gold[SC_MAPGEN_CELLS];
      FILE *gf = fopen(gp, "rb");
      if (!gf) { fprintf(stderr, "[sweep] cannot open %s\n", gp); return 1; }
      { static unsigned char raw[0x20000];
        const size_t got = fread(raw, 1, sizeof raw, gf);
        fclose(gf);
        if (got < 0x10200 + 2 * SC_MAPGEN_CELLS) {
          fprintf(stderr, "[sweep] %s is too small (%u bytes)\n",
                  gp, (unsigned)got);
          return 1;
        }
        /* The map lives at $7F0200 -- bank 7F, so 0x10200 into a WRAM dump.
         * This offset was wrong once already (7E, not 7F) and cost a whole
         * "golden reference" built on the wrong buffer. */
        for (unsigned i = 0; i < SC_MAPGEN_CELLS; i++) {
          const unsigned o = 0x10200u + 2u * i;
          gold[i] = (uint16_t)((raw[o] | (raw[o + 1] << 8)) & 0x3ffu);
        } }

      { const unsigned idx =
            (unsigned)strtoul(getenv("SC_MAPGEN_SELFTEST")
                                  ? getenv("SC_MAPGEN_SELFTEST") : "3", NULL, 0);
        /* $0b2a, the previous map's kept index -- part of the seeding. */
        const uint8_t sweep_prev = (uint8_t)(getenv("SC_MAPGEN_PREV")
            ? strtoul(getenv("SC_MAPGEN_PREV"), NULL, 0) : 0u);
        static ScMapGenState gs;
        unsigned best[4] = {0, 0, 0, 0}, bestc[4] = {0, 0, 0, 0};
        unsigned besta[4] = {0, 0, 0, 0};
        double sum = 0.0; unsigned runs = 0;
        for (unsigned carry = 0; carry < 2u; carry++) {
          for (unsigned a = 0; a < 0x10000u; a++) {
            ScMapGenPrng pr;
            memset(gs.map, 0, sizeof gs.map);
            sc_mapgen_seed(&pr, (uint16_t)a, (uint8_t)(idx & 0xff),
                           (uint8_t)((idx >> 8) & 0xff),
                           (uint8_t)((idx >> 16) & 0xff), sweep_prev, carry);
            sc_mapgen_generate(&pr, &gs);
            { unsigned m = 0;
              for (unsigned i = 0; i < SC_MAPGEN_CELLS; i++)
                if ((gs.map[i] & 0x3ffu) == gold[i]) m++;
              sum += m; runs++;
              if (m > best[0]) {
                for (int k = 3; k > 0; k--) {
                  best[k] = best[k-1]; bestc[k] = bestc[k-1]; besta[k] = besta[k-1];
                }
                best[0] = m; bestc[0] = carry; besta[0] = a;
              } }
          }
          fprintf(stderr, "[sweep] carry=%u done, best so far %u (%.1f%%)\n",
                  carry, best[0], 100.0 * best[0] / SC_MAPGEN_CELLS);
        }
        fprintf(stderr, "[sweep] mean match over %u runs: %.1f (%.1f%%)\n",
                runs, sum / runs, 100.0 * (sum / runs) / SC_MAPGEN_CELLS);
        for (int k = 0; k < 4; k++)
          fprintf(stderr, "[sweep] #%d carry=%u A=%04X  %u/%u = %.1f%%\n",
                  k + 1, bestc[k], besta[k], best[k], (unsigned)SC_MAPGEN_CELLS,
                  100.0 * best[k] / SC_MAPGEN_CELLS); }
      return 0;
    } }

  { const char *st = getenv("SC_MAPGEN_SELFTEST");
    if (st && *st) {
      const char *outp = getenv("SC_MAPGEN_OUT");
      const char *cs = getenv("SC_MAPGEN_CARRY");
      const char *as = getenv("SC_MAPGEN_A");
      const unsigned idx = (unsigned)strtoul(st, NULL, 0);
      const int large = getenv("SC_MAPGEN_LARGE")?atoi(getenv("SC_MAPGEN_LARGE")):0;
      const unsigned cells = large==5?12288000:large==4?3072000:large==3?768000:large==2?192000:large?SC_WORLD_CELLS:SC_MAPGEN_CELLS;
      static ScMapGenState gs;
      ScMapGenPrng pr;
      { const char *pv = getenv("SC_MAPGEN_PREV");
        sc_mapgen_seed(&pr, (uint16_t)(as ? strtoul(as, NULL, 0) : 0u),
                       (uint8_t)(idx & 0xff), (uint8_t)((idx >> 8) & 0xff),
                       (uint8_t)((idx >> 16) & 0xff),
                       (uint8_t)(pv ? strtoul(pv, NULL, 0) : 0u),
                       (unsigned)(cs ? strtoul(cs, NULL, 0) : 0u)); }
      { const char *sa = getenv("SC_MAPGEN_SNAP_AT");
        if (sa && *sa) g_sc_mapgen_snap_at = strtoul(sa, NULL, 0); }
      { const char *rp = getenv("SC_MAPGEN_REPEAT");
        const unsigned reps = rp && *rp ? (unsigned)strtoul(rp, NULL, 0) : 1u;
        /* Was a guess that the reference had accumulated several passes, since
         * the preview regenerates while the button is held. Disproved -- more
         * passes make the match worse, and the second wipes the first's work.
         * Kept only as a sweep knob; 1 is correct. */
        g_sc_mapgen_prng_steps = 0;
        for (unsigned r = 0; r < reps; r++) {
          if (large==5) sc_mapgen_generate_mega(&pr, &gs);
          else if (large==4) sc_mapgen_generate_colossal(&pr, &gs);
          else if (large==3) sc_mapgen_generate_giant(&pr, &gs);
          else if (large==2) sc_mapgen_generate_huge(&pr, &gs);
          else if (large) sc_mapgen_generate_large(&pr, &gs);
          else sc_mapgen_generate(&pr, &gs);
        } }
      if (outp && *outp) {
        FILE *f = fopen(outp, "wb");
        /* With SC_MAPGEN_SNAP_AT, write the map as it stood after exactly that
         * many draws rather than the finished one. */
        if (f) {
          fwrite(!large && g_sc_mapgen_snapped ? g_sc_mapgen_snap : gs.map,
                 2, cells, f);
          fclose(f);
        }
      }
      { unsigned hist[64] = {0}, nz = 0;
        for (unsigned i = 0; i < cells; i++) {
          const unsigned v = gs.map[i] & 0x3ffu;
          if (v) nz++;
          if (v < 64) hist[v]++;
        }
        fprintf(stderr, "[selftest] idx=%u nonzero=%u  0:%u 1:%u 2:%u 3:%u"
                        " 20:%u 21:%u 24:%u 27:%u\n",
                idx, nz, hist[0], hist[1], hist[2], hist[3],
                hist[0x14], hist[0x15], hist[0x18], hist[0x1b]);
        fprintf(stderr, "[selftest] prng_steps=%lu s0=%04X s1=%04X\n",
                g_sc_mapgen_prng_steps, (unsigned)pr.s0, (unsigned)pr.s1);
        { static const char *nm[5] = { "centre f380", "path   f5b9",
                                       "cluster f311", "shore  f444",
                                       "scatter f3a3" };
          unsigned long prev = 0;
          for (int k = 0; k < 5; k++) {
            fprintf(stderr, "[phase] %-12s steps=%6lu  cells=%5lu (%+ld)"
                            "  changed=%lu cleared=%lu\n",
                    nm[k], g_sc_mapgen_phase_steps[k],
                    g_sc_mapgen_phase_cells[k],
                    (long)g_sc_mapgen_phase_cells[k] - (long)prev,
                    g_sc_mapgen_phase_changed[k], g_sc_mapgen_phase_cleared[k]);
            prev = g_sc_mapgen_phase_cells[k];
          } } }
      return 0;
    } }
  /* SC_LANG=U|E|F|G|J -- pick the regional ROM.
   *
   * All five regions are 512KB and all five pass --qualify 600 unchanged on
   * the interpreter tier, which is ROM-agnostic: it interprets whatever bytes
   * are there. So a language selector costs nothing but the file choice.
   *
   * The AOT tier is a different matter -- see the fingerprint guard below.
   *
   * The project assumes no file name: the region's image is found in the
   * working directory by its contents (ScFindRom, FNV-1a over the file). An
   * explicit ROM argument always wins over SC_LANG. */
  const char *rom_path = NULL;
  static char s_found_rom[1024];
  { const char *lang = getenv("SC_LANG");
    if (lang && *lang) {
      static const struct { char code; unsigned long fnv; } kRoms[] = {
        { 'U', SC_ROM_FNV_US }, { 'E', SC_ROM_FNV_EU }, { 'F', SC_ROM_FNV_FR },
        { 'G', SC_ROM_FNV_DE }, { 'J', SC_ROM_FNV_JP },
      };
      char want = (char)toupper((unsigned char)lang[0]);
      bool known = false;
      for (size_t i = 0; i < sizeof(kRoms)/sizeof(kRoms[0]); i++) {
        if (kRoms[i].code != want) continue;
        known = true;
        if (ScFindRom(kRoms[i].fnv, s_found_rom, sizeof s_found_rom))
          rom_path = s_found_rom;
        else
          fprintf(stderr, "SC_LANG=%c: no ROM of that region in the working directory\n", want);
      }
      if (!known)
        fprintf(stderr, "SC_LANG: want one of U E F G J\n");
    } }
  const char *load_state_path = NULL;
  uint64_t qualify_frames = 0;
  int scale = 3;
  { const char *e = getenv("SC_IO_TRACE"); if (e && *e) s_io_trace_until = strtoull(e, NULL, 0); }
  { const char *e = getenv("SC_PC_TRACE");
    if (e && *e) { s_pc_trace_at_frame = strtoull(e, NULL, 0); s_pc_capture_after = -2; } }
  { const char *e = getenv("SC_ADDR_TRACE"); if (e && *e) parse_addr_trace(e); }
  { const char *e = getenv("SC_GFX_TRACE"); if (e && *e) s_gfx_trace = true; }
  { const char *e = getenv("SC_DECOMP_TRACE"); if (e && *e) s_decomp_trace = true; }
  { const char *e = getenv("SC_UNLOCK_ALL"); if (e && *e) s_unlock_all = true; }
  { const char *e = getenv("SC_MUTE_CITY_WARNINGS"); s_mute_city_warnings=e && *e=='1'; }
  { const char *e = getenv("SC_WRAM_MAP");
    if (e && *e) {
      s_wram_flags = (uint8_t *)calloc(0x20000, 1);
      s_wram_last_pc = (uint32_t *)malloc(0x20000 * sizeof(uint32_t));
      s_wram_wcount = (uint16_t *)calloc(0x20000, sizeof(uint16_t));
      if (s_wram_flags && s_wram_last_pc && s_wram_wcount) {
        memset(s_wram_last_pc, 0xff, 0x20000 * sizeof(uint32_t));
        s_wram_map = true;
      }
    } }
  { const char *e = getenv("SC_WRAM_DUMP_PC");
    if (e && *e) {
      s_dump_pc24 = (uint32_t)strtoul(e, NULL, 16);
      fprintf(stderr, "SC_WRAM_DUMP_PC: dumps fire at guest %02X:%04X\n",
              (unsigned)(s_dump_pc24 >> 16), (unsigned)(s_dump_pc24 & 0xffff));
    } }
#ifdef SC_AOT_TIER
  /* The AOT tier is linked into this build, but the fiber is OPT-IN again.
   *
   * It was briefly the default. Playing on it showed widescreen defects that
   * had already been fixed coming back -- the rendering code is identical, so
   * the cause is that the guest runs compiled bodies under the fiber and any
   * fix that hangs off the per-opcode interpreter path stops firing. Until
   * that is found and closed, the interpreter stays the default, because it
   * is the path every widescreen fix was developed and verified against.
   *
   * SC_FIBER=1 enables it -- that is how the map generator HLE at 01:f1ed
   * runs, which is worth having and is verified bit-exact.
   *
   * The decision is recorded here but ACTED ON after the ROM is read, because
   * the AOT code is compiled against the US image and the region is not known
   * until then. */
  { const char *e = getenv("SC_FIBER");
    s_fiber_explicit = (e && *e);
    s_fiber_want = s_fiber_explicit ? (*e != '0') : 0; }
#endif
  { const char *e = getenv("SC_SCENARIO_EVENT");
    if (e && *e) {
      unsigned long long fr = 0; const char *at = strchr(e, 0x40);
      if (at) fr = strtoull(at + 1, NULL, 0);
      if (!strncmp(e, "meltdown", 8)) {
        s_scen_event_idx = 4; s_scen_event_cd = 1;  s_scen_event_name = "nuclear meltdown";
      } else if (!strncmp(e, "ufo", 3)) {
        s_scen_event_idx = 6; s_scen_event_cd = 16; s_scen_event_name = "UFO";
      } else {
        fprintf(stderr, "SC_SCENARIO_EVENT: expected meltdown|ufo\n");
      }
      s_scen_event_frame = fr;
    } }

  /* SC_DISASTER=<bit>@<frame>: set one $0197 disaster bit at a given frame,
   * headlessly. Exactly what the F10 menu does interactively -- the game's own
   * disaster-selection page sets these bits and 03:b8ae services them -- but
   * scriptable, so a single-disaster run can be recorded per bit without a
   * human driving menus. That is what docs/ROM_MAP.md asks for to attribute
   * the four unidentified arms.
   *
   * Not a freeze: the bit is set once and the ROM clears it itself after its
   * handler runs, so execution stays on paths the game really takes. */
  { const char *e = getenv("SC_DISASTER");
    if (e && *e) {
      unsigned bit = 0; unsigned long long at = 0;
      /* 0-5 are the ROM ladder arms; 6 and 7 are the meltdown and UFO rows
       * added by SC_DISASTER_MENU8, serviced host-side. */
      if (sscanf(e, "%u@%llu", &bit, &at) == 2 && bit < 8) {
        s_disaster_bit = (int)bit; s_disaster_frame = at;
      } else {
        fprintf(stderr, "SC_DISASTER: want <bit 0-7>@<frame>\n");
      }
    } }
  { const char *e = getenv("SC_MAP_WRITE_TRACE"); if (e && *e) s_map_write_trace = true; }
  { const char *e = getenv("SC_VIEW_WATCH"); if (e && *e) s_view_watch = true; }
  {const char *e=getenv("SC_DEVELOPMENT_BATCH_REFERENCE");s_development_batch_reference=e && *e=='1';}
  { const char *e = getenv("SC_DEVELOPMENT_SPEED");
    if (e && *e) {
      int value = atoi(e);
      for (unsigned i = 0; i < sizeof(kDevelopmentSpeeds)/sizeof(kDevelopmentSpeeds[0]); ++i)
        if (value == kDevelopmentSpeeds[i]) s_development_override = value;
    }
  }
  { const char *e = getenv("SC_POPULATION_TEST");
    if (e && *e == '1') s_pop_override = INT_MAX;
  }
  { const char *e = getenv("SC_MENU_PREVIEW");
    if (e && *e) { s_menu_preview = true; s_menu_open = true; } }
  { const char *e = getenv("SC_FREEZE"); if (e && *e) parse_freezes(e); }
  { const char *e = getenv("SC_PC_BITMAP_BANK");
    if (e && *e) s_pc_bitmap_bank = !strcmp(e, "all") ? -2 : (int)strtol(e, NULL, 16); }
  { const char *e = getenv("SC_MX_BITMAP");
    if (e && *e) {
      s_mx_bitmap = calloc(4, sizeof(s_pc_bitmap_all));
      if (s_mx_bitmap) s_mx_bitmap_path = e;
      else fprintf(stderr, "SC_MX_BITMAP: out of memory\n");
    } }
  { const char *e = getenv("SC_PC_BITMAP_START"); if (e && *e) s_pc_bitmap_start_frame = strtoull(e, NULL, 0); }
  /* SC_DEBUG_CODE_AT=<frame>: queue the one-button debug-menu code macro
   * (see queue_debug_menu_code) on controller 2 at that frame, for
   * headless verification in --qualify mode without hand-timing 16
   * presses. */
  { const char *e = getenv("SC_DEBUG_CODE_AT");
    if (e && *e) queue_debug_menu_code(strtoull(e, NULL, 0)); }
  bool rom_given = false, scale_given = false;
  bool force_launcher = false, no_settings = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--video-config") && i + 1 < argc) { ++i;
    } else if (!strcmp(argv[i], "--help")) {
      puts("UrbanRecomp [ROM] [--mods] [--widescreen | --no-widescreen]\n"
           "  --aspect Fit|Height|Width|4:3|8:7|16:10|16:9|21:9|32:9\n"
           "  --view-position Center|TopLeft  --window-size WIDTHxHEIGHT\n"
           "  --video-config FILE  --fullscreen  --scale N\n"
           "  --qualify FRAMES  --load-state FILE  --input FRAME:DURATION:HEX_MASK\n"
           "F11: fullscreen. F10: game settings. No arguments: Mods launcher.");
      return 0;
    } else if (!strcmp(argv[i], "--fullscreen")) { s_fullscreen=1; fullscreen_given=true;
    } else if (!strcmp(argv[i], "--mods")) { force_launcher = true;
    } else if (!strcmp(argv[i], "--widescreen")) { s_custom_video.enabled = true;
    } else if (!strcmp(argv[i], "--no-widescreen")) { s_custom_video.enabled = false; native_only = true;
    } else if (!strcmp(argv[i], "--aspect") && i + 1 < argc) {
      if (!ScParseAspect(argv[++i], &s_custom_video.aspect)) {
        fprintf(stderr, "Unknown view size: %s\n", argv[i]); return 2;
      }
    } else if (!strcmp(argv[i], "--view-position") && i + 1 < argc) {
      const char *v = argv[++i];
      if (strcmp(v,"Center") && strcmp(v,"TopLeft")) return 2;
      s_custom_video.centered = !strcmp(v,"Center");
    } else if (!strcmp(argv[i], "--window-size") && i + 1 < argc) {
      explicit_size=true;
      char tail;
      if (sscanf(argv[++i], "%dx%d%c", &s_window_width, &s_window_height, &tail) != 2 ||
          s_window_width < 64 || s_window_height < 64 || s_window_width > 16384 || s_window_height > 16384)
        return 2;
    } else if (!strcmp(argv[i], "--qualify") && i + 1 < argc) {
      qualify_frames = strtoull(argv[++i], NULL, 0);
    } else if (!strcmp(argv[i], "--launcher") || !strcmp(argv[i], "--mods")) {
      force_launcher = true;
    } else if (!strcmp(argv[i], "--no-settings")) {
      no_settings = true;
    } else if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
      scale = atoi(argv[++i]);
      scale_given = true;
    } else if (!strcmp(argv[i], "--input") && i + 1 < argc) {
      if (!add_input_event(argv[++i])) {
        fprintf(stderr, "invalid --input event; expected start:duration:hexmask\n");
        return 2;
      }
    } else if (!strcmp(argv[i], "--input2") && i + 1 < argc) {
      if (!add_input2_event(argv[++i])) {
        fprintf(stderr, "invalid --input2 event; expected start:duration:hexmask\n");
        return 2;
      }
    } else if (!strcmp(argv[i], "--load-state") && i + 1 < argc) {
      load_state_path = argv[++i];
    } else if (argv[i][0] != '-') {
      rom_path = argv[i];
      rom_given = true;
    }
  }

  /* Settings and the pre-boot launcher (src/sc_launcher.c).
   *
   * A player's run starts without a ROM argument and gets the launcher --
   * unless they told it to skip itself; --launcher brings it back. Any run
   * that is not --qualify then applies sc-settings.ini: window, audio, and the
   * SC_WIDESCREEN / SC_NINTH / SC_TRANSLATION / SC_PACKET_PATCH variables,
   * each only where it is not already set by hand. --qualify runs, and
   * --no-settings, stay exactly as their environment says, so every tool and
   * comparison in tools/ and docs/ keeps meaning what it meant. */
  static ScSettings s_launch_settings;
  ScSetAppIdentity();   /* before the launcher opens the first window */
  ScSettingsLoad(&s_launch_settings, kScSettingsPath);
  if (rom_given) snprintf(s_launch_settings.rom, sizeof s_launch_settings.rom, "%s", rom_path);
  if (!qualify_frames && !no_settings) {
    if (force_launcher || (!rom_given && !s_launch_settings.skip_launcher)) {
      const int r = ScLauncherRun(&s_launch_settings, kScSettingsPath, &s_custom_video, s_video_config);
      if (r == 0) return 0;
      if (r > 0) { rom_path = s_launch_settings.rom; rom_given = true; }
    }
    if (!rom_given && !getenv("SC_LANG")) {
      FILE *probe = fopen(s_launch_settings.rom, "rb");
      if (probe) { fclose(probe); rom_path = s_launch_settings.rom; }
    }
    if (!scale_given) scale = s_launch_settings.window_scale;
    if (!fullscreen_given) s_fullscreen = s_launch_settings.fullscreen;
    s_linear_filter = s_launch_settings.linear_filter != 0;
    s_enable_audio = s_launch_settings.enable_audio != 0;
    ScSettingsApply(&s_launch_settings);
  }
  if (!rom_path && ScFindRom(SC_ROM_FNV_US, s_found_rom, sizeof s_found_rom))
    rom_path = s_found_rom;
  if (!rom_path) {
    ScMacStartupError("no ROM: pass the path of your own copy, pick it in the "
                      "launcher, or put it (any file name) in the working directory");
    return 1;
  }

  if (!explicit_size) {
    s_window_height=224*scale;
    ScViewport initial=ScVideoViewport(&s_custom_video,1280,720);
    s_window_width=(int)(s_window_height*initial.width*initial.pixel_aspect/initial.height+.5);
    if (s_window_width>1600) { s_window_height=s_window_height*1600/s_window_width; s_window_width=1600; }
  }
  uint32_t rom_size = 0;
  uint8_t *rom_data = read_file(rom_path, &rom_size);
  if(!rom_data) {
    char message[1400];
    snprintf(message,sizeof message,"Cannot read ROM '%s'. Select your own clean SimCity SNES ROM in the launcher.",rom_path);
    ScMacStartupError(message);
    return 1;
  }
  /* The launcher verifies the cartridge body without an optional copier
   * header. Use that same body for native profile selection and live reads;
   * the player's file is unchanged. */
  if(rom_size>512 && rom_size%1024==512) {
    rom_size-=512;
    memmove(rom_data,rom_data+512,rom_size);
    fprintf(stderr,"rom: removed 512-byte copier header in memory\n");
  }
  s_rom_data = rom_data; s_rom_size = rom_size;
  /* Region report + AOT fingerprint guard.
   *
   * The generated AOT code is compiled against the US ROM: its addresses, its
   * dispatch table, every cfg directive. Point it at another region and it
   * would execute US code offsets over foreign bytes -- silently, and wrongly.
   *
   * Native profiles are selected by the verified fingerprint. The older
   * SC_FIBER tier remains US-only and uses a separate ABI; it must never
   * enter US AOT bodies on a foreign cartridge.
   *
   * FNV-1a over the whole file. US = 0xec01686a; E/F/G/J are 0xb76b1a0d,
   * 0xe1f99069, 0xaeca7623, 0xccb8c347. */
  { uint32_t fp = 2166136261u;
    for (uint32_t i = 0; i < rom_size; i++) { fp ^= rom_data[i]; fp *= 16777619u; }
    const uint8_t region = rom_size > 0x7fd9 ? rom_data[0x7fd9] : 0xff;
    const char *name = region == 0x00 ? "Japan" : region == 0x01 ? "USA"
                     : region == 0x02 ? "Europe" : region == 0x06 ? "France"
                     : region == 0x09 ? "Germany" : "unknown";
    s_rom_is_us = (fp == 0xec01686au);
    s_rom_fnv = fp;
    bool compiled_rom=ScProgramSelectRom(fp);
#ifdef SC_NATIVE_ONLY
    if(!compiled_rom) {
      char message[512];
      snprintf(message,sizeof message,"No compiled native profile for cartridge fingerprint %08x. "
          "Select a clean supported SimCity SNES ROM. Patched ROMs are not supported; the restored music is already bundled.",fp);
      ScMacStartupError(message);
      free(rom_data);s_rom_data=NULL;s_rom_size=0;
      return 1;
    }
#endif
    ScMapView_SetRomIsUs(s_rom_is_us);
    ScMapView_SetWorld(&s_world);
    fprintf(stderr, "rom: %s  region=%s (%02x)  fnv=%08x%s\n",
            rom_path, name, region, fp, s_rom_is_us ? "  [AOT-compatible]" : "");

    /* SC_TRANSLATION=<file.bin> -- a translated message block, from
     * tools/text_tool.py pack.
     *
     * Applied to the IN-MEMORY image, and deliberately after the fingerprint
     * above has been taken. Patching the ROM file instead would change that
     * fingerprint, and it gates the host map renderer, SC_FIBER, the cursor
     * cadence patch and the view fix -- so a translator would silently lose
     * four features by translating. This way the file on disk is never
     * touched and a translation ships as its own small blob, carrying only
     * its author's words and no ROM content.
     *
     * Latin releases store the block as characters. Japan stores tile
     * indices, so there is nothing here to overwrite. */
    scpk_apply_rom(rom_data, rom_size);

    { const char *tr = getenv("SC_TRANSLATION");
      if (tr && *tr) {
        /* Blob from tools/text_tool.py: "SCTR", ver, target region, record
         * count, payload length, then the message block itself.
         *
         * We run the US image and only the US image -- every ROM-address
         * hook in this file, the AOT tier and the host map renderer are keyed
         * to it. Other ROMs are donors: their text is lifted out and laid
         * over the US block, so a player gets German or French text with the
         * US build's features intact.
         *
         * A translation may be LONGER than the English original -- German
         * runs 278 bytes over, French 395. That is fine: 9597 bytes of $FF
         * filler follow the block, up to the $080000 bank boundary, and the
         * extra separators read as empty records past the last real one,
         * which nothing asks for. Hence the budget rather than a size match. */
        enum { kTrOff = 0x07A868u, kTrBudget = 0x080000u - 0x07A868u };
        uint32_t got = 0;
        uint8_t *blob = read_file(tr, &got);
        if (!blob)
          fprintf(stderr, "SC_TRANSLATION: cannot read '%s'\n", tr);
        else if (got < 12 || memcmp(blob, "SCTR", 4) != 0)
          fprintf(stderr, "SC_TRANSLATION: '%s' is not a translation blob -- "
                          "make one with tools/text_tool.py import\n", tr);
        else {
          const uint32_t recs = (uint32_t)blob[6] | ((uint32_t)blob[7] << 8);
          const uint32_t len  = (uint32_t)blob[8]  | ((uint32_t)blob[9] << 8)
                              | ((uint32_t)blob[10] << 16) | ((uint32_t)blob[11] << 24);
          if (!s_rom_is_us)
            fprintf(stderr, "SC_TRANSLATION: these blobs target the US image; "
                            "run the US ROM and use the other one as the donor\n");
          else if (len + 12u > got || len > kTrBudget)
            fprintf(stderr, "SC_TRANSLATION: '%s' is malformed or too long "
                            "(%u bytes, budget %u)\n",
                    tr, (unsigned)len, (unsigned)kTrBudget);
          else if (kTrOff + len > rom_size)
            fprintf(stderr, "SC_TRANSLATION: ROM too short\n");
          else {
            memcpy(rom_data + kTrOff, blob + 12, len);
            s_tr_off = kTrOff; s_tr_len = len;
            /* The messages are reached through a POINTER TABLE, not by
             * counting separators. It sits immediately before the block at
             * $07A800 -- 52 entries of a 16-bit bank-relative address --
             * and every entry matches a record start in the stock image.
             *
             * A translation whose records are different lengths therefore
             * leaves every pointer aiming into the middle of some other
             * message. Reported from play as wrong text, broken line
             * breaks, a message reduced to one line and a stray tile --
             * four symptoms, one cause. Rebuild it from what we wrote. */
            { enum { kPtrTab = 0x07A800u, kPtrBase = 0xA868u, kPtrCount = 52 };
              uint32_t rec = 0; int n = 0;
              for (uint32_t i = 0; i <= len && n < kPtrCount; i++) {
                if (i == 0 || (i < len && blob[12 + i - 1] == 0xFF)) {
                  uint32_t a = kPtrBase + rec;
                  rom_data[kPtrTab + n * 2]     = (uint8_t)(a & 0xff);
                  rom_data[kPtrTab + n * 2 + 1] = (uint8_t)(a >> 8);
                  n++;
                }
                if (i < len && blob[12 + i] == 0xFF) rec = i + 1;
              }
              fprintf(stderr, "translation: %d message pointers rebuilt\n", n); }
            /* v2 blobs carry the briefings after the text. */
            if (blob[4] >= 2 && 12u + len + 2u <= got) {
              const uint8_t *q = blob + 12 + len;
              int nb = q[0] | (q[1] << 8); q += 2;
              for (int i = 0; i < nb && i < kMaxBriefs; i++) {
                if ((size_t)(q - blob) + 8u > got) break;
                uint32_t src = (uint32_t)q[0] | ((uint32_t)q[1] << 8)
                             | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
                uint32_t bl  = (uint32_t)q[4] | ((uint32_t)q[5] << 8)
                             | ((uint32_t)q[6] << 16) | ((uint32_t)q[7] << 24);
                q += 8;
                if ((size_t)(q - blob) + bl > got) break;
                s_brief_data[s_brief_count] = (uint8_t *)malloc(bl);
                if (!s_brief_data[s_brief_count]) break;
                memcpy(s_brief_data[s_brief_count], q, bl);
                s_brief_src[s_brief_count] = src;
                s_brief_len[s_brief_count] = bl;
                s_brief_count++;
                q += bl;
              }
              if (blob[4] >= 3 && (size_t)(q - blob) + 4u <= got) {
                uint32_t tl = (uint32_t)q[0] | ((uint32_t)q[1] << 8)
                            | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
                q += 4;
                if (tl && (size_t)(q - blob) + tl <= got) {
                  s_scen_tiles = (uint8_t *)malloc(tl);
                  if (s_scen_tiles) {
                    memcpy(s_scen_tiles, q, tl);
                    s_scen_tiles_len = tl;
                  }
                  q += tl;
                }
                if (blob[4] >= 4 && (size_t)(q - blob) + 2u <= got) {
                  int ng = q[0] | (q[1] << 8); q += 2;
                  for (int i = 0; i < ng && s_glyph_count < kMaxGlyphs; i++) {
                    if ((size_t)(q - blob) + 2u + kFontTile > got) break;
                    s_glyph_idx[s_glyph_count] = (uint16_t)(q[0] | (q[1] << 8));
                    memcpy(s_glyph_px[s_glyph_count], q + 2, kFontTile);
                    s_glyph_count++;
                    q += 2 + kFontTile;
                  }
                }
                if (blob[4] >= 5 && (size_t)(q - blob) + 4u <= got) {
                  uint32_t sl = (uint32_t)q[0] | ((uint32_t)q[1] << 8)
                               | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
                  q += 4;
                  const uint8_t *e = q + sl;
                  if (sl >= 2 && e <= blob + got) {
                    int np = q[0] | (q[1] << 8); q += 2;
                    for (int i = 0; i < np && s_bp_count < kBpPages; i++) {
                      if (q + 5 > e) break;
                      s_bp_src[s_bp_count] = (uint32_t)q[0] | ((uint32_t)q[1] << 8)
                        | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
                      q += 4;
                      if (blob[4] >= 7) {
                        if (q + 2 > e) break;
                        s_bp_row[s_bp_count] = q[0];
                        s_bp_col[s_bp_count] = q[1];
                        q += 2;
                      } else {
                        s_bp_row[s_bp_count] = 4; s_bp_col[s_bp_count] = 4;
                      }
                      unsigned tl = *q++;
                      if (q + tl > e) break;
                      if (tl > kBpChars - 1) tl = kBpChars - 1;
                      memcpy(s_bp_title[s_bp_count], q, tl);
                      s_bp_title[s_bp_count][tl] = 0;
                      q += tl;
                      if (q >= e) break;
                      unsigned nl = *q++;
                      unsigned kept = 0;
                      for (unsigned k = 0; k < nl && q < e; k++) {
                        unsigned ll = *q++;
                        if (q + ll > e) { q = e; break; }
                        if (kept < kBpLines) {
                          unsigned c = ll > kBpChars - 1 ? kBpChars - 1 : ll;
                          memcpy(s_bp_line[s_bp_count][kept], q, c);
                          s_bp_line[s_bp_count][kept][c] = 0;
                          kept++;
                        }
                        q += ll;
                      }
                      s_bp_nlines[s_bp_count] = (uint8_t)kept;
                      s_bp_count++;
                    }
                  }
                }
                if (blob[4] >= 6 && (size_t)(q - blob) + 2u <= got) {
                  int nsg = q[0] | (q[1] << 8); q += 2;
                  for (int i = 0; i < nsg && s_sg_count < kMaxGlyphs; i++) {
                    if ((size_t)(q - blob) + 18u > got) break;
                    s_sg_idx[s_sg_count] = (uint16_t)(q[0] | (q[1] << 8));
                    memcpy(s_sg_px[s_sg_count], q + 2, 16);
                    s_sg_count++; q += 18;
                  }
                }
                if (blob[4] >= 8 && (size_t)(q - blob) + 4u <= got) {
                  uint32_t cl = (uint32_t)q[0] | ((uint32_t)q[1] << 8)
                               | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
                  q += 4;
                  const uint8_t *ce = q + cl;
                  if (cl >= 2 && ce <= blob + got) {
                    int nc = q[0] | (q[1] << 8); q += 2;
                    for (int i = 0; i < nc && s_card_count < kMaxCards; i++) {
                      if (q + 11 > ce || memcmp(q, "SCCD", 4) != 0) break;
                      int cw = q[5], ch = q[6];
                      s_card_col[s_card_count] = q[7];
                      s_card_row[s_card_count] = q[8];
                      s_card_w[s_card_count] = (uint8_t)cw;
                      s_card_h[s_card_count] = (uint8_t)ch;
                      int nt = q[9] | (q[10] << 8);
                      q += 11;
                      if (cw * ch > 72 || nt > kCardTiles) break;
                      if (q + cw * ch * 2 > ce) break;
                      for (int k = 0; k < cw * ch; k++, q += 2)
                        s_card_map[s_card_count][k] = (uint16_t)(q[0] | (q[1] << 8));
                      int kept = 0;
                      for (int k = 0; k < nt; k++) {
                        if (q + 34 > ce) break;
                        s_card_tid[s_card_count][kept] = (uint16_t)(q[0] | (q[1] << 8));
                        memcpy(s_card_px[s_card_count][kept], q + 2, 32);
                        kept++; q += 34;
                      }
                      s_card_nt[s_card_count] = (uint8_t)kept;
                      s_card_count++;
                    }
                  }
                }
                if (blob[4] >= 9 && (size_t)(q - blob) + 4u <= got) {
                  uint32_t sl = (uint32_t)q[0] | ((uint32_t)q[1] << 8)
                               | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
                  q += 4;
                  const uint8_t *se = q + sl;
                  if (sl >= 2 && se <= blob + got) {
                    int ns = q[0] | (q[1] << 8); q += 2;
                    for (int i = 0; i < ns && s_surf_count < kMaxSurf; i++) {
                      if (q + 4 > se) break;
                      uint32_t fl = (uint32_t)q[0] | ((uint32_t)q[1] << 8)
                                   | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
                      q += 4;
                      if (q + fl > se || fl < 11 || memcmp(q, "SCSF", 4) != 0) break;
                      s_surf[s_surf_count] = (uint8_t *)malloc(fl);
                      if (!s_surf[s_surf_count]) break;
                      memcpy(s_surf[s_surf_count], q, fl);
                      s_surf_len[s_surf_count] = fl;
                      s_surf_count++;
                      q += fl;
                    }
                  }
                }
              }
            }
            fprintf(stderr, "translation: %s applied (%u messages, %u bytes "
                            "at $%06X, %u spare)\n",
                    tr, (unsigned)recs, (unsigned)len, (unsigned)kTrOff,
                    (unsigned)(kTrBudget - len));
            if (s_surf_count)
              fprintf(stderr, "translation: %d surface(s) loaded\n", s_surf_count);
            if (s_card_count)
              fprintf(stderr, "translation: %d scenario cards loaded\n",
                      s_card_count);
            if (s_sg_count)
              fprintf(stderr, "translation: %d briefing glyphs loaded\n",
                      s_sg_count);
            if (s_bp_count)
              fprintf(stderr, "translation: %d briefing pages loaded as strings\n",
                      s_bp_count);
            if (s_glyph_count)
              fprintf(stderr, "translation: %d accent glyphs loaded\n",
                      s_glyph_count);
            if (s_scen_tiles_len)
              fprintf(stderr, "translation: scenario picture tiles loaded\n");
            if (s_brief_count)
              fprintf(stderr, "translation: %d scenario briefings loaded\n",
                      s_brief_count);
          }
        }
        free(blob);
      } }
  }
#ifdef SC_AOT_TIER
  /* Decided HERE, not where SC_FIBER is parsed: env parsing runs before the ROM
   * is read, so the fingerprint is not known yet. The first version of this
   * guard sat at the parse site and did nothing at all -- a German ROM ran 60
   * compiled bounces straight past it. */
  if (s_fiber_want && !s_rom_is_us) {
    if (s_fiber_explicit) {
      fprintf(stderr,
              "SC_FIBER refused: the AOT code is compiled against the US ROM "
              "and this image is a different region.\n"
              "  Run without SC_FIBER -- the interpreter tier handles every "
              "region.\n");
      return 1;
    }
    /* Only the default asked for it, so step down rather than refuse to run.
     * Every region stays playable; this one just runs on the interpreter. */
    fprintf(stderr, "[fiber] not a US image -- running on the interpreter "
                    "tier, which handles every region.\n");
    s_fiber_want = 0;
  }
  if (s_fiber_want) {
    if (!ScFiberDrive_Init()) {
      fprintf(stderr, "SC_FIBER: could not start the game fiber\n");
      return 1;
    }
    s_fiber_mode = true;
    /* Feed the coverage bitmaps from the bridge, or a fiber run records
     * nothing at all and every tool in tools/ silently sees an empty bitmap.
     * Interpreted opcodes go into the bitmaps exactly as the per-opcode host
     * records them; compiled-body ENTRIES are collected separately, because a
     * bounce is not an extent. */
    { extern void (*g_interp_bridge_pc_hook)(uint32_t, int, int);
      extern void (*g_interp_bridge_bounce_hook)(uint32_t, int, int);
      g_interp_bridge_pc_hook = sc_note_executed_pc;
      g_interp_bridge_bounce_hook = sc_note_aot_entry; }
    fprintf(stderr, "[fiber] driving the guest inside the fiber "
                    "(entry I_RESET_M1X1)\n");
  }
#endif
  /* REMOVED: the "D-pad bug family" ROM patches (11 sites, ~470 lines).
   *
   * They never should have existed. The runner returned the two halves of
   * the auto-joypad read transposed -- snes.c's $4218 case returned the
   * high byte and $4219 the low one. Per hardware the joypad word is:
   *     bits 15-8 : B, Y, Select, Start, Up, Down, Left, Right
   *     bits  7-0 : A, X, L, R, 0, 0, 0, 0     (low nibble = controller ID)
   * so $4218 carries A/X/L/R plus four always-zero bits and $4219 carries
   * the D-pad. The transposition put the D-pad in $011b and left $011c --
   * the byte the game actually tests -- reading zero.
   *
   * `LDA $011b (16-bit) / AND #$0f00` is simply how you read the D-pad on
   * this hardware: it tests $011c bits 0-3. It looked like a per-site bug
   * only because the emulator was feeding it the wrong byte. Every one of
   * the eleven patches here was compensating for that single defect, and
   * once snes.c was corrected the two compensations cancelled: with both
   * applied the input went dead again.
   *
   * Measured directly. On the bank-loan dialog (the cleanest discriminator,
   * since its handler depends on exactly one patched site) holding Right,
   * with the runner fixed:
   *     patches applied  -> $0b17 stays 0   (cursor stuck)
   *     patches removed  -> $0b17 goes 0->1 (cursor moves, stock ROM code)
   * And holding Left now yields $011b=00 / $011c=02 -- Left in $011c bit 1,
   * exactly as the hardware layout specifies.
   *
   * Credit for the root cause goes to the parallel Metal Marines work,
   * which hit the identical pattern and correctly refused to believe two
   * unrelated commercial games shipped the same input bug. That was the
   * tell here too and it was missed: eleven independent sites "sharing a
   * bug" is an anomaly to explain, not corroboration to lean on.
   *
   * The two ROM patches kept below are unrelated to input and still stand
   * on their own evidence: the $01f3 cursor step-delay tweak and the
   * 00:c0fb $7e21b5 stomp fix.
   *
   * Anything in docs/ that was reasoned from "$011b holds the D-pad" needs
   * re-deriving against the corrected runner. */

  /* Cursor step-cadence speed tweak (docs/INVESTIGATION_cursor_cadence.md)
   * -- NOT part of the D-pad bug family above, and unlike those, not
   * confirmed to differ from real hardware. This is a deliberate design
   * constant, not a "wrong nibble"-style bug: found via the same
   * deterministic --load-state/--input testing that cracked fast travel,
   * tracing the real cursor-position writer (`01:c2b1: STA $01eb`, not the
   * stale `01:c214` the old investigation notes guessed) back to a
   * countdown-delay gate at `01:c0dd`: `LDA $01f3 (16-bit); BEQ +4 (fall
   * through if zero); DEC $01f3; RTS (bail if nonzero)`. After a
   * successful step, two mirror-image sites (the increment and decrement
   * direction handlers) both reset it with the identical `LDA #$0003;
   * STA $01f3` -- confirmed live: 15/35 calls bail on a nonzero `$01f3`,
   * the other 20/35 fall through and write `$01eb`, exactly matching this
   * gate. Same constant in both symmetric sites reads as intentional
   * pacing, not a bug -- lowering it here is a speed tweak, matching the
   * spirit of the existing fast-forward feature and the "for a future
   * speed mod" framing the old investigation doc already used, not a
   * claim that real hardware behaves differently. */
  {
    static const uint32_t kCursorDelaySites[] = {
      0xc2e0, /* 01:c2df's immediate operand low byte -- increment handler */
      0xc3c9, /* 01:c3c8's immediate operand low byte -- decrement handler */
    };
    int patched = 0;
    for (size_t i = 0; i < sizeof(kCursorDelaySites) / sizeof(kCursorDelaySites[0]); i++) {
      uint32_t off = kCursorDelaySites[i];
      /* Gated on the US fingerprint: this checks a SINGLE byte, which cannot
       * identify a site in a different build. Measured: without the gate, both
       * US patches "applied" cleanly to all of E/F/G/J -- i.e. they were
       * patching foreign ROMs on coincidental byte matches. */
      if (s_rom_is_us && off < rom_size && rom_data[off] == 0x03) {
        rom_data[off] = 0x00;
        patched++;
      }
    }
    fprintf(stderr, "cursor cadence: patched %d/%d step-delay reset sites ($01f3: 3 -> 0)\n",
            patched, (int)(sizeof(kCursorDelaySites) / sizeof(kCursorDelaySites[0])));
  }

  /* Cursor cadence, part 2 -- INVESTIGATED, NOT APPLIED. The $01f3 delay
   * above turned out to be only the first of two gates in series. $01f3
   * ==0 unlocks a second flag, $01ff: a shared "step pending" lock across
   * both axes, set to a per-direction bitmask (e.g. 0x0800 for Up, 0x0100
   * for Right) by the direction handlers (01:c145 etc.) right after every
   * step, cleared only by the shared post-step tail at 01:c2d4
   * (`AND #$0007; BEQ` -- only unlocks once the cursor's new clamped
   * position is a multiple of 8, i.e. roughly 1-in-4 calls, since each
   * step moves 2 units). Tried the same fix idea as $01f3 (widen the
   * AND mask so it always unlocks) and confirmed live it's a *regression*:
   * $01ff isn't purely wasted time -- while it's set, a second ladder at
   * 01:c195 bypasses it entirely by re-testing whichever direction bit is
   * still set in $01ff and re-issuing that same step directly, which is
   * *also* real, useful step throughput (measured: holding Right alone,
   * removing the lock dropped the total step rate from 48/100 frames down
   * to 25/100 -- the bypass path stops firing once $01ff no longer holds
   * a pending direction to re-issue, and the primary path alone doesn't
   * make up the difference). Left unpatched; the two gates interact in a
   * way that isn't a simple "remove the delay" fix like $01f3 was. */

  /* View screen D-pad fix (docs/INVESTIGATION_dpad.md "View screen's
   * D-pad: FIXED"): the write side ($7e21b4 for Left/Right, $7e21b5 for
   * Up/Down, both driven by 01:f189/f190 and 01:f19a/f1bf) was already
   * confirmed working in an earlier session, but nothing visibly moved.
   * Root cause, found via deterministic --load-state/--input testing plus
   * a live memory watch (SC_VIEW_WATCH=1) that catches every addressing
   * mode -- unlike a static opcode scan, which only turned up $7e21b5's
   * own read-modify-write increment in unrelated bank 5 code, not a
   * genuine consumer: $7e21b4 (Left/Right) already works correctly
   * end-to-end (confirmed live: cleanly decrements frame over frame while
   * held, e.g. 7d->7a->77->74->...). $7e21b5 (Up/Down) does not: a
   * universal, always-on per-frame routine at 00:c0fb (`SEP #$20;
   * LDA #$e0; STA $7e21b5`, part of a loop at 00:8aa8 that rebuilds a
   * whole row of UI icon sprites into OAM via DMA every frame, on every
   * screen -- not View-specific, confirmed also firing on the classic
   * map) unconditionally resets $7e21b5 to a fixed $e0 every single
   * frame, stomping whatever 01:f1bf just wrote before anything can read
   * the new value. Confirmed live holding Down: 01:f1bf computes a real
   * new value (e.g. $dc), but 01:f1a7 (the only reader of $7e21b5
   * anywhere in the ROM -- verified via the same live watch, on every
   * screen, not just View) only ever observes the reset value $e0, never
   * the update.
   *
   * Fix: NOP out just this one STA (4 bytes, EA EA EA EA), leaving its
   * six sibling table-slot writes ($7e21b9/bd/c1/d5/d9/dd, part of the
   * same per-frame icon rebuild) completely untouched -- as surgical as a
   * byte patch gets. Safe because $7e21b5 has exactly one consumer in the
   * entire ROM (the View screen's own Up/Down check); nothing else reads
   * it, on any screen, so skipping this one reset can't leave stale data
   * visible anywhere else. (A first attempt at this looked like it hung
   * the full 10800-frame --qualify baseline -- turned out to be an
   * unrelated false alarm from heavy host system load that session, not
   * this patch: a 90s-timeout retest completed in 40s with byte-identical
   * baseline output. Re-verify with a generous timeout if this is ever
   * in doubt again.) */
  /* SC_DISASTER_MENU8=1 -- see service_disaster_menu8(). Two byte patches:
   * drop the pair of ASLs so all eight $0197 bits reach the row walker, and
   * raise the row count from 6 to 8. Byte-checked, and applied here because
   * cart_init() copies the ROM -- a patch after that lands in a buffer nobody
   * reads. */
  if (getenv("SC_DISASTER_MENU8")) {
    /* The row-count patch is GONE. Raising LDY #$0005 to #$0007 and dropping
     * the two ASLs did give the page eight bits to walk, and it wrecked the
     * colours on the Speed, Options and Disasters pages: slots 6 and 7 of the
     * buffer at $7e2063 are not free, so the extra rows wrote checkbox tiles
     * and palettes over other UI elements. See the note above.
     *
     * What is left is only the host-side servicing of bits 6 and 7, which
     * touches no ROM and keeps SC_DISASTER=6/7 usable as a headless trigger.
     * The F10 MELTDOWN and UFO rows remain the working way to fire these. */
    s_disaster_menu8 = true;
    fprintf(stderr, "disaster bits 6/7 serviced host-side (no ROM patch)\n");
  }
  {
    uint32_t off = 0x40fb; /* 00:c0fb's STA $7e21b5 (long), file offset = addr-0x8000 (bank 0) */
    if (s_rom_is_us && off + 3 < rom_size && rom_data[off] == 0x8f && rom_data[off+1] == 0xb5 &&
        rom_data[off+2] == 0x21 && rom_data[off+3] == 0x7e) {
      rom_data[off] = rom_data[off+1] = rom_data[off+2] = rom_data[off+3] = 0xea; /* NOP x4 */
      fprintf(stderr, "view fix: patched 00:c0fb STA $7e21b5 -> NOP (stop stomping the Up/Down View cursor)\n");
    } else {
      fprintf(stderr, "view fix: 00:c0fb site NOT patched (byte mismatch)\n");
    }
  }

  g_snes = snes_init(g_ram);
  { const char *e = getenv("SC_BEAM_LEGACY");
    if (e && *e && *e != '0') s_own_beam = false; }
  if (s_own_beam) sc_own_the_beam();
  sc_wram_watch_install();
  sc_dma_vram_install();
  sc_vram_write_watch_install();
  cart_set_master_clock_source(g_snes->cart, &g_master_cycles);
  g_ppu = g_snes->ppu;
  if (!snes_loadRom(g_snes, rom_data, (int)rom_size)) {
    char message[1400];
    snprintf(message,sizeof message,"Could not load cartridge '%s'.",rom_path);
    ScMacStartupError(message);
    return 1;
  }
  /* cart_init() copies the ROM, so confirm the translation survived into
   * the buffer that actually executes. This file already records one
   * patch that landed in the wrong copy and silently did nothing. */
  /* cart_init() copies the ROM. The building labels are patched in the
   * image, so confirm they survived into the buffer that executes -- this
   * file already records one patch that landed in the wrong copy and
   * silently did nothing. */
  if (s_scpk_rom_len) {
    const uint8_t *live = sc_live_rom();
    if (!live || memcmp(live + s_scpk_rom_off, rom_data + s_scpk_rom_off,
                        s_scpk_rom_len) != 0)
      fprintf(stderr, "packet patch: cart spans LOST -- the cart copy does not carry them\n");
    else
      fprintf(stderr, "packet patch: cart spans live in the cart image\n");
  }
  if (s_tr_len) {
    const uint8_t *live = sc_live_rom();
    if (!live || memcmp(live + s_tr_off, rom_data + s_tr_off, s_tr_len) != 0)
      fprintf(stderr, "translation: LOST -- the cart copy does not carry it\n");
    else
      fprintf(stderr, "translation: live in the cart image\n");
  }
  /* Arm the label watch before the game runs. OAM slots 90..97 are staged
   * at $7E:2168; a hit records the writing function in the trace ring. */
#if defined(SNESRECOMP_TRACE) && SNESRECOMP_TRACE
  if (getenv("SC_LABEL_TRACE")) {
    cpu_trace_clear_wram_watches();
    for (unsigned a = 0x2168; a < 0x2190; a += 2)
      cpu_trace_set_wram_watch(0x7e, (uint16_t)a, 2, 0, 0, 1);
    fprintf(stderr, "label trace: watching $7E:2168..$7E:218F  (needs SNESRECOMP_TRACE=1)\n");
  }
#endif
  snes_reset(g_snes, true);
  { const char *e = getenv("SC_WIDESCREEN");
    if (e && *e) {
      int v = atoi(e);
      if (v < 0) v = 0;
      if (v > 96) v = 96;
      s_ws_extra = v;
    } }
  { const char *e = getenv("SC_NINTH");
    if (e && *e && *e != '0') s_ninth_scenario = true; }
  { const char *e = getenv("SC_WS_FILL");
    if (e && *e) s_ws_margin_fill = (*e != '0'); }
  { const char *e = getenv("SC_WS_MIRROR");
    if (e && *e) s_ws_mirror = (uint8_t)strtol(e, NULL, 0); }
  { const char *e = getenv("SC_WS_LIGHTS");
    if (e && *e) s_ws_widen_lights = (*e != '0'); }
  { const char *e = getenv("SC_WS_TITLE");
    if (e && *e) s_ws_widen_title = (*e != '0'); }
  { const char *e = getenv("SC_WS_MENU");
    if (e && *e) s_ws_widen_menu = (*e != '0'); }
  { const char *e = getenv("SC_WS_OBJ_CLIP");
    if (e && *e) s_ws_obj_clip = (*e != '0'); }
  { const char *e = getenv("SC_WS_MARGIN_OBJ");
    if (e && *e) s_margin_obj_on = (*e != '0'); }
  { const char *e = getenv("SC_WS_VEHICLES");
    if (e && *e) s_ws_vehicles = (*e != '0'); }
  { const char *e = getenv("SC_WS_OAM");
    if (e && *e) s_ws_oam_strict = (*e != '0'); }
  { const char *e = getenv("SC_NEW_RENDERER");
    if (e && *e) { s_force_legacy = (*e == '0'); if (!s_force_legacy) s_render_flags = 1; } }
  { const char *e = getenv("SC_BANK_PROFILE"); if (e && *e) s_bank_profile = (*e != '0'); }
  { const char *e = getenv("SC_BANK_PROFILE_PAGE");
    if (e && *e) s_bank_page_sel = (int)strtol(e, NULL, 16) & 0xff; }
  {const char *e=getenv("SC_PC_PROFILE_PAGE");if(e && *e) s_pc_profile_page=(int)strtol(e,NULL,16)&255;}
  {const char *e=getenv("SC_NATIVE_BIND_REFERENCE");s_native_bind_eager=e && *e=='1';}
  {const char *e=getenv("SC_DEVELOPMENT_PROFILE");if(e && *e=='1')
    ScDevelopmentSetProfileClock(SDL_GetPerformanceCounter,SDL_GetPerformanceFrequency());}
  { const char *e = getenv("SC_WS_CLAMP");
    if (e && *e) { s_ws_clamp = (uint8_t)strtol(e, NULL, 0); s_ws_clamp_auto = false; } }
  { const char *e = getenv("SC_HOST_HDMA");
    if (e && *e) s_host_hdma = (*e != '0'); }
  { const char *e = getenv("SC_REPLAY_MENU");
    if (e && *e) s_replay_menu = (*e != '0'); }
  /* SC_REPLAY_FREE=1 arms the free-play latch without going through the menu.
   * The selector screen is unreachable from every save state, so this is the
   * only way to exercise the half of the feature that changes the rules --
   * and it doubles as a way to turn any scenario already in progress into a
   * free-play city. */
  { const char *e = getenv("SC_HUD_MASK");
    if (e && *e) s_hud_mask = (uint8_t)strtol(e, NULL, 0); }
  load_sylt_card();
  load_sylt_map();
  { const char *e = getenv("SC_REPLAY_FREE");
    if (e && *e && *e != '0') s_replay_free = 1; }
  { const char *e = getenv("SC_NINTH_SCROLL");
    if (e && *e) s_ninth_scroll = (int)strtol(e, NULL, 0); }
  { const char *e = getenv("SC_HOST_MAP");
    if (e && *e) s_host_map = (*e != '0'); }
  if (native_only) { s_ws_extra = 0; s_host_map = false; }
  if (s_custom_video.enabled) {
    /* The custom mod owns a separate canvas; guest raster stays native. */
    s_ws_extra = 0; s_host_map = false;
  }
  /* Say which binary this is, unconditionally.
   *
   * "Did you start an old version?" is not a question either of us should have
   * to answer by inspecting timestamps. The compiler stamps the build, so the
   * log names it. */
  fprintf(stderr, "build: %s %s  widescreen=%d ninth=%d hostmap=%d\n",
          __DATE__, __TIME__, s_ws_extra, s_ninth_scenario ? 1 : 0,
          s_host_map ? 1 : 0);
  if (s_ws_extra > 0) {
    if (!explicit_size) s_window_width = (kVideoWidth + s_ws_extra * 2) * scale;
    s_video_w = kVideoWidth + s_ws_extra * 2;
    s_video_pitch = s_video_w * 4;
    PpuSetExtraSpace(g_ppu, (uint8_t)s_ws_extra);
    s_ws_scratch = (uint8_t *)calloc((size_t)s_video_pitch, kVideoHeight + 1);
    s_ws_scratch_bg = (uint8_t *)calloc((size_t)s_video_pitch, kVideoHeight + 1);
    s_ws_obj_layer = (uint8_t *)calloc((size_t)s_video_pitch, kVideoHeight + 1);
    /* The per-frame choice above owns this now -- see the mode note there. */
    fprintf(stderr, "widescreen: %d px per side -> %dx%d\n",
            s_ws_extra, s_video_w, kVideoHeight);
  }
  PpuBeginDrawing(g_ppu, s_video_pixels, (size_t)s_video_pitch, s_render_flags);
  host_map_init();
  ScRendererInit(&s_custom_renderer, g_snes->cart->rom, rom_size, s_rom_is_us);
  s_custom_renderer.vram_revision=ScPpuVramRevision;
  if(s_rom_is_us) {
    uint8_t *font=calloc(1,0x20000);
    if(font) {
      ScDecompResult result;
      sc_decomp_run(font,clipboard_rom_read,&s_custom_renderer,9,0xc0fb,0,&result);
      if(!result.bad) ScRendererClipboardFont(&s_custom_renderer,font+0x8000,result.bytes_out);
      free(font);
    }
  }
  s_custom_renderer.population=&s_population;
  if(s_rom_is_us) {
    ScJourneyMenuInit(g_snes->cart->rom,rom_size);
    ScWorldGuestBindRom(g_snes->cart->rom,rom_size);
  }
  { const char *large=getenv("SC_LARGE_MAPS");
    s_large_maps=large?atoi(large):s_launch_settings.large_maps; if(s_large_maps<0 || s_large_maps>5) s_large_maps=0; }
  {const char *style=getenv("SC_TERRAIN_STYLE");
    s_terrain_style=style?atoi(style):s_launch_settings.terrain_style;
    if(s_terrain_style<0 || s_terrain_style>=SC_TERRAIN_STYLES)s_terrain_style=0;}
  s_custom_renderer.world=&s_world;
  s_custom_renderer.sylt = s_ninth_scenario;   /* its pin and mark */
  if (!ScRendererResize(&s_custom_renderer,
        ScVideoViewport(&s_custom_video, s_window_width, s_window_height))) return 1;
  if (s_custom_video.enabled) fprintf(stderr,"custom renderer: %s, %dx%d, core %d,%d\n",
    ScAspectName(s_custom_video.aspect), s_custom_renderer.view.width, s_custom_renderer.view.height,
    s_custom_renderer.view.core_x, s_custom_renderer.view.core_y);

  g_cpu = interp816_init(NULL, bus_read, bus_write);
  interp816_reset(g_cpu);

  /* --load-state <path>: overwrite the just-reset boot state with a
   * previously captured save state (see save_state/load_state and the
   * numbered-slot hotkeys below) -- lets a --qualify run, or the windowed
   * host, start already positioned on a specific screen/scenario instead
   * of from cold boot, so a fixed --input sequence can be replayed against
   * it deterministically. Applied after reset/loadRom so it fully
   * supersedes them rather than racing anything. */
  if (load_state_path) {
    if (!load_state(load_state_path)) {
      fprintf(stderr, "cannot load state '%s'\n", load_state_path);
      return 1;
    }
    fprintf(stderr, "loaded state '%s', now at frame %llu\n",
            load_state_path, (unsigned long long)s_frames);

#ifdef SC_AOT_TIER
    /* The fiber executes its own CpuState, which load_state does not touch --
     * and Init() pinned it to the reset contract during env parsing, before
     * any state existed. Left alone, the fiber runs boot registers over
     * restored WRAM: measured as a hang at $05935A inside 60 frames, while the
     * same state is fine on the interpreter and the fiber is fine from boot. */
    /* load_state() adopts into the fiber itself now. */
#endif
  }

  if (qualify_frames) {
    /* Diagnostic fixture for exercising the expanded game before exposing
     * the new-city option. Old cities keep their top-left tiles and fields. */
    if (getenv("SC_WORLD_TEST") && !s_world.active && s_rom_is_us) {
      ScMapGenPrng pr={0}; sc_mapgen_prng_seed_from_spin(&pr,0xc7);
      if(atoi(getenv("SC_WORLD_TEST"))==2) ScWorldGenerateHuge(&s_world,&pr);else ScWorldGenerate(&s_world,&pr);
      for (unsigned y=0;y<100;++y) memcpy(s_world.tiles+y*ScWorldWidth(&s_world)*2,g_ram+0x10200+y*240,240);
      for (unsigned i=0;i<SC_WORLD_FIELDS;++i) {
        const ScWorldField *field=&ScWorldFields[i];
        unsigned bytes=field->stock_width*field->element_bytes;
        for (unsigned y=0;y<field->stock_height;++y)
          memcpy(s_world.fields[i]+y*ScWorldFieldWidth(&s_world,i)*field->element_bytes,g_ram+0x10000+field->base+y*bytes,bytes);
      }
      ram_set_w(0x1c5,ScWorldWidth(&s_world)-25);ram_set_w(0x1c9,ScWorldHeight(&s_world)-22);
      fprintf(stderr,"world: diagnostic %ux%u city enabled\n",ScWorldWidth(&s_world),ScWorldHeight(&s_world));
    }
    return run_qualification(qualify_frames);
  }

  /* The cartridge's save memory on disk (src/sc_sram.c): saved cities and
   * scenario win marks survive closing the game. Windowed runs only -- a
   * --qualify run has returned above, so no tool or comparison ever reads or
   * writes the player's saves. Opened after --load-state, so the file wins
   * over whatever SRAM the state carried. SC_SRAM_PATH names the file,
   * SC_SRAM=0 turns this off. */
  { const char *off = getenv("SC_SRAM");
    if (!(off && *off == '0')) {
      char path[1024];
      const char *e = getenv("SC_SRAM_PATH");
      if (e && *e) {
        snprintf(path, sizeof path, "%s", e);
      } else {
        const char *tag = s_rom_fnv == SC_ROM_FNV_US ? "us"
                        : s_rom_fnv == SC_ROM_FNV_EU ? "eu"
                        : s_rom_fnv == SC_ROM_FNV_FR ? "fr"
                        : s_rom_fnv == SC_ROM_FNV_DE ? "de"
                        : s_rom_fnv == SC_ROM_FNV_JP ? "jp" : "rom";
        snprintf(path, sizeof path, "urbanrecomp-%s.srm", tag);
      }
      if (ScSram_Open(g_snes->cart->ram, g_snes->cart->ramSize, path)) {
        snprintf(s_population_path, sizeof s_population_path, "%s.population", path);
        snprintf(s_world_path, sizeof s_world_path, "%s.world", path);
      }
    } }

  /* SDL3 returns true on success where SDL2 returned 0, so a bare `!= 0`
   * reads a successful init as a failure -- with an empty SDL_GetError(),
   * because nothing actually went wrong. Caught only by launching the window:
   * --qualify never initialises video, so the headless verification that
   * cleared the SDL3 migration could not have found this. */
#if SNESRECOMP_SDL3
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
#else
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
#endif
    return startup_sdl_failure("SDL_Init failed");
  }
#if SNESRECOMP_SDL3
  const SDL_WindowFlags window_flags = s_fullscreen ? SDL_WINDOW_FULLSCREEN : 0;
#else
  const Uint32 window_flags = s_fullscreen == 2 ? SDL_WINDOW_FULLSCREEN
                            : s_fullscreen == 1 ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
#endif
  SDL_Window *window = snesrecomp_sdl_create_window(
      "Urban Recomp", s_window_width, s_window_height,
      SDL_WINDOW_RESIZABLE | window_flags);
  if (!window) return startup_sdl_failure("SDL_CreateWindow failed");
  ScSetWindowIcon(window);
  /* No SDL_RENDERER_PRESENTVSYNC: on some hosts (observed under a VM) the
   * driver's vsync wait blocks for longer than one real display refresh
   * (e.g. ~33ms instead of ~16.67ms), silently halving the whole loop's
   * rate -- since simulation advancement here is 1:1 with each present,
   * that drags the SNES-side "logical" game down to half speed too (every
   * animation, not just this cursor), while the interpreter itself was
   * never the bottleneck (SC_FRAME_TIME showed zero frames exceeding the
   * 16.67ms budget). Pace manually against the wall clock instead below. */
  /* vsync off deliberately -- see the comment above; pacing is manual. */
  SDL_Renderer *renderer = ScGpuTerrainRenderer(window);
  if(!renderer) renderer = snesrecomp_sdl_create_renderer(window, false, false);
  if (!renderer) return startup_sdl_failure("SDL_CreateRenderer failed");
  { const char *rn = snesrecomp_sdl_renderer_name(renderer);
    fprintf(stderr, "renderer: %s\n", rn ? rn : "?"); }
  SDL_Texture *texture = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      s_custom_video.enabled ? s_custom_renderer.view.width : s_video_w,
      s_custom_video.enabled ? s_custom_renderer.view.height : kVideoHeight);
  if(!texture)return startup_sdl_failure("SDL_CreateTexture failed");

  /* The framebuffer is ARGB8888 but the PPU never writes an alpha byte, so
   * every pixel carries A=0. Under SDL2 that was harmless: a texture defaults
   * to SDL_BLENDMODE_NONE and alpha is ignored. SDL3 defaults the same texture
   * to blending, so A=0 renders it fully transparent -- a black window, with a
   * frame loop, blit and present that all report success. Pin the mode. */
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
  snesrecomp_sdl_set_texture_linear(texture, s_linear_filter);

  /* Queued (pushed) audio, not a pull callback: this host owns the DSP drain
   * loop and hands over finished samples. SDL3 removed SDL_QueueAudio and
   * folded the same behaviour into SDL_AudioStream, so the backend difference
   * lives in sc_sdl_compat.h rather than here. */
  ScAudio audio; SDL_memset(&audio, 0, sizeof(audio));
  const char *music_setting=getenv("SC_MUSIC_THREAD");
  bool want_music_thread=music_setting?*music_setting!='0':getenv("SC_SCRIPTED_INPUT")==NULL;
  bool restored_music=false;
  const char *restored_dir=getenv("SC_RESTORED_MUSIC");
  if(s_enable_audio && s_rom_is_us && want_music_thread && (!restored_dir || strcmp(restored_dir,"0"))) {
    char path[4096];
    if(!restored_dir) {
#if SNESRECOMP_SDL3
      const char *base=SDL_GetBasePath();
#else
      char *base=SDL_GetBasePath();
#endif
      snprintf(path,sizeof path,"%smusic/restored",base?base:"");
#if !SNESRECOMP_SDL3
      SDL_free(base);
#endif
      restored_dir=path;
    }
    restored_music=ScMusicLoadRestored(restored_dir);
    if(!restored_music) fprintf(stderr,"[restored music] complete pack unavailable; using original SPC soundtrack\n");
  }
  bool audio_dev = s_enable_audio && sc_audio_open(&audio, restored_music?44100:32040, 2, 1024);
  if (audio_dev) {
    fprintf(stderr, "audio: opened driver=%s freq=%d channels=%d samples=%d\n",
            SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?",
            audio.freq, audio.channels, audio.samples);
  } else {
    /* Previously silent on failure -- every audio code path below is
     * gated on `if (audio_dev)`, so a failed open just ran the whole
     * game with no sound and no indication why. */
    fprintf(stderr, "audio: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
  }

  bool music_thread=false;
  {
    if(audio_dev && want_music_thread) music_thread=ScMusicStart(g_snes->apu,&audio);
    if(music_thread && restored_music) {
      ScMusicLock();ScMusicRestoreLocked(g_ram[8],!host_map_screen_live() || (ram_w(0x195)&8)!=0);ScMusicUnlock();
    } else if(restored_music) {
      ScMusicLoadRestored(NULL);
      if(audio_dev) {sc_audio_close(&audio);audio_dev=sc_audio_open(&audio,32040,2,1024);}
    }
    if(music_thread) fprintf(stderr,"audio: dedicated SPC/DSP music thread active\n");
  }
  double audio_acc = 0.0;
  int16_t audio_buf[1024 * 2];
  bool quit = false;
  ScGpuTerrain *gpu_terrain=NULL;
  ScGpuFields *gpu_fields=NULL;
  bool gpu_failed=false;
  {const char *e=getenv("SC_GPU_TERRAIN");s_gpu_terrain_enabled=!e || *e!='0';}
  /* Live FPS counter in the window title, updated once/sec -- lets a user
   * on a slow host (e.g. a VM) tell at a glance whether the emulator itself
   * is keeping up with real time, independent of anything ROM-side. */
  uint64_t fps_window_start = SDL_GetPerformanceCounter();
  /* SC_PERF=1: once a second, where the wall clock of a host frame went --
   * guest emulation, texture upload and draw, the pacing sleep, and the
   * present. Average and worst frame, in ms. */
  const char *perf_frame_path=getenv("SC_PERF_FRAME_PATH");
  const bool perf_on = getenv("SC_PERF") != NULL || (perf_frame_path && *perf_frame_path);
  FILE *perf_frame_file=NULL;
  FILE *bank_frame_file=NULL;
  const char *bank_frame_path=getenv("SC_BANK_FRAME_PATH");
  if(bank_frame_path && *bank_frame_path) {
    bank_frame_file=fopen(bank_frame_path,"w");
    if(bank_frame_file) {
      s_bank_profile=1;setvbuf(bank_frame_file,NULL,_IOFBF,512*1024);
      fputs("frame,emu_ms",bank_frame_file);
      for(unsigned p=0;p<256;++p) fprintf(bank_frame_file,",bank%02x",p);
      for(unsigned p=0;p<256;++p) fprintf(bank_frame_file,",page%02x",p);
      for(unsigned p=0;p<256;++p) fprintf(bank_frame_file,",pc%02x",p);
      fputc('\n',bank_frame_file);
    }
  }
  ScWorldGuestInterpreterProfile(s_bank_profile!=0);
  if(perf_frame_path && *perf_frame_path) {
    perf_frame_file=fopen(perf_frame_path,"w");
    if(perf_frame_file) {
      setvbuf(perf_frame_file,NULL,_IOFBF,512*1024);
      fputs("frame,city,development,input_ms,emu_ms,audio_ms,draw_ms,present_ms,sleep_ms,advisor,extra_attempts,host_raster_ms,objects_ms,terrain_capture_ms,rows_ms,native_capture_ms,repair_ms,hud_ms,pointer_ms,map_track_ms\n",perf_frame_file);
    }
  }
  const char *perf_exit_text=getenv("SC_PERF_EXIT_AT");
  const uint64_t perf_exit_frame=perf_exit_text?strtoull(perf_exit_text,NULL,0):0;
  s_perf_detail=perf_on;s_perf_clock_ms=1000.0/SDL_GetPerformanceFrequency();
  const char *render_profile=getenv("SC_RENDER_PROFILE");
  s_custom_renderer.measure_clock=render_profile && *render_profile=='1'?SDL_GetPerformanceCounter:NULL;
  enum { kPerfInput, kPerfEmu, kPerfAudio, kPerfDraw, kPerfSleep, kPerfPresent,
         kPerfCount };
  double perf_sum[kPerfCount] = {0}, perf_max[kPerfCount] = {0};
  double perf_total[kPerfCount]={0};
  double perf_current[kPerfCount]={0};
  uint64_t perf_total_frames=0,perf_total_guests=0;
  int perf_frames = 0;
  const double perf_ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
#define SC_PERF_ADD(slot, t0, t1) do { if (perf_on) { \
    const double _ms = (double)((t1) - (t0)) * perf_ms; \
    perf_sum[slot] += _ms; perf_total[slot]+=_ms; perf_current[slot]=_ms; if (_ms > perf_max[slot]) perf_max[slot] = _ms; } } while (0)
  uint64_t fps_window_frames = 0;
  uint64_t fps_guest_frames=0;
  double full_frame_ms=8.0,extra_frame_ms=6.0;
  /* Recorded equivalent of holding Tab, for dummy-SDL pacing checks. */
  const bool test_fast_forward=getenv("SC_FAST_FORWARD")!=NULL;
  /* Manual frame pacer, replacing vsync (see the renderer-creation comment
   * above): target the SNES's real ~60.0988fps, sleeping off any leftover
   * budget each loop iteration instead of blocking on a potentially-broken
   * driver vsync wait. */
  const double kTargetFrameSeconds = 1.0 / 60.0988;
  uint64_t next_frame_deadline = SDL_GetPerformanceCounter();
  while (!quit) {
    const uint64_t loop_t0 = perf_on ? SDL_GetPerformanceCounter() : 0;
    const uint64_t perf_extra_begin=perf_frame_file?s_development.extra_attempts:0;
    unsigned long long bank_begin[256],page_begin[256],pc_begin[256];
    if(bank_frame_file) {
      memcpy(bank_begin,s_bank_ops,sizeof bank_begin);memcpy(page_begin,s_b3_page,sizeof page_begin);
      memcpy(pc_begin,s_pc_profile_ops,sizeof pc_begin);
    }
    if(perf_on) memset(perf_current,0,sizeof perf_current);
    SDL_Event ev;
    /* Owned regression windows exercise the actual SDL wheel/pinch routing.
     * SC_ZOOM_EVENTS=tick:kind:amount:ctrl[:window_x:window_y],...;
     * kind is w (wheel) or p (pinch).
     * This does not move the system pointer or affect another application's
     * input. Absent the test variable, the block stays inactive. */
    {
      static const char *events;
      static bool initialized;
      static unsigned tick;
      if(!initialized) {events=getenv("SC_ZOOM_EVENTS");initialized=true;}
      if(events) {
        const char *cursor=events;unsigned at,ctrl;char kind;double amount;int used;
        while(sscanf(cursor,"%u:%c:%lf:%u%n",&at,&kind,&amount,&ctrl,&used)==4) {
          double px,py;SDL_Event pointer_event={0};zoom_event_pointer(&pointer_event,&px,&py);
          int extended;
          if(sscanf(cursor,"%u:%c:%lf:%u:%lf:%lf%n",&at,&kind,&amount,&ctrl,&px,&py,&extended)==6)used=extended;
          if(at==tick && isfinite(amount)) {
            SDL_Event gesture={0};
            if(kind=='w') {
              SDL_SetModState(ctrl?KMOD_CTRL:KMOD_NONE);
              gesture.type=SDL_MOUSEWHEEL;
              gesture.wheel.windowID=SDL_GetWindowID(window);
              gesture.wheel.y=fmax(-32,fmin(32,amount));
#if SNESRECOMP_SDL3
              gesture.wheel.mouse_x=px;gesture.wheel.mouse_y=py;
#elif SDL_VERSION_ATLEAST(2,26,0)
              gesture.wheel.mouseX=px;gesture.wheel.mouseY=py;
#endif
#if !SNESRECOMP_SDL3 && SDL_VERSION_ATLEAST(2,0,18)
              gesture.wheel.preciseY=(float)amount;
#endif
            }
#if SNESRECOMP_SDL3 && SDL_VERSION_ATLEAST(3,4,0)
            else if(kind=='p') {
              gesture.type=SDL_EVENT_PINCH_UPDATE;
              gesture.pinch.windowID=SDL_GetWindowID(window);
              gesture.pinch.scale=(float)amount;
            }
#endif
            if(gesture.type) SDL_PushEvent(&gesture);
          }
          cursor+=used;if(*cursor++!=',')break;
        }
        ++tick;
      }
    }
    /* Owned regression windows can deliver frame-stable keyboard events
     * without changing the user's foreground window or keyboard state. */
    { const char *events=getenv("SC_KEY_EVENTS");
      static unsigned tick;
      if(events) {
        unsigned at,code,repeat,mods;int consumed;
        while(*events) {
          mods=0;
          if(sscanf(events,"%u:%u:%u:%u%n",&at,&code,&repeat,&mods,&consumed)!=4 &&
             sscanf(events,"%u:%u:%u%n",&at,&code,&repeat,&consumed)!=3)break;
          if(at==tick && code<SDL_NUM_SCANCODES) {
            SDL_Event key={0};key.type=SDL_KEYDOWN;
            SC_EVENT_SCANCODE(key)=(SDL_Scancode)code;key.key.repeat=repeat!=0;
            SC_EVENT_KEYMOD(key)=mods;
            SDL_PushEvent(&key);
          }
          events+=consumed;
          if(*events++!=',') break;
        }
        ++tick;
      }
    }
    for (;;) {
      /* SC_PERF also names a poll that stalls, and the event it returned. */
      const uint64_t poll_t0 = perf_on ? SDL_GetPerformanceCounter() : 0;
      const bool got = SDL_PollEvent(&ev) != 0;
      if (perf_on) {
        const double ms = (double)(SDL_GetPerformanceCounter() - poll_t0) * perf_ms;
        if (ms > 20.0) {
          unsigned sub = 0;
#if !SNESRECOMP_SDL3
          if (got && ev.type == SDL_WINDOWEVENT) sub = ev.window.event;
#endif
          fprintf(stderr, "[perf] SDL_PollEvent took %.1f ms (event 0x%x/%u) at frame %llu\n",
                  ms, got ? (unsigned)ev.type : 0u, sub, (unsigned long long)s_frames);
        }
      }
      if (!got) break;
      if (ev.type == SDL_QUIT) quit = true;
      if(ev.type==SDL_KEYDOWN && SC_EVENT_SCANCODE(ev)==SDL_SCANCODE_GRAVE &&
          !ev.key.repeat && (SC_EVENT_KEYMOD(ev)&KMOD_CTRL) && (SC_EVENT_KEYMOD(ev)&KMOD_SHIFT) &&
          s_rom_is_us && (ram_w(0x14)==17 || ram_w(0x14)==3)) {
        s_test_revealed=!s_test_revealed;
        if(ram_w(0x14)==3 && s_test_revealed)s_test_menu_pending=true;
        if(!s_test_revealed && ram_w(0x421)==2)ram_set_w(0x421,0);
        fprintf(stderr,"[test city] hidden Load City entry %s\n",s_test_revealed?"revealed":"hidden");
        continue;
      }
      if(ev.type==SDL_KEYDOWN && SC_EVENT_SCANCODE(ev)==SDL_SCANCODE_ESCAPE && !ev.key.repeat) {
        if(s_preview_expanded && map_selection_preview_live()) {
          s_preview_expanded=false;continue;
        }
        if(getenv("SC_SAVE_DIALOG_DIAG")) fprintf(stderr,"[escape save] key frame %llu mode=%u city=%u modal=%u/%u/%u/%u dialog=%u\n",
            (unsigned long long)s_frames,ram_w(0x14),ram_w(0x3e),ram_w(0xd7),ram_w(0x379),g_ram[0x391],g_ram[0xe3],s_mouse_dialog);
        if(s_menu_open) s_menu_open=false;
        else if(s_rom_is_us && g_ram[0x14]==0 && ram_w(0x3e) &&
            !ram_w(0xd7) && !ram_w(0x379) && !g_ram[0x391] && !g_ram[0xe3] &&
            s_mouse_dialog==SC_MOUSE_DIALOG_NONE && !s_save_dialog_active) {
          s_save_dialog_pending=true;
          s_middle_pan=(ScMousePan){0};
        } else {
          s_escape_back_frames=2;
          s_escape_back_input=(s_mouse_dialog==SC_MOUSE_DIALOG_SLOTS ||
              s_mouse_dialog==SC_MOUSE_DIALOG_SAVE_CONFIRM)?kPad_X:
              s_rom_is_us?ScMouseUiModalInput(g_ram,0,true):kPad_A;
        }
        continue;
      }
      if (getenv("SC_SCRIPTED_INPUT") && !(getenv("SC_KEY_EVENTS") &&
          ev.type==SDL_KEYDOWN && ev.key.windowID==0)) continue; /* owned UI regression window */
      if(!s_menu_open && s_custom_renderer.map_preview.active &&
          (ram_w(0x14)==5 || ram_w(0x14)==6) && !ram_w(0xb31) &&
          ev.type==SDL_MOUSEWHEEL && (SDL_GetModState()&KMOD_CTRL)) {
#if !SNESRECOMP_SDL3 && SDL_VERSION_ATLEAST(2,0,18)
        double amount=ev.wheel.preciseY;
#else
        double amount=ev.wheel.y;
#endif
        if(ev.wheel.direction==SDL_MOUSEWHEEL_FLIPPED)amount=-amount;
        int ww,wh,dw,dh;SDL_GetWindowSize(window,&ww,&wh);SDL_GetRendererOutputSize(renderer,&dw,&dh);
        if(ww<=0 || wh<=0)continue;
        ScRect rect=map_selection_preview_rect(s_custom_renderer.view,dw,dh);
        double mx,my;zoom_event_pointer(&ev,&mx,&my);
        double x=(mx*dw/ww-rect.x)/rect.w,y=(my*dh/wh-rect.y)/rect.h;
        if(!isfinite(x) || !isfinite(y) || x<0 || x>=1 || y<0 || y>=1)continue;
        if(isfinite(amount))sc_mapgen_preview_zoom(&s_custom_renderer.map_preview,
            pow(1.25,fmax(-32,fmin(32,amount))),x,y);
        if(getenv("SC_MAP_PREVIEW_DIAG"))fprintf(stderr,"[map preview] zoom %.4f center %.4f,%.4f anchor %.4f,%.4f size %u,%u\n",
            s_custom_renderer.map_preview.zoom,s_custom_renderer.map_preview.center_x,s_custom_renderer.map_preview.center_y,
            x,y,s_custom_renderer.map_preview.width,s_custom_renderer.map_preview.height);
        continue;
      }
      if(!s_menu_open && !s_build_active && !s_build_pending && !s_clip_drag && !s_clip_pending && host_map_screen_live() &&
         !ram_w(0xd7) && !ram_w(0x379) && !g_ram[0x391] && !g_ram[0xe3]) {
        if(ev.type==SDL_MOUSEWHEEL && (SDL_GetModState()&KMOD_CTRL)) {
#if SNESRECOMP_SDL3
          double amount=ev.wheel.y;
#elif SDL_VERSION_ATLEAST(2,0,18)
          double amount=ev.wheel.preciseY;
#else
          double amount=ev.wheel.y;
#endif
          if(ev.wheel.direction==SDL_MOUSEWHEEL_FLIPPED) amount=-amount;
          if(isfinite(amount))city_zoom_at_pointer(window,renderer,&ev,pow(1.125,fmax(-32,fmin(32,amount))));
          continue;
        }
#if SNESRECOMP_SDL3 && SDL_VERSION_ATLEAST(3,4,0)
        if(ev.type==SDL_EVENT_PINCH_UPDATE) {
          if(isfinite(ev.pinch.scale) && ev.pinch.scale>0)city_zoom_at_pointer(window,renderer,&ev,ev.pinch.scale);
          continue;
        }
#endif
      }
      if (ev.type == SDL_KEYDOWN && !ev.key.repeat && SC_EVENT_SCANCODE(ev)==SDL_SCANCODE_F11) {
        s_fullscreen=!s_fullscreen;
#if SNESRECOMP_SDL3
        SDL_SetWindowFullscreen(window,s_fullscreen);
#else
        SDL_SetWindowFullscreen(window,s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#endif
      }
      if (ev.type == SDL_KEYDOWN && SC_EVENT_SCANCODE(ev) == SDL_SCANCODE_F1) {
        if (s_pc_bitmap_bank != -1) {
          if (s_pc_bitmap_bank == -2) memset(s_pc_bitmap_all, 0, sizeof(s_pc_bitmap_all));
          else memset(s_pc_bitmap, 0, sizeof(s_pc_bitmap));
        }
        s_gfx_trace_hits = 0;
        s_dbg_live_hits = 0;
        fprintf(stderr, "[F1] PC bitmap capture / gfx trace reset at frame %llu\n",
                (unsigned long long)s_frames);
      }
      /* F2: one-button entry of the documented debug-menu code on
       * controller 2 (see queue_debug_menu_code) -- press once while on
       * the "Goodbye! See you soon" quit-confirmation screen, instead of
       * hand-timing all 16 inputs. !ev.key.repeat so holding F2 doesn't
       * re-queue every auto-repeat tick. */
      if (ev.type == SDL_KEYDOWN && SC_EVENT_SCANCODE(ev) == SDL_SCANCODE_F2 && !ev.key.repeat) {
        queue_debug_menu_code(s_frames + 1);
      }
      /* +/- : zoom the host-rendered map.
       *
       * Only possible because the map is drawn host-side; the guest's own
       * renderer is fixed at 8 pixels per cell. Steps through a small set of
       * cell sizes rather than free-scaling, so every step stays an exact
       * nearest-neighbour ratio and the tiles keep their shape.
       *
       * The HUD is unaffected -- it comes from the guest at 1:1 and is
       * composited after, so it stays crisp at every zoom level. */
      if (ev.type == SDL_KEYDOWN && !ev.key.repeat && s_host_map) {
        static const int kCellSizes[] = { 2, 4, 8, 16, 32 };
        const int n = (int)(sizeof(kCellSizes) / sizeof(kCellSizes[0]));
        SDL_Scancode sc = SC_EVENT_SCANCODE(ev);
        int dir = 0;
        if (sc == SDL_SCANCODE_EQUALS || sc == SDL_SCANCODE_KP_PLUS) dir = +1;
        if (sc == SDL_SCANCODE_MINUS  || sc == SDL_SCANCODE_KP_MINUS) dir = -1;
        if (dir) {
          int cur = ScMapView_GetCellPx(), idx = 2;
          for (int i = 0; i < n; i++) if (kCellSizes[i] == cur) idx = i;
          idx += dir;
          if (idx < 0) idx = 0;
          if (idx >= n) idx = n - 1;
          ScMapView_SetCellPx(kCellSizes[idx]);
          fprintf(stderr, "[zoom] %d px per map cell (%s)\n", kCellSizes[idx],
                  kCellSizes[idx] == 8 ? "native" :
                  kCellSizes[idx] > 8 ? "zoomed in" : "zoomed out");
        }
      }

      /* F3: toggle absolute desktop mouse control (enabled by default). */
      if (ev.type == SDL_KEYDOWN && SC_EVENT_SCANCODE(ev) == SDL_SCANCODE_F3 && !ev.key.repeat) {
        s_mouse_enabled = !s_mouse_enabled;
        fprintf(stderr, "[F3] mouse cursor control %s\n", s_mouse_enabled ? "ON" : "OFF");
      }
      /* F9: toggle the fast D-pad cursor (see apply_mouse_delta/
       * s_fast_cursor_enabled above). Off by default -- same "opt-in,
       * not authentic ROM behavior" reasoning as F3. */
      if (ev.type == SDL_KEYDOWN && SC_EVENT_SCANCODE(ev) == SDL_SCANCODE_F9 && !ev.key.repeat) {
        s_fast_cursor_enabled = !s_fast_cursor_enabled;
        fprintf(stderr, "[F9] fast D-pad cursor %s\n", s_fast_cursor_enabled ? "ON" : "OFF");
      }
      /* F10/F12: settings menu (see the "minimal in-game settings menu" block
       * above) -- toggles a host-side overlay listing this project's
       * existing toggles/actions in one generic, table-driven list instead
       * of each needing its own memorized hotkey. */
      if (ev.type == SDL_KEYDOWN && (SC_EVENT_SCANCODE(ev) == SDL_SCANCODE_F10 ||
          SC_EVENT_SCANCODE(ev) == SDL_SCANCODE_F12) && !ev.key.repeat) {
        s_menu_open = !s_menu_open;
        fprintf(stderr, "[F10/F12] settings menu %s\n", s_menu_open ? "OPEN" : "CLOSED");
      }
      if (s_menu_open && ev.type == SDL_KEYDOWN) {
        switch (SC_EVENT_SCANCODE(ev)) {
          case SDL_SCANCODE_UP:
            /* Step until a non-header lands under the cursor. Bounded by
             * kSettingCount so an all-header table cannot spin forever. */
            for (size_t n = 0; n < kSettingCount; n++) {
              s_menu_selected =
                  (s_menu_selected - 1 + (int)kSettingCount) % (int)kSettingCount;
              if (s_settings[s_menu_selected].kind != kSettingHeader) break;
            }
            break;
          case SDL_SCANCODE_DOWN:
            for (size_t n = 0; n < kSettingCount; n++) {
              s_menu_selected = (s_menu_selected + 1) % (int)kSettingCount;
              if (s_settings[s_menu_selected].kind != kSettingHeader) break;
            }
            break;
          case SDL_SCANCODE_RETURN:
            setting_activate(&s_settings[s_menu_selected]);
            break;
          case SDL_SCANCODE_LEFT:
          case SDL_SCANCODE_RIGHT:
            setting_adjust(&s_settings[s_menu_selected],SC_EVENT_SCANCODE(ev)==SDL_SCANCODE_LEFT?-1:1);
            break;
          default: break;
        }
      }
      /* Handle every mouse event while it is being polled. Checking only the
       * last event after polling lost clicks followed by motion/window events. */
      if(s_menu_open && (ev.type==SDL_MOUSEMOTION ||
          (ev.type==SDL_MOUSEBUTTONDOWN && ev.button.button==SDL_BUTTON_LEFT))) {
        double x=ev.type==SDL_MOUSEMOTION?ev.motion.x:ev.button.x;
        double y=ev.type==SDL_MOUSEMOTION?ev.motion.y:ev.button.y;
        int row=settings_mouse_row(window,renderer,x,y);
        if(row>=0) {
          s_menu_selected=row;
          if(ev.type==SDL_MOUSEBUTTONDOWN) setting_activate(&s_settings[row]);
        }
      }
      /* F4: dump WRAM to a fixed path right now, on demand -- for pinning
       * down exact WRAM byte values at a precise live moment (e.g. hold a
       * button combo, press F4, inspect $7e011b/$7e011c directly) instead
       * of inferring values from instruction traces. */
      if (ev.type == SDL_KEYDOWN && SC_EVENT_SCANCODE(ev) == SDL_SCANCODE_F4 && !ev.key.repeat) {
        const char *path = "wram_snapshot.bin";
        if (write_wram_dump(path))
          fprintf(stderr, "[F4] dumped WRAM to %s at frame %llu\n", path, (unsigned long long)s_frames);
        else
          fprintf(stderr, "[F4] failed to write WRAM dump to %s\n", path);
      }
      /* F5-F8: directly toggle the stock ROM's own debug-menu cheat flags
       * word, WRAM $0425 -- found by tracing a published Pro Action Replay
       * code list's "Enable Debugger" address (01:88e7, a boot-time load
       * from SRAM $700009 into $0425) forward to every site that reads
       * $0425, and the in-game debug-menu handler itself (00:da04-da3f,
       * which XORs a per-option bitmask table at 00:da50 into $0425, and
       * commits it to SRAM $700009 when "Memory: SET" is chosen). This
       * sidesteps needing the documented controller-2 entry code or menu
       * navigation entirely -- same end effect, poked directly.
       * Bit 0x02 (Needless Money) and 0x04 (Valve Max) are independently
       * confirmed by disassembling their consumers (01:bb7a's money-
       * deduction skip; 03:8b37's RCI-demand-meter force). Bits 0x01/0x08
       * are inferred from the option table's position/ordering only (not
       * yet confirmed against a consumer) -- label accordingly if this
       * turns out wrong. */
      if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
        const char *label = NULL; uint8_t bit = 0;
        switch (SC_EVENT_SCANCODE(ev)) {
          case SDL_SCANCODE_F5: label = "No Disasters (unconfirmed bit)"; bit = 0x01; break;
          case SDL_SCANCODE_F6: label = "Needless Money"; bit = 0x02; break;
          case SDL_SCANCODE_F7: label = "Valve Max"; bit = 0x04; break;
          case SDL_SCANCODE_F8: label = "Water Reclaim (unconfirmed bit)"; bit = 0x08; break;
          default: break;
        }
        if (bit) {
          g_ram[0x0425] ^= bit;
          fprintf(stderr, "[cheat] %s %s ($0425=%02x)\n", label,
                  (g_ram[0x0425] & bit) ? "ON" : "OFF", g_ram[0x0425]);
        }
      }
      /* Numbered save-state slots: Shift+1..Shift+0 saves slot 1-9/0,
       * plain 1..0 loads it -- 10 slots so a specific scenario (e.g. "on
       * the map screen, cursor visible, nothing held") can be captured
       * once interactively and then reloaded instantly and deterministically
       * for repeated testing, instead of re-navigating menus (or guessing
       * --input timing) every run. SNES Select is bound to B (not Shift)
       * below specifically so holding Shift for a save/load never also
       * feeds a Select press into the game at that exact moment -- a save
       * state should capture "nothing else held," not "Select held". */
      if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
        static const struct { SDL_Scancode sc; char digit; } kSlotKeys[] = {
          { SDL_SCANCODE_1, '1' }, { SDL_SCANCODE_2, '2' }, { SDL_SCANCODE_3, '3' },
          { SDL_SCANCODE_4, '4' }, { SDL_SCANCODE_5, '5' }, { SDL_SCANCODE_6, '6' },
          { SDL_SCANCODE_7, '7' }, { SDL_SCANCODE_8, '8' }, { SDL_SCANCODE_9, '9' },
          { SDL_SCANCODE_0, '0' },
        };
        for (size_t i = 0; i < sizeof(kSlotKeys) / sizeof(kSlotKeys[0]); i++) {
          if (SC_EVENT_SCANCODE(ev) != kSlotKeys[i].sc) continue;
          char path[32];
          snprintf(path, sizeof(path), "savestate_%c.bin", kSlotKeys[i].digit);
          if (SC_EVENT_KEYMOD(ev) & KMOD_SHIFT) {
            if (save_state(path))
              fprintf(stderr, "[state] saved slot %c -> %s at frame %llu\n",
                      kSlotKeys[i].digit, path, (unsigned long long)s_frames);
            else
              fprintf(stderr, "[state] failed to save slot %c -> %s\n", kSlotKeys[i].digit, path);
          } else {
            if (load_state(path))
              fprintf(stderr, "[state] loaded slot %c <- %s, now at frame %llu\n",
                      kSlotKeys[i].digit, path, (unsigned long long)s_frames);
            else
              fprintf(stderr, "[state] failed to load slot %c <- %s (not saved yet?)\n",
                      kSlotKeys[i].digit, path);
          }
          break;
        }
      }
    }
    if(s_fit_screen_requested) {
      s_fit_screen_requested=false;
      int before_w=0,before_h=0;
      SDL_GetRendererOutputSize(renderer,&before_w,&before_h);
      ScViewport before=ScVideoViewport(&s_custom_video,before_w,before_h);
      if(s_custom_video.enabled) before=s_custom_renderer.view;
      else { before.width=s_video_w;before.core_x=s_ws_extra; }
      if(s_custom_video.enabled && s_custom_video.aspect==SC_FIT) {
        before.pixel_scale=s_custom_video.fit_scale;
        before.pixel_aspect=s_custom_video.fit_pixel_aspect;
      }
      ScVideoCaptureScale(&s_custom_video,before,before_w,before_h);
      if(!s_custom_video.enabled) {
        /* Switching from classic widescreen also restores native raster
         * coordinates; the adaptive compositor owns the expanded surface. */
        s_ws_extra=0;s_host_map=false;s_video_w=kVideoWidth;s_video_pitch=kVideoWidth*4;
        PpuSetExtraSpace(g_ppu,0);
        PpuBeginDrawing(g_ppu,s_video_pixels,(size_t)s_video_pitch,s_render_flags);
        ScRendererResetHistory(&s_custom_renderer);
      }
      s_custom_video.enabled=true;s_custom_video.aspect=SC_FIT;s_custom_video.centered=false;
      SDL_MaximizeWindow(window);
      if(!ScVideoSave(&s_custom_video,s_video_config))
        fprintf(stderr,"settings: could not save fit-to-screen preference\n");
    }
    s_custom_renderer.map_zoom=s_custom_video.map_zoom>0?s_custom_video.map_zoom:1;
    {const char *e=getenv("SC_CITY_ZOOM");if(e && atof(e)>0)s_custom_renderer.map_zoom=atof(e);}
    int drawable_w = 0, drawable_h = 0;
    SDL_GetRendererOutputSize(renderer, &drawable_w, &drawable_h);
    /* Older Fit preferences have no captured scale. Lock their initial view
     * once, so subsequent resizes reveal land instead of magnifying it. */
    if(s_custom_video.enabled && s_custom_video.aspect==SC_FIT &&
       (s_custom_video.fit_scale<=0 || s_custom_video.fit_pixel_aspect<=0))
      ScVideoCaptureScale(&s_custom_video,
        ScVideoViewport(&s_custom_video,drawable_w,drawable_h),drawable_w,drawable_h);
    ScViewport viewport = ScVideoViewport(&s_custom_video, drawable_w, drawable_h);
    if (!s_custom_video.enabled) { viewport.width = s_video_w; viewport.core_x = s_ws_extra; }
    /* A paused frame is retained until simulation resumes, including while
     * resizing its window. Never clear the paused picture for a new canvas. */
    if (s_menu_open && s_custom_video.enabled) {
      viewport=s_custom_renderer.view;
      /* Keep the frozen background at its old tile size until it can be
       * redrawn at the larger canvas after the settings overlay closes. */
      if(s_custom_video.aspect==SC_FIT) {
        viewport.pixel_scale=s_custom_video.fit_scale;
        viewport.pixel_aspect=s_custom_video.fit_pixel_aspect;
      }
    }
    if (s_custom_video.enabled &&
        (viewport.width != s_custom_renderer.view.width || viewport.height != s_custom_renderer.view.height)) {
      SDL_Texture *replacement = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, viewport.width, viewport.height);
      if (!replacement || !ScRendererResize(&s_custom_renderer, viewport)) {
        if (replacement) SDL_DestroyTexture(replacement);
        fprintf(stderr,"Unable to resize widescreen surface: %s\n",SDL_GetError());
        quit = true; continue;
      }
      SDL_SetTextureBlendMode(replacement, SDL_BLENDMODE_NONE);
      snesrecomp_sdl_set_texture_linear(replacement, s_linear_filter);
      SDL_DestroyTexture(texture); texture = replacement;
      fprintf(stderr,"custom resize: %dx%d -> %dx%d core %d,%d\n",drawable_w,drawable_h,
        viewport.width,viewport.height,viewport.core_x,viewport.core_y);
    }
    s_destination = ScVideoDestination(viewport, drawable_w, drawable_h);
    const uint8_t *keys = snesrecomp_sdl_get_keyboard_state();
    bool scripted_input = getenv("SC_SCRIPTED_INPUT") != NULL;
    static const uint8_t empty_keys[512] = {0};
    if (scripted_input) keys = empty_keys;
    /* Owned UI replays: start-guest-frame:duration:SDL-scancode. These
     * held states exercise normal keyboard paths without touching Windows. */
    uint8_t held_keys[512]={0};
    const char *holds=getenv("SC_KEY_HOLDS");
    if(holds) {
      unsigned long long start,duration;unsigned code;int used;
      while(*holds && sscanf(holds,"%llu:%llu:%u%n",&start,&duration,&code,&used)==3) {
        if(code<sizeof held_keys && s_frames>=start && s_frames-start<duration)held_keys[code]=1;
        holds+=used;if(*holds++!=',')break;
      }
      keys=held_keys;
    }
    static SDL_Scancode pan_key=SDL_SCANCODE_UNKNOWN;
    if(pan_key==SDL_SCANCODE_UNKNOWN)pan_key=sc_scancode_from_key(SDLK_x);
    /* Absolute SDL pointer coordinates are mapped through the rendered view. */
    const uint8_t cursor_before_mouse_x = g_ram[0x01eb];
    const uint8_t cursor_before_mouse_y = g_ram[0x01ed];
    bool mouse_target_valid = false;
    bool mouse_pointer_moved=false;
    bool mouse_ui_handled = false, mouse_ui_hit = false;
    bool mouse_ui_select = false;
    bool mouse_raw_left = false;
    bool mouse_pan_active = false;
    static bool mouse_pan_grab_requested;
    static bool mouse_pan_relative_requested;
    static double mouse_pan_virtual_x,mouse_pan_virtual_y;
    static double mouse_pan_restore_x,mouse_pan_restore_y;
    bool mouse_navigation_hit = false;
    uint16_t mouse_edge_input = 0;
    int mouse_target_x = 0, mouse_target_y = 0;
    int mouse_city_x = 0, mouse_city_y = 0;
    bool mouse_city_hit = false;
    bool mouse_clip_hit = false, mouse_clip_consumed = false;
    int mouse_clip_button = -1;
    s_clip_hover=false;
    s_custom_renderer.pointer_active = false;
    s_custom_renderer.pointer_hud = false;
    s_custom_renderer.pointer_hidden = false;
    s_custom_renderer.clipboard_cursor=s_clip_tool!=0;
    uint32_t mouse_buttons = 0;
    if (s_mouse_enabled && !scripted_input && !s_menu_open) {
      const uint32_t wf = (uint32_t)SDL_GetWindowFlags(window);
      bool focused = (wf & SDL_WINDOW_INPUT_FOCUS) &&
                           ((wf & SDL_WINDOW_MOUSE_FOCUS) || s_build_active || s_clip_drag || s_middle_pan.active);
      double mx, my;
#if SNESRECOMP_SDL3
      float fx = 0, fy = 0;
      mouse_buttons = SDL_GetMouseState(&fx, &fy);
      mx = fx; my = fy;
#else
      int ix = 0, iy = 0;
      mouse_buttons = SDL_GetMouseState(&ix, &iy);
      mx = ix; my = iy;
#endif
      /* Relative motion never reaches a window edge. Keep an unbounded
       * virtual position for the drag accumulator, independent of SDL's
       * centered, invisible cursor. Recorded canvas input remains absolute. */
      if(mouse_pan_relative_requested && !getenv("SC_MOUSE_INPUT")) {
        double dx,dy;sc_relative_mouse_delta(&dx,&dy);
        mouse_pan_virtual_x+=dx;mouse_pan_virtual_y+=dy;
        mx=mouse_pan_virtual_x;my=mouse_pan_virtual_y;
        if(!(mouse_buttons&(SDL_BUTTON(SDL_BUTTON_MIDDLE)|SDL_BUTTON(SDL_BUTTON_RIGHT)))) {
          mx=mouse_pan_restore_x;my=mouse_pan_restore_y;
        }
      }
      int ww = 0, wh = 0;
      SDL_GetWindowSize(window, &ww, &wh);
      mouse_raw_left = focused && (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT));
      /* Menus are centered even when the city HUD is anchored at top left.
       * The renderer's live view is the authority for the displayed frame. */
      ScViewport pointer_view = s_custom_video.enabled ?
                                s_custom_renderer.view : viewport;
      /* Recorded canvas-space pointer events exercise this same windowed
       * input path under the dummy SDL driver, including captured releases. */
      const char *mouse_test=getenv("SC_MOUSE_INPUT");
      if (mouse_test) {
        static unsigned tick;
        unsigned at,buttons=0; double x=-1000,y=-1000; int consumed;
        const char *event=mouse_test;
        while (sscanf(event,"%u:%lf:%lf:%u%n",&at,&mx,&my,&buttons,&consumed)==4) {
          if (at>tick) break;
          x=mx; y=my; mouse_buttons=buttons;
          event+=consumed;
          if (*event++!=',') break;
        }
        ++tick;
        mx=(s_destination.x+x*s_destination.w/pointer_view.width)*ww/drawable_w;
        my=(s_destination.y+y*s_destination.h/pointer_view.height)*wh/drawable_h;
        focused=true;
        mouse_raw_left=(mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT))!=0;
        const char *tool=getenv("SC_MOUSE_TOOL");
        if (tool) ram_set_w(0x20d,(uint16_t)atoi(tool));
      }
      bool inside = focused && (s_custom_video.enabled ?
          ScRendererWindowToGuest(&s_custom_renderer,s_destination,ww,wh,drawable_w,drawable_h,
              mx,my,&mouse_target_x,&mouse_target_y,&mouse_navigation_hit) :
          ScVideoWindowToGuest(pointer_view,s_destination,ww,wh,drawable_w,drawable_h,
              mx,my,&mouse_target_x,&mouse_target_y));
      bool preview_live=map_selection_preview_live();
      ScRect preview_rect=map_selection_preview_rect(pointer_view,drawable_w,drawable_h);
      double px=ww>0?mx*drawable_w/ww:0,py=wh>0?my*drawable_h/wh:0;
      bool preview_hit=focused && px>=preview_rect.x && px<preview_rect.x+preview_rect.w &&
          py>=preview_rect.y && py<preview_rect.y+preview_rect.h;
      s_preview_cursor_x=px;s_preview_cursor_y=py;
      if(!preview_live)s_preview_expanded=false;
      bool preview_left=mouse_raw_left;
      if(preview_live && preview_left && !s_preview_left_down && (preview_hit || s_preview_expanded)) {
        s_preview_expanded=!s_preview_expanded;s_preview_click_owned=true;
        if(s_preview_expanded)s_preview_complete=true;
        if(getenv("SC_MAP_PREVIEW_DIAG"))fprintf(stderr,"[map preview] expanded %d\n",s_preview_expanded);
      }
      s_preview_left_down=preview_left;
      s_preview_input_blocked=preview_live && (s_preview_expanded || s_preview_click_owned);
      if(s_preview_click_owned) {
        if(!preview_left)s_preview_click_owned=false;
        mouse_raw_left=false;mouse_buttons&=~SDL_BUTTON(SDL_BUTTON_LEFT);
      }
      if(preview_live && s_preview_expanded && focused) {
        inside=true;
        mouse_target_x=(px-s_destination.x)*pointer_view.width/s_destination.w-pointer_view.core_x;
        mouse_target_y=(py-s_destination.y)*pointer_view.height/s_destination.h-pointer_view.core_y;
      }
      if(focused && s_rom_is_us && host_map_screen_live() && ram_w(0x1d7) &&
          !ram_w(0xd7) && !ram_w(0x379) && s_mouse_dialog==SC_MOUSE_DIALOG_NONE &&
          !s_custom_renderer.advisor_frame && !s_custom_renderer.map_hold) {
        int gx,gy;
        if(ScVideoWindowToGuest(pointer_view,s_destination,ww,wh,drawable_w,drawable_h,mx,my,&gx,&gy)) {
          for(unsigned b=0;b<2;++b) {
            ScVideoRect rect=ScRendererClipboardButton(pointer_view,b);
            int x=gx+pointer_view.core_x,y=gy+pointer_view.core_y;
            if(x>=rect.x && x<rect.x+rect.w && y>=rect.y && y<rect.y+rect.h) {
              mouse_clip_hit=true;mouse_clip_button=b;
            }
          }
        }
      }
      mouse_city_hit=inside && !mouse_navigation_hit && s_mouse_dialog==SC_MOUSE_DIALOG_NONE && s_custom_video.enabled &&
          ScRendererCityPoint(&s_custom_renderer,g_ram,mouse_target_x,mouse_target_y,
              &mouse_city_x,&mouse_city_y);
      static bool last_inside;
      static int last_x, last_y;
      const bool right = focused && (mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT));
      const bool middle = focused && (mouse_buttons & SDL_BUTTON(SDL_BUTTON_MIDDLE));
      const bool middle_was_active=s_middle_pan.active;
      const bool city_pan_allowed=focused && s_custom_video.enabled && !mouse_raw_left && !s_build_active && !s_build_pending &&
          !s_clip_drag && !s_clip_pending && host_map_screen_live() && !ram_w(0x379) &&
          s_mouse_dialog==SC_MOUSE_DIALOG_NONE && !s_custom_renderer.advisor_frame &&
          (s_middle_pan.active || (!ram_w(0xd7) && !g_ram[0x391] && !g_ram[0xe3] && !s_custom_renderer.map_hold));
      const bool preview_pan_allowed=preview_live && focused && !mouse_raw_left;
      const bool pan_allowed=city_pan_allowed || preview_pan_allowed;
      const bool pan_land=inside && !mouse_navigation_hit && !mouse_clip_hit &&
          (s_custom_video.enabled?mouse_city_hit:(!ram_w(0x1d7) || (mouse_target_x>=56 && mouse_target_y>=48)));
      const bool pan_held=middle || (right && !s_clip_tool);
      double sensitivity=(double)s_mouse_sensitivity/100*s_pan_max_tiles*scroll_key_multiplier(keys);
      double zoom=s_custom_renderer.map_zoom>0?s_custom_renderer.map_zoom:1;
      double pan_sx=preview_pan_allowed?(double)drawable_w/ww/preview_rect.w:
          ww>0 && s_destination.w>0?(double)drawable_w/ww*pointer_view.width/s_destination.w/zoom*sensitivity:0;
      double pan_sy=preview_pan_allowed?(double)drawable_h/wh/preview_rect.h:
          wh>0 && s_destination.h>0?(double)drawable_h/wh*pointer_view.height/s_destination.h/zoom*sensitivity:0;
      ScMousePanDelta pan_delta=ScMousePanUpdate(&s_middle_pan,pan_allowed,preview_pan_allowed?middle:pan_held,
          preview_pan_allowed?preview_hit:pan_land,mx,my,pan_sx,pan_sy);
      if(s_middle_pan.active && preview_pan_allowed) {
        sc_mapgen_preview_pan(&s_custom_renderer.map_preview,pan_delta.x,pan_delta.y);
        if(getenv("SC_MAP_PREVIEW_DIAG") && (pan_delta.x || pan_delta.y))
          fprintf(stderr,"[map preview] pan %.4f,%.4f center %.1f,%.1f\n",pan_delta.x,pan_delta.y,
              s_custom_renderer.map_preview.center_x,s_custom_renderer.map_preview.center_y);
      } else if(s_middle_pan.active) ScRendererPan(&s_custom_renderer,pan_delta.x,pan_delta.y);
      if(getenv("SC_MOUSE_PAN_DIAG") && (middle_was_active || s_middle_pan.active))
        fprintf(stderr,"[host pan] frame %llu active %d delta %.4f,%.4f camera %.4f,%.4f zoom %.6f native %d,%d mode %u\n",
            (unsigned long long)s_frames,(int)s_middle_pan.active,pan_delta.x,pan_delta.y,
            s_custom_renderer.scroll_x+s_custom_renderer.scroll_adjust_x+s_custom_renderer.camera_x-lround(s_custom_renderer.camera_x),
            s_custom_renderer.scroll_y+s_custom_renderer.scroll_adjust_y+s_custom_renderer.camera_y-lround(s_custom_renderer.camera_y),zoom,
            (int16_t)ram_w(0x1bd)*8,(int16_t)ram_w(0x1bf)*8,ram_w(0xd7));
      if(!middle_was_active && s_middle_pan.active) SDL_CaptureMouse(true);
      if(s_middle_pan.active) {
        s_mouse_dir_frames=0;
      } else {
        if (inside) {
          int dx = last_inside ? mouse_target_x - last_x : 0;
          int dy = last_inside ? mouse_target_y - last_y : 0;
          mouse_pointer_moved=dx || dy || !last_inside || middle_was_active;
          /* Write the full absolute position, never clamp the travel delta:
           * a jump across the viewport must land correctly in one frame. */
          /* The ROM pointer remains byte sized. Host construction uses the
           * full world coordinate, never this safe native cursor proxy. */
          g_ram[0x01eb] = (uint8_t)(mouse_city_hit ? 128 : mouse_target_x<0?0:mouse_target_x>255?255:mouse_target_x);
          g_ram[0x01ed] = (uint8_t)(mouse_city_hit ? 128 : mouse_target_y<0?0:mouse_target_y>223?223:mouse_target_y);
          if (mouse_city_hit && !right && ram_w(0x020d)<=15) {
            s_custom_renderer.pointer_active=true;
            s_custom_renderer.pointer_x=mouse_target_x;
            s_custom_renderer.pointer_y=mouse_target_y;
          } else if(s_custom_video.enabled && s_custom_renderer.city_input && !mouse_navigation_hit &&
              !right && s_mouse_dialog==SC_MOUSE_DIALOG_NONE && ram_w(0x1d7) &&
              !ram_w(0xd7) && !ram_w(0x379) && !g_ram[0x391] && !g_ram[0xe3]) {
            int gx,gy;
            ScVideoWindowToGuest(pointer_view,s_destination,ww,wh,drawable_w,drawable_h,mx,my,&gx,&gy);
            if((gy>=0 && gy<46) || (gx>=0 && gx<56 && gy>=46 && gy<224)) {
              s_custom_renderer.pointer_active=true;s_custom_renderer.pointer_hud=true;
              s_custom_renderer.pointer_x=gx;s_custom_renderer.pointer_y=gy;
            }
          }
          mouse_target_valid = true;
          s_mouse_dir = 0;
          if (dx < 0) s_mouse_dir |= kPad_Left;
          if (dx > 0) s_mouse_dir |= kPad_Right;
          if (dy < 0) s_mouse_dir |= kPad_Up;
          if (dy > 0) s_mouse_dir |= kPad_Down;
          s_mouse_dir_frames = s_mouse_dir ? 1 : 0;
          if (s_rom_is_us && !s_preview_input_blocked) {
            ScMouseUiResult ui = mouse_ui_point(g_ram, mouse_target_x, mouse_target_y,
                false, s_ninth_scenario);
            mouse_ui_select = dx || dy || (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) ||
                (ScMouseUiBudgetLive(g_ram) && !last_inside);
            mouse_ui_handled = ui.handled; mouse_ui_hit = ui.hit;
            if (mouse_navigation_hit) { mouse_ui_handled=true; mouse_ui_hit=false; }
            if (mouse_clip_hit) {mouse_ui_handled=true;mouse_ui_hit=false;}
            if (ui.handled) s_mouse_dir_frames = 0;
          }
          static unsigned edge_clock;
          const bool buttons_up = !(mouse_buttons &
              (SDL_BUTTON(SDL_BUTTON_LEFT) | SDL_BUTTON(SDL_BUTTON_RIGHT) | SDL_BUTTON(SDL_BUTTON_MIDDLE)));
          int edge = mouse_target_x < 8 ? -1 : mouse_target_x >= 248 ? 1 : 0;
          if (buttons_up && edge && g_ram[0x14] == 11 && s_rom_is_us) {
            if (edge_clock++ % 30 == 0)
              ScMouseUiScenarioScroll(g_ram, edge, s_ninth_scenario);
          } else {
            edge_clock = 0;
          }
          if (buttons_up && !middle_was_active && !keys[pan_key] && host_map_screen_live()) {
            /* The visible HUD occupies the top and left edges. */
            const bool hud_hidden = !(g_ram[0x01d7] | g_ram[0x01d8]);
            int canvas_x=mouse_target_x+pointer_view.core_x;
            int canvas_y=mouse_target_y+pointer_view.core_y;
            if (canvas_x >= pointer_view.width-8) mouse_edge_input |= kPad_Right;
            if (canvas_y >= pointer_view.height-8) mouse_edge_input |= kPad_Down;
            if ((hud_hidden || pointer_view.core_x>0) && canvas_x < 8) mouse_edge_input |= kPad_Left;
            if ((hud_hidden || pointer_view.core_y>0) && canvas_y < 8) mouse_edge_input |= kPad_Up;
            if(mouse_edge_input && s_custom_video.enabled) {
              if(s_custom_renderer.city_input && !s_custom_renderer.map_hold) {
                double zoom=s_custom_renderer.map_zoom>0?s_custom_renderer.map_zoom:1;
                double speed=2/zoom*scroll_key_multiplier(keys);
                int ex=((mouse_edge_input&kPad_Right)!=0)-((mouse_edge_input&kPad_Left)!=0);
                int ey=((mouse_edge_input&kPad_Down)!=0)-((mouse_edge_input&kPad_Up)!=0);
                ScRendererPan(&s_custom_renderer,ex*speed,ey*speed);
                if(getenv("SC_MOUSE_PAN_DIAG"))fprintf(stderr,
                    "[host edge] frame %llu camera %d,%d zoom %.6f native %d,%d mode %u\n",
                    (unsigned long long)s_frames,s_custom_renderer.scroll_x,s_custom_renderer.scroll_y,zoom,
                    (int16_t)ram_w(0x1bd)*8,(int16_t)ram_w(0x1bf)*8,ram_w(0xd7));
              }
              mouse_edge_input=0;
            } else if(mouse_edge_input)mouse_edge_input|=kPad_A;
          }
        } else {
          /* Letterboxing is outside the rendered input surface. */
          if (!s_build_active && !s_clip_drag) mouse_buttons = 0;
          s_mouse_dir_frames = 0;
        }
      }
      last_inside = inside; last_x = mouse_target_x; last_y = mouse_target_y;
      if (!focused) mouse_buttons=0;
      mouse_pan_active=focused && s_middle_pan.active;
      if(mouse_pan_active && !mouse_pan_grab_requested) {
        mouse_pan_virtual_x=mouse_pan_restore_x=mx;
        mouse_pan_virtual_y=mouse_pan_restore_y=my;
      }
    } else {
      s_middle_pan=(ScMousePan){0};
    }
    if(mouse_pan_relative_requested!=s_middle_pan.active) {
      bool accepted=sc_window_relative_mouse(window,s_middle_pan.active);
      mouse_pan_relative_requested=accepted && s_middle_pan.active;
      /* A mode transition/warp is not a drag movement. Discard stale deltas
       * before the first real relative event; warp only after disabling. */
      double discard_x,discard_y;sc_relative_mouse_delta(&discard_x,&discard_y);
      if(!s_middle_pan.active && (SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS))
        SDL_WarpMouseInWindow(window,(int)mouse_pan_restore_x,(int)mouse_pan_restore_y);
      if(!accepted) fprintf(stderr,"mouse drag relative mode: %s\n",SDL_GetError());
      if(getenv("SC_MOUSE_PAN_DIAG"))
        fprintf(stderr,"[mouse drag relative] frame %llu enabled %d accepted %d\n",
            (unsigned long long)s_frames,(int)s_middle_pan.active,(int)accepted);
    }
    s_custom_renderer.pointer_hidden=s_middle_pan.active;
    s_custom_renderer.mouse_panning=s_middle_pan.active;
    /* Only panning confines the pointer. Releasing, opening F12, disabling
     * mouse input or losing focus must always restore normal desktop motion. */
    if(mouse_pan_grab_requested!=mouse_pan_active) {
      bool accepted=sc_window_mouse_grab(window,mouse_pan_active);
      mouse_pan_grab_requested=mouse_pan_active;
      if(!accepted) fprintf(stderr,"mouse pan grab: %s\n",SDL_GetError());
      if(getenv("SC_MOUSE_PAN_DIAG"))
        fprintf(stderr,"[mouse pan grab] frame %llu requested %d actual %d accepted %d\n",
            (unsigned long long)s_frames,(int)mouse_pan_active,
            (int)sc_window_mouse_grabbed(window),(int)accepted);
    }
    if (s_fast_cursor_enabled) {
      /* Host-driven, independent of the ROM's own cadence -- see
       * s_fast_cursor_enabled's comment above. Diagonal holds add both
       * axes, same as the ROM's own D-pad would. */
      int fdx = 0, fdy = 0;
      if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_H]) fdx -= s_fast_cursor_step;
      if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_K]) fdx += s_fast_cursor_step;
      if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_U]) fdy -= s_fast_cursor_step;
      if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_J]) fdy += s_fast_cursor_step;
      if (fdx || fdy) apply_mouse_delta(fdx, fdy);
    }
    uint16_t input = 0;
    /* The pad through the launcher's keybinds.ini when this build has the
     * launcher (src/sc_launcher.c); its first run writes the layout below, so
     * nothing moves for a player who never opens the Controller page. */
    static int s_keybinds = -1;
    if (s_keybinds < 0) s_keybinds = ScKeybindsInit() ? 1 : 0;
    if (s_keybinds) {
      input = (uint16_t)ScKeybindsRead((const unsigned char *)keys);
      /* U/H/J/K stay a spare D-pad for testing, unless a binding took them. */
      if (keys[SDL_SCANCODE_U] && !ScKeybindsUses(SDL_SCANCODE_U)) input |= kPad_Up;
      if (keys[SDL_SCANCODE_J] && !ScKeybindsUses(SDL_SCANCODE_J)) input |= kPad_Down;
      if (keys[SDL_SCANCODE_H] && !ScKeybindsUses(SDL_SCANCODE_H)) input |= kPad_Left;
      if (keys[SDL_SCANCODE_K] && !ScKeybindsUses(SDL_SCANCODE_K)) input |= kPad_Right;
    } else {
    /* Diamond cluster U/H/J/K as an alternate D-pad, alongside arrow keys,
     * for testing (U=up, H=left, J=down, K=right). */
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_U]) input |= kPad_Up;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_J]) input |= kPad_Down;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_H]) input |= kPad_Left;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_K]) input |= kPad_Right;
    /* Letter bindings are resolved by *keycode*, not scancode, so they follow
     * the labels on the keyboard rather than QWERTY positions.
     *
     * This used to accept SDL_SCANCODE_Z and SDL_SCANCODE_Y together, which
     * papered over the QWERTZ/QWERTY swap only because both fed the same
     * button. They are separate buttons now (Y = SNES Y, X = SNES B), so the
     * positional approach would put them on the wrong buttons on a German
     * layout -- there the key labelled Y sits where QWERTY has Z.
     * SDL_GetScancodeFromKey maps "the key that types this character" to its
     * scancode under the active layout, which is what keys[] is indexed by.
     * Resolved once: the layout can change at runtime, but re-querying every
     * frame for every button buys nothing here. */
    static SDL_Scancode sc_l, sc_r, sc_x, sc_a, sc_y, sc_b, sc_select;
    static bool binds_ready = false;
    if (!binds_ready) {
      sc_l      = sc_scancode_from_key(SDLK_q);   /* Q -> L      */
      sc_r      = sc_scancode_from_key(SDLK_w);   /* W -> R      */
      sc_x      = sc_scancode_from_key(SDLK_a);   /* A -> X      */
      sc_a      = sc_scancode_from_key(SDLK_s);   /* S -> A      */
      sc_y      = sc_scancode_from_key(SDLK_y);   /* Y -> Y      */
      sc_b      = sc_scancode_from_key(SDLK_x);   /* X -> B      */
      sc_select = sc_scancode_from_key(SDLK_b);   /* B -> Select */
      binds_ready = true;
    }
    if (keys[sc_l]) input |= kPad_L;
    if (keys[sc_r]) input |= kPad_R;
    if (keys[sc_x]) input |= kPad_X;
    if (keys[sc_a]) input |= kPad_A;
    if (keys[sc_y]) input |= kPad_Y;
    if (keys[sc_b]) input |= kPad_B;
    if (keys[SDL_SCANCODE_RETURN]) input |= kPad_Start;
    /* Select is bound to B (not Shift) so Shift is free for the
     * save-state slot hotkeys (Shift+1..Shift+0) without also feeding a
     * Select press into the game every time a state is saved/loaded. */
    if (keys[sc_select]) input |= kPad_Select;
    }   /* fixed bindings */
    unsigned keyboard_pan_dirs=(keys[SDL_SCANCODE_RIGHT]?kPad_Right:0) |
        (keys[SDL_SCANCODE_LEFT]?kPad_Left:0) | (keys[SDL_SCANCODE_DOWN]?kPad_Down:0) |
        (keys[SDL_SCANCODE_UP]?kPad_Up:0);
    bool pan_was_latched=s_keyboard_pan_latched;
    bool keyboard_pan_allowed=s_rom_is_us && s_custom_video.enabled &&
        s_custom_renderer.city_input && !s_custom_renderer.advisor_frame &&
        !s_custom_renderer.map_hold && !s_city_present_pending && !s_menu_open &&
        !ram_w(0xd7) && !ram_w(0x379) && !g_ram[0x391] && !g_ram[0xe3] &&
        !s_middle_pan.active && !s_build_active && !s_build_pending &&
        !s_clip_drag && !s_clip_pending && !mouse_raw_left;
    s_keyboard_pan_latched=keyboard_pan_allowed && keys[pan_key] &&
        (s_keyboard_pan_latched || keyboard_pan_dirs);
    if(s_keyboard_pan_latched) {
      /* Consume the physical shortcut's bindings only. Keep standalone X
       * and controller buttons unchanged. Holding X after releasing arrows
       * remains a pan hold, so it cannot accidentally place a building. */
      uint8_t other_keys[512];memcpy(other_keys,keys,sizeof other_keys);
      other_keys[pan_key]=other_keys[SDL_SCANCODE_UP]=other_keys[SDL_SCANCODE_DOWN]=
          other_keys[SDL_SCANCODE_LEFT]=other_keys[SDL_SCANCODE_RIGHT]=0;
      uint16_t consumed=s_keybinds?(uint16_t)(ScKeybindsRead((const unsigned char *)keys) &
          ~ScKeybindsRead(other_keys)):(kPad_B|kPad_Up|kPad_Down|kPad_Left|kPad_Right);
      input&=~consumed;
      if(!pan_was_latched) {
        ram_set_w(0x1f5,0);ram_set_w(0x1ff,0);ram_set_w(0x1c1,0);ram_set_w(0x1f7,0);
      }
    }
    /* Mouse buttons: LEFT = SNES B, RIGHT = SNES A.
     *
     * Lets host-mouse cursor control (F3) actually select and interact, not
     * just move the cursor. Not gated on s_mouse_enabled: useful as a plain
     * extra binding regardless -- one hand on the mouse for pointing, click to
     * act, without reaching for the keyboard.
     *
     * Binding history, since it has moved twice on request: B originally, then
     * X after play-testing, now B for left with A added on right.
     *
     * Note these are the SERIAL-order pad bits (kPad_B = $0001, kPad_A =
     * $0100), not the $4218/$4219 hardware layout -- see
     * docs/HANDOVER_metal_marines.md #1. */
    ScMouseUiPointerUpdate(&s_ui_mouse_pointer,mouse_target_valid && !s_menu_open,
        mouse_pointer_moved,mouse_raw_left,input!=0,mouse_target_x,mouse_target_y);
    s_custom_renderer.menu_pointer_active=s_ui_mouse_pointer.active;
    s_custom_renderer.menu_pointer_x=s_ui_mouse_pointer.x;
    s_custom_renderer.menu_pointer_y=s_ui_mouse_pointer.y;
    s_custom_renderer.map_number=g_ram[0xb27]+g_ram[0xb28]*10+g_ram[0xb29]*100+s_map_number_high*1000;
    if(s_custom_renderer.map_preview.active && !ram_w(0xb31) &&
       (ram_w(0x14)==5 || ram_w(0x14)==6)) {
      uint64_t now=SDL_GetPerformanceCounter();
      if(!s_preview_started)s_preview_started=now;
      unsigned frame=(now-s_preview_started)*60/SDL_GetPerformanceFrequency();
      s_custom_renderer.map_preview.frame=s_preview_complete || frame>90?90:frame;
    }
    /* Budget mouse input takes control only on motion/press. A stationary
     * pointer must not pull a keyboard/gamepad selection back to its last
     * desktop position after the guest has jumped to another arrow. */
    if(s_rom_is_us && ScMouseUiBudgetLive(g_ram) && !mouse_ui_select) {
      g_ram[0x01eb]=cursor_before_mouse_x;g_ram[0x01ed]=cursor_before_mouse_y;
      mouse_target_valid=false;
    }
    /* A keyboard direction owns the guest cursor while held. Do not reset
     * its movement to the desktop pointer at the start of the next frame. */
    if ((input & (kPad_Left | kPad_Right | kPad_Up | kPad_Down)) || s_keyboard_pan_latched) {
      g_ram[0x01eb] = cursor_before_mouse_x;
      g_ram[0x01ed] = cursor_before_mouse_y;
      mouse_target_valid = false;
      s_custom_renderer.pointer_active = false;
      s_custom_renderer.pointer_hud = false;
      s_mouse_dir_frames = 0;
      mouse_edge_input = 0;
    } else if (mouse_ui_handled && mouse_ui_select && !s_preview_input_blocked) {
      mouse_ui_point(g_ram, mouse_target_x, mouse_target_y, true, s_ninth_scenario);
      if (g_ram[0x14] == 5 && g_ram[0x0b2d] == 1 && g_ram[0x0b31])
        s_map_mouse_refresh_pending = true;
      if (g_ram[0x14] == 5 && g_ram[0x0b2d] == 1 && g_ram[0x0b31] &&
          (mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)))
        s_map_mouse_accept_pending = true;
    }
    {
      static bool map_mouse_was_down,number_mouse_editing;
      bool map=s_rom_is_us && ram_w(0x14)==5;
      bool arrows=map && mouse_target_valid &&
          ScMouseUiMapNumberArrows(mouse_target_x,mouse_target_y);
      if(arrows && mouse_raw_left)number_mouse_editing=true;
      /* Keep editing across releases and between digit pairs. Generate once
       * the pointer leaves their shared boundary, or OK is explicitly used. */
      if(number_mouse_editing && !arrows) {
        if(map && ram_w(0xb31))s_map_mouse_refresh_pending=true;
        number_mouse_editing=false;
      }
      if(map && ram_w(0xb31) && ram_w(0xb2d)==0 &&
          map_mouse_was_down && !mouse_raw_left)s_map_mouse_refresh_pending=true;
      map_mouse_was_down=map && mouse_raw_left;
      if(!map)number_mouse_editing=false;
    }
    if (g_ram[0x14] != 5) {
      s_map_mouse_accept_pending = false; s_map_mouse_refresh_pending = false;
    } else if(g_ram[0xb2d]!=1) {
      s_map_mouse_accept_pending=false;
    }
    {
      static bool previous_left;
      const bool right = mouse_buttons & SDL_BUTTON(SDL_BUTTON_RIGHT);
      const bool hud_hidden = !ram_w(0x01d7);
      const bool on_land = s_custom_video.enabled ? mouse_city_hit :
          hud_hidden || (mouse_target_x >= 56 && mouse_target_y >= 48);
      const bool supported = s_mouse_enabled && s_rom_is_us && !sc_fiber_active() &&
          s_mouse_dialog==SC_MOUSE_DIALOG_NONE &&
          mouse_target_valid && !mouse_navigation_hit && host_map_screen_live() && !ram_w(0xd7) && on_land &&
          ram_w(0x020d) <= 15 && !s_menu_open && !scripted_input;
      if (!host_map_screen_live()) {
        s_build_pending=false;s_clip_tool=s_clip_pending=0;s_clip_drag=false;
      }
      bool pressed=mouse_raw_left && !previous_left;
      if(pressed && mouse_clip_hit && !s_build_active && !s_build_pending && !s_clip_pending) {
        unsigned tool=(unsigned)mouse_clip_button+1;
        if(tool==1 || s_clipboard.count) {
          s_clip_tool=s_clip_tool==tool?0:tool;
          s_clip_native_tool=ram_w(0x20d);
        } else g_ram[5]=2;
        mouse_clip_consumed=true;
      }
      /* Selecting an ordinary palette tool leaves the clipboard tool. */
      if(s_clip_tool && (ram_w(0x20d)!=s_clip_native_tool ||
          (pressed && mouse_target_valid && mouse_target_x>=8 && mouse_target_x<56 &&
           mouse_target_y>=46 && mouse_target_y<176))) {
        s_clip_tool=s_clip_pending=0;s_clip_drag=false;
      }
      if(right && (s_clip_tool || s_clip_drag)) {
        s_clip_tool=s_clip_pending=0;s_clip_drag=false;mouse_clip_consumed=true;
      }
      if(s_clip_tool && supported && !mouse_clip_hit && !s_clip_pending) {
        s_clip_hover=true;
        s_clip_x1=s_custom_video.enabled?mouse_city_x:(int16_t)ram_w(0x1bd)+mouse_target_x/8;
        s_clip_y1=s_custom_video.enabled?mouse_city_y:(int16_t)ram_w(0x1bf)+mouse_target_y/8;
        if(pressed && !s_clip_pending && !right) {
          s_clip_native_tool=ram_w(0x20d);
          if(s_clip_tool==1) {
            s_clip_drag=true;s_clip_x0=s_clip_x1;s_clip_y0=s_clip_y1;
            s_clip_scroll_x=(int16_t)ram_w(0x1bd);s_clip_scroll_y=(int16_t)ram_w(0x1bf);
            SDL_CaptureMouse(true);
          } else s_clip_pending=2;
        }
      }
      if(s_clip_drag) {
        if(ram_w(0xd7) || ram_w(0x379) || s_menu_open ||
            (int16_t)ram_w(0x1bd)!=s_clip_scroll_x || (int16_t)ram_w(0x1bf)!=s_clip_scroll_y) {
          s_clip_drag=false;s_build_cancelled=true;
        } else if(!mouse_raw_left && previous_left) {
          s_clip_drag=false;s_clip_pending=1;
        }
      }
      if(s_clip_tool || s_clip_pending || mouse_clip_hit || mouse_clip_consumed) {
        mouse_edge_input=0;s_mouse_dir_frames=0;
        if(on_land || s_clip_pending || mouse_clip_hit) mouse_clip_consumed=true;
      }
      s_custom_renderer.clipboard_cursor=s_clip_tool!=0;
      if (pressed && supported && !s_clip_tool && !s_clip_pending && !mouse_clip_consumed &&
          !s_build_pending && !right) {
        s_build_tool = ram_w(0x020d);
        s_build_scroll_x = (int16_t)ram_w(0x01bd); s_build_scroll_y = (int16_t)ram_w(0x01bf);
        s_build_x0 = s_custom_video.enabled ? mouse_city_x : s_build_scroll_x + mouse_target_x / 8;
        s_build_y0 = s_custom_video.enabled ? mouse_city_y : s_build_scroll_y + mouse_target_y / 8;
        s_build_plan.count=0;
        s_build_active = true; s_build_cancelled = false;
        SDL_CaptureMouse(true);
      }
      if (s_build_active) {
        if (right || !host_map_screen_live() || ram_w(0x379) || s_menu_open ||
            ram_w(0x020d) != s_build_tool ||
            (int16_t)ram_w(0x01bd) != s_build_scroll_x || (int16_t)ram_w(0x01bf) != s_build_scroll_y) {
          s_build_active = false; s_build_cancelled = true;
        } else {
          if (supported) {
            int x=s_custom_video.enabled?mouse_city_x:s_build_scroll_x+mouse_target_x/8;
            int y=s_custom_video.enabled?mouse_city_y:s_build_scroll_y+mouse_target_y/8;
            if(!s_build_plan.count || x!=s_build_x1 || y!=s_build_y1) {
              s_build_x1=x;s_build_y1=y;
              ScConstructionPlanWorld(&s_build_plan,&s_world,s_build_tool,s_build_x0,s_build_y0,x,y);
            }
          }
          /* Leaving the canvas/HUD retains the last valid plan. Captured
           * release commits it; re-entry continues the same gesture. */
          if (!mouse_raw_left && previous_left && s_build_plan.count) {
            s_build_active = false; s_build_pending = true;
          }
        }
      }
      if (s_build_active || s_build_cancelled || s_build_pending) {
        mouse_edge_input=0;s_mouse_dir_frames=0;
      }
      if (s_custom_video.enabled && (s_build_active || s_build_pending)) {
        s_custom_renderer.pointer_active=true;
        s_custom_renderer.pointer_hud=false;
        s_custom_renderer.pointer_x=s_build_x1*8-s_custom_renderer.scroll_x-s_custom_renderer.scroll_adjust_x;
        s_custom_renderer.pointer_y=s_build_y1*8-s_custom_renderer.scroll_y-s_custom_renderer.scroll_adjust_y;
      }
      if (!mouse_raw_left) s_build_cancelled = false;
      previous_left = mouse_raw_left;
      if (!s_build_active && !s_clip_drag && !s_middle_pan.active) SDL_CaptureMouse(false);
    }
    { const uint32_t mb = s_mouse_enabled ? mouse_buttons : SDL_GetMouseState(NULL, NULL);
      /* On a MENU, feed the synthesised d-pad without waiting for a button.
       *
       * apply_mouse_delta() pokes $01eb/$01ed, and that is what moves the
       * cursor in the city view. On the menu pages it does nothing: they
       * track their own selection, and moving the pointer left the last
       * d-pad choice selected. Reported from play three ways -- the mouse
       * not activating buttons in the tax menu, not working on normal menus,
       * and working only while a button is held. The last one is the tell:
       * holding LEFT is what lets the synthesised direction through here,
       * and the direction is the only thing a menu reacts to.
       *
       * Not fed unconditionally, because in the city view the poke ALREADY
       * moves the cursor -- adding the d-pad there would move it twice per
       * frame. host_map_screen_live() is the existing discriminator: it is
       * false for exactly the pages that report $14 == 0 without BG2, which
       * are the menu pages this is for. */
      const bool mouse_on_menu = !host_map_screen_live();
      if (s_mouse_enabled && s_mouse_dir_frames > 0 && mouse_on_menu) {
        input |= s_mouse_dir;
        s_mouse_dir_frames--;
      } else if (s_mouse_dir_frames > 0) {
        s_mouse_dir_frames--;
      }
      if ((mb & SDL_BUTTON(SDL_BUTTON_LEFT)) &&
          (!mouse_ui_handled || mouse_ui_hit) &&
          !(s_custom_video.enabled && mouse_city_hit && ram_w(0x020d)>15) &&
          !s_build_active && !s_build_pending && !s_build_cancelled && !mouse_clip_consumed) input |= kPad_B;
      /* Right button drives the map pan directly (see the pan block in the
       * mouse handler) rather than feeding A, so it does not also trigger the
       * ROM's own hold-A scroll and double up. */
      if ((mb & SDL_BUTTON(SDL_BUTTON_RIGHT)) && !s_middle_pan.active && !s_build_cancelled && !mouse_clip_consumed) {
        input |= kPad_A;                      /* the game's own pan modifier */
      } }
    input |= mouse_edge_input;
    /* Don't feed the keyboard to the game while the settings menu is open:
     * the menu navigates with Up/Down/Left/Right/Enter, which are also the
     * SNES D-pad and Start bindings. The game is frozen so nothing acts on
     * them immediately, but whatever is held on the frame the menu closes
     * would otherwise leak straight through as a real button press. */
    if (s_menu_open || scripted_input) input = 0;
    if(map_selection_preview_live() && s_preview_input_blocked)input=0;
    if(s_escape_back_frames) {input|=s_escape_back_input;--s_escape_back_frames;}
    apply_frame_input(s_frames);
    apply_freezes();
    input|=g_snes->input1_currentState;
    g_snes->input1_currentState=s_rom_is_us?ScMouseUiModalInput(g_ram,input,false):input;

    /* Held Tab runs up to six guest frames within the display's time budget.
     * Draw the last expanded image and play its audio; retain full guest
     * input, PPU, APU and calendar work in the intermediate frames. */
    /* DRAG TURBO: run extra guest frames while a mouse button is held.
     *
     * The cursor and the map scroll are not slow because their routines are
     * slow -- they are STARVED. Traced live: bank $03, the city simulation,
     * holds the CPU for ~4 consecutive frames at a time, and the bank-1 cursor
     * dispatcher does not run at all during those, so input steps only on the
     * bank-1 frames. A 4-on/4-off duty cycle. That is authentic behaviour, not
     * a recomp defect, and it is why the cartridge shipped with SNES Mouse
     * support.
     *
     * Nothing host-side can make the dispatcher run during a frame the ROM
     * spends elsewhere. What the host CAN do is give it more frames: running
     * N guest frames per host frame while dragging multiplies the number of
     * turns it gets, so bulldozing and panning proceed N times faster.
     *
     * The honest cost: the SIMULATION also advances N times faster while the
     * button is held. For a drag lasting a second or two that is a fraction of
     * a game-month, but it is not free, so it is off by default. */
    const bool dragging = s_drag_turbo > 1 &&
      (SDL_GetMouseState(NULL, NULL) &
       (SDL_BUTTON(SDL_BUTTON_LEFT) | SDL_BUTTON(SDL_BUTTON_RIGHT))) != 0;
    /* No automatic boost of any kind. The map-generation and decompressor
     * turbos both existed to collapse waits; the decompiled generator removes
     * the one that mattered, and the other was advancing the simulation during
     * ordinary play. Only explicit held gestures remain. */
    bool fast_forward = keys[SDL_SCANCODE_TAB] || test_fast_forward;
    bool shift_fast_forward=fast_forward && (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]);
    s_wait_lane_enabled=fast_forward;
    /* Ctrl accelerates scrolling 3x; Ctrl+Shift uses 10x. */
    s_scroll_multiplier=host_map_screen_live() && !s_menu_open?(int)scroll_key_multiplier(keys):1;
    /* Deterministic equivalent of Ctrl for mouse/keyboard scroll replays. */
    {const char *e=getenv("SC_SCROLL_MULTIPLIER");if(e && !s_menu_open) s_scroll_multiplier=atoi(e)==10?10:atoi(e)==3?3:1;}
    if(s_keyboard_pan_latched) {
      double zoom=s_custom_renderer.map_zoom>0?s_custom_renderer.map_zoom:1;
      double speed=2/zoom*s_scroll_multiplier;
      int dx=((keyboard_pan_dirs&kPad_Right)!=0)-((keyboard_pan_dirs&kPad_Left)!=0);
      int dy=((keyboard_pan_dirs&kPad_Down)!=0)-((keyboard_pan_dirs&kPad_Up)!=0);
      ScRendererPan(&s_custom_renderer,dx*speed,dy*speed);
      if(getenv("SC_MOUSE_PAN_DIAG"))fprintf(stderr,
          "[keyboard pan] frame %llu camera %d,%d zoom %.6f native %d,%d directions %u\n",
          (unsigned long long)s_frames,s_custom_renderer.scroll_x,s_custom_renderer.scroll_y,zoom,
          (int16_t)ram_w(0x1bd)*8,(int16_t)ram_w(0x1bf)*8,keyboard_pan_dirs);
    }
    ScVehicles_FullView(s_custom_renderer.camera_x || s_custom_renderer.camera_y);
    s_measure_custom_frame=fast_forward || perf_on;
    if(s_gpu_terrain_enabled && s_custom_video.enabled && !gpu_terrain && !gpu_failed) {
      gpu_terrain=ScGpuTerrainCreate(renderer,s_linear_filter);
      const char *field_reference=getenv("SC_GPU_FIELDS_REFERENCE");
      if(gpu_terrain && (!field_reference || *field_reference!='1')) {
        gpu_fields=ScGpuFieldsCreate(renderer);
        if(gpu_fields) {
          ScStencilBackend backend={gpu_fields,ScGpuFieldsBegin,ScGpuFieldsData,
              ScGpuFieldsCrimeBegin,ScGpuFieldsCrimeData,ScGpuFieldsLandBegin,ScGpuFieldsLandData};
          /* Land readback is correct but has not improved production timings.
           * Keep this job opt-in while retaining the established GPU fields. */
          const char *gpu_land=getenv("SC_GPU_LAND");
          if(!gpu_land || *gpu_land!='1') {backend.land_begin=NULL;backend.land_data=NULL;}
          ScWorldGuestSetStencilBackend(&backend);
        }
      }
      if(!gpu_terrain) {
        fprintf(stderr,"[gpu terrain] unavailable; retaining CPU renderer\n");
        s_gpu_terrain_enabled=false;gpu_failed=true;
      }
    }
    if(gpu_failed) s_gpu_terrain_enabled=false;
    if(!ScRendererDeferTerrain(&s_custom_renderer,s_gpu_terrain_enabled && s_custom_video.enabled && gpu_terrain!=NULL)) {
      fprintf(stderr,"[gpu terrain] capture allocation failed; retaining CPU renderer\n");
      s_gpu_terrain_enabled=false;ScRendererDeferTerrain(&s_custom_renderer,false);
    }
    int frames_this_iter = fast_forward ? (shift_fast_forward?24:6) : (dragging ? s_drag_turbo : 1);
    /* Fixed batches are an oracle for comparing identical guest frames with
     * and without skipped pixels, independently of adaptive wall timings. */
    static int test_tab_batch=-1;
    if(test_tab_batch<0) {const char *e=getenv("SC_TAB_TEST_BATCH");
      test_tab_batch=e?atoi(e):0;if(test_tab_batch<1 || test_tab_batch>6) test_tab_batch=0;}
    if(fast_forward && test_tab_batch) frames_this_iter=test_tab_batch;
    const uint64_t batch_t0=SDL_GetPerformanceCounter();
    const double frame_budget_ms=(kTargetFrameSeconds*1000.0-2.0)*(shift_fast_forward?4:1);

    /* SC_FRAME_TIME=<ms threshold>: log (rate-limited, 500 hits) wall-clock
     * time for any run_one_frame() call slower than the threshold -- there's
     * no frame-pacing throttle in this loop other than vsync on the present
     * call, so if simulating a frame's worth of 65816 instructions takes
     * longer than ~16.67ms on some screen, the game visibly runs below
     * 60fps on that screen specifically, with no other symptom. */
    const char *frame_time_thresh_env = getenv("SC_FRAME_TIME");
    uint64_t frame_t0 = (frame_time_thresh_env || perf_on) ? SDL_GetPerformanceCounter() : 0;
    SC_PERF_ADD(kPerfInput, loop_t0, frame_t0);

    /* While the settings menu is open, freeze the game -- skip advancing
     * the emulator entirely and just keep re-presenting the last rendered
     * frame every host iteration, same freeze-and-redraw approach ar-recomp's
     * own settings overlay uses (see the menu block above). s_video_pixels
     * (and therefore `texture` below) simply isn't touched this iteration,
     * so whatever was last rendered stays on screen underneath the overlay. */
    bool guard_tripped = false;
    poll_mouse_construction();
    if (!s_menu_open && !s_build_work) {
      for (int ffi = 0; ffi < frames_this_iter && !s_build_work; ffi++) {
        /* Reserve a fully drawn final frame. Tab uses spare display time,
         * rather than requiring six frames regardless of city workload.
         * Intermediate frames retain native PPU/APU timing and sprite
         * evaluation, but omit the host's expanded image composition. */
        double spent_ms=(SDL_GetPerformanceCounter()-batch_t0)*s_perf_clock_ms;
        bool final_frame=ffi+1==frames_this_iter ||
            (fast_forward && !test_tab_batch && spent_ms+extra_frame_ms+full_frame_ms>frame_budget_ms);
        s_skip_custom_frame=fast_forward && !final_frame;
        /* Intermediate Tab frames keep sprite/beam/APU work, but their native
         * background image is never presented. The final frame is always full. */
        { static int skip_native=-1;
          if(skip_native<0) {const char *e=getenv("SC_TAB_SKIP_PIXELS");skip_native=!e || *e!='0';}
          ScPpuSkipPixels(s_skip_custom_frame && skip_native);
          ScPpuSkipObjects(s_skip_custom_frame && skip_native); }
        ScPpuMeasurePixels((fast_forward || perf_on)?SDL_GetPerformanceCounter:NULL,s_perf_clock_ms);
        s_custom_frame_ms=0;
        uint64_t guest_t0=SDL_GetPerformanceCounter();
        if (mouse_target_valid && !s_fast_cursor_enabled) {
          g_ram[0x01eb] = (uint8_t)(mouse_city_hit ? 128 : mouse_target_x<0?0:mouse_target_x>255?255:mouse_target_x);
          g_ram[0x01ed] = (uint8_t)(mouse_city_hit ? 128 : mouse_target_y<0?0:mouse_target_y>223?223:mouse_target_y);
        }
        ScGpuFieldsPoll(gpu_fields);
        if (!run_one_frame()) {
          fprintf(stderr, "frame %llu: opcode guard tripped (hang/runaway) -- stopping\n",
                  (unsigned long long)s_frames);
          guard_tripped = true;
          break;
        }
        ++fps_guest_frames;
        if(perf_on) ++perf_total_guests;
        double guest_ms=(SDL_GetPerformanceCounter()-guest_t0)*s_perf_clock_ms;
        if(perf_on) {s_perf_custom_ms+=s_custom_frame_ms;s_perf_native_ms+=ScPpuPixelMilliseconds();}
        double *estimate=s_skip_custom_frame?&extra_frame_ms:&full_frame_ms;
        /* React immediately to an expensive simulation phase; recover the
         * boost gradually when it ends, rather than oscillating into hitches. */
        *estimate=guest_ms>*estimate?guest_ms:*estimate*0.9+guest_ms*0.1;
        if(fast_forward && !s_skip_custom_frame) {
          /* Refresh the skipped-frame estimate even at 1x. Otherwise an
           * expensive phase could leave Tab stuck at 1x after it finishes. */
          double skipped_ms=guest_ms-s_custom_frame_ms-ScPpuPixelMilliseconds();
          /* The override keeps the old full-raster estimate for the oracle. */
          {const char *e=getenv("SC_TAB_SKIP_PIXELS");if(e && *e=='0') skipped_ms+=ScPpuPixelMilliseconds();}
          if(skipped_ms<0) skipped_ms=0;
          extra_frame_ms=skipped_ms>extra_frame_ms?skipped_ms:extra_frame_ms*0.9+skipped_ms*0.1;
        }
        if(final_frame) {frames_this_iter=ffi+1;break;}
        /* Extra fast-forward frames still need input re-armed exactly like
         * the top of this loop does every iteration: apply_frame_input()
         * resets input1_currentState to 0 (or any scripted qualify-mode
         * input) before the live keyboard state is OR'd back in -- skipping
         * the reset here would let stale bits accumulate across frames. */
        if (ffi + 1 < frames_this_iter) {
          apply_frame_input(s_frames);
          g_snes->input1_currentState |= input;
        }
      }
    }
    s_skip_custom_frame=false;
    ScPpuSkipPixels(false);ScPpuSkipObjects(false);
    ScPpuMeasurePixels(NULL,0);
    if (guard_tripped) break;
    const uint64_t emu_t1 = perf_on ? SDL_GetPerformanceCounter() : 0;
    SC_PERF_ADD(kPerfEmu, frame_t0, emu_t1);
    ScSram_Tick();
    if(music_thread) {
      static bool stall_test_done;
      const char *e=getenv("SC_AUDIO_STALL_AT");
      if(!stall_test_done && e && s_frames>=strtoull(e,NULL,0)) {
        stall_test_done=true;ScMusicStats before=ScMusicGetStats();SDL_Delay(250);
        ScMusicStats after=ScMusicGetStats();
        fprintf(stderr,"[music stall] 250ms cycles=%llu samples=%llu audible=%llu failures=%llu\n",
            (unsigned long long)(after.cycles-before.cycles),
            (unsigned long long)(after.samples-before.samples),
            (unsigned long long)(after.nonzero_samples-before.nonzero_samples),
            (unsigned long long)after.write_failures);
      }
    }

    /* A fast-forward batch leaves several frames of audio in the DSP ring;
     * the audio drain below trims it to one frame, so the sound does not
     * trail the picture afterwards. Two earlier attempts at that froze the
     * sound for good, because dsp_getSamples() then consumed a fixed 534
     * samples whether or not they existed. It now takes exactly what it is
     * asked for, never more than exists. */

    if (frame_time_thresh_env) {
      static uint32_t s_frame_time_hits;
      double ms = (double)(SDL_GetPerformanceCounter() - frame_t0) * 1000.0 /
                  (double)SDL_GetPerformanceFrequency();
      double thresh = atof(frame_time_thresh_env);
      if (ms >= thresh && s_frame_time_hits < 500) {
        fprintf(stderr, "[frametime f=%llu] %.2fms\n", (unsigned long long)s_frames, ms);
        s_frame_time_hits++;
      }
    }

    if (getenv("SC_DEBUG_LIVE")) {
      /* Self-triggering: don't start logging until $01ed first changes
       * (i.e. movement has actually begun), so a few seconds of imprecise
       * F1 timing before/after the user actually holds a direction don't
       * burn through the hit cap on dead frames. */
      static uint8_t s_dbg_live_last_ed = 0xff;
      static bool s_dbg_live_armed;
      if (!s_dbg_live_armed) {
        if (s_dbg_live_last_ed == 0xff) s_dbg_live_last_ed = g_ram[0x01ed];
        else if (g_ram[0x01ed] != s_dbg_live_last_ed) s_dbg_live_armed = true;
      }
      if (s_dbg_live_armed && s_dbg_live_hits < 2000) {
        fprintf(stderr, "[dbgl f=%llu] $01ed=%02x $00d7=%02x $01f3=%02x $01ff=%02x\n",
                (unsigned long long)s_frames, g_ram[0x01ed], g_ram[0xd7],
                g_ram[0x01f3], g_ram[0x01ff]);
        s_dbg_live_hits++;
      }
    }

    if (getenv("SC_LIVE_C9CA")) {
      static uint8_t s_last_c9 = 0xff, s_last_ca = 0xff, s_last_011b = 0xff;
      if (g_ram[0x00c9] != s_last_c9 || g_ram[0x00ca] != s_last_ca || g_ram[0x011b] != s_last_011b) {
        fprintf(stderr, "[c9ca f=%llu] $011b=%02x $c9=%02x $ca=%02x $01fb=%02x $14=%02x\n",
                (unsigned long long)s_frames, g_ram[0x011b], g_ram[0x00c9], g_ram[0x00ca],
                g_ram[0x01fb], g_ram[0x0014]);
        s_last_c9 = g_ram[0x00c9]; s_last_ca = g_ram[0x00ca]; s_last_011b = g_ram[0x011b];
      }
    }

    /* Audio: drain one frame's worth of DSP output at the native SNES rate,
     * same pacing model as snesrecomp/cosim/ref_driver.c's deterministic
     * consumer, queued to the SDL audio device instead of discarded. */
    if(music_thread) ScMusicGuestFrame(sc_audio_guest_cycle(),s_menu_open);
    if (audio_dev && !music_thread) {
      /* Don't accrue playback debt for wall-clock time the emulator wasn't
       * actually running: while the settings menu is open no frames are
       * simulated, so no samples are produced and there is nothing to pace
       * against. (This is the immediate half of the fix below.) */
      if (!s_menu_open) audio_acc += kDspSamplesPerFrame;
      /* Hard-clamp the accumulator to exactly the drain condition's upper
       * bound, which is also audio_buf's capacity. Without this, ANY stall
       * of two or more host iterations where the ring hasn't refilled --
       * menu open, a slow frame, an ordinary underrun -- pushes audio_acc
       * past 1024 and the `wantN <= 1024` test below then fails forever,
       * since audio_acc is only ever decremented *inside* that branch.
       * That's a self-latching permanent-silence trap: two bad frames and
       * sound never returns for the rest of the session, with no error and
       * no recovery path. Clamping converts it into what an underrun
       * should be -- a brief dropout that self-corrects on the next frame.
       * (Found via the F10 menu, which reproduced it every single time by
       * construction; it is a pre-existing bug the menu merely made
       * trivial to hit, and a strong candidate for the long-standing
       * intermittent audio complaints tracked separately.) */
      if (audio_acc > 1024.0) audio_acc = 1024.0;
      int wantN = (int)audio_acc;
      Dsp *dsp = g_snes->apu->dsp;
      uint32_t available = dsp->sampleWrite - dsp->sampleRead;
      if(frames_this_iter>1 && wantN>0 && available>(uint32_t)wantN) {
        dsp_trimSamples(dsp,(uint32_t)wantN);
        available=dsp->sampleWrite-dsp->sampleRead;
      }
      /* dsp_getSamples() takes exactly as many native samples as it is
       * asked for, and never more than exist (runner/src/snes/dsp.c). */
      static uint64_t s_audio_dbg_queued, s_audio_dbg_calls, s_audio_dbg_fails;
      if (available >= (uint32_t)wantN && wantN > 0 && wantN <= 1024) {
        audio_acc -= (double)wantN;
        const uint32_t got = dsp_getSamples(dsp, audio_buf, wantN);
        const Uint32 queued_samples = sc_audio_queued(&audio) / (2 * sizeof(int16_t));
        static int16_t rs_buf[kAudioOutMax * 2];
        const int out_n = sc_audio_rate_control(audio_buf, (int)got, queued_samples, rs_buf);
        /* Hard cap for a stall the rate control cannot absorb: past a quarter
         * of a second queued, the frame is dropped rather than played late. */
        int qrc = 0;
        if (queued_samples < 8192u && out_n > 0)
          qrc = sc_audio_queue(&audio, rs_buf, (Uint32)(out_n * 2 * sizeof(int16_t)));
        s_audio_dbg_calls++;
        if (qrc != 0) s_audio_dbg_fails++;
        else s_audio_dbg_queued += (uint64_t)out_n;
      }
      /* And on the DSP side: fast-forward (Tab) and drag turbo run several
       * guest frames per host frame, but only one frame's worth is played.
       * Whatever is left beyond about three frames is dropped, so the sound
       * catches up with the picture instead of trailing it by up to the
       * ring's quarter second. */
      if (dsp->sampleWrite - dsp->sampleRead > 1600u)
        dsp_trimSamples(dsp, 534u);
      /* SC_AUDIO_DEBUG: periodic drain-loop status, for diagnosing
       * windowed-only audio issues -- headless qualify mode shows healthy
       * DSP production (92% active frames) as a baseline, so if this
       * never fires or queued/drained stay at 0, the bug is specifically
       * in this drain loop or the SDL device, not the underlying audio
       * synthesis. Built while chasing a reported total-silence bug that
       * turned out to be self-inflicted (see the revert above) -- kept
       * for next time. */
      if (getenv("SC_AUDIO_DEBUG") && (s_frames % 180) == 0) {
        fprintf(stderr, "audio: f=%llu queued=%u dsp_backlog=%u drift=%+.3f%% "
                        "played_total=%llu calls=%llu fails=%llu\n",
                (unsigned long long)s_frames,
                sc_audio_queued(&audio) / (unsigned)(2 * sizeof(int16_t)),
                (unsigned)(dsp->sampleWrite - dsp->sampleRead), s_audio_drift * 100.0,
                (unsigned long long)s_audio_dbg_queued, (unsigned long long)s_audio_dbg_calls,
                (unsigned long long)s_audio_dbg_fails);
      }
    }

    const uint64_t draw_t0 = perf_on ? SDL_GetPerformanceCounter() : 0;
    SC_PERF_ADD(kPerfAudio, emu_t1, draw_t0);
    void *pixels = NULL; int pitch = 0;
    bool clipboard_ui=s_rom_is_us && host_map_screen_live() && ram_w(0x1d7) &&
        !ram_w(0xd7) && !ram_w(0x379) && s_mouse_dialog==SC_MOUSE_DIALOG_NONE &&
        !s_custom_renderer.advisor_frame && !s_custom_renderer.map_hold;
    if(clipboard_ui && s_custom_video.enabled) {
      ScViewport v=s_custom_renderer.view;
      for(int y=v.core_y;y<v.core_y+224 && y<v.height;++y)
        if(y>=0 && (y<v.core_y+10 || (s_clip_tool && y>=v.core_y+176)))
          ScRendererClipboardRow(&s_custom_renderer,g_ppu,v,s_clip_tool,s_clipboard.count!=0,
              s_clipboard.price,y,s_custom_renderer.pixels+(size_t)y*v.width);
      ScRendererHudPointer(&s_custom_renderer,g_ppu);
    }
    SDL_Texture *present_texture=texture;
    if(s_custom_renderer.defer_terrain && s_custom_renderer.terrain.deferred) {
      ScGpuTerrainDisplaySize(gpu_terrain,(unsigned)s_destination.w,(unsigned)s_destination.h);
      SDL_Texture *computed=ScGpuTerrainDraw(gpu_terrain,&s_custom_renderer);
      if(computed) present_texture=computed;
      else {
        fprintf(stderr,"[gpu terrain] dispatch failed; reverting to CPU renderer\n");
        s_gpu_terrain_enabled=false;gpu_failed=true;ScRendererDeferTerrain(&s_custom_renderer,false);
      }
    }
    bool _lok = present_texture==texture && (SDL_LockTexture(texture, NULL, &pixels, &pitch) SC_SDL_OK);
    /* Row-wise, NOT one memcpy of the whole array. s_video_pixels is sized for
     * the maximum widescreen width so the allocation never depends on the
     * runtime value -- copying sizeof() of it into a narrower texture would
     * both overrun the destination and misalign every row. */
    if (_lok && pixels) {
      const int width = s_custom_video.enabled ? s_custom_renderer.view.width : s_video_w;
      const int height = s_custom_video.enabled ? s_custom_renderer.view.height : kVideoHeight;
      const int row_bytes = width * 4;
      for (int y = 0; y < height; y++) {
        memcpy((uint8_t *)pixels + (size_t)y * pitch,
          s_custom_video.enabled ? (const uint8_t *)(s_custom_renderer.pixels + (size_t)y * width) :
          s_video_pixels + (size_t)y * s_video_pitch, (size_t)row_bytes);
        if (!s_custom_video.enabled && host_map_screen_live() && ram_w(0x1d7))
          ScRendererPopulationRow(&s_custom_renderer,g_ppu,viewport,false,y,
              (uint32_t *)((uint8_t *)pixels+(size_t)y*pitch));
        if(clipboard_ui && !s_custom_video.enabled)
          ScRendererClipboardRow(&s_custom_renderer,g_ppu,viewport,s_clip_tool,s_clipboard.count!=0,
              s_clipboard.price,y,(uint32_t *)((uint8_t *)pixels+(size_t)y*pitch));
      }
    }
    if (_lok) SDL_UnlockTexture(texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    ScRect dest = SC_RECT(s_destination.x, s_destination.y, s_destination.w, s_destination.h);
    /* The map panel stays pixel-sharp even when the launcher's optional
     * smoothing is enabled. Its detailed overlay covers regeneration too. */
    if(present_texture==texture)
      snesrecomp_sdl_set_texture_linear(texture,s_linear_filter && !s_custom_renderer.map_preview_frame);
    bool hide_city_setup=s_city_present_pending && g_ram[0x14]==0;
    bool _cok = hide_city_setup || (SDL_RenderCopy(renderer, present_texture, NULL, &dest) SC_SDL_OK);
    if(!hide_city_setup)draw_map_selection_preview(renderer,s_custom_video.enabled?s_custom_renderer.view:viewport);
    if(!hide_city_setup && (s_build_active || s_build_pending) && host_map_screen_live() && !s_menu_open)
      draw_build_preview(renderer,s_custom_video.enabled?s_custom_renderer.view:viewport);
    if(clipboard_ui && !s_menu_open && (s_clip_drag || s_clip_pending || (s_clip_tool==2 && s_clip_hover))) {
      ScViewport v=s_custom_video.enabled?s_custom_renderer.view:viewport;
      double sx=(double)s_destination.w/v.width,sy=(double)s_destination.h/v.height;
      int scroll_x=s_custom_video.enabled?s_custom_renderer.scroll_x+s_custom_renderer.scroll_adjust_x:(int16_t)ram_w(0x1bd)*8;
      int scroll_y=s_custom_video.enabled?s_custom_renderer.scroll_y+s_custom_renderer.scroll_adjust_y:(int16_t)ram_w(0x1bf)*8;
      int left=s_clip_x1,top=s_clip_y1,w=s_clipboard.width,h=s_clipboard.height;
      bool copy=s_clip_drag || s_clip_pending==1;
      if(copy) {
        left=s_clip_x0<s_clip_x1?s_clip_x0:s_clip_x1;top=s_clip_y0<s_clip_y1?s_clip_y0:s_clip_y1;
        w=abs(s_clip_x1-s_clip_x0)+1;h=abs(s_clip_y1-s_clip_y0)+1;
      }
      bool valid=copy || (left>=0 && top>=0 && left+w<=(s_world.active?ScWorldWidth(&s_world):120) &&
          top+h<=(s_world.active?ScWorldHeight(&s_world):100));
      SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer,255,valid?230:70,valid?60:70,240);
      SDL_Rect clip={(int)s_destination.x,(int)s_destination.y,(int)s_destination.w,(int)s_destination.h};
      SDL_RenderSetClipRect(renderer,&clip);
      double z=s_custom_renderer.zoom_frame?s_custom_renderer.map_zoom:1;
      int dx0=(scroll_x-v.core_x/z-56/z)/8-left-1,dy0=(scroll_y-v.core_y/z-46/z)/8-top-1;
      int dx1=dx0+v.width/(8*z)+16,dy1=dy0+v.height/(8*z)+16;
      if(dx0<0) dx0=0;if(dy0<0) dy0=0;if(dx1>w) dx1=w;if(dy1>h) dy1=h;
      static const int neighbour[4][2]={{0,-1},{1,0},{0,1},{-1,0}};
      for(int dy=dy0;dy<dy1;++dy) for(int dx=dx0;dx<dx1;++dx) {
        if(!copy && !s_clipboard.mask[dy*w+dx]) continue;
        int gx=(left+dx)*8-scroll_x,gy=(top+dy)*8-scroll_y;
        for(unsigned edge=0;edge<4;++edge) {
          int nx=dx+neighbour[edge][0],ny=dy+neighbour[edge][1];
          if(nx>=0 && ny>=0 && nx<w && ny<h && (copy || s_clipboard.mask[ny*w+nx])) continue;
          int ax=gx+(edge==1?8:0),ay=gy+(edge==2?8:0);
          int bx=ax+(edge==0 || edge==2?8:0),by=ay+(edge==1 || edge==3?8:0);
          double pax=ax,pay=ay,pbx=bx,pby=by;
          if(s_custom_video.enabled) {ScRendererProjectCity(&s_custom_renderer,&pax,&pay);ScRendererProjectCity(&s_custom_renderer,&pbx,&pby);}
          if((pay<46 && pby>=0) || (pax<56 && pbx>=8 && pay<224 && pby>=46)) continue;
          SDL_RenderDrawLine(renderer,s_destination.x+(v.core_x+pax)*sx,s_destination.y+(v.core_y+pay)*sy,
              s_destination.x+(v.core_x+pbx)*sx,s_destination.y+(v.core_y+pby)*sy);
        }
      }
      SDL_RenderSetClipRect(renderer,NULL);
    }
    /* Validation hook: publish only complete presented frames, atomically.
     * No guest work is performed for a capture, including during resizing. */
    if (s_custom_video.enabled) {
      const char *capture=getenv("SC_LIVE_CAPTURE");
      static int last_w,last_h;
      if (capture && (last_w!=viewport.width || last_h!=viewport.height)) {
        char path[1024], temp[1024];
        snprintf(path,sizeof(path),"%s-%dx%d.ppm",capture,viewport.width,viewport.height);
        snprintf(temp,sizeof(temp),"%s.tmp",path);
        if (write_ppm(temp)) {
#ifdef _WIN32
          MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
#else
          rename(temp,path);
#endif
        }
        last_w=viewport.width; last_h=viewport.height;
      }
    }
    { static int diag = -1;
      if (diag < 0) diag = getenv("SC_SDL_DIAG") ? 0 : 99;
      if (diag < 99 && (s_frames % 60) == 0) {
        fprintf(stderr, "[sdl] lock=%d pitch=%d expect=%d copy=%d err=%s\n",
                (int)_lok, pitch, (int)s_video_pitch, (int)_cok, SDL_GetError()); } }
    if (s_menu_open) render_settings_menu(renderer);
    if (s_replay_open) render_replay_menu(renderer);

    /* SC_RENDER_DUMP_AT=<frame> + SC_RENDER_DUMP_PATH: capture the RENDERER,
     * overlays included. SC_DUMP_AT captures s_video_pixels, which is the
     * guest frame before any host overlay is drawn on top of it, so it cannot
     * see the settings menu, the replay box or the Sylt card at all. */
    { static long long at = -2; static const char *path;
      if (at == -2) { const char *e = getenv("SC_RENDER_DUMP_AT");
        at = e ? atoll(e) : -1;
        path = getenv("SC_RENDER_DUMP_PATH");
        if (!path) path = "render_dump.ppm"; }
      if (at >= 0 && (long long)s_frames >= at) {
        at = -1;
        /* One-shot: dump and quit, like SC_MENU_PREVIEW. --qualify never
         * reaches this loop at all, so a capture of any host overlay has to
         * come from a real windowed run, and it should not outstay it. */
        quit = true;
        const char *state_path=getenv("SC_RENDER_STATE_PATH");
        if (state_path && *state_path) save_state(state_path);
        if (write_renderer_ppm(renderer, path))
          fprintf(stderr, "[SC_RENDER_DUMP] wrote %s at frame %llu\n", path,
                  (unsigned long long)s_frames);
      } }
    if (s_menu_preview && --s_menu_preview_countdown <= 0) {
      if (write_renderer_ppm(renderer, "menu_preview.ppm"))
        fprintf(stderr, "[SC_MENU_PREVIEW] dumped menu_preview.ppm\n");
      else
        fprintf(stderr, "[SC_MENU_PREVIEW] failed to dump menu_preview.ppm\n");
      quit = true;
    }

    const uint64_t draw_t1 = SDL_GetPerformanceCounter();
    SC_PERF_ADD(kPerfDraw, draw_t0, draw_t1);
    const uint64_t present_t0 = perf_on ? SDL_GetPerformanceCounter() : 0;
    /* SC_DUMP_DIR + SC_DUMP_INTERVAL, for the INTERACTIVE loop.
     *
     * The same pair has worked in run_qualification() for a long time, and I
     * assumed it worked here too -- it does not, that hook is in the headless
     * path only. A capture session recorded zero frames because of it, which
     * matters whenever a defect only shows while the map is moving and so
     * cannot be caught in a screenshot. */
    { const char *dd = getenv("SC_DUMP_DIR"), *di = getenv("SC_DUMP_INTERVAL");
      if (dd && *dd && di && *di) {
        static unsigned long long cap_frame;
        const unsigned long long iv = strtoull(di, NULL, 0);
        if (iv && cap_frame % iv == 0) {
          char pth[512];
          snprintf(pth, sizeof pth, "%s/frame_%010llu.ppm", dd, cap_frame);
          if (!(getenv("SC_DUMP_RENDERER")?write_renderer_ppm(renderer,pth):write_ppm(pth)))
            fprintf(stderr, "SC_DUMP_DIR: cannot write %s\n", pth);
        }
        cap_frame++;
      } }
    /* SDL_RenderPresent returns void on SDL2 and bool on SDL3, so it cannot
     * share the SC_SDL_OK spelling with the other calls. */
#if SNESRECOMP_SDL3
    { bool _pok = SDL_RenderPresent(renderer);
#else
    { SDL_RenderPresent(renderer); bool _pok = true;
#endif
      static int pdiag = -1;
      if (pdiag < 0) pdiag = getenv("SC_SDL_DIAG") ? 0 : 99;
      if (pdiag < 99 && (s_frames % 60) == 0) {
        int ow = 0, oh = 0;
#if SNESRECOMP_SDL3
        SDL_GetRenderOutputSize(renderer, &ow, &oh);
#else
        SDL_GetRendererOutputSize(renderer, &ow, &oh);
#endif
        fprintf(stderr, "[sdl] present=%d out=%dx%d same_renderer=%d err=%s\n",
                (int)_pok, ow, oh,
                (int)(SDL_GetRenderer(window) == renderer), SDL_GetError());
      } }

    if (perf_on) {
      SC_PERF_ADD(kPerfPresent, present_t0, SDL_GetPerformanceCounter());
      perf_frames++;
    }
    /* Present completed input and terrain immediately, then wait for the
     * next frame. Sleeping before presentation added the unused CPU budget
     * to input-to-display latency on every otherwise fast frame. */
    next_frame_deadline += (uint64_t)(kTargetFrameSeconds * (double)SDL_GetPerformanceFrequency());
    uint64_t now = SDL_GetPerformanceCounter();
    /* Use otherwise idle pacing time for private placement, keeping a short
     * deadline and polling window events again at the next 60 Hz iteration. */
    if(s_build_work && now+SDL_GetPerformanceFrequency()/1000<next_frame_deadline) {
      uint64_t work_t0=now;
      poll_mouse_construction_until(next_frame_deadline-SDL_GetPerformanceFrequency()/1000);
      now=SDL_GetPerformanceCounter();SC_PERF_ADD(kPerfEmu,work_t0,now);
    }
    const uint64_t sleep_t0 = now;
    if (now < next_frame_deadline) {
      double remaining_ms = (double)(next_frame_deadline - now) * 1000.0 /
                             (double)SDL_GetPerformanceFrequency();
      if (remaining_ms > 1.0) SDL_Delay((Uint32)(remaining_ms - 1.0));
      while (SDL_GetPerformanceCounter() < next_frame_deadline) { /* spin for the last <1ms */ }
    } else {
      /* Running behind (e.g. this frame's work overran budget) -- don't
       * try to catch up by presenting a burst of frames back-to-back;
       * just resync the deadline to now so pacing doesn't accumulate
       * drift after a one-off slow frame. */
      next_frame_deadline = now;
    }
    SC_PERF_ADD(kPerfSleep, sleep_t0, SDL_GetPerformanceCounter());
    fps_window_frames++;
    if(perf_on) ++perf_total_frames;
    if(perf_frame_file) fprintf(perf_frame_file,"%llu,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%llu,%.6f",
        (unsigned long long)s_frames,(int)s_custom_renderer.city_frame,s_development_speed,
        perf_current[kPerfInput],perf_current[kPerfEmu],perf_current[kPerfAudio],
        perf_current[kPerfDraw],perf_current[kPerfPresent],perf_current[kPerfSleep],
        (int)s_custom_renderer.advisor_frame,
        (unsigned long long)(s_development.extra_attempts>=perf_extra_begin?
            s_development.extra_attempts-perf_extra_begin:0),s_custom_frame_ms);
    if(perf_frame_file) {
      for(unsigned stage=0;stage<SC_RENDER_STAGES;++stage)
        fprintf(perf_frame_file,",%.6f",s_custom_renderer.measure_ticks[stage]*s_perf_clock_ms);
      fputc('\n',perf_frame_file);
    }
    memset(s_custom_renderer.measure_ticks,0,sizeof s_custom_renderer.measure_ticks);
    if(bank_frame_file) {
      fprintf(bank_frame_file,"%llu,%.6f",(unsigned long long)s_frames,perf_current[kPerfEmu]);
      for(unsigned p=0;p<256;++p) fprintf(bank_frame_file,",%llu",s_bank_ops[p]-bank_begin[p]);
      for(unsigned p=0;p<256;++p) fprintf(bank_frame_file,",%llu",s_b3_page[p]-page_begin[p]);
      for(unsigned p=0;p<256;++p) fprintf(bank_frame_file,",%llu",s_pc_profile_ops[p]-pc_begin[p]);
      fputc('\n',bank_frame_file);
    }
    if(perf_exit_frame && s_frames>=perf_exit_frame) quit=true;
    double fps_window_elapsed = (double)(SDL_GetPerformanceCounter() - fps_window_start) /
                                 (double)SDL_GetPerformanceFrequency();
    if (fps_window_elapsed >= 1.0) {
      char title[128];
      double boost=fps_window_frames?(double)fps_guest_frames/fps_window_frames:1.0;
      if(fast_forward) snprintf(title,sizeof(title),"Urban Recomp -- %.1f fps (Tab %.1fx)",
          (double)fps_window_frames/fps_window_elapsed,boost);
      else snprintf(title, sizeof(title), "Urban Recomp -- %.1f fps",
          (double)fps_window_frames / fps_window_elapsed);
      SDL_SetWindowTitle(window, title);
      if (perf_on && perf_frames) {
        static const char *const kPerfName[kPerfCount] = {
            "input", "emu", "audio", "draw", "sleep", "present"};
        fprintf(stderr, "[perf] %.1f fps", (double)fps_window_frames / fps_window_elapsed);
        for (int k = 0; k < kPerfCount; k++) {
          fprintf(stderr, "  %s %.2f/%.2f", kPerfName[k], perf_sum[k] / perf_frames, perf_max[k]);
          perf_sum[k] = perf_max[k] = 0;
        }
        fprintf(stderr,"  raster %.2f native %.2f host %.2f power %.2f spatial %llu C-development %llu boost %.2fx\n",s_perf_raster_ms/perf_frames,
            s_perf_native_ms/perf_frames,s_perf_custom_ms/perf_frames,
            s_perf_power_ms/perf_frames,(unsigned long long)s_perf_spatial_cells,
            (unsigned long long)s_perf_native_development_calls,boost);
        s_perf_native_ms=s_perf_custom_ms=0;
        s_perf_native_development_calls=0;
        s_perf_raster_ms=s_perf_power_ms=0;s_perf_spatial_cells=0;
        perf_frames = 0;
      }
      fps_window_frames = 0;
      fps_guest_frames=0;
      fps_window_start = SDL_GetPerformanceCounter();
    }
  }

  if(music_thread) {
    ScMusicStats stats=ScMusicGetStats();
    fprintf(stderr,"[music thread] cycles=%llu samples=%llu audible=%llu blocks=%llu failures=%llu\n",
        (unsigned long long)stats.cycles,(unsigned long long)stats.samples,
        (unsigned long long)stats.nonzero_samples,(unsigned long long)stats.blocks,
        (unsigned long long)stats.write_failures);
  }
  ScMusicStop();
  ScConstructionFree(s_build_work);s_build_work=NULL;
  if(perf_frame_file) fclose(perf_frame_file);
  if(bank_frame_file) fclose(bank_frame_file);
  if(s_native_missing_file) {fclose(s_native_missing_file);s_native_missing_file=NULL;}
  if (audio_dev) sc_audio_close(&audio);
  test_city_restore_sram();
  ScSram_Flush();
  ScDevelopmentReportProfile(stderr);
  if(perf_on && perf_total_frames) fprintf(stderr,
      "[perf total] display %llu guest %llu emu %.3f draw %.3f present %.3f ms/frame\n",
      (unsigned long long)perf_total_frames,(unsigned long long)perf_total_guests,
      perf_total[kPerfEmu]/perf_total_frames,perf_total[kPerfDraw]/perf_total_frames,
      perf_total[kPerfPresent]/perf_total_frames);
  if(s_bank_profile) {
    unsigned long long pages[256];memcpy(pages,s_b3_page,sizeof pages);
    fprintf(stderr,"[bank profile] bank %02x %llu dispatches; remaining hot pages:\n",s_bank_page_sel,s_bank_ops[s_bank_page_sel]);
    for(unsigned n=0;n<12;++n) {
      unsigned best=0;for(unsigned p=1;p<256;++p) if(pages[p]>pages[best]) best=p;
      if(!pages[best]) break;
      fprintf(stderr,"  %02x:%02xxx %llu\n",s_bank_page_sel,best,pages[best]);pages[best]=0;
    }
  }
  if(s_bank_profile) {
    fprintf(stderr,"[interpreter calls] main:");
    for(unsigned bank=0;bank<256;++bank) if(s_interpreter_bank_ops[bank])
      fprintf(stderr," bank%02x=%llu",bank,(unsigned long long)s_interpreter_bank_ops[bank]);
    fputc('\n',stderr);
    fprintf(stderr,"[native binding] deferred=%llu fallback=%llu\n",
        (unsigned long long)s_native_bind_deferred,(unsigned long long)s_native_bind_fallback);
    fprintf(stderr,"[program C] edges=%llu guest-clocks=%llu\n",
        (unsigned long long)ScProgramCalls(),(unsigned long long)ScProgramCycles());
    fprintf(stderr,"[program control] edges=%llu\n",
        (unsigned long long)ScProgramControlCalls());
    fprintf(stderr,"[whole-executable interpreter] instructions=%llu guest-clocks=%llu\n",
        (unsigned long long)interp816_insns_total(),(unsigned long long)interp816_cycles_total());
    fprintf(stderr,"[program lane] entries=%llu edges=%llu\n",
        (unsigned long long)s_program_lane_entries,(unsigned long long)s_program_lane_edges);
    fprintf(stderr,"[program blocks] entries=%llu edges=%llu\n",
        (unsigned long long)s_program_block_entries,(unsigned long long)s_program_block_edges);
    fprintf(stderr,"[wait lane] entries=%llu spans=%llu\n",
        (unsigned long long)s_wait_lane_entries,(unsigned long long)s_wait_lane_spans);
    fprintf(stderr,"[development lane] entries=%llu spans=%llu\n",
        (unsigned long long)s_development_lane_entries,(unsigned long long)s_development_lane_spans);
      fprintf(stderr,"[city lane] entries=%llu spans=%llu\n",(unsigned long long)s_city_lane_entries,(unsigned long long)s_city_lane_spans);
    uint64_t pages[256];memcpy(pages,s_interpreter_pages,sizeof pages);
    fprintf(stderr,"[main interpreter pages] bank %02x:\n",s_bank_page_sel);
    for(unsigned n=0;n<12;++n) {
      unsigned best=0;for(unsigned p=1;p<256;++p) if(pages[p]>pages[best]) best=p;
      if(!pages[best]) break;
      fprintf(stderr,"  %02x:%02xxx %llu\n",s_bank_page_sel,best,(unsigned long long)pages[best]);pages[best]=0;
    }
    if(s_pc_profile_page>=0) {
      fprintf(stderr,"[main interpreter PCs] bank %02x page %02x:\n",s_bank_page_sel,s_pc_profile_page);
      for(unsigned n=0;n<24;++n) {
        unsigned best=0;for(unsigned p=1;p<256;++p) if(s_interpreter_pc[p]>s_interpreter_pc[best]) best=p;
        if(!s_interpreter_pc[best]) break;
        fprintf(stderr,"  %02x:%02x%02x %llu\n",s_bank_page_sel,s_pc_profile_page,best,(unsigned long long)s_interpreter_pc[best]);
        s_interpreter_pc[best]=0;
      }
    }
    const uint64_t *ops=ScWorldGuestInterpreterCounts();uint64_t total=0;
    memset(pages,0,sizeof pages);
    for(unsigned p=0;p<65536;++p) {total+=ops[p];pages[p>>8]+=ops[p];}
    fprintf(stderr,"[kernel interpreter calls] bank03=%llu\n",(unsigned long long)total);
    for(unsigned n=0;n<12;++n) {
      unsigned best=0;for(unsigned p=1;p<256;++p) if(pages[p]>pages[best]) best=p;
      if(!pages[best]) break;
      fprintf(stderr,"  03:%02xxx %llu\n",best,(unsigned long long)pages[best]);pages[best]=0;
    }
  }
  if(gpu_fields) fprintf(stderr,"[gpu fields] native publication cells=%llu\n",(unsigned long long)ScWorldGuestStencilCells());
  fprintf(stderr,"[gpu service] fused cells=%llu GPU cells=%llu\n",(unsigned long long)ScWorldGuestServiceCells(),(unsigned long long)ScWorldGuestServiceGpuCells());
  fprintf(stderr,"[gpu crime] published cells=%llu\n",(unsigned long long)ScWorldGuestCrimeGpuCells());
  fprintf(stderr,"[gpu land] published summaries=%llu\n",(unsigned long long)ScWorldGuestLandGpuCells());
  fprintf(stderr,"[power stages] spans=%llu guest-clocks=%llu\n",(unsigned long long)ScPowerTraversalStageSpans(),(unsigned long long)ScPowerTraversalStageClocks());
  if(perf_on || s_bank_profile)fprintf(stderr,"[power cache] solved=%llu reused=%llu\n",
      (unsigned long long)s_power_refresh.network_solves,(unsigned long long)s_power_refresh.policy_reuses);
  ScWorldGuestSetStencilBackend(NULL);ScGpuFieldsDestroy(gpu_fields);
  ScGpuTerrainDestroy(gpu_terrain);
  if(s_bank_profile && s_pc_profile_page>=0) {
    fprintf(stderr,"[PC profile] bank %02x page %02x:\n",s_bank_page_sel,s_pc_profile_page);
    for(unsigned n=0;n<24;++n) {
      unsigned best=0;for(unsigned i=1;i<256;++i) if(s_pc_profile_ops[i]>s_pc_profile_ops[best]) best=i;
      if(!s_pc_profile_ops[best]) break;
      fprintf(stderr,"  %02x:%02x%02x %llu\n",s_bank_page_sel,s_pc_profile_page,best,s_pc_profile_ops[best]);
      s_pc_profile_ops[best]=0;
    }
  }
  ScFleetFree(s_fleet);
  SDL_DestroyTexture(texture);
  if(s_preview_texture)SDL_DestroyTexture(s_preview_texture);
  free(s_preview_cells);free(s_preview_reveal);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  ScRendererDestroy(&s_custom_renderer);
  ScClipboardClear(&s_clipboard);
  if(s_development_batches)fprintf(stderr,"[development batches] zones=%u attempts=%llu passes=%llu\n",
      ScDevelopmentBatchesZones(s_development_batches),
      (unsigned long long)ScDevelopmentBatchesAttempts(s_development_batches),
      (unsigned long long)ScDevelopmentBatchesPasses(s_development_batches));
  ScDevelopmentBatchesDestroy(s_development_batches);s_development_batches=NULL;
  ScPopulationCensusDestroy(s_population_census);s_population_census=NULL;
  write_pc_bitmap_dump();
  write_map_trace_summary();
  { const char *p = getenv("SC_WRAM_MAP"); if (s_wram_map && p) write_wram_map(p); }
  /* Same SRAM dump the headless path does -- a real play session is the only
   * way to get SRAM with actual saved cities in it, so it is worth capturing
   * from the windowed exit too. */
  { const char *p = getenv("SC_SRAM_DUMP_PATH");
    if (p && *p) {
      report_sram_header("exit");
      fprintf(stderr, write_sram_dump(p) ? "dumped SRAM to %s\n"
                                         : "failed to write SRAM dump to %s\n", p);
    } }
  return 0;
}
