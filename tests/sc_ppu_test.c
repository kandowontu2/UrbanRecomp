#include "sc_ppu.h"
#include "sc_native_ppu.h"
#include "sc_obj.h"
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
    for(int x=-kPpuExtraLeftRight;x<256+kPpuExtraLeftRight;++x)
        assert(a->objBuffer.data[x+kPpuExtraLeftRight]==ScObjPixel(b,x));
    assert(!memcmp(a->brightnessMult,b->brightnessMult,sizeof a->brightnessMult));
    assert(!memcmp(a->brightnessMultHalf,b->brightnessMultHalf,sizeof a->brightnessMultHalf));
    assert(!memcmp(a->wsOamMotionGrace,b->wsOamMotionGrace,sizeof a->wsOamMotionGrace));
    assert(!memcmp(a->wsOamMotionX,b->wsOamMotionX,sizeof a->wsOamMotionX));
}
static void native_pixels(Ppu *full,Ppu *skip,uint8_t *image,uint8_t *scratch) {
    ScTerrainFrame captured={0},raw={0};assert(ScTerrainResize(&captured,256,224));
    assert(ScTerrainResize(&raw,256,224));
    /* A descriptor capture without a complete VRAM version must retain the
     * reference behavior, never point the shader at missing memory. */
    ppu_reset(skip);ScNativePpuCaptureRaw(&raw,skip,0,1);
    assert(!(raw.native[0].flags&SC_NATIVE_RAW_BG));
    raw.resource_capacity=SC_RESOURCE_VRAM+224*16384;raw.snapshots=224;
    raw.resources=calloc(raw.resource_capacity,sizeof *raw.resources);assert(raw.resources);
    for(unsigned id=0;id<256;++id) {
        ppu_reset(full);PpuSetExtraSpace(full,0);
        full->inidisp=id%16;full->bgmode=1|(id&1?8:0);
        full->bgTileAdr=0x321;full->bgXsc[0]=0x40|(id%4);
        full->bgXsc[1]=0x50|((id/4)%4);full->bgXsc[2]=0x60|((id/16)%4);
        full->screenEnabled[0]=id%32;full->screenEnabled[1]=(id*13)%32;
        full->screenWindowed[0]=(id*7)%32;full->screenWindowed[1]=(id*19)%32;
        full->cgadsub=id;full->cgwsel=(id*61)&0xfe;full->fixedColor=(id*197)&32767;
        full->windowsel=(id*0x1b2935)&0xffffff;full->wbgobjlog=id*157;
        full->window1left=id%128;full->window1right=255-id%64;
        full->window2left=255-id%192;full->window2right=id%128;
        full->obsel=(id%8)<<5;full->oamaddh=id&8?0x80:0;full->oamaddl=(id*7)&255;
        for(unsigned i=0;i<0x8000;++i) full->vram[i]=(uint16_t)(i*197+(i>>3)*719+id*37);
        for(unsigned i=0;i<256;++i) full->cgram[i]=(i*313+id*139)&32767;
        for(unsigned i=0;i<128;++i) {
            full->oam[i*2]=((id%16==3 || id%16==5)?80:(i*13+id)%224)*256+((i*17+id)%256);
            full->oam[i*2+1]=(i*11%256)|((i%8)<<9)|((i%4)<<12)|((i%4)<<14);
        }
        memset(full->highOam,id%2?0xaa:0,sizeof full->highOam);
        ppu_reset(skip);*skip=*full;
        uint32_t flags=(id&2?kPpuRenderFlags_NewRenderer:0)|(id&4?kPpuRenderFlags_NoSpriteLimits:0);
        PpuBeginDrawing(full,image,kPpuBufWidth*4,flags);PpuBeginDrawing(skip,scratch,kPpuBufWidth*4,flags);
        for(unsigned line=0;line<=224;++line) {
            for(unsigned layer=0;layer<3;++layer) {
                full->hScroll[layer]=skip->hScroll[layer]=(id*17+line*3+layer*251)&1023;
                full->vScroll[layer]=skip->vScroll[layer]=(id*31+line*5+layer*193)&1023;
            }
            full->inidisp=skip->inidisp=(id+line/32)%16;
            full->vram[0x1000+(line%2048)]=skip->vram[0x1000+(line%2048)]=(uint16_t)(line*719+id);
            full->cgram[line%256]=skip->cgram[line%256]=(line*193+id*139)&32767;
            if(line==50) full->oam[0]=skip->oam[0]=(uint16_t)(0xff00|(id*19&255));
            if(line==96) full->highOam[0]=skip->highOam[0]=(uint8_t)(id*197);
            if(line==127) full->obsel=skip->obsel=(uint8_t)(((id+3)%8)<<5);
            if(line==160) {
                full->oamaddh=skip->oamaddh=0x80;full->oamaddl=skip->oamaddl=(id*29)&255;
            }
            ScPpuReferenceLine(full,line);ScPpuSkipPixels(true);ScPpuDeferObjects(true);ppu_runLine(skip,line);
            equivalent(full,skip);
            assert(ScNativePpuSupported(skip));
            if(!line) continue;
            ScNativePpuCapture(&captured,full,line-1,line);
            raw.rows[line-1].chr_snapshot=SC_RESOURCE_VRAM+(line-1)*16384;
            memcpy(raw.resources+SC_RESOURCE_VRAM+(line-1)*16384,skip->vram,65536);
            ScNativePpuCaptureRaw(&raw,skip,line-1,line);
            assert(raw.native[line-1].flags&SC_NATIVE_RAW_BG);
            unsigned obj_count=0;assert(ScObjSnapshot(skip,NULL,&obj_count));
            assert(!!(raw.native[line-1].flags&SC_NATIVE_RAW_OBJ)==(obj_count<=SC_OBJ_GPU_SLIVERS));
            if(obj_count<=SC_OBJ_GPU_SLIVERS) for(unsigned x=0;x<256;++x) {
                unsigned expected_obj=full->objBuffer.data[x+kPpuExtraLeftRight];
                assert(ScTerrainNativeObject(&raw,line-1,x)==((expected_obj&255)|((expected_obj>>12)<<8)));
            }
            const uint32_t *expected=(const uint32_t *)(image+(line-1)*kPpuBufWidth*4);
            for(unsigned x=0;x<256;++x) {
                uint32_t actual=ScNativePixel(&captured,x,line-1);
                if((actual&0xffffff)!=(expected[x]&0xffffff)) {
                    fprintf(stderr,"native pixel case=%u flags=%u line=%u x=%u actual=%08x expected=%08x\n",id,flags,line,x,actual,expected[x]);
                    fprintf(stderr,"main/sub buffers=%04x/%04x math=%02x control=%02x win=%x/%x screens=%x/%x\n",full->bgBuffers[0].data[x+kPpuExtraLeftRight],full->bgBuffers[1].data[x+kPpuExtraLeftRight],full->cgadsub,full->cgwsel,full->screenWindowed[0],full->screenWindowed[1],full->screenEnabled[0],full->screenEnabled[1]);
                    fprintf(stderr,"obj=%04x/%04x windows=%x logic=%x bounds=%u,%u,%u,%u fixed=%x\n",full->objBuffer.data[x+kPpuExtraLeftRight],skip->objBuffer.data[x+kPpuExtraLeftRight],full->windowsel,full->wbgobjlog,full->window1left,full->window1right,full->window2left,full->window2right,full->fixedColor);
                    for(unsigned l=0;l<3;++l) {unsigned px=x+captured.native[line-1].phase[l];ScNativeTile t=captured.native[line-1].tiles[l][px/8];fprintf(stderr,"layer%u phase=%u planes=%08x word=%04x\n",l,captured.native[line-1].phase[l],t.planes,t.attributes);}
                    abort();
                }
            }
        }
        /* Compare only after all live VRAM, palette and scroll mutations.
         * Both frames must still reconstruct the original scanlines. The
         * decoded frame above was independently checked against both PPUs. */
        memset(skip->vram,0,sizeof skip->vram);
        for(unsigned y=0;y<224;++y) {
            for(unsigned layer=0;layer<3;++layer) for(unsigned tile=0;tile<33;++tile) {
                ScNativeTile actual=ScTerrainNativeTile(&raw,y,layer,tile);
                assert(!memcmp(&actual,&captured.native[y].tiles[layer][tile],sizeof actual));
            }
            for(unsigned x=0;x<256;++x)
                assert(ScNativePixel(&raw,x,y)==ScNativePixel(&captured,x,y));
        }
        ++snes_frame_counter;
    }
    skip->bgmode=0;assert(!ScNativePpuSupported(skip));skip->bgmode=0x11;assert(!ScNativePpuSupported(skip));
    skip->bgmode=1;skip->mosaic=0x11;assert(!ScNativePpuSupported(skip));skip->mosaic=0;
    skip->setini=8;assert(!ScNativePpuSupported(skip));skip->setini=1;assert(!ScNativePpuSupported(skip));
    skip->setini=0;skip->inidisp=0x80;assert(!ScNativePpuSupported(skip));
    ScTerrainDestroy(&captured);ScTerrainDestroy(&raw);ScPpuSkipPixels(false);ScPpuDeferObjects(false);
    puts("PASS: 256 complete native Mode 1 frames match both PPU renderers, live VRAM/palettes, phase/wrap/flip, OAM, windows, colour math and brightness");
}
int main(void) {
    Ppu *full=ppu_init(),*skip=ppu_init();assert(full && skip);
    /* Both byte ports and explicit host writes publish a render-only revision.
     * Unrelated registers leave it alone; guest VRAM/remap semantics match. */
    ppu_reset(full);full->vramPointer=0x1234;full->vramIncrementOnHigh=true;
    uint64_t revision=ScPpuVramRevision(full);
    ppu_write(full,0x18,0x5a);assert(full->vram[0x1234]==0x005a);
    assert(ScPpuVramRevision(full)==revision+1);
    ppu_write(full,0x19,0xa7);assert(full->vram[0x1234]==0xa75a);
    assert(ScPpuVramRevision(full)==revision+2);
    ppu_write(full,0,15);assert(ScPpuVramRevision(full)==revision+2);
    ScPpuVramChanged();assert(ScPpuVramRevision(full)==revision+3);
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
        ppu_reset(skip);*skip=*full;
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
                ScPpuSkipPixels(frame<2);ScPpuSkipObjects(frame<2);ppu_runLine(skip,line);
                if(case_id&16) assert(ScPpuPixelMilliseconds()==(frame==2 && line>0?1:0));
                equivalent(full,skip);
            }
            if(frame<2) for(size_t i=0;i<bytes;++i) assert(images[1][i]==0xa5);
            else assert(!memcmp(images[0],images[1],bytes));
            ++snes_frame_counter;
        }
    }
    ScPpuMeasurePixels(NULL,1);native_pixels(full,skip,images[0],images[1]);
    ScPpuSkipPixels(false);ScPpuSkipObjects(false);free(images[0]);free(images[1]);ppu_free(full);ppu_free(skip);
    puts("PASS: skipped PPU pixels retain flags, OAM, brightness and next displayed frame (32 cases)");
    return 0;
}
