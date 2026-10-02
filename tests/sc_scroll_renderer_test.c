#include "sc_renderer.h"
#include "snes/ppu.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static void word(uint8_t *data,unsigned at,unsigned v) {data[at]=v;data[at+1]=v>>8;}
static void frame(ScRenderer *r,Ppu *p,uint8_t *ram,const uint32_t *native) {
    for(int y=0;y<224;++y) ScRendererLine(r,p,ram,y,native);
}
int main(void) {
    Ppu *p=calloc(1,sizeof *p),*before=malloc(sizeof *p);
    ScRenderer *r=calloc(1,sizeof *r);ScWorld *world=calloc(1,sizeof *world);
    uint8_t *ram=calloc(1,0x20000),*rom=calloc(1,0x80000);
    assert(p && before && r && world && ram && rom);
    uint32_t native[256];for(int x=0;x<256;++x) native[x]=0xffff0000;
    for(unsigned size=0;size<4;++size) for(unsigned centered=0;centered<2;++centered)
    for(unsigned deferred=0;deferred<2;++deferred) for(int direction=-1;direction<=1;++direction) {
        memset(p,0,sizeof *p);memset(ram,0,0x20000);memset(rom,0,0x80000);memset(world,0,sizeof *world);
        world->active=size>0;world->huge=size>=2;world->giant=size==3;
        for(unsigned i=0;i<958;++i) word(rom,0x14f2d+2*i,0x2300);
        /* Green live terrain, blue roof, red stale native staging. */
        p->inidisp=15;p->bgmode=1;p->screenEnabled[0]=3;p->bgXsc[0]=0x40;p->bgXsc[1]=0x48;
        p->cgram[1]=31<<5;p->cgram[2]=31<<10;p->cgram[3]=31;p->cgram[193]=32767;
        for(int i=0;i<32;++i) p->brightnessMult[i]=(i<<3)|(i>>2);
        for(int row=0;row<8;++row) {p->vram[row]=0xff;p->vram[16+row]=0xff00;p->vram[32+row]=0xffff;}
        for(unsigned i=0;i<1024;++i) {p->vram[0x4000+i]=0x2300;p->vram[0x4800+i]=2;}
        unsigned cx=size==3?800:size==2?300:size==1?160:20,cy=size==3?700:size==2?270:size==1?120:20;
        word(ram,0x1bd,cx);word(ram,0x1bf,cy);word(ram,0x1d7,65535);ram[0x3e]=1;
        p->hScroll[1]=cx*8+6;p->vScroll[1]=cy*8;
        unsigned width=size?ScWorldWidth(world):120;
        uint8_t *map=size?world->tiles:ram+0x10200;
        word(map,2*((cy+13)*width+cx+14),1);word(rom,0x14f2d+2,1);
        ScRendererInit(r,rom,0x80000,true);r->world=size?world:NULL;
        ScViewport view=centered?(ScViewport){684,300,214,38,1,0}:(ScViewport){448,224,0,0,1,0};
        assert(ScRendererResize(r,view));assert(ScRendererDeferTerrain(r,deferred!=0));
        frame(r,p,ram,native);assert(!r->scroll_repair);
        /* A reversed fine step or Ctrl-size step starts checking interior
         * cells, not just the native eight-pixel overscan edges. */
        p->hScroll[1]=(uint16_t)(p->hScroll[1]+(direction==0?12:direction*4));
        memcpy(before,p,sizeof *p);frame(r,p,ram,native);
        assert(r->scroll_repair && r->staged_mismatches>0 && !memcmp(before,p,sizeof *p));
        assert(ScRendererPixel(r,view.core_x+80,view.core_y+96)==0xff00ff00);
        int roof_x=13*8-((r->scroll_x+r->scroll_adjust_x)-(int)cx*8)+1;
        assert(ScRendererPixel(r,view.core_x+roof_x,view.core_y+96)==0xff0000ff);
        assert(ScRendererPixel(r,view.core_x+24,view.core_y+96)==0xffff0000); /* toolbar */
        /* A wrong cached roof must not win over live terrain. */
        for(unsigned i=0;i<1024;++i) p->vram[0x4000+i]=2;
        frame(r,p,ram,native);
        assert(ScRendererPixel(r,view.core_x+80,view.core_y+96)==0xff00ff00);
        /* Evaluated high-priority objects retain ownership over the repair. */
        p->screenEnabled[0]=19;p->objBuffer.data[kPpuExtraLeftRight+120]=0xe0c1;
        frame(r,p,ram,native);
        assert(ScRendererPixel(r,view.core_x+120,view.core_y+96)==0xffffffff);
        p->objBuffer.data[kPpuExtraLeftRight+120]=0;p->screenEnabled[0]=3;
        /* Paused menus keep the reference native image; validation resumes
         * on returning to the city, without touching guest state. */
        word(ram,0x379,1);frame(r,p,ram,native);
        assert(ScRendererPixel(r,view.core_x+80,view.core_y+96)==0xffff0000 && r->scroll_repair);
        word(ram,0x379,0);frame(r,p,ram,native);
        assert(ScRendererPixel(r,view.core_x+80,view.core_y+96)==0xff00ff00);
        /* Once both staged maps agree, the native core takes over again. */
        word(rom,0x14f2d+2,0x2300);
        for(unsigned i=0;i<1024;++i) {p->vram[0x4000+i]=0x2300;p->vram[0x4800+i]=0;}
        frame(r,p,ram,native);assert(!r->scroll_repair && r->staged_mismatches==0);
        frame(r,p,ram,native);
        assert(ScRendererPixel(r,view.core_x+80,view.core_y+96)==0xffff0000);
        ScRendererDestroy(r);
    }
    free(p);free(before);free(r);free(world);free(ram);free(rom);
    puts("PASS: 48 scroll cases, Normal/Big/Huge/960x800, fine reversal/Ctrl, stale interiors/roofs, UI/OBJ, CPU/deferred and cache recovery");
    return 0;
}

