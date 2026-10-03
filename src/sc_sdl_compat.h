/*
 * Project-local SDL2/SDL3 shims, for the things the shared
 * runner/src/desktop/sdl_compat.h does not cover.
 *
 * Everything else this host needs is already in the shared shim -- window and
 * renderer creation, key/mod access, display bounds, mouse state, mutexes,
 * audio pause and mix. Use those, not these. These gaps remain because upstream's
 * own host does not have them:
 *
 * 1. QUEUED AUDIO. mmx23_host_main.inc uses a pull callback (SDL2) or an
 *    SDL_AudioStream callback (SDL3). This host does neither: it owns the DSP
 *    drain loop and *pushes* finished samples, so it wants the queue API.
 *    SDL2 spells that SDL_QueueAudio on a device; SDL3 removed it and folded
 *    the same behaviour into SDL_AudioStream -- open with a NULL callback and
 *    push, which is a queue by another name.
 *
 * 2. SCANCODE from a key event. The shared shim has SNESRECOMP_SDL_EVENT_KEY
 *    and _MOD but not the scancode, and this host binds on scancodes (see the
 *    SDL_GetScancodeFromKey comment in main.c -- bindings follow the key
 *    labels on a QWERTZ layout, which needs the physical code).
 *
 * 3. MOUSE GRAB. Panning confines the cursor without grabbing the keyboard.
 */
#ifndef SC_SDL_COMPAT_H_INCLUDED
#define SC_SDL_COMPAT_H_INCLUDED

#include "desktop/sdl_compat.h"

/* SDL3 moved the SDL_Keysym fields up into the event itself. */
#if SNESRECOMP_SDL3
#define SC_EVENT_SCANCODE(ev) ((ev).key.scancode)
#define SC_EVENT_KEYMOD(ev)   ((ev).key.mod)
#else
#define SC_EVENT_SCANCODE(ev) ((ev).key.keysym.scancode)
#define SC_EVENT_KEYMOD(ev)   ((ev).key.keysym.mod)
#endif

/* SDL3 added an out-parameter reporting which modifier the key needs on the
 * active layout. This host only wants the physical code, so it is discarded --
 * but keep the call layout-aware rather than positional: the bindings here
 * follow the key LABELS (Y is SNES Y, X is SNES B), which on a QWERTZ layout
 * are not where QWERTY puts them. See the binding block in main.c. */
static inline SDL_Scancode sc_scancode_from_key(SDL_Keycode key) {
#if SNESRECOMP_SDL3
    return SDL_GetScancodeFromKey(key, NULL);
#else
    return SDL_GetScancodeFromKey(key);
#endif
}

/* ── queued audio ────────────────────────────────────────────────────────── */

typedef struct ScAudio {
#if SNESRECOMP_SDL3
    SDL_AudioStream *stream;
#else
    SDL_AudioDeviceID device;
#endif
    int freq, channels, samples;
} ScAudio;

/* Opens a device for pushed S16 stereo audio. Returns true on success and
 * fills *out; on failure returns false and SDL_GetError() explains why. */
static inline bool sc_audio_open(ScAudio *out, int freq, int channels,
                                 int samples) {
    SDL_AudioSpec want;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = freq;
    want.channels = (Uint8)channels;
#if SNESRECOMP_SDL3
    want.format = SDL_AUDIO_S16;
    /* SDL_AudioSpec lost `samples` in SDL3: buffer sizing is the stream's
     * business now, so the caller's request is only advisory here. */
    out->stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, NULL, NULL);
    if (!out->stream) return false;
    SDL_ResumeAudioStreamDevice(out->stream);
#else
    SDL_AudioSpec have;
    want.format = AUDIO_S16SYS;
    want.samples = (Uint16)samples;
    out->device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!out->device) return false;
    SDL_PauseAudioDevice(out->device, 0);
    freq = have.freq;
    channels = have.channels;
    samples = have.samples;
#endif
    out->freq = freq;
    out->channels = channels;
    out->samples = samples;
    return true;
}

static inline bool sc_audio_opened(const ScAudio *a) {
#if SNESRECOMP_SDL3
    return a->stream != NULL;
#else
    return a->device != 0;
#endif
}

/* Push finished samples. Returns 0 on success, negative on failure, matching
 * SDL2's SDL_QueueAudio so call sites keep their existing error handling. */
static inline int sc_audio_queue(ScAudio *a, const void *data, Uint32 bytes) {
#if SNESRECOMP_SDL3
    return SDL_PutAudioStreamData(a->stream, data, (int)bytes) ? 0 : -1;
#else
    return SDL_QueueAudio(a->device, data, bytes);
#endif
}

/* Bytes still waiting to be played -- the backpressure signal this host's
 * drain loop paces on. */
static inline Uint32 sc_audio_queued(ScAudio *a) {
#if SNESRECOMP_SDL3
    int n = SDL_GetAudioStreamQueued(a->stream);
    return n > 0 ? (Uint32)n : 0u;
#else
    return SDL_GetQueuedAudioSize(a->device);
#endif
}

static inline void sc_audio_clear(ScAudio *a) {
#if SNESRECOMP_SDL3
    SDL_ClearAudioStream(a->stream);
#else
    SDL_ClearQueuedAudio(a->device);
#endif
}

/* Capture routes outside-window button releases; grabbing confines a pan.
 * Keep these separate so construction/selection drags can leave the window. */
static inline bool sc_window_mouse_grab(SDL_Window *window, bool grabbed) {
#if SNESRECOMP_SDL3
    return SDL_SetWindowMouseGrab(window, grabbed);
#elif SDL_VERSION_ATLEAST(2, 0, 16)
    SDL_SetWindowMouseGrab(window, grabbed ? SDL_TRUE : SDL_FALSE);
    return true;
#else
    SDL_SetWindowGrab(window, grabbed ? SDL_TRUE : SDL_FALSE);
    return true;
#endif
}

static inline bool sc_window_mouse_grabbed(SDL_Window *window) {
#if SNESRECOMP_SDL3 || SDL_VERSION_ATLEAST(2, 0, 16)
    return SDL_GetWindowMouseGrab(window) != 0;
#else
    return SDL_GetWindowGrab(window) != 0;
#endif
}

static inline bool sc_window_relative_mouse(SDL_Window *window, bool enabled) {
    SDL_SetHint("SDL_MOUSE_RELATIVE_MODE_CENTER", "1");
#if SNESRECOMP_SDL3
    return SDL_SetWindowRelativeMouseMode(window, enabled);
#else
    (void)window;
    return SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) == 0;
#endif
}

static inline void sc_relative_mouse_delta(double *x, double *y) {
#if SNESRECOMP_SDL3
    float dx=0,dy=0;
#else
    int dx=0,dy=0;
#endif
    SDL_GetRelativeMouseState(&dx,&dy);
    *x=dx;*y=dy;
}

static inline void sc_audio_close(ScAudio *a) {
#if SNESRECOMP_SDL3
    if (a->stream) { SDL_DestroyAudioStream(a->stream); a->stream = NULL; }
#else
    if (a->device) { SDL_CloseAudioDevice(a->device); a->device = 0; }
#endif
}

#endif /* SC_SDL_COMPAT_H_INCLUDED */
