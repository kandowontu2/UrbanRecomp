#include "sc_music.h"
#include "sc_pcm.h"
#include "snes/apu.h"
#include "snes/dsp.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static struct {
    Apu *apu;
    ScAudio *output;
    SDL_mutex *mutex;
    SDL_Thread *thread;
    atomic_bool running,paused;
    atomic_uint_fast64_t guest;
    ScMusicStats stats;
} music;
static ScPcm restored;
static uint64_t output_phase;
static bool profile_sync;
static uint64_t sync_calls,sync_cycles,sync_ticks,sync_max_ticks;
static uint64_t sync_lock_ticks,sync_lock_max_ticks;
static _Thread_local uint64_t last_lock_ticks;
bool ScMusicLoadRestored(const char *directory) {
    if(ScMusicRunning()) return false;
    bool ready=ScPcmLoad(&restored,directory,44100);
    if(ready) fprintf(stderr,"[restored music] all 19 PCM tracks loaded, 44.1 kHz stereo\n");
    return ready;
}
uint8_t ScMusicCommand(uint8_t command) {
    if(!ScMusicRunning()) return command;
    ScMusicLock();uint8_t spc=ScPcmCommand(&restored,command);ScMusicUnlock();
    if(restored.ready && command) fprintf(stderr,"[restored music] command %u, track %u\n",command,command<20?command:0);
    return spc;
}
void ScMusicEnabled(bool enabled) {
    ScMusicLock();restored.enabled=enabled;ScMusicUnlock();
}
void ScMusicRestoreLocked(uint8_t command,bool enabled) {
    if(!restored.ready || !ScMusicRunning()) return;
    restored.track=0;restored.position=0;
    ScPcmCommand(&restored,command);
    restored.enabled=enabled;
    /* Zero suppresses NEW SPC song starts, but it leaves a previously loaded
     * song running. The US driver acknowledges 0x16 and releases its music
     * voices; its SFX ports continue to work. This bounded load-only handshake
     * precedes zero, so a save cannot play both soundtracks together. */
    if(command && command<20) {
        apu_writePortNow(music.apu,0,0x16);
        for(unsigned cycles=0;music.apu->outPorts[0]!=0x16 && cycles<65536;++cycles) apu_cycle(music.apu);
        apu_writePortNow(music.apu,0,0);
        dsp_trimSamples(music.apu->dsp,0);
        fprintf(stderr,"[restored music] restored track %u, music %s\n",command,enabled?"on":"off");
    }
}

bool ScMusicRunning(void) {return atomic_load_explicit(&music.running,memory_order_acquire);}
void ScMusicLock(void) {
    if(!music.mutex)return;
    uint64_t started=profile_sync?SDL_GetPerformanceCounter():0;
    SDL_LockMutex(music.mutex);
    if(profile_sync)last_lock_ticks=SDL_GetPerformanceCounter()-started;
}
void ScMusicUnlock(void) {if(music.mutex) SDL_UnlockMutex(music.mutex);}
void ScMusicSyncLocked(uint64_t guest) {
    if(!ScMusicRunning()) return;
    uint64_t started=profile_sync?SDL_GetPerformanceCounter():0;
    uint64_t before=music.apu->portClock;
    /* Synchronous protocol reads may need an echo before the next worker
     * wakeup. Only those reads advance the SPC on the game thread. */
    apu_runToGuestCycle(music.apu,guest,1u<<20);
    music.stats.cycles+=music.apu->portClock-before;
    if(profile_sync) {
        uint64_t ticks=SDL_GetPerformanceCounter()-started,cycles=music.apu->portClock-before;
        ++sync_calls;sync_cycles+=cycles;sync_ticks+=ticks;
        if(ticks>sync_max_ticks)sync_max_ticks=ticks;
        sync_lock_ticks+=last_lock_ticks;
        if(last_lock_ticks>sync_lock_max_ticks)sync_lock_max_ticks=last_lock_ticks;
        double ms=ticks*1000.0/SDL_GetPerformanceFrequency();
        double lock_ms=last_lock_ticks*1000.0/SDL_GetPerformanceFrequency();
        if(ms>=5 || lock_ms>=5)fprintf(stderr,"[music sync] guest=%llu cycles=%llu host-ms=%.3f lock-ms=%.3f\n",
            (unsigned long long)guest,(unsigned long long)cycles,ms,lock_ms);
    }
}
void ScMusicWrite(uint8_t port,uint8_t value,uint64_t guest) {
    if(!ScMusicRunning()) return;
    ScMusicLock();
    while(!apu_schedulePortWrite(music.apu,port,value,guest)) {
        apu_cycle(music.apu);++music.stats.cycles;
    }
    ScMusicUnlock();
}
void ScMusicGuestFrame(uint64_t guest,bool paused) {
    atomic_store_explicit(&music.guest,guest,memory_order_release);
    atomic_store_explicit(&music.paused,paused,memory_order_release);
}
void ScMusicResetLocked(void) {
    if(!ScMusicRunning()) return;
    apu_clearPortQueue(music.apu);dsp_trimSamples(music.apu->dsp,0);sc_audio_clear(music.output);
    output_phase=0;restored.track=0;restored.position=0;
    atomic_store_explicit(&music.guest,0,memory_order_release);
}
ScMusicStats ScMusicGetStats(void) {
    ScMusicLock();ScMusicStats result=music.stats;ScMusicUnlock();return result;
}
static int worker(void *unused) {
    (void)unused;int16_t samples[512*2];
    while(ScMusicRunning()) {
        if(atomic_load_explicit(&music.paused,memory_order_acquire)) {SDL_Delay(2);continue;}
        unsigned queued=sc_audio_queued(music.output)/(2*sizeof(int16_t));
        if(queued>=2048) {SDL_Delay(2);continue;}
        ScMusicLock();
        uint64_t before=music.apu->portClock;
        /* Fast-forward may place commands ahead of the worker's wall clock.
         * Catch up in bounded slices, then keep only recent output. */
        uint64_t guest=atomic_load_explicit(&music.guest,memory_order_acquire);
        apu_runToGuestCycle(music.apu,guest,16384);
        if(dsp_available(music.apu->dsp)>2048) dsp_trimSamples(music.apu->dsp,512);
        unsigned got;
        if(restored.ready) {
            /* Keep PCM at 44.1 kHz; resample only the native SPC sound effects.
             * Retain the interpolation phase and one lookahead across blocks. */
            unsigned rate=(unsigned)music.output->freq;
            uint64_t end=output_phase+512u*32040u;
            unsigned consume=(unsigned)(end/rate),need=(unsigned)((output_phase+511u*32040u)/rate)+2;
            while(dsp_available(music.apu->dsp)<need) apu_cycle(music.apu);
            for(unsigned i=0;i<512;++i) {
                uint64_t position=output_phase+(uint64_t)i*32040;
                unsigned at=(unsigned)(position/rate),frac=(unsigned)(position%rate);
                int16_t al,ar,bl,br;dsp_peek(music.apu->dsp,at,&al,&ar);dsp_peek(music.apu->dsp,at+1,&bl,&br);
                samples[2*i]=(int16_t)(al+(int64_t)(bl-al)*frac/rate);
                samples[2*i+1]=(int16_t)(ar+(int64_t)(br-ar)*frac/rate);
            }
            dsp_advance(music.apu->dsp,consume);output_phase=end%rate;
            restored.rate=rate;ScPcmMix(&restored,samples,512);got=512;
        } else {
            while(dsp_available(music.apu->dsp)<512) apu_cycle(music.apu);
            got=dsp_getSamples(music.apu->dsp,samples,512);
        }
        music.stats.cycles+=music.apu->portClock-before;
        music.stats.samples+=got;++music.stats.blocks;
        for(unsigned i=0;i<got;++i) if(samples[2*i] || samples[2*i+1]) ++music.stats.nonzero_samples;
        if(sc_audio_queue(music.output,samples,got*2*sizeof(int16_t))) {
            ++music.stats.write_failures;ScMusicUnlock();SDL_Delay(5);
        } else {
            ScMusicUnlock();
        }
    }
    return 0;
}
bool ScMusicStart(Apu *apu,ScAudio *output) {
    if(ScMusicRunning() || !apu || !output || !sc_audio_opened(output)) return false;
    memset(&music.stats,0,sizeof music.stats);music.apu=apu;music.output=output;
    const char *profile=getenv("SC_MUSIC_PROFILE");profile_sync=profile && *profile=='1';
    sync_calls=sync_cycles=sync_ticks=sync_max_ticks=0;
    sync_lock_ticks=sync_lock_max_ticks=0;
    music.mutex=SDL_CreateMutex();if(!music.mutex) return false;
    atomic_store(&music.paused,false);atomic_store(&music.guest,0);atomic_store(&music.running,true);
    output_phase=0;
    music.thread=SDL_CreateThread(worker,"UrbanRecomp music",NULL);
    if(!music.thread) {
        atomic_store(&music.running,false);SDL_DestroyMutex(music.mutex);music.mutex=NULL;return false;
    }
    return true;
}
void ScMusicStop(void) {
    if(!ScMusicRunning()) {ScPcmDestroy(&restored);return;}
    atomic_store_explicit(&music.running,false,memory_order_release);
    SDL_WaitThread(music.thread,NULL);music.thread=NULL;
    if(profile_sync)fprintf(stderr,"[music sync total] calls=%llu cycles=%llu host-ms=%.3f max-ms=%.3f lock-ms=%.3f max-lock-ms=%.3f\n",
        (unsigned long long)sync_calls,(unsigned long long)sync_cycles,
        sync_ticks*1000.0/SDL_GetPerformanceFrequency(),sync_max_ticks*1000.0/SDL_GetPerformanceFrequency(),
        sync_lock_ticks*1000.0/SDL_GetPerformanceFrequency(),sync_lock_max_ticks*1000.0/SDL_GetPerformanceFrequency());
    SDL_DestroyMutex(music.mutex);music.mutex=NULL;music.apu=NULL;music.output=NULL;
    ScPcmDestroy(&restored);
}
