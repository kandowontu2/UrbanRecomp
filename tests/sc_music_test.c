#include "sc_music.h"
#include "snes/apu.h"
#include "snes/spc.h"
#include "snes/cpu.h"
#include "snes/dsp.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

uint64_t g_apu_timer0_total_ticks;
int snes_frame_counter;
void RtlApuLock(void) {ScMusicLock();}
void RtlApuUnlock(void) {ScMusicUnlock();}
static FILE *fixture;
static void fixture_read(SaveLoadInfo *sli,void *data,size_t size) {
    (void)sli;assert(fread(data,1,size,fixture)==size);
}
static void load_apu_fixture(Apu *apu,const char *path) {
    fixture=fopen(path,"rb");assert(fixture);
    uint32_t header[4];assert(fread(header,1,sizeof header,fixture)==sizeof header);
    assert(header[0]==0x54534353 && header[1]==4);
    assert(fseek(fixture,sizeof header+sizeof(Cpu),SEEK_SET)==0);
    SaveLoadInfo sli={fixture_read};apu_saveload(apu,&sli);fclose(fixture);
    apu_clearPortQueue(apu);dsp_trimSamples(apu->dsp,0);
}
int main(int argc,char **argv) {
#if SNESRECOMP_SDL3
    assert(SDL_Init(SDL_INIT_AUDIO));
#else
    assert(SDL_Init(SDL_INIT_AUDIO)==0);
#endif
    ScAudio output={0};assert(sc_audio_open(&output,32040,2,1024));
    Apu *apu=apu_init();assert(apu);apu_reset(apu);
    apu->romReadable=false;apu->spc->pc=0x200;
    apu->ram[0x200]=0x2f;apu->ram[0x201]=0xfe; /* BRA: stable synthetic driver. */
    assert(ScMusicStart(apu,&output));SDL_Delay(100);
    ScMusicStats before=ScMusicGetStats();SDL_Delay(250);ScMusicStats after=ScMusicGetStats();
    /* No simulated game frames during this stall. Synthesis and playback
     * must continue on the dedicated thread at the native sample cadence. */
    assert(after.samples-before.samples>=6000 && after.samples-before.samples<=10000);
    assert(after.cycles-before.cycles>=6000*32 && !after.write_failures);
    assert(sc_audio_queued(&output)/(2*sizeof(int16_t))<4096);
    ScMusicGuestFrame(123456,true);SDL_Delay(20);before=ScMusicGetStats();SDL_Delay(100);
    after=ScMusicGetStats();assert(after.samples==before.samples && after.cycles==before.cycles);
    ScMusicWrite(0,0x12,100);ScMusicWrite(0,0x34,110);ScMusicWrite(1,0x56,110);
    ScMusicLock();ScMusicSyncLocked(200);assert(apu->inPorts[0]==0x34 && apu->inPorts[1]==0x56);
    ScMusicResetLocked();assert(!apu_portQueueDepth(apu));ScMusicUnlock();
    ScMusicGuestFrame(0,false);SDL_Delay(100);after=ScMusicGetStats();assert(after.samples>before.samples);
    ScMusicStop();assert(!ScMusicRunning());
    assert(ScMusicStart(apu,&output));SDL_Delay(20);ScMusicStop();
    if(argc>1) {
        sc_audio_close(&output);assert(sc_audio_open(&output,44100,2,1024));
        assert(ScMusicLoadRestored(argv[1]));assert(ScMusicStart(apu,&output));
        ScMusicGuestFrame(0,true);SDL_Delay(20);
        for(unsigned n=1;n<20;++n) assert(ScMusicCommand((uint8_t)n)==0);
        assert(ScMusicCommand(20)==20);assert(ScMusicCommand(7)==0);
        ScMusicGuestFrame(0,false);SDL_Delay(100);before=ScMusicGetStats();SDL_Delay(250);after=ScMusicGetStats();
        assert(after.samples-before.samples>=9000 && after.samples-before.samples<=13000);
        assert(after.nonzero_samples-before.nonzero_samples>5000 && !after.write_failures);
        ScMusicGuestFrame(0,true);SDL_Delay(20);before=ScMusicGetStats();SDL_Delay(100);after=ScMusicGetStats();
        assert(after.samples==before.samples);
        ScMusicEnabled(false);ScMusicGuestFrame(0,false);SDL_Delay(100);
        before=ScMusicGetStats();SDL_Delay(100);after=ScMusicGetStats();
        assert(after.samples>before.samples && after.nonzero_samples==before.nonzero_samples);
        ScMusicEnabled(true);SDL_Delay(100);after=ScMusicGetStats();assert(after.nonzero_samples>before.nonzero_samples);
        ScMusicLock();ScMusicResetLocked();ScMusicRestoreLocked(3,true);ScMusicUnlock();
        SDL_Delay(100);ScMusicStop();
        puts("PASS: restored 44.1 kHz worker, all 19 selections, independent 250ms stall, PCM pause/mute/resume and state restore");
    }
    if(argc>2) {
        load_apu_fixture(apu,argv[2]);assert(ScMusicLoadRestored(argv[1]));
        assert(ScMusicStart(apu,&output));ScMusicGuestFrame(0,true);SDL_Delay(20);
        ScMusicLock();ScMusicRestoreLocked(3,true);
        int16_t samples[1024*2];int peak=0;
        for(unsigned i=0;i<32040*32*2;++i) {
            apu_cycle(apu);
            if(i%16384==0 && i<63000*32) dsp_trimSamples(apu->dsp,0);
        }
        unsigned got=dsp_getSamples(apu->dsp,samples,1024);
        for(unsigned i=0;i<got*2;++i)if(abs(samples[i])>peak)peak=abs(samples[i]);
        assert(peak<=2); /* original soundtrack stopped; residual echo DC only */
        apu_writePortNow(apu,2,1);peak=0;dsp_trimSamples(apu->dsp,0);
        for(unsigned i=0;i<32040*32/2;++i) {
            apu_cycle(apu);
            if(i%16384==16383) {
                got=dsp_getSamples(apu->dsp,samples,1024);
                for(unsigned j=0;j<got*2;++j)if(abs(samples[j])>peak)peak=abs(samples[j]);
            }
        }
        printf("RESTORE sound-effect peak=%d\n",peak);assert(peak>100);
        ScMusicUnlock();ScMusicStop();
        puts("PASS: real US-driver save restore stops original BGM and retains audible native sound effects");
    }
    apu_free(apu);sc_audio_close(&output);SDL_Quit();
    puts("PASS: independent SPC/DSP playback through 250ms game-thread stall, ordered ports, pause/reset/restart and clean shutdown");
    return 0;
}
