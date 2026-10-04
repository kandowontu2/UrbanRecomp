#include "sc_renderer.h"
#include "snes/ppu.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void word(uint8_t *data,unsigned at,unsigned value) { data[at]=value; data[at+1]=value>>8; }
static void free_camera_test(void) {
    ScRenderer *r=calloc(1,sizeof *r);ScWorld *world=calloc(1,sizeof *world);
    assert(r && world);world->active=world->huge=world->giant=world->colossal=true;
    r->world=world;r->view=(ScViewport){730,492,0,0,1,0};r->zoom_hud=true;
    r->map_zoom=.25;r->scroll_x=r->native_scroll_x=2400;
    r->scroll_y=r->native_scroll_y=2400;
    for(unsigned n=0;n<8;++n) ScRendererPan(r,-.125,.375);
    assert(r->camera_x==-1 && r->camera_y==3 && r->scroll_x==2399 && r->scroll_y==2403);
    ScRendererPan(r,-191,-99);
    assert(r->scroll_x==2208 && r->scroll_y==2304 && r->map_zoom==.25);
    double x=r->camera_x,y=r->camera_y;
    for(unsigned n=0;n<1000;++n) ScRendererPan(r,0,0);
    assert(r->camera_x==x && r->camera_y==y);
    ScRendererPan(r,100000,100000);
    assert(r->scroll_x==12608 && r->scroll_y==11226); /* full-map viewport bounds */
    assert(r->native_scroll_x==2400 && r->native_scroll_y==2400 && r->map_zoom==.25);
    ScRendererPan(r,-100000,-100000);
    assert(r->scroll_x==-56 && r->scroll_y==-302); /* 64 screen pixels beyond top */
    ScRendererResetCamera(r);assert(!r->camera_x && !r->camera_y);
    assert(r->scroll_x==2400 && r->scroll_y==2400 && r->map_zoom==.25);
    free(r);free(world);
    puts("PASS: fractional host camera, no idle drift, preserved zoom/native camera and full 1920x1600 bounds");
}
int main(void) {
    free_camera_test();
    Ppu *p=calloc(1,sizeof(*p)), *before=malloc(sizeof(*p));
    uint8_t *ram=calloc(1,0x20000), *rom=calloc(1,0x80000);
    assert(p && before && ram && rom);
    for (int i=0;i<32;++i) p->brightnessMult[i]=(i<<3)|(i>>2);
    p->inidisp=15; p->bgmode=1; p->screenEnabled[0]=2;
    p->cgram[1]=31; p->cgram[2]=31<<5;
    /* One red pixel at tile (0,0), one green at (7,7). */
    p->vram[0]=0x0080; p->vram[7]=0x0100;
    for (int i=0;i<958;++i) word(rom,0x14f2d+i*2,0x300);
    static ScRenderer r; ScRendererInit(&r,rom,0x80000,true);
    assert(ScRendererMapPixel(&r,p,ram,0,0)==0xffff0000);
    assert(ScRendererMapPixel(&r,p,ram,7,7)==0xff00ff00);
    /* Live DMA-style updates to either plane pair must invalidate decoded
     * rows immediately. Palette changes must never reuse an old colour. */
    p->cgram[4]=31<<10; p->cgram[8]=31;
    p->vram[0]=0; p->vram[8]=0x0080;
    assert(ScRendererMapPixel(&r,p,ram,0,0)==0xff0000ff);
    p->vram[8]=0x8000;
    assert(ScRendererMapPixel(&r,p,ram,0,0)==0xffff0000);
    p->cgram[8]=31<<5;
    assert(ScRendererMapPixel(&r,p,ram,0,0)==0xff00ff00);
    p->vram[8]=0; p->vram[0]=0x0080;
    assert(ScRendererMapPixel(&r,p,ram,0,0)==0xffff0000);
    word(rom,0x156a9,0xc000);
    assert(ScRendererMapPixel(&r,p,ram,7,7)==0xffff0000);
    assert(ScRendererMapPixel(&r,p,ram,0,0)==0xff00ff00);
    assert(ScRendererMapPixel(&r,p,ram,-1,0)==0xff000000);
    assert(ScRendererMapPixel(&r,p,ram,960,800)==0xff000000);
    word(ram,0x10200,1023);
    assert(ScRendererMapPixel(&r,p,ram,0,0)==0xff000000);
    word(ram,0x10200,0);
    /* Only the southeast cell has a roof. Its upper-left pixel belongs
     * at (0,0), not (7,7), and uses city CHR even if BG1 has another base. */
    word(rom,0x156a9,0); word(rom,0x156a9+2,0);
    word(rom,0x14f2d+2,1);
    word(ram,0x10200+(120+1)*2,1);
    p->bgTileAdr=4;
    p->vram[16]=0x8000; /* green at roof tile (0,0) */
    assert(ScRendererMapPixel(&r,p,ram,0,0)==0xff00ff00);
    assert(ScRendererMapPixel(&r,p,ram,7,7)==0xff00ff00); /* base green */
    assert(ScRendererMapPixel(&r,p,ram,1,0)==0xff000000); /* transparent roof */
    ram[0x3e]=1;
    ScVideoSettings settings={.enabled=true,.aspect=SC_FIT,.centered=true};
    ScViewport v=ScVideoViewport(&settings,720,1280);
    assert(ScRendererResize(&r,v));
    uint32_t native[256];
    for (int x=0;x<256;++x) native[x]=(unsigned)x<<8;
    memcpy(before,p,sizeof(*p));
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(!memcmp(before,p,sizeof(*p))); /* renderer never mutates PPU */
    for (int y=0;y<224;++y)
        assert(!memcmp(r.pixels+(size_t)(y+v.core_y)*v.width+v.core_x,native,sizeof(native)));
    settings.aspect=SC_32_9; v=ScVideoViewport(&settings,3840,1080);
    assert(ScRendererResize(&r,v));
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(!memcmp(before,p,sizeof(*p)));
    assert(!ScRendererResize(&r,(ScViewport){8192,224,0,0,1,0}));
    /* A fax desk on BG3 uses a 16-column sheet, not the menu's guessed
     * eight-column repeat. Furniture in lower rows borrows the wood period. */
    memset(p,0,sizeof(*p)); memset(ram,0,0x20000);
    for (int i=0;i<32;++i) p->brightnessMult[i]=(i<<3)|(i>>2);
    p->inidisp=15; p->screenEnabled[0]=4; p->bgXsc[2]=0x50;
    p->cgram[65]=31; p->cgram[66]=31<<5;
    for (int y=0;y<32;++y) for (int x=0;x<32;++x)
        p->vram[0x5000+y*32+x]=0x20+(y%16)*16+x%16;
    for (int row=0;row<8;++row) {
        p->vram[0x20*8+row]=0x00ff;
        p->vram[0x28*8+row]=0xff00;
    }
    ram[0x14]=15;
    assert(ScRendererResize(&r,(ScViewport){512,224,0,0,1,0}));
    ScRendererLine(&r,p,ram,0,native);
    assert(r.wood_layer==2 && r.wood_period==16);
    assert(r.view.core_x==128);
    assert(r.pixels[384]==0xffff0000 && r.pixels[448]==0xff00ff00);
    memcpy(before,p,sizeof(*p));
    for (int y=1;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(!memcmp(before,p,sizeof(*p)));
    uint32_t *menu_pixels=malloc(512*224*sizeof *menu_pixels);assert(menu_pixels);
    memcpy(menu_pixels,r.pixels,512*224*sizeof *menu_pixels);
    r.map_zoom=.25;
    for(int y=0;y<224;++y)ScRendererLine(&r,p,ram,y,native);
    assert(!r.zoom_frame && !memcmp(menu_pixels,r.pixels,512*224*sizeof *menu_pixels));
    r.map_zoom=2;
    for(int y=0;y<224;++y)ScRendererLine(&r,p,ram,y,native);
    assert(!r.zoom_frame && !memcmp(menu_pixels,r.pixels,512*224*sizeof *menu_pixels));
    r.map_zoom=1;free(menu_pixels);
    /* $14 advances before the title finishes fading. Keep its scenery until
     * the hardware goes dark; use the current scanline's brightness. */
    memset(p,0,sizeof(*p)); p->inidisp=15; p->bgmode=1;
    p->screenEnabled[0]=1; p->bgXsc[0]=0x40;
    p->brightnessMult[31]=255; p->cgram[1]=31;
    for (int y=0;y<8;++y) p->vram[y]=0xff;
    ram[0x14]=1; ScRendererLine(&r,p,ram,0,native);
    assert(r.view.core_x==128 && r.pixels[384]==0xffff0000);
    ram[0x14]=2; p->inidisp=7; p->brightnessMult[31]=119;
    ScRendererLine(&r,p,ram,0,native);
    assert(r.pixels[384]==0xff770000);
    p->inidisp=0x8f; ScRendererLine(&r,p,ram,0,native);
    assert(r.pixels[384]==0xff000000 && !r.title_live);
    /* Statistics/tax pages: isolated panel/cursor rows cannot smear their
     * colour across the canvas. The majority already includes the fade. */
    ram[0x14]=0; p->bgmode=0; p->screenEnabled[0]=0; p->inidisp=7;
    for (int y=0;y<224;++y) {
        for (int x=0;x<256;++x) native[x]=(y>=41 && y<=53) ? 0xffbb8844 : 0xff224466;
        ScRendererLine(&r,p,ram,y,native);
    }
    assert(r.pixels[45*512+450]==0xff224466);
    assert(r.pixels[45*512+383]==0xffbb8844);
    /* The selector's offscreen card is real content, not another wood tile. */
    memset(p,0,sizeof(*p)); p->inidisp=15; p->screenEnabled[0]=1;
    p->bgXsc[0]=0x31; p->brightnessMult[31]=255; p->cgram[1]=31;
    p->vram[0x3400]=2;
    for (int y=0;y<8;++y) p->vram[2*8+y]=0xff;
    assert(ScRendererResize(&r,(ScViewport){800,224,0,0,1,0}));
    ram[0x14]=11; ScRendererLine(&r,p,ram,0,native);
    assert(r.pixels[272+256]==0xffff0000);
    assert(r.pixels[272+480]==0xff000000); /* no repeat of cards beyond strip */
    /* Repeating title lights are OAM, not background tiles. Ignore the
     * parked copy at raw X=257 when finding the visible row's pitch. */
    memset(p,0,sizeof(*p)); p->inidisp=15; p->bgmode=1;
    assert(ScRendererResize(&r,(ScViewport){512,224,0,0,1,0}));
    p->screenEnabled[0]=16; p->brightnessMult[31]=255; p->cgram[129]=31;
    for (int i=0;i<4;++i) p->oam[i*2]=(180<<8)|(1+i*64);
    for (int y=0;y<8;++y) p->vram[y]=0xff;
    p->oam[8]=(180<<8)|1; p->highOam[1]=1;
    ram[0x14]=1; ScRendererLine(&r,p,ram,0,native);
    /* OAM Y 180: row 0 on line 180, as the PPU draws it. */
    ScRendererLine(&r,p,ram,180,native);
    assert(r.light_pitch==64 && r.pixels[180*512+385]==0xffff0000);
    /* Fine scroll wraps before WRAM advances its coarse cell. The margin
     * must advance by two pixels, not jump backwards by six. */
    ram[0x14]=0; ram[0x3e]=1; ram[0x1bd]=10; ram[0x1bf]=10;
    p->screenEnabled[0]=2; p->hScroll[1]=86; p->vScroll[1]=80;
    ScRendererLine(&r,p,ram,0,native);
    assert(r.view.core_x==0 && r.scroll_x+r.scroll_adjust_x==86);
    p->hScroll[1]=88; ScRendererLine(&r,p,ram,0,native);
    assert(r.scroll_x+r.scroll_adjust_x==88);
    ram[0x1bd]=11; p->hScroll[1]=90; ScRendererLine(&r,p,ram,0,native);
    assert(r.scroll_x+r.scroll_adjust_x==90 && r.scroll_adjust_x==0);
    p->hScroll[1]=88; ScRendererLine(&r,p,ram,0,native);
    p->hScroll[1]=86; ScRendererLine(&r,p,ram,0,native);
    assert(r.scroll_x+r.scroll_adjust_x==86);
    /* A moving vehicle keeps its positive X across 255 and the classic
     * 352-pixel limit. A teleported parked HUD slot must remain hidden.
     * OAM Y 100: row 0 on line 100, as the PPU draws it. */
    p->screenEnabled[0]=18; p->oam[0]=(100<<8)|252; p->oam[1]=0x3800;
    p->cgram[193]=31; p->highOam[0]=0;
    ScRendererLine(&r,p,ram,0,native);
    for (int x=256;x<=360;x+=4) {
        p->oam[0]=(100<<8)|(x&255); p->highOam[0]=1;
        ScRendererLine(&r,p,ram,0,native);
        ScRendererLine(&r,p,ram,100,native);
        assert(r.pixels[100*512+x]==0xffff0000);
    }
    p->cgram[1]=31<<5; /* opaque terrain */
    for (int y=0;y<8;++y) p->vram[16+y]=0xff;
    p->oam[1]=0x0801; /* low-priority red object behind terrain */
    ScRendererLine(&r,p,ram,100,native);
    assert(r.pixels[100*512+360]==0xff00ff00);
    p->oam[1]=0x3801;
    ScRendererLine(&r,p,ram,100,native);
    assert(r.pixels[100*512+360]==0xffff0000);
    p->oam[0]=(100<<8)|128; p->highOam[0]=1;
    ScRendererLine(&r,p,ram,0,native);
    assert(!r.object_grace[0]);
    /* Large legitimate edits must never be mistaken for a city load. */
    for (int i=0;i<5000;++i) word(ram,0x10200+i*2,7);
    ScRendererLine(&r,p,ram,0,native);
    assert(!r.map_hold && r.changed_cells[4999]);
    /* In-game load writes a new map before the old city's fade ends. Keep
     * the prior map and palette until dark -> lit, then release together. */
    ScRendererLine(&r,p,ram,0,native);
    unsigned old_palette=r.held_ppu->cgram[1];
    ScRendererBeginMapLoad(&r);
    for (int i=0;i<5000;++i) word(ram,0x10200+i*2,8);
    p->cgram[1]=123; ram[0x1bd]=20;
    ScRendererLine(&r,p,ram,0,native);
    assert(r.map_hold && r.held_ppu->cgram[1]==old_palette);
    p->inidisp=0x8f; ScRendererLine(&r,p,ram,0,native);
    assert(r.map_hold && r.map_dark && r.pixels[256]==0xff000000);
    p->inidisp=15; ScRendererLine(&r,p,ram,0,native);
    assert(!r.map_hold && r.held_ppu->cgram[1]==123);
    /* Wrapped guest edge tiles are replaced by world terrain; an opaque HUD
     * tile protects the whole edge band on its row. The interior stays exact. */
    memset(p,0,sizeof(*p)); memset(ram,0,0x20000);
    p->inidisp=15; p->bgmode=1; p->screenEnabled[0]=2;
    p->brightnessMult[31]=255; p->cgram[1]=31<<5;
    ram[0x3e]=1; ram[0x1bd]=10; ram[0x1bf]=10;
    for (int y=0;y<8;++y) p->vram[y]=0xff;
    for (int x=0;x<256;++x) native[x]=0xffff0000;
    ScRendererResetHistory(&r); ScRendererLine(&r,p,ram,0,native);
    assert(r.repaired_edges[0]==3);
    assert(r.pixels[0]==0xff00ff00 && r.pixels[255]==0xff00ff00);
    assert(r.pixels[8]==0xffff0000 && r.pixels[247]==0xffff0000);
    p->screenEnabled[0]=6; p->bgTileAdr=0x100; p->bgXsc[2]=0x60;
    p->vram[0x6000]=1;
    for (int y=0;y<8;++y) p->vram[0x1008+y]=0xff;
    ScRendererLine(&r,p,ram,0,native);
    assert(r.repaired_edges[0]==2 && r.pixels[0]==0xffff0000);
    /* Committed terrain appears in the native core on the first new frame,
     * even while native VRAM still contains old tiles. Roofs touch the cell
     * northwest of their owner. HUD/OBJ and unedited native pixels survive. */
    p->screenEnabled[0]=2; p->bgTileAdr=0;
    p->cgram[2]=31<<10;
    word(rom,0x156a9+2,1); word(rom,0x14f2d+2,1);
    for (int y=0;y<8;++y) p->vram[16+y]=0xff00;
    word(ram,0x10200+2*(22*120+26),1);
    memcpy(before,p,sizeof(*p));
    ScRendererLine(&r,p,ram,0,native);
    ScRendererLine(&r,p,ram,96,native);
    assert(r.pixels[96*512+128]==0xff0000ff);
    assert(r.pixels[96*512+144]==0xffff0000);
    ScRendererLine(&r,p,ram,88,native);
    assert(r.pixels[88*512+120]==0xff0000ff);
    assert(!memcmp(before,p,sizeof(*p)));
    p->screenEnabled[0]=18;
    p->cgram[193]=31;
    p->objBuffer.data[kPpuExtraLeftRight+128]=0x20c1;
    ScRendererLine(&r,p,ram,96,native);
    assert(r.pixels[96*512+128]==0xff0000ff); /* low OBJ remains behind city */
    p->objBuffer.data[kPpuExtraLeftRight+128]=0xe0c1;
    ScRendererLine(&r,p,ram,96,native);
    assert(r.pixels[96*512+128]==0xffff0000);
    p->screenEnabled[0]=3; p->bgTileAdr=2; p->bgXsc[0]=0x40;
    p->cgram[1]=31;
    p->vram[0x4000+12*32+16]=1;
    for (int y=0;y<8;++y) p->vram[0x2010+y]=0xff;
    ScRendererLine(&r,p,ram,96,native);
    assert(r.pixels[96*512+128]==0xffff0000);
    p->screenEnabled[0]=2;
    p->cgram[1]=31<<5;
    word(ram,0x10200+2*(22*120+26),0);
    ScRendererLine(&r,p,ram,0,native);
    ScRendererLine(&r,p,ram,96,native);
    assert(r.pixels[96*512+128]==0xff00ff00);
    /* Center only the advisor's opaque BG3/OBJ pixels. The dimmed BG1 HUD
     * stays left, transparent page pixels reveal the stationary city, and
     * even black OBJ pixels remain opaque. Exercise both axes at once. */
    memset(p,0,sizeof(*p)); memset(ram,0,0x20000);
    p->inidisp=15; p->bgmode=1; p->screenEnabled[0]=20; p->screenEnabled[1]=3;
    p->cgwsel=2; p->cgadsub=0x60; p->bgTileAdr=0x321;
    /* Both city layers were hidden under the original page. Moving that
     * page must reveal the background, without moving/mutating the window. */
    p->screenWindowed[1]=3; p->windowsel=0x22;
    p->window1left=24; p->window1right=247;
    p->bgXsc[0]=0x40; p->bgXsc[1]=0x48; p->bgXsc[2]=0x50;
    p->cgram[1]=31<<5; p->cgram[2]=31; p->cgram[3]=31<<10;
    for (int i=0;i<32;++i) p->brightnessMult[i]=(i<<3)|(i>>2);
    for (int y=0;y<8;++y) {
        p->vram[0x2000+y]=0xff;
        p->vram[0x1010+y]=0xff00;
        p->vram[0x3008+y]=0xdfdf; /* transparent black glyph at x=42 */
    }
    for (int y=0;y<32;++y) {
        p->vram[0x4000+y*32+2]=1;
        p->vram[0x5000+y*32+5]=1;
    }
    p->objBuffer.data[kPpuExtraLeftRight+64]=0x2080;
    ram[0x3e]=1; ram[0x1bd]=10; ram[0x1bf]=10;
    for (int x=0;x<256;++x) native[x]=x==42 ? 0 : (x>=40 && x<48) ? 0x000000ff :
        x==64 ? 0 : (x>=16 && x<24) ? 0x007b0000 : 0x00007b00;
    ScRendererResetHistory(&r);
    assert(ScRendererResize(&r,(ScViewport){684,448,0,0,1,0}));
    memcpy(before,p,sizeof(*p));
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.advisor_frame && r.view.core_x==0 && r.view.core_y==0);
    assert(!memcmp(before,p,sizeof(*p)));
    {
        int gx,gy;bool navigation;
        ScVideoRect d={20,40,1368,896};
        assert(ScRendererWindowToGuest(&r,d,704,488,1408,976,
            10+214+84,20+112+140,&gx,&gy,&navigation));
        assert(gx==84 && gy==140 && !navigation);
        assert(!ScRendererWindowToGuest(&r,d,704,488,1408,976,
            10+213,20+112+140,&gx,&gy,&navigation));
        assert(ScRendererWindowToGuest(&r,d,704,488,1408,976,
            10+214+255,20+112+223,&gx,&gy,&navigation));
        assert(gx==255 && gy==223);
    }
    assert(r.pixels[20*684+16]==0xff7b0000); /* HUD did not move */
    assert(r.pixels[20*684+40]==0xff007b00); /* no old page */
    assert(r.pixels[(112+20)*684+214+40]==0xff0000ff);
    assert(r.pixels[(112+20)*684+214+42]==0xff000000); /* black page lettering */
    assert(r.pixels[(112+20)*684+214+64]==0xff000000);
    assert(!r.advisor_pixels[20*256+50]); /* transparent page background */
    assert(r.pixels[(112+20)*684+214+50]==0xff007b00);
    p->inidisp=0x8f;
    for (int x=0;x<256;++x) native[x]=0;
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[(112+20)*684+214+40]==0xff000000);
    p->inidisp=15; p->screenEnabled[0]=23; p->screenEnabled[1]=4;
    ScRendererLine(&r,p,ram,0,native);
    assert(!r.advisor_frame && r.view.core_x==0); /* returning to the city */
    ram[0x14]=1;
    ScRendererLine(&r,p,ram,0,native);
    assert(r.view.core_x==214 && r.view.core_y==112); /* title, both axes */
    /* Wide-city status groups move independently of terrain. Input must
     * follow their new position through window scaling/DPI. City terrain
     * accepts the entire canvas, while standalone menus keep native bounds. */
    memset(p,0,sizeof(*p)); memset(ram,0,0x20000);
    p->inidisp=15; p->bgmode=1; p->screenEnabled[0]=23; p->screenEnabled[1]=4;
    for (int i=0;i<32;++i) p->brightnessMult[i]=(i<<3)|(i>>2);
    p->cgram[129]=31; p->cgram[145]=31<<5;
    ram[0x3e]=2; word(ram,0x1d7,65535);
    word(ram,0x1bd,20); word(ram,0x1bf,30);
    /* Native date sprite and financial sprite share an opaque 8px tile. */
    for (int y=0;y<8;++y) p->vram[y]=0xff;
    p->oam[22]=(12<<8)|17; p->oam[23]=0x3000;
    p->oam[38]=(22<<8)|147; p->oam[39]=0x3200;
    p->oam[80]=(46<<8)|190; p->oam[81]=0x3166;
    for (int x=0;x<256;++x) native[x]=0xff0000ff;
    assert(ScRendererResize(&r,(ScViewport){448,224,0,0,1,0}));
    ScRendererResetHistory(&r); memcpy(before,p,sizeof *p);
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.split_hud && r.pan_frame && !memcmp(before,p,sizeof *p));
    assert(r.pixels[12*448+17]==0xffff0000);
    assert(r.pixels[22*448+339]==0xff00ff00);
    assert(r.pixels[22*448+147]!=0xff00ff00);
    ScVideoRect marker=ScRendererMinimapView(&r,ram);
    assert(marker.x==398 && marker.y==64 && marker.w==13 && marker.h==7);
    assert(r.pixels[marker.y*448+marker.x]==0xffffffff);
    int gx,gy;
    ScVideoRect dest={10,20,896,448};
    bool navigation;
    assert(ScRendererWindowToGuest(&r,dest,1000,500,2000,1000,344,32,&gx,&gy,&navigation));
    assert(!navigation);
    assert(gx==147 && gy==22);
    assert(ScRendererWindowToGuest(&r,dest,1000,500,2000,1000,425,134,&gx,&gy,&navigation));
    assert(navigation);
    assert(gx==228 && gy==124); /* right arrow */
    assert(ScRendererWindowToGuest(&r,dest,1000,500,2000,1000,310,190,&gx,&gy,&navigation));
    assert(gx==305 && gy==180 && !navigation);
    int wx,wy;
    assert(ScRendererCityPoint(&r,ram,gx,gy,&wx,&wy) && wx==58 && wy==52);
    assert(!ScRendererCityPoint(&r,ram,30,100,&wx,&wy)); /* toolbar */
    assert(!ScRendererCityPoint(&r,ram,300,22,&wx,&wy)); /* header */
    /* Terrain zoom changes world hit testing, never the HUD rectangles.
     * Forward projection and inverse selection must agree at tile centers. */
    r.zoom_frame=r.zoom_hud=true;
    for(int z=0;z<3;++z) {
        r.map_zoom=z==0?.25:z==1?.5:2;
        double zx=200,zy=100;
        ScRendererProjectCity(&r,&zx,&zy);
        assert(ScRendererCityPoint(&r,ram,(int)zx,(int)zy,&wx,&wy));
        assert(wx==(r.scroll_x+r.scroll_adjust_x+200)/8);
        assert(wy==(r.scroll_y+r.scroll_adjust_y+100)/8);
        assert(!ScRendererCityPoint(&r,ram,30,100,&wx,&wy));
        assert(!ScRendererCityPoint(&r,ram,300,22,&wx,&wy));
    }
    r.zoom_frame=r.zoom_hud=false;r.map_zoom=1;
    for (int width=448;width<=684;width+=236) for (int centered=0;centered<2;++centered)
    for (int dpi=1;dpi<=3;++dpi) {
        r.view=(ScViewport){width,300,centered?(width-256)/2:0,centered?38:0,1,0};
        ScVideoRect d={20,40,width*2,600};
        double x=(20+(width-10.5)*2)/dpi,y=(40+(280.5)*2)/dpi;
        assert(ScRendererWindowToGuest(&r,d,1000,500,1000*dpi,500*dpi,x,y,&gx,&gy,&navigation));
        assert(gx==width-11-r.view.core_x && gy==280-r.view.core_y);
        assert(ScRendererCityPoint(&r,ram,gx,gy,&wx,&wy));
        assert(wx==(160+gx)/8 && wy==(240+gy)/8);
        assert(!ScRendererWindowToGuest(&r,d,1000,500,1000*dpi,500*dpi,0,0,&gx,&gy,&navigation));
    }
    /* Every direction is composed once at its resized border, including the
     * rows outside the native 224-line frame. Mouse navigation uses the same
     * placement with DPI/zoom scaling and a centered native core. */
    const int arrow_views[][2]={{256,224},{256,492},{448,224},{730,492},{448,600}};
    p->cgram[161]=0x7fe0;
    const int axs[]={214,70,134,134},ays[]={118,118,62,174};
    for(unsigned v=0;v<5;++v) for(unsigned center=0;center<2;++center) for(unsigned gpu=0;gpu<2;++gpu) {
        int width=arrow_views[v][0],height=arrow_views[v][1];
        ScViewport view={width,height,center?(width-256)/2:0,center?(height-224)/2:0,1,0};
        assert(ScRendererResize(&r,view));ScRendererResetHistory(&r);assert(ScRendererDeferTerrain(&r,gpu!=0));
        for(int a=0;a<4;++a) {p->oam[(49+a)*2]=(ays[a]<<8)|axs[a];p->oam[(49+a)*2+1]=0x3400;}
        memcpy(before,p,sizeof *p);
        for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
        assert(r.pan_frame && !memcmp(before,p,sizeof *p));
        for(int a=0;a<4;++a) {
            int dx=a==0?width-view.core_x-256:a==1?0:width>256?(width+view.core_x+56)/2-view.core_x-142:0;
            int dy=a<2?(height-view.core_y-224)/2:a==3?height-view.core_y-224:0;
            int tx=view.core_x+axs[a]+dx,ty=view.core_y+ays[a]+dy;
            assert(ScRendererPixel(&r,tx,ty)==0xff00ffff);
            ScVideoRect d={20,40,width*2,height*2};
            assert(ScRendererWindowToGuest(&r,d,width+20,height+40,(width+20)*2,(height+40)*2,
                10+tx+3,20+ty+3,&gx,&gy,&navigation));
            assert(navigation && gx==axs[a]+3 && gy==ays[a]+3);
            if(dx || dy) assert(ScRendererPixel(&r,view.core_x+axs[a],view.core_y+ays[a])!=0xff00ffff);
        }
        /* Hidden directions must stay hidden on tall canvases. */
        p->oam[104]=(240<<8)|134;
        for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
        int downx=view.core_x+(width>256?(width+view.core_x+56)/2-view.core_x-8:134);
        assert(ScRendererPixel(&r,downx,height-50)!=0xff00ffff);
    }
    assert(ScRendererDeferTerrain(&r,false));
    for(int a=0;a<4;++a) p->oam[(49+a)*2]=(240<<8)|128;
    assert(ScRendererResize(&r,(ScViewport){448,224,0,0,1,0}));ScRendererResetHistory(&r);
    r.city_input=false;
    assert(!ScRendererWindowToGuest(&r,dest,1000,500,2000,1000,310,190,&gx,&gy,&navigation));
    r.city_input=true;
    /* The cursor follows the real endpoint even if OAM still contains an
     * older byte-sized proxy position. No native sprite or RAM is changed. */
    r.pan_frame=false;
    word(rom,0x2164,0xa1d3); word(rom,0x21d3,0x111);
    const uint8_t corners[]={253,251,0,0x30,3,251,0,0x30,
        253,1,0,0x30,3,1,0,0x30,0};
    memcpy(rom+0x21d5,corners,sizeof corners);
    p->oam[0]=(91<<8)|141; p->oam[1]=0x3000;
    p->oam[144]=(91<<8)|141; p->oam[145]=0x3200; /* vehicle behind cursor */
    for (int slot=1;slot<4;++slot) p->oam[slot*2]=(240<<8)|128;
    r.pointer_active=true; r.pointer_x=400; r.pointer_y=100;
    memcpy(before,p,sizeof *p);
    for (int y=0;y<224;++y) {
        for (int x=0;x<256;++x) native[x]=y>=91 && y<=98 && x>=141 && x<=148?0xffff0000:0xff0000ff;
        ScRendererLine(&r,p,ram,y,native);
    }
    assert(r.pixels[91*448+141]!=0xffff0000);
    assert(r.pixels[98*448+141]!=0xffff0000); /* clear the last sprite row */
    assert(r.pixels[98*448+141]==0xff00ff00); /* retain the underlying object */
    assert(r.pixels[91*448+413]!=0xffff0000); /* old OAM/RAM offset */
    assert(r.pixels[91*448+397]==0xffff0000); /* tile Y96-5, X400-3 */
    assert(!memcmp(before,p,sizeof *p));
    p->oam[144]=(240<<8)|128;
    /* The tool table is byte-indexed. Each of the 15 construction tools
     * must emit only its four original corners, never unrelated ROM art. */
    const uint8_t tool_records[]={0,0,0,0,0,1,1,1,1,1,2,2,3,2,2,1};
    memcpy(rom+0x8000,tool_records,sizeof tool_records);
    const int lefts[]={-3,-4,-3,-3},rights[]={3,19,27,43},bottoms[]={1,18,25,41};
    for (int record=1;record<=3;++record) {
        unsigned at=0x2240+(record-1)*20;
        word(rom,0x2164+record*2,at+0x8000);word(rom,at,0x111);
        const uint8_t pieces[]={(uint8_t)lefts[record],251,0,0x30,(uint8_t)rights[record],251,0,0x30,
            (uint8_t)lefts[record],(uint8_t)bottoms[record],0,0x30,
            (uint8_t)rights[record],(uint8_t)bottoms[record],0,0x30,0};
        memcpy(rom+at+2,pieces,sizeof pieces);
    }
    for (int tool=0;tool<=14;++tool) {
        word(ram,0x20d,tool);
        for (int y=0;y<224;++y) {
            for (int x=0;x<256;++x) native[x]=y>=91 && y<=98 && x>=141 && x<=148?0xffff0000:0xff0000ff;
            ScRendererLine(&r,p,ram,y,native);
        }
        int record=tool_records[tool],xs[]={400+lefts[record],400+rights[record]},ys[]={91,96+bottoms[record]};
        for (int y=70;y<150;++y) for (int x=70;x<448;++x) {
            bool corner=false;
            for (int j=0;j<2;++j) for (int i=0;i<2;++i)
                if (x>=xs[i] && x<xs[i]+8 && y>=ys[j] && y<ys[j]+8) corner=true;
            assert((r.pixels[y*448+x]==0xffff0000)==corner);
        }
    }
    word(ram,0x20d,0);
    r.clipboard_cursor=true;
    for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    for(int y=70;y<150;++y) for(int x=70;x<448;++x) assert(r.pixels[y*448+x]!=0xffff0000);
    r.clipboard_cursor=false;
    /* The whole 16px hand crosses the HUD boundary at its relocated X.
     * Parked corners do not become artifacts when the next frame is land. */
    r.pointer_active=false;
    p->oam[0]=(43<<8)|146; p->oam[1]=0x3000; p->highOam[0]=2;
    for (int tile=0;tile<4;++tile) for(int y=0;y<8;++y)
        p->vram[(tile%2+(tile/2)*16)*16+y]=0xff;
    for (int slot=1;slot<4;++slot) {
        p->oam[slot*2]=(120<<8)|230;
        p->highOam[0]|=1<<(slot*2); /* X486: native hidden pieces */
    }
    memcpy(before,p,sizeof *p);
    for (int y=0;y<224;++y) {
        for (int x=0;x<256;++x) native[x]=y>=43 && y<=58 && x>=146 && x<=161?0xffff0000:0xff0000ff;
        ScRendererLine(&r,p,ram,y,native);
    }
    assert(r.pixels[43*448+338]==0xffff0000);
    assert(r.pixels[58*448+353]==0xffff0000);
    assert(r.pixels[58*448+146]!=0xffff0000);
    r.pointer_active=true;
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[91*448+397]==0xffff0000);
    assert(r.pixels[58*448+146]!=0xffff0000);
    assert(r.pixels[120*448+230]!=0xffff0000);
    assert(!memcmp(before,p,sizeof *p));
    r.pointer_active=false; memset(p->highOam,0,sizeof p->highOam);
    /* A clipboard cursor in the city must still use the live desktop endpoint
     * for the HUD hand, including the relocated right header. OAM can retain
     * an old hand/corner position while the guest is busy with development. */
    for(int tile=0;tile<4;++tile) for(int y=0;y<8;++y)
        p->vram[(PPU_objTileAdr2(p)+(0xec+(tile%2)+(tile/2)*16)*16+y)&0x7fff]=0xff;
    r.pointer_active=r.pointer_hud=r.clipboard_cursor=true;
    const int hud_points[][2]={{20,70},{420,24},{440,43},{128,5}};
    for(unsigned i=0;i<sizeof hud_points/sizeof *hud_points;++i) {
        r.pointer_x=hud_points[i][0];r.pointer_y=hud_points[i][1];
        memcpy(before,p,sizeof *p);
        for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
        r.clipboard_font_valid=true;
        for(int y=0;y<10;++y) ScRendererClipboardRow(&r,p,r.view,2,true,5555,y,r.pixels+y*448);
        ScRendererHudPointer(&r,p);
        assert(r.pixels[r.pointer_y*448+r.pointer_x]==0xffff0000);
        assert(r.pixels[(r.pointer_y+15)*448+r.pointer_x+7]==0xffff0000);
        assert(!memcmp(before,p,sizeof *p));
    }
    r.pointer_active=r.pointer_hud=r.clipboard_cursor=false;
    for (int slot=0;slot<4;++slot) p->oam[slot*2]=(240<<8)|128;
    p->oam[80]=(46<<8)|190; p->oam[81]=0x3166;
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    /* A parked minimap must not leave its host position marker floating
     * at the far right, or intercept the mouse in that invisible panel. */
    p->highOam[10]=1;
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(!r.pan_frame && r.pixels[64*448+398]!=0xffffffff);
    assert(ScRendererWindowToGuest(&r,dest,1000,500,2000,1000,425,134,&gx,&gy,&navigation));
    assert(!navigation);
    p->highOam[10]=0;
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pan_frame);
    ScWorld *large=calloc(1,sizeof *large); assert(large); large->active=true; r.world=large;
    marker=ScRendererMinimapView(&r,ram);
    assert(marker.x==395 && marker.y==60 && marker.w==7 && marker.h==4);
    r.scroll_x=215*8; r.scroll_y=178*8;
    marker=ScRendererMinimapView(&r,ram);
    assert(marker.x==419 && marker.y==78 && marker.w==3 && marker.h==3);
    /* Expanding the view changes coverage, never the world position/scale. */
    r.scroll_x=160; r.scroll_y=240; r.view.width=684;
    marker=ScRendererMinimapView(&r,ram);
    assert(marker.x==631 && marker.y==60 && marker.w==11 && marker.h==4);
    r.view.width=256; r.split_hud=false;
    marker=ScRendererMinimapView(&r,ram);
    assert(marker.x==203 && marker.y==60 && marker.w==4 && marker.h==4);
    large->huge=true;r.scroll_x=450*8;r.scroll_y=372*8;
    marker=ScRendererMinimapView(&r,ram);
    assert(marker.w>0 && marker.h>0 && marker.x>=200 && marker.x+marker.w<=230);
    r.scroll_x=160;r.scroll_y=240;r.city_input=true;
    assert(ScRendererCityPoint(&r,ram,400,100,&wx,&wy)==false); /* current 256-wide canvas */
    r.view.width=684;r.scroll_x=430*8;r.scroll_y=350*8;
    assert(ScRendererCityPoint(&r,ram,380,120,&wx,&wy) && wx==477 && wy==365);
    assert(!ScRendererCityPoint(&r,ram,600,120,&wx,&wy)); /* actual map edge */
    r.scroll_x=160;r.scroll_y=240;r.view.width=256;
    free(large); r.world=NULL;
    /* Thirteen-digit population uses the exact OBJ font used by native money.
     * Its 5..9 glyphs live on a different tile row; assuming consecutive
     * tile IDs would render unrelated artwork instead of numbers. */
    const uint8_t digit_tiles[]={0x60,0x61,0x62,0x63,0x64,0x70,0x71,0x72,0x73,0x74};
    memcpy(rom+0x5e1,digit_tiles,sizeof digit_tiles);
    for (int digit=0;digit<10;++digit) for (int y=0;y<8;++y)
        p->vram[PPU_objTileAdr2(p)+digit_tiles[digit]*16+y]=y==7?0xff:0x80|(1<<(digit%8));
    p->oam[39]=0x3182; p->oam[41]=0x3183;
    p->oam[53]=0x3174;
    p->oam[54]=(30<<8)|195; p->oam[55]=0x3174;
    ScPopulation pop={0}; pop.valid=true; pop.value=SC_POPULATION_MAX; r.population=&pop;
    assert(ScRendererResize(&r,(ScViewport){448,224,0,0,1,0}));
    memcpy(before,p,sizeof *p);
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[22*448+299]==0xffff0000);
    for (int digit=0;digit<13;++digit) for (int y=0;y<8;++y) for (int x=0;x<8;++x)
        assert(r.pixels[(22+y)*448+299+digit*8+x]==r.pixels[(30+y)*448+387+x]);
    assert(!memcmp(before,p,sizeof *p));
    /* Every digit follows the ROM table, not just the cap's repeated nines. */
    for (int digit=0;digit<10;++digit) {
        pop.value=UINT64_C(1000000)+digit;
        p->oam[55]=(uint16_t)(0x3100|digit_tiles[digit]);
        for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
        for (int y=0;y<8;++y) for (int x=0;x<8;++x)
            assert(r.pixels[(22+y)*448+395+x]==r.pixels[(30+y)*448+387+x]);
    }
    /* Fast counts below one million use the host value too. Stale native
     * digits must not survive, and six-column padding keeps the icon fixed. */
    pop.live=true;
    for (int digit=0;digit<10;++digit) {
        pop.value=(unsigned)digit;
        p->oam[55]=(uint16_t)(0x3100|digit_tiles[digit]);
        memcpy(before,p,sizeof *p);
        for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
        for (int y=0;y<8;++y) for (int x=0;x<8;++x)
            assert(r.pixels[(22+y)*448+395+x]==r.pixels[(30+y)*448+387+x]);
        assert(!memcmp(before,p,sizeof *p));
    }
    pop.live=false;
    pop.value=SC_POPULATION_MAX;
    assert(ScRendererResize(&r,(ScViewport){256,224,0,0,1,0}));
    for (int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[10*256+107]==0xffff0000 && r.pixels[17*256+107]==0xffff0000);
    assert(r.pixels[22*256+147]!=0xffff0000); /* no stale native counter */
    r.population=NULL;
    /* Paste prices use the same OBJ digit table, pixels and palette as the
     * stock counter. Controls use a separately supplied original body font;
     * neither overlay changes OAM, VRAM or any other PPU state. */
    uint8_t clip_font[128*16];memset(clip_font,255,sizeof clip_font);
    ScRendererClipboardFont(&r,clip_font,sizeof clip_font);
    ScViewport cv={448,224,0,0,1,0};
    uint32_t reference_row[448],price_row[448];
    memcpy(before,p,sizeof *p);
    r.population=&pop;pop.live=true;
    for(int g=0;g<10;++g) {
        pop.value=9-g;
        for(int y=0;y<8;++y) {
            for(int x=0;x<448;++x) reference_row[x]=price_row[x]=0xff123456;
            ScRendererPopulationRow(&r,p,cv,true,22+y,reference_row);
            ScRendererClipboardRow(&r,p,cv,2,true,UINT64_C(9876543210),204+y,price_row);
            for(int x=0;x<8;++x) assert(price_row[20+g*8+x]==reference_row[395+x]);
            assert(price_row[100]==0xff123456);
        }
    }
    ScVideoRect cb=ScRendererClipboardButton((ScViewport){448,224,80,0,1,0},0);
    assert(cb.x==192 && cb.y==0 && cb.w==36 && cb.h==10);
    assert(!memcmp(before,p,sizeof *p));r.population=NULL;
    /* A stale cached lightning tile must clear without a camera pan. Keep
     * the same glyph when its real building is still unpowered. */
    memset(p,0,sizeof *p); memset(ram,0,0x20000);
    for(int i=0;i<32;++i) p->brightnessMult[i]=(i<<3)|(i>>2);
    p->inidisp=15;p->bgmode=1;p->screenEnabled[0]=3;p->bgXsc[0]=0x40;p->bgXsc[1]=0x50;
    p->cgram[49]=31;p->cgram[2]=31<<5;
    for(int y=0;y<8;++y) {p->vram[0x376*16+y]=0xff;p->vram[0x10*16+y]=0xff00;}
    word(rom,0x156a9,0x10);word(rom,0x156a9+0x84*2,0x10);word(rom,0x156a9+0x27c*2,0x10);
    rom[0x184eb+0x84]=rom[0x184eb+0x27c]=1;
    p->vram[0x4000+12*32+16]=0x1376;
    ram[0x3e]=1;word(ram,0x10200+2*(12*120+16),0x84);
    for(int x=0;x<256;++x) native[x]=0xffff0000;
    ScRendererResetHistory(&r);
    for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[96*256+128]==0xffff0000);
    word(ram,0x10200+2*(12*120+16),0x8084);memcpy(before,p,sizeof *p);
    for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[96*256+128]==0xff00ff00 && !memcmp(before,p,sizeof *p));
    word(ram,0x10200+2*(12*120+16),0x27c);ScRendererResetHistory(&r);
    for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[96*256+128]==0xff00ff00);
    /* Huge's newly zoned buildings must warn without any cached BG1 tile,
     * in both the native core and the extended viewport. CHR changes retain
     * the authentic blinking animation; power clears it on the next frame. */
    p->cgram[65]=31;
    large=calloc(1,sizeof *large);assert(large);large->active=large->huge=true;r.world=large;
    assert(ScRendererResize(&r,(ScViewport){448,224,0,0,1,0}));
    memset(p->vram+0x4000,0,1024*sizeof(uint16_t));
    word(ram,0x1bd,420);word(ram,0x1bf,360);
    rom[0x184eb+0x13b]=1;word(rom,0x156a9+0x13b*2,0x10);
    word(large->tiles,2*(370*480+435),0x13b);word(large->tiles,2*(370*480+460),0x13b);
    ScRendererResetHistory(&r);memcpy(before,p,sizeof *p);
    for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[87*448+128]==0xffff0000 && r.pixels[87*448+328]==0xffff0000);
    assert(!memcmp(before,p,sizeof *p));
    for(int y=0;y<8;++y) p->vram[0x376*16+y]=0;
    for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[87*448+128]==0xff00ff00 && r.pixels[87*448+328]==0xff00ff00);
    for(int y=0;y<8;++y) p->vram[0x376*16+y]=0xff;
    ScWorldPutCell(large,435,370,0x813b);ScWorldPutCell(large,460,370,0x27c);
    for(int y=0;y<224;++y) ScRendererLine(&r,p,ram,y,native);
    assert(r.pixels[87*448+128]==0xff00ff00 && r.pixels[87*448+328]==0xff00ff00);
    free(large);r.world=NULL;
    /* Zoomed HUD leaves an eight-pixel land gutter before the toolbox.
     * Poison it between frames: both immediate and deferred composition
     * must replace stale pixels, while keeping the native toolbox fixed. */
    memset(p,0,sizeof *p);memset(ram,0,0x20000);
    for(int i=0;i<32;++i)p->brightnessMult[i]=(i<<3)|(i>>2);
    p->inidisp=15;p->bgmode=1;p->screenEnabled[0]=3;
    p->bgXsc[0]=0x40;p->bgXsc[1]=0x50;p->cgram[1]=31<<5;
    for(int y=0;y<8;++y)p->vram[0x100+y]=0xff;
    word(rom,0x156a9,0x10);word(rom,0x14f2d,0x300);
    ram[0x3e]=1;word(ram,0x1d7,65535);
    word(ram,0x1bd,40);word(ram,0x1bf,40);
    for(int x=0;x<256;++x)native[x]=0xff0000ff;
    for(int centered=0;centered<2;++centered)for(int deferred=0;deferred<2;++deferred)
    for(int z=0;z<3;++z) {
        ScViewport gutter_view={448,224,centered?96:0,0,1,0};
        assert(ScRendererResize(&r,gutter_view));ScRendererResetHistory(&r);
        r.map_zoom=z==0?.25:z==1?.5:2;r.defer_terrain=deferred!=0;
        for(int x=0;x<8;++x)r.pixels[100*448+gutter_view.core_x+x]=0xffff00ff;
        memcpy(before,p,sizeof *p);
        for(int y=0;y<224;++y)ScRendererLine(&r,p,ram,y,native);
        assert(r.zoom_frame && r.zoom_hud && (r.repaired_edges[100]&1));
        for(int x=0;x<8;++x)
            assert(ScRendererPixel(&r,gutter_view.core_x+x,100)==0xff00ff00);
        assert(ScRendererPixel(&r,gutter_view.core_x+32,100)==0xff00ff00);
        assert(!memcmp(before,p,sizeof *p));
        /* Opaque native UI in the edge band still owns those pixels. */
        p->screenEnabled[0]=7;p->bgXsc[2]=0x60;p->cgram[2]=31;
        for(int cell=0;cell<1024;++cell)p->vram[0x6000+cell]=1;
        for(int y=0;y<8;++y)p->vram[8+y]=0xff00;
        for(int y=0;y<224;++y)ScRendererLine(&r,p,ram,y,native);
        assert(!(r.repaired_edges[100]&1));
        for(int x=0;x<56;++x)
            assert(ScRendererPixel(&r,gutter_view.core_x+x,100)==0xffff0000);
        assert(ScRendererPixel(&r,gutter_view.core_x+80,100)==0xff00ff00);
        p->screenEnabled[0]=3;memset(p->vram+8,0,8*sizeof *p->vram);
    }
    /* Native navigation/modal gates may pause city input, but must never
     * switch its selected terrain scale or the fixed HUD's projection pivot. */
    r.defer_terrain=false;r.map_zoom=.0625;
    p->screenEnabled[0]=23;p->screenEnabled[1]=4;
    for(int y=0;y<224;++y)ScRendererLine(&r,p,ram,y,native);
    assert(r.zoom_frame && r.zoom_hud && r.city_input);
    word(ram,0x379,255);
    for(int y=0;y<224;++y)ScRendererLine(&r,p,ram,y,native);
    assert(r.zoom_frame && r.zoom_hud && !r.city_input && r.city_overlay_frame);
    word(ram,0x379,0);ram[0x391]=255;ram[0xe3]=255;
    for(int y=0;y<224;++y)ScRendererLine(&r,p,ram,y,native);
    assert(r.zoom_frame && r.zoom_hud && !r.city_input && r.city_overlay_frame);
    p->screenEnabled[0]=20;p->screenEnabled[1]=3;p->cgadsub=3;
    for(int y=0;y<224;++y)ScRendererLine(&r,p,ram,y,native);
    assert(r.zoom_frame && r.zoom_hud && r.advisor_frame && !r.city_overlay_frame);
    ScRendererDestroy(&r); free(p); free(before); free(ram); free(rom);
    puts("PASS: tile flips, overlays, map bounds, native pixels, tall/wide surfaces and PPU immutability");
    return 0;
}
