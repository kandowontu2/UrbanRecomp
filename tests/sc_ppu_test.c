#include "sc_ppu.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
Snes *g_snes;
unsigned char g_snesrecomp_last_hdmaen;
int snes_frame_counter;
void ScPpuReferenceLine(Ppu *ppu,int line);
static uint64_t clock_tick(void) { static uint64_t t;return ++t; }

static void equivalent(Ppu *a,Ppu *b) {
    assert(!memcmp(&a->inidisp,&b->inidisp,PPU_SAVESTATE_REGS_SIZE));
    assert(!memcmp(&a->cgram,&b->cgram,PPU_SAVESTATE_MEM_SIZE));
    assert(a->rangeOver==b->rangeOver && a->timeOver==b->timeOver);
    assert(a->evenFrame==b->evenFrame && a->lineHasSprites==b->lineHasSprites);
    assert(!memcmp(&a->objBuffer,&b->objBuffer,sizeof a->objBuffer));
    assert(!memcmp(a->brightnessMult,b->brightnessMult,sizeof a->brightnessMult));
    assert(!memcmp(a->brightnessMultHalf,b->brightnessMultHalf,sizeof a->brightnessMultHalf));
    assert(!memcmp(a->wsOamMotionGrace,b->wsOamMotionGrace,sizeof a->wsOamMotionGrace));
    assert(!memcmp(a->wsOamMotionX,b->wsOamMotionX,sizeof a->wsOamMotionX));
}
int main(void) {
    Ppu *full=ppu_init(),*skip=ppu_init();assert(full && skip);
    size_t bytes=kPpuBufWidth*4*240;
    uint8_t *images[2]={calloc(1,bytes),calloc(1,bytes)};assert(images[0] && images[1]);
    for(unsigned case_id=0;case_id<32;++case_id) {
        ppu_reset(full);full->inidisp=15;full->bgmode=case_id%2?1:0;
        full->screenEnabled[0]=0x1f;full->screenEnabled[1]=3;
        full->cgwsel=2;full->cgadsub=case_id%4?0x63:0;
        full->obsel=(case_id%8)<<5;full->oamaddl=case_id%2?1:0;
        full->setini=case_id%2?2:0;
        PpuSetExtraSpace(full,case_id%2?32:0);
        for(unsigned i=0;i<0x8000;++i) full->vram[i]=(uint16_t)(i*197+case_id*37);
        for(unsigned i=0;i<256;++i) full->cgram[i]=(i*313+case_id)&32767;
        for(unsigned i=0;i<128;++i) {
            full->oam[i*2]=(case_id%4?i*13:8)*256+(i%2?248:0);
            full->oam[i*2+1]=i|((i%4)<<12)|((i%3)<<14);
        }
        memset(full->highOam,case_id%2?255:0,sizeof full->highOam);
        *skip=*full;
        uint32_t flags=(case_id&4?kPpuRenderFlags_NewRenderer:0)|
            (case_id&8?kPpuRenderFlags_NoSpriteLimits:0);
        PpuBeginDrawing(full,images[0],kPpuBufWidth*4,flags);
        PpuBeginDrawing(skip,images[1],kPpuBufWidth*4,full->renderFlags);
        memset(images[0],0xa5,bytes);memset(images[1],0xa5,bytes);
        for(unsigned frame=0;frame<3;++frame) {
            for(int line=0;line<=224;++line) {
                if(line==71) full->inidisp=skip->inidisp=case_id%2?0x8f:7;
                if(line==121) full->inidisp=skip->inidisp=15;
                full->vram[17]=skip->vram[17]=(uint16_t)(line*37);
                ScPpuReferenceLine(full,line);
                ScPpuMeasurePixels(case_id&16?clock_tick:NULL,1);
                ScPpuSkipPixels(frame<2);ppu_runLine(skip,line);
                if(case_id&16) assert(ScPpuPixelMilliseconds()==(frame==2 && line>0?1:0));
                equivalent(full,skip);
            }
            if(frame<2) for(size_t i=0;i<bytes;++i) assert(images[1][i]==0xa5);
            else assert(!memcmp(images[0],images[1],bytes));
            ++snes_frame_counter;
        }
    }
    ScPpuSkipPixels(false);free(images[0]);free(images[1]);ppu_free(full);ppu_free(skip);
    puts("PASS: skipped PPU pixels retain flags, OAM, brightness and next displayed frame (32 cases)");
    return 0;
}
