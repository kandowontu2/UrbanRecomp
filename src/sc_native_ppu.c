#include "sc_native_ppu.h"
#include "snes/ppu.h"
#include <stddef.h>

bool ScNativePpuSupported(const Ppu *p) {
    if(!p || PPU_mode(p)!=1 || (p->bgmode&0x70) || PPU_forcedBlank(p) ||
       PPU_interlace(p) || p->frameInterlace || PPU_pseudoHires(p) ||
       (PPU_mosaicSize(p)>1 && (p->mosaic&7))) return false;
    /* Host layer extraction/scaling has its own CPU renderer contract. */
    if(p->wsLayerMirror || p->wsLayerRepeat || p->widescreenLineEnhancer ||
       p->wsHudSplitHeight || p->wsHudOamSlots || p->wsHudOamSlots2) return false;
    for(unsigned layer=0;layer<3;++layer) {
        if(p->overlayRenderBuffer[layer] || p->wsStretchY1[layer] || p->wsRepeatY1[layer]) return false;
        for(unsigned band=0;band<kPpuWsAnchorBands;++band)
            if(p->wsAnchorY1[band][layer]) return false;
    }
    return !p->overlayRenderBuffer[kPpuOverlaySource_Obj];
}
void ScNativePpuCapture(ScTerrainFrame *f,const Ppu *p,unsigned y,unsigned line) {
    ScNativeRow *row=f->native+y;row->flags=(PPU_bg3priority(p)?1:0)|
        (p->renderFlags&kPpuRenderFlags_NewRenderer?2:0);
    for(unsigned layer=0;layer<3;++layer) {
        unsigned phase=p->hScroll[layer]&7,sy=(line+p->vScroll[layer])&1023;
        row->phase[layer]=phase;
        for(unsigned i=0;i<33;++i) {
            unsigned sx=(p->hScroll[layer]-phase+i*8)&1023;
            unsigned map=PPU_bgTilemapAdr(p,layer)+((sy>>3)&31)*32+((sx>>3)&31);
            if((sx&256) && PPU_bgTilemapWider(p,layer)) map+=0x400;
            if((sy&256) && PPU_bgTilemapHigher(p,layer))
                map+=PPU_bgTilemapWider(p,layer)?0x800:0x400;
            unsigned word=p->vram[map&0x7fff],depth=layer==2?2:4;
            unsigned dy=(word&0x8000)?7-(sy&7):sy&7;
            unsigned at=(PPU_bgTileAdr(p,layer)+(word&1023)*(depth*4)+dy)&0x7fff;
            row->tiles[layer][i]=(ScNativeTile){p->vram[at]|
                (depth==4?(uint32_t)p->vram[(at+8)&0x7fff]<<16:0),word};
        }
    }
    ScTerrainRow *state=f->rows+y;
    state->main=p->screenEnabled[0];state->sub=p->screenEnabled[1];
    state->window_main=p->screenWindowed[0];state->window_sub=p->screenWindowed[1];
    state->windows=p->windowsel;state->logic=p->wbgobjlog;
    state->bounds=p->window1left|(uint32_t)p->window1right<<8|(uint32_t)p->window2left<<16|(uint32_t)p->window2right<<24;
    state->math=p->cgadsub;state->control=p->cgwsel;state->fixed=p->fixedColor;
    for(unsigned i=0;i<32;++i) state->brightness[i]=p->brightnessMult[i];
    for(unsigned i=0;i<256;++i) {
        f->palette[(size_t)y*256+i]=p->cgram[i];
        unsigned obj=p->objBuffer.data[i+kPpuExtraLeftRight];
        f->overlays[(size_t)y*f->width+state->core_x+i].object=(obj&255)|((obj>>12)<<8);
    }
}
