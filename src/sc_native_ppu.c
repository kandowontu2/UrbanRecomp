#include "sc_native_ppu.h"
#include "sc_obj.h"
#include "snes/ppu.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

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
static void capture(ScTerrainFrame *f,const Ppu *p,unsigned y,unsigned line,bool raw) {
    ScNativeRow *row=f->native+y;row->flags=(PPU_bg3priority(p)?1:0)|
        (p->renderFlags&kPpuRenderFlags_NewRenderer?2:0)|(raw?SC_NATIVE_RAW_BG:0);
    const ScObjSliver *slivers=NULL;unsigned count=0;
    bool raw_obj=raw && ScObjSnapshot(p,&slivers,&count) && count<=SC_OBJ_GPU_SLIVERS;
    if(raw_obj) {
        static bool reported;
        static int diagnostic=-1;
        if(diagnostic<0) diagnostic=getenv("SC_GPU_NATIVE_OBJ_DIAG")!=NULL;
        if(diagnostic && !reported) {
            fprintf(stderr,"[native object] GPU sliver descriptors enabled, capacity %u\n",SC_OBJ_GPU_SLIVERS);
            reported=true;
        }
        row->flags|=SC_NATIVE_RAW_OBJ|(count<<8);
        for(unsigned i=0;i<count;++i) {
            unsigned layer=i/30,at=5+2*(i%30);
            row->layer[layer][at]=slivers[i].position;
            row->layer[layer][at+1]=slivers[i].source;
        }
    } else ScObjMaterialize(p);
    for(unsigned layer=0;layer<3;++layer) {
        unsigned phase=p->hScroll[layer]&7,sy=(line+p->vScroll[layer])&1023;
        row->phase[layer]=phase;
        if(raw) {
            row->layer[layer][0]=(p->hScroll[layer]-phase)&1023;
            row->layer[layer][1]=sy;
            row->layer[layer][2]=PPU_bgTilemapAdr(p,layer);
            row->layer[layer][3]=PPU_bgTileAdr(p,layer);
            row->layer[layer][4]=(PPU_bgTilemapWider(p,layer)?1:0)|
                (PPU_bgTilemapHigher(p,layer)?2:0);
            continue;
        }
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
        unsigned obj=raw_obj?0:ScObjPixel(p,i);
        f->overlays[(size_t)y*f->width+state->core_x+i].object=(obj&255)|((obj>>12)<<8);
    }
}
void ScNativePpuCapture(ScTerrainFrame *f,const Ppu *p,unsigned y,unsigned line) {
    capture(f,p,y,line,false);
}
void ScNativePpuCaptureRaw(ScTerrainFrame *f,const Ppu *p,unsigned y,unsigned line) {
    unsigned snapshot=f->rows[y].chr_snapshot;
    bool raw=f->resources && snapshot>=2048 && (snapshot-2048)%16384==0 &&
        (snapshot-2048)/16384<f->snapshots && snapshot<=f->resource_capacity &&
        f->resource_capacity-snapshot>=16384;
    capture(f,p,y,line,raw);
}
