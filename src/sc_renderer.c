#include "sc_renderer.h"
#include "sc_mouse_ui.h"
#include "sc_native_ppu.h"
#include "sc_obj.h"
#include "snes/ppu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

enum { MAP = 0x10200, TILES = 0x156a9, OVERLAYS = TILES - 0x77c, CELL_TYPES = 0x77c/2 };
#define MEASURE_BEGIN(r) ((r)->measure_clock ? (r)->measure_clock() : 0)
#define MEASURE_END(r,stage,start) do { if((r)->measure_clock) \
    (r)->measure_ticks[stage]+=(r)->measure_clock()-(start); } while(0)
static bool changed_cell(const ScRenderer *r,int x,int y);
static unsigned zoom_step(const ScRenderer *r) {
    return (unsigned)floor(65536/(r->map_zoom>0?r->map_zoom:1)+.5);
}
static int project_inverse(int value,int origin,unsigned step) {
    int64_t product=(int64_t)(value-origin)*step;
    return origin+(int)(product>=0?product/65536:-((-product+65535)/65536));
}
static int zoom_local(const ScRenderer *r,int value,bool vertical) {
    return r->zoom_frame?project_inverse(value,r->zoom_hud?(vertical?46:56):0,zoom_step(r)):value;
}
static double zoom_forward(const ScRenderer *r,double value,bool vertical) {
    int origin=r->zoom_hud?(vertical?46:56):0;
    return r->zoom_frame?origin+(value-origin)*65536/zoom_step(r):value;
}
void ScRendererProjectCity(const ScRenderer *r,double *x,double *y) {
    *x=zoom_forward(r,*x,false);*y=zoom_forward(r,*y,true);
}
static bool gpu_native_repair(const ScRenderer *r,const Ppu *p) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_GPU_REPAIR_REFERENCE");reference=e && *e=='1';}
    return !reference && !r->reference_terrain && r->native_line && r->defer_terrain &&
        !r->zoom_frame && !r->advisor_frame && !r->map_hold && (p->screenEnabled[0]&2) && !PPU_forcedBlank(p);
}
static unsigned u16(const uint8_t *data, size_t offset) {
    return data[offset] | ((unsigned)data[offset+1] << 8);
}
static uint32_t color(const Ppu *p, unsigned index) {
    if (PPU_forcedBlank(p)) return 0xff000000;
    unsigned c = p->cgram[index & 255];
    return 0xff000000 | (uint32_t)p->brightnessMult[c & 31] << 16 |
           (uint32_t)p->brightnessMult[(c >> 5) & 31] << 8 |
           p->brightnessMult[(c >> 10) & 31];
}
static bool window_contains(const Ppu *p,int layer,int x) {
    unsigned flags=(p->windowsel>>(layer*4))&15;
    bool a=(x>=p->window1left && x<=p->window1right) != ((flags&1)!=0);
    bool b=(x>=p->window2left && x<=p->window2right) != ((flags&4)!=0);
    if (!(flags&2)) return (flags&8) ? b : false;
    if (!(flags&8)) return a;
    switch ((p->wbgobjlog>>(layer*2))&3) {
    case 0: return a||b; case 1: return a&&b; case 2: return a!=b; default: return a==b;
    }
}
static uint32_t composite_color(const Ppu *p,unsigned main,int layer,unsigned sub,int sublayer,int x) {
    if (PPU_forcedBlank(p)) return 0xff000000;
    /* Opaque layers need neither color-window evaluation nor channel math. */
    if (!PPU_clipMode(p) && (!(PPU_mathEnabled(p)&(1<<layer)) || PPU_preventMathMode(p)==3))
        return color(p,main);
    bool win=window_contains(p,5,x<0 ? 0 : x>255 ? 255 : x);
    unsigned clip=PPU_clipMode(p), prevent=PPU_preventMathMode(p);
    unsigned c=p->cgram[main&255];
    if (clip==3 || (clip==2 && win) || (clip==1 && !win)) c=0;
    bool math=(PPU_mathEnabled(p)&(1<<layer)) &&
              !(prevent==3 || (prevent==2 && win) || (prevent==1 && !win));
    unsigned second=PPU_addSubscreen(p) && sublayer!=5 ? p->cgram[sub&255] : p->fixedColor;
    uint32_t result=0xff000000;
    for (int channel=0;channel<3;++channel) {
        int value=(c>>(channel*5))&31;
        if (math) {
            int operand=(second>>(channel*5))&31;
            value+=PPU_subtractColor(p) ? -operand : operand;
            if (PPU_halfColor(p) && (sublayer!=5 || !PPU_addSubscreen(p))) value>>=1;
            if (value<0) value=0;
            if (value>31) value=31;
        }
        result|=(uint32_t)p->brightnessMult[value]<<(16-channel*8);
    }
    return result;
}
static unsigned tile_pixel(const Ppu *p, unsigned word, unsigned base,
                           int x, int y, int depth, unsigned palette_offset) {
    int row = word & 0x8000 ? 7-(y&7) : y&7;
    int bit = word & 0x4000 ? x&7 : 7-(x&7);
    base += (word & 1023) * (4*depth) + row;
    unsigned ci=0;
    if(depth==4) {
        /* Cache only decoded row bits, checking both live VRAM words on
         * every read. DMA, animated tiles and scanline changes invalidate
         * immediately; palettes, flips and color math remain live. */
        static struct {uint16_t low,high;uint64_t pixels;} rows[0x8000];
        unsigned at=base&0x7fff;
        uint16_t low=p->vram[at],high=p->vram[(at+8)&0x7fff];
        if(rows[at].low!=low || rows[at].high!=high) {
            uint64_t pixels=0;
            for(unsigned b=0;b<8;++b) {
                unsigned value=((low>>b)&1)|(((low>>(b+8))&1)<<1)|
                    (((high>>b)&1)<<2)|(((high>>(b+8))&1)<<3);
                pixels|=(uint64_t)value<<(8*b);
            }
            rows[at].low=low;rows[at].high=high;rows[at].pixels=pixels;
        }
        ci=(unsigned)(rows[at].pixels>>(8*bit))&15;
        return ci?ci+((word>>10)&7)*16+palette_offset:0;
    }
    for (int plane=0; plane<depth; plane+=2) {
        unsigned bits=p->vram[(base + plane*4) & 0x7fff];
        ci |= ((bits >> bit)&1) << plane;
        ci |= ((bits >> (bit+8))&1) << (plane+1);
    }
    return ci ? ci + ((word >> 10)&7)*(1<<depth) + palette_offset : 0;
}
static int sprite_x(const Ppu *p,int slot) {
    int index=slot*2;
    return (p->oam[index]&255)|(((p->highOam[index/8]>>(index%8))&1)<<8);
}
static const int sprite_sizes[8][2]={{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
/* One pixel of a sprite given as an OAM word (tile | attributes << 8) and a
 * size. The size is a parameter for sprites whose slot does not carry it: the
 * vehicles kept for the margin sit in parked slots, whose size bit is the
 * park's, and the selector's pins and marks come from the ROM's records. */
static unsigned sprite_word_pixel(const Ppu *p,unsigned attr,int size,int x,int y) {
    if (x<0 || y<0 || x>=size || y>=size) return 0;
    if (attr&0x4000) x=size-1-x;
    if (attr&0x8000) y=size-1-y;
    unsigned tile=(((attr&0xf0)+(y/8)*16)&255)|(((attr&15)+x/8)&15);
    unsigned base=attr&0x100 ? PPU_objTileAdr2(p) : PPU_objTileAdr1(p);
    return tile_pixel(p,tile|(((attr>>9)&7)<<10),base,x,y,4,128);
}
static unsigned sprite_pixel(const Ppu *p,int slot,int x,int y) {
    int index=slot*2, size=sprite_sizes[PPU_objSize(p)][(p->highOam[index/8]>>(index%8+1))&1];
    return sprite_word_pixel(p,p->oam[index+1],size,x,y);
}
static unsigned vehicle_pixel(const ScRenderer *r,const Ppu *p,const ScVehicleSprite *v,int size,int x,int y) {
    if(!v->rom_chr)return sprite_word_pixel(p,v->attr,size,x,y);
    if(x<0 || y<0 || x>=size || y>=size)return 0;
    if(v->attr&0x4000)x=size-1-x;
    if(v->attr&0x8000)y=size-1-y;
    unsigned word=(v->rom_chr&0x3fff)+((y/8)*(v->rom_chr&0x40000000u?4:2)+x/8)*16+(y&7);
    unsigned at=0x30000+word*2;if(at+17>=r->rom_size)return 0;
    unsigned a=u16(r->rom,at),b=u16(r->rom,at+16),bit=7-(x&7);
    unsigned ci=((a>>bit)&1)|(((a>>(bit+8))&1)<<1)|(((b>>bit)&1)<<2)|(((b>>(bit+8))&1)<<3);
    return ci?ci+128+((v->attr>>9)&7)*16:0;
}
/* The ROM through the cart image (LoROM), for src/sc_selector.c. */
static uint8_t rom_read(void *ctx,uint32_t adr) {
    const ScRenderer *r=(const ScRenderer *)ctx;
    size_t off=((size_t)((adr>>16)&0x7f)<<15)|(adr&0x7fff);
    return r->rom && off<r->rom_size ? r->rom[off] : 0;
}
static void find_lights(ScRenderer *r,const Ppu *p) {
    r->light_slot=-1; r->light_pitch=0;
    if (!r->title_live) return;
    int best=2;
    for (int i=0;i<104;++i) {
        int y=p->oam[i*2]>>8, lo=256, pitch=512, count=0, xs[104];
        if (y<150 || y>215) continue;
        bool seen=false;   /* one pass per row, not per member */
        for (int k=0;k<i && !seen;++k)
            seen=p->oam[k*2+1]==p->oam[i*2+1] && (p->oam[k*2]>>8)==y;
        if (seen) continue;
        for (int j=0;j<104;++j) {
            int x=sprite_x(p,j);
            if (x>=256 || p->oam[j*2+1]!=p->oam[i*2+1] || (p->oam[j*2]>>8)!=y) continue;
            int at=count++;
            while (at>0 && xs[at-1]>x) { xs[at]=xs[at-1]; --at; }
            xs[at]=x;
        }
        /* The pitch is the smallest gap between two neighbours. This used to
         * start from lo = 256 and count each member's distance to it as a
         * gap, so the first member visited set it to 256 - x: 20 px for a
         * light at 236. As the row moved left that changed every frame, and
         * the copies in the margins stretched, squeezed and seemed to run
         * backwards -- reported from play as a wrong animation. */
        if (count) lo=xs[0];
        for (int k=1;k<count;++k)
            if (xs[k]-xs[k-1]>0 && xs[k]-xs[k-1]<pitch) pitch=xs[k]-xs[k-1];
        if (count>best && pitch>0 && pitch<=128) {
            best=count; r->light_slot=i; r->light_x=lo; r->light_pitch=pitch;
        }
    }
}
static unsigned cell_pixel(const ScRenderer *r,const Ppu *p,const uint8_t *ram,
                           int x,int y,bool overlay) {
    bool large=r->map_hold?r->held_large:r->world && r->world->active;
    unsigned width=large?(r->map_hold?(r->held_mega?3840:r->held_colossal?1920:r->held_giant?960:r->held_huge?480:240):ScWorldWidth(r->world)):120,height=large?(r->map_hold?(r->held_mega?3200:r->held_colossal?1600:r->held_giant?800:r->held_huge?400:200):ScWorldHeight(r->world)):100;
    if (!r->rom || x<0 || y<0 || (unsigned)x>=width*8 || (unsigned)y>=height*8) return 0;
    unsigned offset_cell=((y/8)*width+x/8)*2;
    const uint8_t *map=large?r->world->tiles:ram+MAP;
    unsigned cell=u16(r->map_hold?r->held_map:map,offset_cell)&1023;
    if (cell>=CELL_TYPES) return 0;
    size_t offset=(overlay ? OVERLAYS : TILES)+cell*2;
    if (offset+1 >= r->rom_size) return 0;
    unsigned word=u16(r->rom,offset);
    if (overlay && (word&1023)==0x300) return 0;
    return tile_pixel(p,word,PPU_bgTileAdr(p,1),x,y,4,0);
}
static int scroll_delta(int a,int b) { int d=(a-b)&255; return d>128 ? d-256 : d; }
static bool city_live(const ScRenderer *r,const Ppu *p,const uint8_t *ram) {
    return r->rom_is_us && ram[0x14]==0 && u16(ram,0x3e)!=0 &&
           PPU_mode(p)==1 && ((p->screenEnabled[0]|p->screenEnabled[1])&2) &&
           r->wood_layer<0 && !(!(p->screenEnabled[0]&2) && (p->screenEnabled[0]&1));
}
static void track_scroll(ScRenderer *r,const Ppu *p,const uint8_t *ram) {
    if (!city_live(r,p,ram)) { r->scroll_valid=r->scroll_repair=false; return; }
    int h=p->hScroll[1]&255,v=p->vScroll[1]&255;
    int x=(int16_t)u16(ram,0x1bd)*8+(h&7),y=(int16_t)u16(ram,0x1bf)*8+(v&7);
    if (r->scroll_valid) {
        int dx=scroll_delta(h,r->scroll_h),dy=scroll_delta(v,r->scroll_v);
        if(dx || dy || x!=r->native_scroll_x || y!=r->native_scroll_y) r->scroll_repair=true;
        if (abs(dx)<32 && abs(dy)<32) {
            int ax=dx-(x-r->native_scroll_x),ay=dy-(y-r->native_scroll_y);
            if (ax%8==0) r->scroll_adjust_x+=ax;
            if (ay%8==0) r->scroll_adjust_y+=ay;
            if (!dx && !dy && x==r->native_scroll_x && y==r->native_scroll_y) ++r->scroll_still;
            else r->scroll_still=0;
            if (r->scroll_still>=8) r->scroll_adjust_x=r->scroll_adjust_y=0;
            if (r->scroll_adjust_x>8) r->scroll_adjust_x=8;
            if (r->scroll_adjust_x< -8) r->scroll_adjust_x=-8;
            if (r->scroll_adjust_y>8) r->scroll_adjust_y=8;
            if (r->scroll_adjust_y< -8) r->scroll_adjust_y=-8;
        } else r->scroll_adjust_x=r->scroll_adjust_y=0;
    } else r->scroll_adjust_x=r->scroll_adjust_y=r->scroll_still=0;
    r->scroll_valid=true;r->native_scroll_x=x;r->native_scroll_y=y;
    r->scroll_x=x+(int)lround(r->camera_x);r->scroll_y=y+(int)lround(r->camera_y);
    r->scroll_h=h;r->scroll_v=v;
}
static void track_objects(ScRenderer *r,const Ppu *p,const uint8_t *ram) {
    bool city=city_live(r,p,ram);
    for (int slot=0;slot<128;++slot) {
        int raw=sprite_x(p,slot), y=p->oam[slot*2]>>8;
        unsigned attr=p->oam[slot*2+1]&0xfe00;
        int dx=(raw-r->object_raw[slot])&511; if (dx>256) dx-=512;
        int dy=scroll_delta(y,r->object_y[slot]);
        bool step=r->objects_valid && city && attr==r->object_attr[slot] && abs(dx)<=16 && abs(dy)<=16;
        if (!step) {
            r->object_grace[slot]=0;
            r->object_x[slot]=raw<256 ? raw : raw-512;
        } else {
            r->object_x[slot]+=dx;
            if (dx || dy) r->object_grace[slot]=16;
            else if (r->object_grace[slot]) --r->object_grace[slot];
        }
        r->object_raw[slot]=raw; r->object_y[slot]=y; r->object_attr[slot]=attr;
    }
    r->objects_valid=city;
}
static void track_map_swap(ScRenderer *r,const Ppu *p,const uint8_t *ram) {
    if (!city_live(r,p,ram)) {
        if (r->map_valid) memset(r->changed_cells,0,sizeof r->changed_cells);
        r->map_valid=r->map_hold=false; return;
    }
    bool large=r->world && r->world->active;
    const uint8_t *map=large?r->world->tiles:ram+MAP;
    unsigned bytes=large?ScWorldCells(r->world)*2:24000;
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_MAP_TRACK_REFERENCE");reference=e && *e=='1';}
    const uint64_t *revisions=large && !reference && !r->reference_terrain?
        ScWorldTileRevisions(r->world):NULL;
    bool full=!r->map_valid || r->map_bytes!=bytes;
    bool held=r->map_hold;
    unsigned width=large?ScWorldWidth(r->world):120;
    bool black=PPU_forcedBlank(p) || !PPU_brightness(p);
    if (r->map_valid) {
        /* A revision covers 256 cells. Native and host writers invalidate
         * only changed regions; this avoids a multi-megabyte compare and
         * two whole-map copies on every quiet frame. Keep a full scan oracle. */
        for(unsigned chunk=0;chunk<(bytes+SC_WORLD_TILE_CHUNK_BYTES-1)/SC_WORLD_TILE_CHUNK_BYTES;++chunk) {
          if(!full && revisions && r->map_revisions[chunk]==revisions[chunk]) continue;
          unsigned begin=chunk*SC_WORLD_TILE_CHUNK_BYTES,end=begin+SC_WORLD_TILE_CHUNK_BYTES;
          if(end>bytes) end=bytes;
          for (unsigned i=begin;i<end;i+=2) {
            unsigned raw=u16(map,i),delta=raw^u16(r->previous_map,i),tile=raw&1023;
            /* A flood changes power bits on ordinary terrain too. Only a
             * building owner has a warning to redraw for that change. */
            bool changed=(delta&1023)!=0 || ((delta&0x8000) &&
                r->rom && tile<CELL_TYPES && (r->rom[0x184eb+tile]&1));
            if (changed && !r->map_hold) {
                r->changed_cells[i/2]=1;
                /* The power glyph belongs to the southeast footprint cell,
                 * even when only its owner's power bit changed. */
                if ((i/2)%width<width-1 && i/2+width+1<bytes/2)
                    r->changed_cells[i/2+width+1]=1;
            }
          }
          memcpy(r->previous_map+begin,map+begin,end-begin);
          if(!held) memcpy(r->held_map+begin,map+begin,end-begin);
          if(revisions) r->map_revisions[chunk]=revisions[chunk];
        }
        if (r->map_hold) {
            if (black) r->map_dark=true;
            if ((!black && r->map_dark) || ++r->map_age>900) {
                r->map_hold=false;
                memset(r->changed_cells,0,sizeof r->changed_cells);
            }
        }
    }
    if(full) {
        memcpy(r->previous_map,map,bytes);
        if(revisions) memcpy(r->map_revisions,revisions,
            ((bytes+SC_WORLD_TILE_CHUNK_BYTES-1)/SC_WORLD_TILE_CHUNK_BYTES)*sizeof *revisions);
    }
    if (!r->map_hold) {
        if(full || held) memcpy(r->held_map,map,bytes);
        memcpy(r->held_ppu,p,sizeof *p);
        ScObjCopyBuffer(r->held_ppu,p);
        r->held_x=r->scroll_x+r->scroll_adjust_x;
        r->held_y=r->scroll_y+r->scroll_adjust_y;
        r->held_large=large; r->held_huge=large && r->world->huge;r->held_giant=large && r->world->giant;r->held_colossal=large && r->world->colossal;r->held_mega=large && r->world->mega;
    }
    r->map_bytes=bytes;r->map_valid=true;
}
static void object_row(const ScRenderer *r,const Ppu *p,int y,uint16_t *pixels) {
    memset(pixels,0,(size_t)r->view.width*sizeof(*pixels));
    /* The vehicles the game drops once they are right of its 256 columns
     * (src/sc_vehicles.c): drawn first, so the OAM's own sprites win.
     * Rows as the PPU has them -- a sprite's row 0 is on line Y -- which the
     * guest's half of an object crossing the edge also follows. */
    for (int k=0;k<r->vehicle_count;++k) {
        const ScVehicleSprite *v=&r->vehicles[k];
        int size=v->host?(v->large?16:8):sprite_sizes[PPU_objSize(p)][v->large ? 1 : 0];
        int row=y-v->y;
        if (row<0 || row>=size) continue;
        unsigned attr=v->host?v->attr:p->oam[v->slot*2+1];
        for (int dx=0;dx<size;++dx) {
            int local=v->x+dx;
            if (local<256 && !v->host) continue;   /* the authentic columns are the core's */
            int x=local+r->view.core_x;
            if (x<0 || x>=r->view.width) continue;
            unsigned ci=v->host?vehicle_pixel(r,p,v,size,dx,row):sprite_word_pixel(p,attr,size,dx,row);
            if (ci) pixels[x]=(uint16_t)(ci|(((attr>>12)&3)<<8));
        }
    }
    int first=PPU_objPriority(p) ? (p->oamaddl&0xfe)/2 : 0;
    for (int rank=127;rank>=0;--rank) {
        int slot=(first+rank)&127;
        if (r->city_input && (r->pointer_active || r->pointer_hidden || r->split_hud) && slot<4) continue; /* composed once at the host endpoint */
        if (r->pan_frame && slot>=39 && slot<=52) continue;
        if (!r->object_grace[slot]) continue; /* parked HUD/cursor copies */
        /* Row 0 is on line Y, as the PPU draws it (it evaluates a line's
         * sprites one line early). y+1 put every margin sprite a row above
         * the core's half of the same object -- the train at the seam. */
        int row=(y-r->object_y[slot])&255;
        if (row>=64) continue;
        int left=r->object_x[slot]+r->view.core_x;
        for (int dx=0;dx<64;++dx) {
            int x=left+dx;
            if (x<0 || x>=r->view.width) continue;
            unsigned ci=sprite_pixel(p,slot,dx,row);
            if (ci) pixels[x]=(uint16_t)(ci|(((p->oam[slot*2+1]>>12)&3)<<8));
        }
    }
}
uint32_t ScRendererMapPixel(const ScRenderer *r,const Ppu *p,const uint8_t *ram,int x,int y) {
    bool large=r->world && r->world->active;
    if (!r->rom_is_us || !r->rom || x<0 || y<0 ||
        (unsigned)x>=(large?ScWorldWidth(r->world):120)*8 || (unsigned)y>=(large?ScWorldHeight(r->world):100)*8)
        return color(p,0);
    unsigned ci=cell_pixel(r,p,ram,x,y,false);
    /* Both tables reference the city CHR (BG2). Roofs extend one whole
     * cell up-left: the covering tile belongs to the southeast neighbor. */
    unsigned over=cell_pixel(r,p,ram,x+8,y+8,true);
    return color(p,over ? over : ci);
}
static unsigned bg_word(const Ppu *p,int layer,int x,int y) {
    x=(x+p->hScroll[layer])&1023; y=(y+p->vScroll[layer])&1023;
    int bits=PPU_bigTiles(p,layer) ? 4 : 3;
    unsigned addr=PPU_bgTilemapAdr(p,layer)+((y>>bits)&31)*32+((x>>bits)&31);
    if ((x&(32<<bits)) && PPU_bgTilemapWider(p,layer)) addr+=0x400;
    if ((y&(32<<bits)) && PPU_bgTilemapHigher(p,layer))
        addr+=PPU_bgTilemapWider(p,layer) ? 0x800 : 0x400;
    return p->vram[addr&0x7fff];
}
static unsigned bg_sample(const Ppu *p,int layer,int x,int y,bool *priority) {
    int mode=PPU_mode(p);
    if (mode>1) return 0;
    int depth=mode==0 || layer==2 ? 2 : 4;
    x=(x+p->hScroll[layer])&1023; y=(y+p->vScroll[layer])&1023;
    int bits=PPU_bigTiles(p,layer) ? 4 : 3;
    unsigned addr=PPU_bgTilemapAdr(p,layer)+((y>>bits)&31)*32+((x>>bits)&31);
    if ((x&(32<<bits)) && PPU_bgTilemapWider(p,layer)) addr+=0x400;
    if ((y&(32<<bits)) && PPU_bgTilemapHigher(p,layer))
        addr+=PPU_bgTilemapWider(p,layer) ? 0x800 : 0x400;
    unsigned word=p->vram[addr&0x7fff];
    if (priority) *priority=(word&0x2000)!=0;
    if (bits==4) {
        unsigned n=word&1023;
        if (((x&8)!=0) != ((word&0x4000)!=0)) n++;
        if (((y&8)!=0) != ((word&0x8000)!=0)) n+=16;
        word=(word&~1023u)|(n&1023);
    }
    return tile_pixel(p,word,PPU_bgTileAdr(p,layer),x,y,depth,mode==0 ? layer*32 : 0);
}
/* BG1 caches the lightning glyph independently of the power bitmap. A
 * building centre owns the glyph in its 3x3 footprint. Plants supply their
 * own power (the native 03:b0f8 helper also treats them as powered). */
static bool powered_owner_near(const ScRenderer *r,const uint8_t *ram,int px,int py) {
    if(!r->rom) return false;
    int wx=px/8,wy=py/8;
    int width=r->world && r->world->active?ScWorldWidth(r->world):120;
    int height=r->world && r->world->active?ScWorldHeight(r->world):100;
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
        int a=wx+dx,b=wy+dy;
        if (a<0 || b<0 || a>=width || b>=height) continue;
        const uint8_t *map=r->world && r->world->active?r->world->tiles:ram+MAP;
        unsigned raw=u16(map,2*(b*width+a));
        unsigned tile=raw&1023;
        if (tile<CELL_TYPES && (r->rom[0x184eb+tile]&1))
            return (raw&0x8000)!=0 || tile==0x27c || tile==0x28c;
    }
    return false;
}
static bool stale_power_warning(const ScRenderer *r,const Ppu *p,const uint8_t *ram,
                                int x,int y,int sx,int sy) {
    if (bg_word(p,0,x,y+1)!=0x1376 || !r->rom) return false;
    if (u16(ram,0x1d7) && (y<46 || (x<56 && y<224))) return false;
    return powered_owner_near(r,ram,sx+x,sy+y+1);
}
/* The native cache adds $1376 one cell southeast of a building centre.
 * Newly placed zones and the expanded viewport cannot wait for that cache's
 * next sweep. Use the real power flag and the same live, animated CHR. */
static bool power_warning_cell(const ScRenderer *r,const uint8_t *ram,int x,int y) {
    if (!r->rom || x<8 || y<8) return false;
    int wx=x/8-1,wy=y/8-1;
    bool large=r->world && r->world->active;
    int width=large?ScWorldWidth(r->world):120,height=large?ScWorldHeight(r->world):100;
    if (wx>=width-1 || wy>=height-1) return false;
    const uint8_t *map=large?r->world->tiles:ram+MAP;
    unsigned raw=u16(map,2*(wy*width+wx)),tile=raw&1023;
    return !(raw&0x8000) && tile<CELL_TYPES &&
        (r->rom[0x184eb+tile]&1) && tile!=0x27c && tile!=0x28c;
}
static unsigned power_warning_pixel(const ScRenderer *r,const Ppu *p,const uint8_t *ram,
                                    int x,int y,int sx,int sy) {
    if (r->map_hold || (u16(ram,0x1d7) && (y<46 || (x<56 && y<224)))) return 0;
    int mx=sx+x,my=sy+y+1;
    if (!power_warning_cell(r,ram,mx,my)) return 0;
    return tile_pixel(p,0x1376,PPU_bgTileAdr(p,0),mx,my,4,0);
}
static unsigned bg_pixel(const Ppu *p,int layer,int x,int y) {
    return bg_sample(p,layer,x,y,NULL);
}
static bool wood_tile(unsigned word) {
    unsigned tile=word&1023;
    return tile>=0x20 && tile<=0x11f && !(word&0xc000);
}
static unsigned wood_grow(unsigned word,int columns) {
    return (word&~15u)|((word+columns)&15);
}
static bool wood_run(const uint16_t *map,int row,int col,int step) {
    unsigned a=map[row*32+col], b=map[row*32+col+step];
    return wood_tile(a) && wood_tile(b) && b==wood_grow(a,step);
}
/* Discover the desk from its tile pattern, including the fax and View Mode.
 * Keep this host-side: the guest's tilemaps and staging columns are untouched. */
static void find_wood(ScRenderer *r,const Ppu *p,const uint8_t *ram) {
    r->wood_layer=-1;
    if (ram[0x14]==1 || PPU_mode(p)>1) return;
    for (int layer=0;layer<(PPU_mode(p)==0 ? 4 : 3);++layer) {
        /* BG2 is the city tile cache in Mode 1. Dense building footprints
         * can match the desk's sequential tile IDs and row repetition;
         * that resemblance must never turn gameplay into menu scenery. */
        if(layer==1 && PPU_mode(p)==1 && ram[0x14]==0 && u16(ram,0x3e)) continue;
        if (!((p->screenEnabled[0]|p->screenEnabled[1])&(1<<layer))) continue;
        unsigned base=PPU_bgTilemapAdr(p,layer);
        if (base+1024>0x8000) continue;
        const uint16_t *map=p->vram+base;
        int rows=0,full=0,cells=0;
        for (int y=0;y<32;++y) {
            int count=0;
            for (int x=0;x<32;++x) count+=wood_tile(map[y*32+x]);
            cells+=count; full+=count==32;
            rows+=wood_run(map,y,0,1) && wood_run(map,y,31,-1);
        }
        if (rows<8 || full<4 || cells<300) continue;
        r->wood_layer=layer; r->wood_period=16;
        for (int period=1;period<=16;period*=2) {
            int tested=0,bad=0;
            for (int y=0;y+period<32;++y) for (int x=0;x<=31;x+=31) {
                unsigned a=map[y*32+x],b=map[(y+period)*32+x];
                if (wood_tile(a) && wood_tile(b)) { ++tested; bad+=a!=b; }
            }
            if (tested>=16 && bad*10<=tested) { r->wood_period=period; break; }
        }
        for (int y=0;y<32;++y) {
            r->wood_rows[y]=0;
            for (int offset=0;offset<32;offset+=r->wood_period) {
                int row=(y+offset)&31;
                if (wood_run(map,row,0,1)) { r->wood_rows[y]=map[row*32]; break; }
                if (wood_run(map,row,31,-1)) { r->wood_rows[y]=wood_grow(map[row*32+31],-31); break; }
            }
        }
        break;
    }
}
/* Does the guest's own map draw anything in the cell this pixel falls in?
 *
 * An empty cell is the shipped map's unused right-hand columns, which the
 * synthesised desk exists to fill. A cell with art is a card, and nothing of
 * ours belongs under one: the picture's dark pixels are colour 0, which the
 * guest's own columns show as the backdrop, so a plank laid under them shows
 * through the picture instead of black. Reported as brown card pictures in
 * the margin columns (issue #2, item 12). */
static bool guest_cell_inked(const Ppu *p,int layer,int x,int y) {
    int mode=PPU_mode(p);
    int depth=mode==0 || layer==2 ? 2 : 4;
    x=(x+p->hScroll[layer])&1023; y=(y+p->vScroll[layer])&1023;
    int bits=PPU_bigTiles(p,layer) ? 4 : 3;
    unsigned addr=PPU_bgTilemapAdr(p,layer)+((y>>bits)&31)*32+((x>>bits)&31);
    if ((x&(32<<bits)) && PPU_bgTilemapWider(p,layer)) addr+=0x400;
    if ((y&(32<<bits)) && PPU_bgTilemapHigher(p,layer))
        addr+=PPU_bgTilemapWider(p,layer) ? 0x800 : 0x400;
    unsigned word=p->vram[addr&0x7fff];
    if (bits==4) {
        unsigned n=word&1023;
        if (((x&8)!=0) != ((word&0x4000)!=0)) n++;
        if (((y&8)!=0) != ((word&0x8000)!=0)) n+=16;
        word=(word&~1023u)|(n&1023);
    }
    unsigned base=PPU_bgTileAdr(p,layer)+(word&1023)*(4u*(unsigned)depth), ink=0;
    for (int i=0;i<4*depth;++i) ink|=p->vram[(base+(unsigned)i)&0x7fff];
    return ink!=0;
}
static uint32_t scenery(const ScRenderer *r,const Ppu *p,const uint8_t *ram,int x,int y) {
    unsigned screen=ram[0x14], ci=0;
    int owner=5;
    /* The selector's screens are $0A-$0C, but each also shows something
     * else for part of its length: $0A is the fax or menu fading out until
     * the selector's map arrives, and $0C is the fax already fading in. So
     * the map on screen decides. Keyed on $0B/$0C alone, the fax's desk was
     * left out of the margins for its whole fade-in and then popped in. */
    bool selector=screen>=10 && screen<=12 && PPU_mode(p)==0 &&
        PPU_bgTilemapAdr(p,0)==0x3000;
    if (r->title_live && PPU_mode(p)==1) {
        /* Title's sky and skyline are repeating scenery; title text/sprites
         * remain in the native view. Per-line palette preserves its gradient. */
        for (int layer=2; layer>=0; --layer) {
            if (!(p->screenEnabled[0]&(1<<layer))) continue;
            unsigned sample=bg_pixel(p,layer,x,y+1);
            if (sample) { ci=sample; owner=layer; }
        }
    } else if (r->wood_layer>=0 && !selector) {
        int layer=r->wood_layer;
        int tx=(x+p->hScroll[layer])&1023, ty=(y+1+p->vScroll[layer])&255;
        unsigned word=wood_grow(r->wood_rows[ty/8],tx/8);
        int depth=PPU_mode(p)==0 || layer==2 ? 2 : 4;
        ci=tile_pixel(p,word,PPU_bgTileAdr(p,layer),tx,ty,depth,PPU_mode(p)==0 ? layer*32 : 0);
        owner=layer;
    } else if (selector) {
        /* Cards and translated names already exist in the wide guest maps.
         * Show that single strip, then continue the desk beyond it. Wrapping
         * the entire layer would repeat cards on ultrawide displays.
         *
         * Only where the strip itself draws nothing: inside a cell the guest
         * has drawn, its own layers are the whole answer, backdrop included,
         * exactly as in the native columns. */
        int tx=x+p->hScroll[0], ty=(y+1+p->vScroll[0])&255;
        bool inked=false;
        for (int layer=3;layer>=0 && !inked;--layer) {
            int lx=x+p->hScroll[layer], ly=y+1+p->vScroll[layer];
            if (!(p->screenEnabled[0]&(1<<layer)) || lx<0 || lx>=416 || ly<0 || ly>=256) continue;
            inked=guest_cell_inked(p,layer,x,y+1);
        }
        unsigned anchor=p->vram[0x3400+(ty/8)*32+9]; /* column 41 */
        if (!wood_tile(anchor) && r->wood_layer==0) anchor=r->wood_rows[ty/8];
        if (!inked && wood_tile(anchor)) {
            unsigned word=wood_grow(anchor,(tx>>3)-41);
            ci=tile_pixel(p,word,PPU_bgTileAdr(p,0),tx,ty,2,0); owner=0;
        }
        for (int layer=3;layer>=0;--layer) {
            int lx=x+p->hScroll[layer], ly=y+1+p->vScroll[layer];
            if (!(p->screenEnabled[0]&(1<<layer)) || lx<0 || lx>=416 || ly<0 || ly>=256) continue;
            unsigned sample=bg_pixel(p,layer,x,y+1);
            if (sample) { ci=sample; owner=layer; }
        }
    } else if (PPU_mode(p)==0 && screen!=0) {
        /* Decorated menus use a background layer. Its clear top strip is
         * repeatable furniture-free scenery, including the fax desk. */
        if ((screen==14 || screen==15) && (p->screenEnabled[0]&4)) {
            ci=bg_pixel(p,2,x,((y+1)&15)-p->vScroll[2]); owner=2;
        } else for (int layer=0; layer<4; ++layer) if (p->screenEnabled[0]&(1<<layer)) {
            ci=bg_pixel(p,layer,x,((y+1)&15)-p->vScroll[layer]); owner=layer; break;
        }
    }
    if (r->title_live && r->light_slot>=0 && (p->screenEnabled[0]&16)) {
        int dx=(x-r->light_x)%r->light_pitch;
        if (dx<0) dx+=r->light_pitch;
        unsigned light=sprite_pixel(p,r->light_slot,dx,y-(p->oam[r->light_slot*2]>>8));
        if (light) { ci=light; owner=4; }
    }
    if (r->selector_row && (p->screenEnabled[0]&16)) {
        unsigned mark=r->selector_row[x+r->view.core_x];
        if (mark) { ci=mark; owner=4; }
    }
    return composite_color(p,ci,owner,0,5,x);
}
/* The host's own sprites on one row, outside the core: the selector's pins
 * and win marks (src/sc_selector.c) -- the game draws those of the cards
 * outside its 256 columns too, but raw OAM X cannot tell them from the
 * blink-hidden bracket, and Sylt's are not in OAM at all -- and the title's
 * SIMCITY sign (src/sc_titlesign.c), which the game emits for the authentic
 * columns only. Last to first, so the earlier sprite -- the lower slot --
 * wins, as it does on the PPU. */
static void host_sprite_row(const ScRenderer *r,const Ppu *p,int y,
                            const ScSelSprite *list,int count,uint16_t *pixels) {
    for (int k=count-1;k>=0;--k) {
        const ScSelSprite *s=&list[k];
        int size=sprite_sizes[PPU_objSize(p)][s->large ? 1 : 0];
        int row=y-s->y;
        if (row<0 || row>=size) continue;
        unsigned attr=(unsigned)s->tile|((unsigned)s->attr<<8);
        for (int dx=0;dx<size;++dx) {
            int local=s->x+dx;
            if (local>=0 && local<256) continue;   /* the core shows its own */
            int x=local+r->view.core_x;
            if (x<0 || x>=r->view.width) continue;
            unsigned ci=sprite_word_pixel(p,attr,size,dx,row);
            if (ci) pixels[x]=(uint16_t)ci;
        }
    }
}
static void host_sprites_row(const ScRenderer *r,const Ppu *p,int y,uint16_t *pixels) {
    memset(pixels,0,(size_t)r->view.width*sizeof(*pixels));
    host_sprite_row(r,p,y,r->selector,r->selector_count,pixels);
    host_sprite_row(r,p,y,r->sign,r->sign_count,pixels);
}
static bool edge_has_overlay(const Ppu *p,int y,int left) {
    for (int x=left;x<left+8;++x) {
        for (int layer=0;layer<=2;layer+=2)
            if ((p->screenEnabled[0]&(1<<layer)) &&
                (!(p->screenWindowed[0]&(1<<layer)) || !window_contains(p,layer,x)) &&
                bg_pixel(p,layer,x,y+1)) return true;
    }
    if (!(p->screenEnabled[0]&16)) return false;
    /* Test each OAM rectangle once, rather than replaying all 128 sprites
     * for every pixel of both edge strips. Preserve the original ink test. */
    for (int slot=0;slot<128;++slot) {
        int index=slot*2;
        int size=sprite_sizes[PPU_objSize(p)][(p->highOam[index/8]>>(index%8+1))&1];
        int row=(y+1-(p->oam[index]>>8))&255;
        if (row>=size) continue;
        int sx=sprite_x(p,slot); if (sx>=256) sx-=512;
        int first=left>sx?left:sx, end=left+8<sx+size?left+8:sx+size;
        for (int x=first;x<end;++x)
            if (sprite_word_pixel(p,p->oam[index+1],size,x-sx,row)) return true;
    }
    return false;
}
void ScRendererInit(ScRenderer *r,const uint8_t *rom,size_t size,bool is_us) {
    memset(r,0,sizeof(*r)); r->rom=rom; r->rom_size=size; r->rom_is_us=is_us; r->wood_layer=-1;
    const char *spans=getenv("SC_TERRAIN_SPANS");r->reference_terrain=spans && *spans=='0';
}
bool ScRendererResize(ScRenderer *r,ScViewport v) {
    if (v.width<256 || v.height<224 || v.width>SC_MAX_CANVAS || v.height>SC_MAX_CANVAS ||
        v.core_x<0 || v.core_y<0 || v.core_x+256>v.width || v.core_y+224>v.height) return false;
    if (!r->held_ppu) { r->held_ppu=malloc(sizeof(Ppu)); if (!r->held_ppu) return false; }
    if (!r->advisor_pixels) {
        r->advisor_pixels=calloc(256*224,sizeof(*r->advisor_pixels));
        if (!r->advisor_pixels) return false;
    }
    size_t count=(size_t)v.width*v.height;
    if(r->defer_terrain && !ScTerrainResize(&r->terrain,v.width,v.height)) return false;
    if (count>r->capacity) {
        uint32_t *pixels=realloc(r->pixels,count*sizeof(*pixels));
        if (!pixels) return false;
        r->pixels=pixels; r->capacity=count;
    }
    r->view=r->gameplay_view=v;
    memset(r->city_cache,0,sizeof r->city_cache);r->city_cache_next=0;
    memset(r->pixels,0,count*sizeof(*r->pixels));
    return true;
}
void ScRendererDestroy(ScRenderer *r) {
    ScTerrainDestroy(&r->terrain);
    free(r->pixels); free(r->held_ppu); free(r->advisor_pixels); memset(r,0,sizeof(*r));
}
bool ScRendererDeferTerrain(ScRenderer *r,bool enabled) {
    if(enabled && !ScTerrainResize(&r->terrain,r->view.width,r->view.height)) return false;
    if(!enabled && r->defer_terrain) {
        for(int y=0;y<r->view.height;++y) for(int x=0;x<r->view.width;++x) {
            size_t at=(size_t)y*r->view.width+x;
            if(r->pixels[at]==SC_TERRAIN_PIXEL) r->pixels[at]=ScTerrainPixel(&r->terrain,x,y);
            else if(r->pixels[at]==SC_NATIVE_PIXEL) r->pixels[at]=ScNativePixel(&r->terrain,x,y);
            else if(SC_IS_RELOCATED_NATIVE(r->pixels[at])) r->pixels[at]=ScRelocatedNativePixel(&r->terrain,r->pixels[at]);
        }
        r->terrain.deferred=0;
    }
    r->defer_terrain=enabled;return true;
}
uint32_t ScRendererPixel(const ScRenderer *r,int x,int y) {
    uint32_t pixel=r->pixels[(size_t)y*r->view.width+x];
    return pixel==SC_TERRAIN_PIXEL?ScTerrainPixel(&r->terrain,x,y):
           pixel==SC_NATIVE_PIXEL?ScNativePixel(&r->terrain,x,y):
           SC_IS_RELOCATED_NATIVE(pixel)?ScRelocatedNativePixel(&r->terrain,pixel):pixel;
}
uint32_t ScRendererPresentationPixel(const ScRenderer *r,unsigned x,unsigned y,
    unsigned width,unsigned height) {
    /* Match nearest texture sampling at exact texel boundaries: the lower
     * texel wins. This keeps the existing HUD footprint at half-integer scales. */
    unsigned sx=(2*x+1)*r->view.width-1,sy=(2*y+1)*r->view.height-1;
    unsigned nx=sx/(2*width),ny=sy/(2*height);
    uint32_t pixel=r->pixels[(size_t)ny*r->view.width+nx];
    if(pixel!=SC_TERRAIN_PIXEL) return ScRendererPixel(r,nx,ny);
    unsigned fx=width==(unsigned)r->view.width?0:(sx%(2*width))*65536/(2*width);
    unsigned fy=height==(unsigned)r->view.height?0:(sy%(2*height))*65536/(2*height);
    return ScTerrainZoomPixel(&r->terrain,nx,ny,fx,fy,
        r->view.core_y+(r->zoom_hud?46:0));
}
bool ScRendererDeferNativeLine(ScRenderer *r,const Ppu *p,const uint8_t *ram,int line) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_NATIVE_PPU_REFERENCE");reference=e && *e=='1';}
    r->native_line=false;
    if(reference || !r->defer_terrain || !r->terrain.native || !p || !ram ||
       line<0 || line>=224 || !ScNativePpuSupported(p)) return false;
    if(line==0) find_wood(r,p,ram);
    if(!city_live(r,p,ram) || r->map_hold) return false;
    static int advisor_reference=-1;
    if(advisor_reference<0) {const char *e=getenv("SC_ADVISOR_PANEL_REFERENCE");advisor_reference=e && *e=='1';}
    bool advisor=(p->screenEnabled[0]&31)==20 && (p->screenEnabled[1]&31)==3 && !(PPU_mathEnabled(p)&20);
    if(advisor?advisor_reference:u16(ram,0xd7) || u16(ram,0x379) || ram[0x391] || ram[0xe3]) return false;
    r->native_line=true;return true;
}
/* Reference capture extracts the exact live row's two plane pairs. The Vulkan
 * path below instead names an immutable VRAM version for that scanline. */
static uint32_t terrain_planes(const ScRenderer *r,const Ppu *p,const uint8_t *ram,
                              int x,int y,bool roof,unsigned *word) {
    *word=0;
    bool large=r->map_hold?r->held_large:r->world && r->world->active;
    unsigned width=large?(r->map_hold?(r->held_mega?3840:r->held_colossal?1920:r->held_giant?960:r->held_huge?480:240):ScWorldWidth(r->world)):120;
    unsigned height=large?(r->map_hold?(r->held_mega?3200:r->held_colossal?1600:r->held_giant?800:r->held_huge?400:200):ScWorldHeight(r->world)):100;
    if(x<0 || y<0 || (unsigned)x>=width*8 || (unsigned)y>=height*8) return 0;
    const uint8_t *map=r->map_hold?r->held_map:large?r->world->tiles:ram+MAP;
    unsigned cell=u16(map,2*((y/8)*width+x/8))&1023;
    if(cell>=CELL_TYPES) return 0;
    size_t at=(roof?OVERLAYS:TILES)+2*cell;
    if(at+1>=r->rom_size) return 0;
    *word=u16(r->rom,at);
    if(roof && (*word&1023)==0x300) return 0;
    unsigned row=*word&0x8000?7-(y&7):y&7;
    unsigned base=(PPU_bgTileAdr(p,1)+(*word&1023)*16+row)&0x7fff;
    return p->vram[base]|(uint32_t)p->vram[(base+8)&0x7fff]<<16;
}
/* Capture VRAM only when it changes, rather than extracting two CHR pairs for
 * every viewport tile on every scanline. The shader resolves raw city cells.
 * Tests/foreign PPUs without a write revision use an exact live comparison. */
static bool terrain_resources(ScRenderer *r,const Ppu *p,unsigned *snapshot) {
    ScTerrainFrame *f=&r->terrain;
    if(!r->rom || r->rom_size<0x184eb+CELL_TYPES) return false;
    uint64_t revision=r->vram_revision && !r->map_hold?r->vram_revision(p):0;
    if(f->snapshots) {
        unsigned at=SC_RESOURCE_VRAM+(f->snapshots-1)*16384;
        bool same=r->vram_revision && !r->map_hold && r->captured_vram_source==p?
            revision==r->captured_vram_revision:!memcmp(f->resources+at,p->vram,65536);
        static int validate=-1;
        if(validate<0) {const char *e=getenv("SC_VRAM_REVISION_VALIDATE");validate=e && *e=='1';}
        if(validate && same && memcmp(f->resources+at,p->vram,65536)) {
            fprintf(stderr,"[terrain capture] stale VRAM revision\n");abort();
        }
        if(same) {*snapshot=at;return true;}
    }
    unsigned required=SC_RESOURCE_VRAM+(f->snapshots+1)*16384;
    if(required>f->resource_capacity) {
        unsigned versions=f->resource_capacity>SC_RESOURCE_VRAM?(f->resource_capacity-SC_RESOURCE_VRAM)/16384:1;
        unsigned capacity=SC_RESOURCE_VRAM+versions*2*16384;
        uint32_t *resources=realloc(f->resources,(size_t)capacity*sizeof *resources);
        if(!resources) return false;
        memset(resources+f->resource_capacity,0,(capacity-f->resource_capacity)*sizeof *resources);
        f->resources=resources;f->resource_capacity=capacity;
    }
    if(!f->snapshots) for(unsigned i=0;i<1024;++i) {
        f->resources[i]=i<CELL_TYPES?u16(r->rom,TILES+2*i)|(u16(r->rom,OVERLAYS+2*i)<<16):0x23000000;
        f->resources[1024+i]=i<CELL_TYPES?r->rom[0x184eb+i]:0;
    }
    if(!f->snapshots) {
        memset(f->resources+SC_RESOURCE_ROM,0,8192*4);
        if(r->rom_size>=0x38000)memcpy(f->resources+SC_RESOURCE_ROM,r->rom+0x30000,32768);
    }
    *snapshot=SC_RESOURCE_VRAM+f->snapshots*16384;
    memcpy(f->resources+*snapshot,p->vram,65536);++f->snapshots;
    r->captured_vram_revision=revision;r->captured_vram_source=p;return true;
}
static void capture_native_row(ScRenderer *r,const Ppu *p,unsigned y,unsigned line) {
    static int reference=-1,diagnostic=-1;
    if(reference<0) {const char *e=getenv("SC_GPU_NATIVE_BG_REFERENCE");reference=e && *e=='1';}
    if(diagnostic<0) {const char *e=getenv("SC_GPU_NATIVE_BG_DIAG");diagnostic=e && *e=='1';}
    if(!reference && !r->reference_terrain &&
       terrain_resources(r,p,&r->terrain.rows[y].chr_snapshot)) {
        ScNativePpuCaptureRaw(&r->terrain,p,y,line);
        static bool reported;
        if(diagnostic && !reported) {
            fprintf(stderr,"[native background] immutable VRAM descriptors enabled\n");reported=true;
        }
    } else ScNativePpuCapture(&r->terrain,p,y,line);
}
static uint32_t terrain_cell(const uint8_t *map,unsigned width,unsigned height,int x,int y) {
    if(x<0 || y<0 || (unsigned)x>=width*8 || (unsigned)y>=height*8) return UINT32_MAX;
    unsigned raw=u16(map,2*((y/8)*width+x/8));
    return (raw&1023)<CELL_TYPES?raw:UINT32_MAX;
}
/* Capture a contiguous city row once, rather than resolving three city cells
 * for every eight-pixel span of every scanline. Exact byte comparisons retain
 * earlier versions when simulation/construction changes the map mid-frame.
 * Padding includes both neighbour columns, and owner rows exclude the final
 * map row/column just as power_warning_cell's original bounds do. */
static bool terrain_city_row(ScRenderer *r,const uint8_t *map,unsigned width,unsigned height,
                             int first,int y,bool owner,unsigned *offset) {
    ScTerrainFrame *f=&r->terrain;unsigned count=f->stride+2;
    int begin=first<0?-first:0,end=(int)width-(owner?1:0)-first;
    if(begin>(int)count) begin=count;
    if(end>(int)count) end=count;
    if(end<begin || y<0 || (unsigned)y>=height-(owner?1:0)) end=begin;
    const uint8_t *live=end>begin?map+2*((size_t)y*width+first+begin):NULL;
    for(unsigned i=0;i<6;++i) {
        const ScCityRowCache *cache=r->city_cache+i;
        if(cache->map==map && cache->width==width && cache->height==height &&
           cache->first==first && cache->y==y && cache->count==count && cache->owner==owner &&
           (!live || !memcmp((const uint8_t *)f->city+cache->offset*2+begin*2,live,(end-begin)*2))) {
            *offset=cache->offset;return true;
        }
    }
    unsigned words=(count+1)/2,required=f->city_words+words;
    if(required>f->city_capacity) {
        unsigned capacity=f->city_capacity?f->city_capacity*2:4096;
        if(capacity<required) capacity=required;
        uint32_t *city=realloc(f->city,(size_t)capacity*sizeof *city);
        if(!city) return false;
        f->city=city;f->city_capacity=capacity;
    }
    *offset=f->city_words*2;uint32_t *captured=f->city+f->city_words;
    memset(captured,255,words*sizeof *captured);
    /* City and VRAM snapshot protocols both use little-endian packed words. */
    if(live) memcpy((uint8_t *)captured+begin*2,live,(end-begin)*2);
    f->city_words=required;
    r->city_cache[r->city_cache_next++%6]=(ScCityRowCache){map,first,y,width,height,count,*offset,owner};
    return true;
}
/* Keep immutable OAM/vehicle records, rather than drawing their pixels into a
 * full-width CPU overlay. Spatial buckets bound shader work to sprites that
 * can cover this 32px span; no pixel scans the complete 128-slot OAM table. */
static bool terrain_objects_grid(ScRenderer *r,const Ppu *p,int y) {
    ScTerrainFrame *f=&r->terrain;ScTerrainRow *row=f->rows+y+r->view.core_y;
    unsigned object_width=r->zoom_frame?f->stride*8-7:f->width;
    if(object_width>SC_MAX_MAP_SPAN)object_width=SC_MAX_MAP_SPAN;
    unsigned key[6]={object_width,(unsigned)r->view.core_x,p->obsel,p->oamaddl,
        (unsigned)r->vehicle_count,(r->city_input && (r->pointer_active || r->pointer_hidden || r->split_hud)?1u:0u)|(r->pan_frame?2u:0u)|(r->zoom_frame?4u:0u)|(r->zoom_hud?8u:0u)};
    if(!r->object_grid_valid || memcmp(key,r->object_grid_key,sizeof key) ||
       memcmp(p->oam,r->object_grid_oam,sizeof p->oam) ||
       memcmp(p->highOam,r->object_grid_high,sizeof p->highOam) ||
       memcmp(r->object_x,r->object_grid_x,sizeof r->object_x) ||
       memcmp(r->object_y,r->object_grid_y,sizeof r->object_y) ||
       memcmp(r->object_grace,r->object_grid_grace,sizeof r->object_grace) ||
       memcmp(r->vehicles,r->object_grid_vehicles,r->vehicle_count*sizeof *r->vehicles)) {
        uint32_t records[128+SC_VEHICLE_SPRITES][6];unsigned count=0,columns=(object_width+31)/32;
        unsigned sizes[SC_MAX_MAP_SPAN/4]={0}; /* eight wrapped Y buckets */
        int first=PPU_objPriority(p)?(p->oamaddl&0xfe)/2:0;
        for(int k=-r->vehicle_count;k<128;++k) {
            int left,top,size,clip=0;unsigned attr,chr=0;
            if(k<0) {
                const ScVehicleSprite *v=&r->vehicles[k+r->vehicle_count];
                left=v->x+r->view.core_x;top=v->y;clip=r->view.core_x+256;
                size=v->host?(v->large?16:8):sprite_sizes[PPU_objSize(p)][v->large?1:0];attr=v->host?v->attr:p->oam[v->slot*2+1];
                if(v->host) {clip=0;chr=v->rom_chr;}
            } else {
                int slot=(first+127-k)&127,index=slot*2;
                if(((key[5]&4) && (slot<4 || ((key[5]&8) && (slot<=38 || (slot>=64 && slot<=108))))) || ((key[5]&1) && slot<4) || ((key[5]&2) && slot>=39 && slot<=52) || !r->object_grace[slot]) continue;
                bool replaced=false;
                if(r->camera_x || r->camera_y) for(int v=0;v<r->vehicle_count;++v)
                    if(r->vehicles[v].slot==slot) {replaced=true;break;}
                if(replaced) continue;
                left=r->object_x[slot]+r->view.core_x;top=r->object_y[slot];
                size=sprite_sizes[PPU_objSize(p)][(p->highOam[index/8]>>(index%8+1))&1];
                attr=p->oam[index+1]|0x10000; /* native OAM wraps Y modulo 256 */
            }
            if(r->camera_x || r->camera_y) {
                left-=(int)lround(r->camera_x);top-=(int)lround(r->camera_y);
                clip=0;if(k>=0) attr&=~0x10000u; /* full Y, never wrap distant objects */
            }
            int begin=left>clip?left:clip,end=left+size;
            if(begin<0) begin=0;
            if(end>(int)object_width) end=object_width;
            if(begin>=end) continue;
            uint32_t *record=records[count++];
            record[0]=(uint32_t)left;record[1]=size;record[2]=(uint32_t)top;record[3]=attr;
            record[4]=chr?chr:(unsigned)(attr&0x100?PPU_objTileAdr2(p):PPU_objTileAdr1(p));record[5]=(uint32_t)clip;
            unsigned vfirst=((unsigned)top&255)/32,vcount=(((unsigned)top&31)+size+31)/32;
            for(unsigned v=0;v<vcount;++v) for(unsigned b=(unsigned)begin/32;b<=(unsigned)(end-1)/32;++b)
                ++sizes[((vfirst+v)&7)*columns+b];
        }
        unsigned header=UINT32_MAX;
        if(count) {
            unsigned buckets=columns*8,words=1+buckets*2;
            for(unsigned b=0;b<buckets;++b) words+=sizes[b]*6;
            unsigned required=f->city_words+words;
            if(required>f->city_capacity) {
                unsigned capacity=f->city_capacity?f->city_capacity*2:4096;
                if(capacity<required) capacity=required;
                uint32_t *city=realloc(f->city,(size_t)capacity*sizeof *city);
                if(!city) return false;
                f->city=city;f->city_capacity=capacity;
            }
            header=f->city_words;unsigned cursor=header+1+buckets*2;f->city[header]=columns;
            unsigned positions[SC_MAX_MAP_SPAN/4];
            for(unsigned b=0;b<buckets;++b) {
                f->city[header+1+b*2]=positions[b]=cursor;f->city[header+2+b*2]=sizes[b];cursor+=sizes[b]*6;
            }
            /* Scatter each immutable sprite only into the buckets it touches.
             * Avoid scanning the whole fleet again for every viewport bucket. */
            for(unsigned i=0;i<count;++i) {
                const uint32_t *record=records[i];int left=(int32_t)record[0],clip=(int32_t)record[5];
                int begin=left>clip?left:clip,end=left+(int)record[1];
                if(begin<0)begin=0;
                if(end>(int)object_width)end=object_width;
                unsigned top=record[2]&255,vfirst=top/32,vcount=((top&31)+record[1]+31)/32;
                for(unsigned v=0;v<vcount;++v)for(unsigned x=begin/32;x<=(unsigned)(end-1)/32;++x) {
                    unsigned bucket=((vfirst+v)&7)*columns+x;
                    memcpy(f->city+positions[bucket],record,6*sizeof *record);positions[bucket]+=6;
                }
            }
            f->city_words=required;
        }
        r->object_grid_header=header;memcpy(r->object_grid_key,key,sizeof key);
        memcpy(r->object_grid_oam,p->oam,sizeof p->oam);memcpy(r->object_grid_high,p->highOam,sizeof p->highOam);
        memcpy(r->object_grid_x,r->object_x,sizeof r->object_x);memcpy(r->object_grid_y,r->object_y,sizeof r->object_y);
        memcpy(r->object_grid_grace,r->object_grace,sizeof r->object_grace);
        memcpy(r->object_grid_vehicles,r->vehicles,r->vehicle_count*sizeof *r->vehicles);r->object_grid_valid=true;
    }
    row->reserved=r->object_grid_header;
    row->world_y=(row->world_y&7)|((uint32_t)y<<8);
    row->math|=SC_ROW_GPU_OBJECTS|SC_ROW_OBJECT_GRID|(y>=0 && y<224?SC_ROW_OBJECT_CORE:0);
    return true;
}
static bool terrain_objects(ScRenderer *r,const Ppu *p,int y) {
    static int grid_reference=-1;
    if(grid_reference<0) {const char *e=getenv("SC_GPU_OBJECT_GRID_REFERENCE");grid_reference=e && *e=='1';}
    if(!grid_reference) return terrain_objects_grid(r,p,y);
    ScTerrainFrame *f=&r->terrain;ScTerrainRow *row=f->rows+y+r->view.core_y;
    uint32_t records[128+SC_VEHICLE_SPRITES][6];unsigned count=0,buckets=(f->width+31)/32;
    unsigned sizes[128]={0}; /* maximum canvas width 4096 / bucket width 32 */
    bool core=y>=0 && y<224;
    int first=PPU_objPriority(p)?(p->oamaddl&0xfe)/2:0;
    for(int k=-r->vehicle_count;k<128;++k) {
        int left,size,dy,clip=0;unsigned attr,chr=0;
        if(k<0) {
            const ScVehicleSprite *v=&r->vehicles[k+r->vehicle_count];
            left=v->x+r->view.core_x;clip=r->view.core_x+256;
            size=v->host?(v->large?16:8):sprite_sizes[PPU_objSize(p)][v->large?1:0];dy=y-v->y;
            attr=v->host?v->attr:p->oam[v->slot*2+1];if(v->host) {clip=0;chr=v->rom_chr;}
        } else {
            int slot=(first+127-k)&127,index=slot*2;
            if((r->city_input && (r->pointer_active || r->pointer_hidden || r->split_hud) && slot<4) ||
               (r->pan_frame && slot>=39 && slot<=52) || !r->object_grace[slot]) continue;
            left=r->object_x[slot]+r->view.core_x;
            size=sprite_sizes[PPU_objSize(p)][(p->highOam[index/8]>>(index%8+1))&1];
            dy=(y-r->object_y[slot])&255;attr=p->oam[index+1];
        }
        if(dy<0 || dy>=size) continue;
        int begin=left>clip?left:clip,end=left+size;
        if(begin<0) begin=0;
        if(end>(int)f->width) end=f->width;
        if(begin>=end || (core && begin>=r->view.core_x && end<=r->view.core_x+256)) continue;
        uint32_t *record=records[count++];
        record[0]=(uint32_t)left;record[1]=size;record[2]=dy;record[3]=attr;
        record[4]=chr?chr:(unsigned)(attr&0x100?PPU_objTileAdr2(p):PPU_objTileAdr1(p));record[5]=(uint32_t)clip;
        for(unsigned b=(unsigned)begin/32;b<=(unsigned)(end-1)/32;++b) ++sizes[b];
    }
    row->reserved=UINT32_MAX;
    if(count) {
        unsigned words=1+buckets*2;
        for(unsigned b=0;b<buckets;++b) words+=sizes[b]*6;
        unsigned required=f->city_words+words;
        if(required>f->city_capacity) {
            unsigned capacity=f->city_capacity?f->city_capacity*2:4096;
            if(capacity<required) capacity=required;
            uint32_t *city=realloc(f->city,(size_t)capacity*sizeof *city);
            if(!city) return false;
            f->city=city;f->city_capacity=capacity;
        }
        unsigned header=f->city_words,cursor=header+1+buckets*2;
        f->city[header]=buckets;row->reserved=header;
        for(unsigned b=0;b<buckets;++b) {
            f->city[header+1+b*2]=cursor;f->city[header+2+b*2]=sizes[b];
            for(unsigned i=0;i<count;++i) {
                const uint32_t *record=records[i];int left=(int32_t)record[0],clip=(int32_t)record[5];
                int begin=left>clip?left:clip,end=left+(int)record[1];
                if(end<=(int)(b*32) || begin>=(int)(b*32+32)) continue;
                memcpy(f->city+cursor,record,6*sizeof *record);cursor+=6;
            }
        }
        f->city_words=required;
    }
    row->math|=SC_ROW_GPU_OBJECTS|(core?SC_ROW_OBJECT_CORE:0);
    return true;
}
static void terrain_row(ScRenderer *r,const Ppu *p,const uint8_t *ram,int y,int sx,int sy) {
    ScTerrainFrame *f=&r->terrain;unsigned ay=y+r->view.core_y;
    int start=sx-r->view.core_x;unsigned phase=(unsigned)start&7;
    ScTerrainRow *row=f->rows+ay;
    memset(f->overlays+(size_t)ay*f->width,0,f->width*sizeof *f->overlays);
    row->phase=phase;row->core_x=r->view.core_x;row->main=p->screenEnabled[0];row->sub=p->screenEnabled[1];
    row->window_main=p->screenWindowed[0];row->window_sub=p->screenWindowed[1];
    row->windows=p->windowsel;row->logic=p->wbgobjlog;
    row->bounds=p->window1left|(uint32_t)p->window1right<<8|(uint32_t)p->window2left<<16|(uint32_t)p->window2right<<24;
    row->math=p->cgadsub;row->control=p->cgwsel;row->fixed=p->fixedColor;
    bool repair=y>=0 && y<224 && gpu_native_repair(r,p);
    bool hud=u16(ram,0x1d7)!=0;
    bool staging=repair && r->scroll_repair && !u16(ram,0xd7) && !u16(ram,0x379) && !ram[0x391];
    if(repair) row->math|=SC_ROW_NATIVE_REPAIR|(staging?SC_ROW_STAGING_CHECK:0)|
        (hud?SC_ROW_CITY_HUD:0)|(hud && y<46?SC_ROW_HUD_TOP:0);
    bool large=r->map_hold?r->held_large:r->world && r->world->active;
    unsigned width=large?(r->map_hold?(r->held_mega?3840:r->held_colossal?1920:r->held_giant?960:r->held_huge?480:240):ScWorldWidth(r->world)):120;
    unsigned height=large?(r->map_hold?(r->held_mega?3200:r->held_colossal?1600:r->held_giant?800:r->held_huge?400:200):ScWorldHeight(r->world)):100;
    const uint8_t *map=r->map_hold?r->held_map:large?r->world->tiles:ram+MAP;
    static int capture_reference=-1;
    if(capture_reference<0) {const char *e=getenv("SC_TERRAIN_CAPTURE_REFERENCE");capture_reference=e && *e=='1';}
    bool raw=(r->zoom_frame || (!capture_reference && !r->reference_terrain)) && terrain_resources(r,p,&row->chr_snapshot);
    if(raw) {
        row->math|=SC_ROW_RAW_TERRAIN;row->chr_base=PPU_bgTileAdr(p,1);
        row->warning_base=PPU_bgTileAdr(p,0);row->world_y=(unsigned)(sy+y+1)&7;
    }
    static int spans_reference=-1;
    if(spans_reference<0) {const char *e=getenv("SC_CITY_SPANS_REFERENCE");spans_reference=e && *e=='1';}
    int first=(start-(int)phase)/8-1,wy=sy+y+1;
    /* Negative pixel coordinates must floor, rather than truncate toward 0. */
    int city_y=wy>=0?wy/8:-1-((-1-wy)/8);
    bool spans=raw && !spans_reference &&
        terrain_city_row(r,map,width,height,first,city_y,false,&row->city_base) &&
        terrain_city_row(r,map,width,height,first,city_y+1,false,&row->city_roof) &&
        terrain_city_row(r,map,width,height,first,r->map_hold?-1:city_y-1,true,&row->city_owner);
    unsigned first_tile=0,last_tile=f->stride;
    if(spans) {
        row->math|=SC_ROW_CITY_SPANS;
        memset(f->tiles+(size_t)ay*f->stride,0,f->stride*sizeof *f->tiles);
        /* Only the original 256px cache needs host repair metadata. */
        if(!repair) last_tile=0;
        else {
            first_tile=((unsigned)r->view.core_x+phase)/8;
            last_tile=((unsigned)r->view.core_x+phase+263)/8;
            if(last_tile>f->stride) last_tile=f->stride;
        }
    }
    for(unsigned i=0;i<32;++i) row->brightness[i]=p->brightnessMult[i];
    for(unsigned i=0;i<256;++i) f->palette[(size_t)ay*256+i]=p->cgram[i];
    if(p->screenEnabled[1]&4) {
        int yy=y<0?0:y>223?223:y;
        static int sub_bg_reference=-1;
        if(sub_bg_reference<0) {const char *e=getenv("SC_GPU_SUB_BG_REFERENCE");sub_bg_reference=e && *e=='1';}
        if(raw && !sub_bg_reference && ScNativePpuSupported(p)) {
            row->math|=SC_ROW_GPU_SUB_BG;
            row->sub_bg[0]=p->hScroll[2];row->sub_bg[1]=(yy+1+p->vScroll[2])&1023;
            row->sub_bg[2]=PPU_bgTilemapAdr(p,2);row->sub_bg[3]=PPU_bgTileAdr(p,2);
            row->sub_bg[4]=(PPU_bgTilemapWider(p,2)?1u:0)|(PPU_bgTilemapHigher(p,2)?2u:0);
        } else for(int x=0;x<256;++x) {
                if(y>=0 && y<224 && x>=8 && x<248) continue;
                row->sub_bg[x]=bg_pixel(p,2,x,yy+1);
        }
    }
    for(unsigned i=first_tile;i<last_tile;++i) {
        int wx=start-(int)phase+(int)i*8,wy=sy+y+1;unsigned base,roof;
        ScTerrainTile *t=f->tiles+(size_t)ay*f->stride+i;
        bool warning=false;
        if(raw) {
            if(spans) *t=ScTerrainRawTile(f,ay,i);
            else {
            t->base=terrain_cell(map,width,height,wx,wy);
            t->roof=terrain_cell(map,width,height,wx+8,wy+8);
            t->reserved=!r->map_hold && wx>=8 && wy>=8 &&
                (unsigned)wx<width*8 && (unsigned)wy<height*8?
                terrain_cell(map,width,height,wx-8,wy-8):UINT32_MAX;
            }
            t->attributes=t->expected=t->staged=0;
            /* Repair validation covers just the original 256px cache. The
             * extended canvas's lookups/planes/warnings run entirely on GPU. */
            int left=(int)i*8-(int)phase-r->view.core_x;
            if(!repair || left>=256 || left+8<=0) continue;
            base=t->base==UINT32_MAX?0:f->resources[t->base&1023]&65535;
            roof=t->roof==UINT32_MAX?0:f->resources[t->roof&1023]>>16;
            unsigned owner=t->reserved,id=owner&1023;
            warning=owner!=UINT32_MAX && !(owner&0x8000) &&
                (f->resources[1024+id]&1) && id!=0x27c && id!=0x28c;
        } else {
        t->base=terrain_planes(r,p,ram,wx,wy,false,&base);
        t->roof=terrain_planes(r,p,ram,wx+8,wy+8,true,&roof);
        t->attributes=((base>>10)&7)*16|(((roof>>10)&7)*16)<<8|
            (base&0x4000?1u<<16:0)|(roof&0x4000?1u<<17:0);t->reserved=0;t->expected=t->staged=0;
        warning=!r->map_hold && power_warning_cell(r,ram,wx,wy);
        if(warning) {
            unsigned at=(PPU_bgTileAdr(p,0)+0x376*16+(wy&7))&0x7fff;
            t->reserved=p->vram[at]|(uint32_t)p->vram[(at+8)&0x7fff]<<16;
        }
        }
        int left=(int)i*8-(int)phase-r->view.core_x,right=left+8;
        if(!repair || left>=256 || right<=0) continue;
        int point=left<0?0:left,limit=right>256?256:right;
        unsigned roof_expected=0x2300;
        bool valid=raw?t->base!=UINT32_MAX:wx>=0 && wy>=0 && (unsigned)wx<width*8 && (unsigned)wy<height*8 &&
            (u16(map,2*((wy/8)*width+wx/8))&1023)<CELL_TYPES;
        if(wx+8>=0 && wy+8>=0 && (unsigned)(wx+8)<width*8 && (unsigned)(wy+8)<height*8 &&
           (u16(map,2*(((wy+8)/8)*width+(wx+8)/8))&1023)<CELL_TYPES) roof_expected=roof;
        t->expected=base|(roof_expected<<16);
        t->attributes|=(valid?SC_TILE_VALID:0)|(warning?SC_TILE_POWER_WARNING:0);
        if(changed_cell(r,wx,wy) || changed_cell(r,wx+8,wy+8)) t->attributes|=SC_TILE_EDITED;
        bool may_clear=bg_word(p,0,point,y+1)==0x1376 || bg_word(p,0,limit-1,y+1)==0x1376;
        if(may_clear && powered_owner_near(r,ram,wx,wy)) t->attributes|=SC_TILE_CLEAR_WARNING;
        int land=point;
        if(hud && land<56) land=56;
        if(staging && (!hud || y>=46) && land<limit) {
            unsigned staged_base=bg_word(p,1,land,y+1),staged_roof=bg_word(p,0,land,y+1);
            t->staged=staged_base|(staged_roof<<16);
            bool bad=!valid || staged_base!=base || ((p->screenEnabled[0]&1) &&
                staged_roof!=roof_expected && !(staged_roof==0x1376 && warning));
            if(bad) r->staged_mismatches+=limit-land;
        }
    }
}
/* Project only the live land layer. Native HUD/menu rows are composed later
 * at their original size; the native 32-column city cache is never sampled
 * as zoomed land. The CPU fallback resolves the same immutable spans as GPU. */
static void zoom_city_row(ScRenderer *r,const Ppu *p,const uint8_t *ram,int y,bool core_only) {
    int sx=r->scroll_x+r->scroll_adjust_x+scroll_delta(p->hScroll[1],r->scroll_h);
    int sy=r->scroll_y+r->scroll_adjust_y+scroll_delta(p->vScroll[1],r->scroll_v);
    int virtual_y=zoom_local(r,y,true);
    int x0=r->view.core_x+zoom_local(r,-r->view.core_x,false);
    unsigned ay=y+r->view.core_y;
    if(!core_only) {
        terrain_row(r,p,ram,y,sx+x0,sy+virtual_y-y);
        ScTerrainRow *row=r->terrain.rows+ay;
        row->zoom_step=zoom_step(r);row->zoom_x=x0;
        row->zoom_fraction=(uint32_t)((int64_t)(r->view.core_x+(r->zoom_hud?56:0))*(65536-(int64_t)row->zoom_step))&65535;
        row->math&=~(SC_ROW_NATIVE_REPAIR|SC_ROW_STAGING_CHECK|SC_ROW_OBJECT_CORE|SC_ROW_CITY_HUD|SC_ROW_HUD_TOP);
        row->math|=SC_ROW_CITY_ZOOM;
        if(!r->advisor_frame && !r->city_overlay_frame)terrain_objects_grid(r,p,y);
        else {
            row->math&=~SC_ROW_GPU_OBJECTS;
            if(r->advisor_frame)row->math|=SC_ROW_ADVISOR_BACKGROUND;
        }
        row->math&=~SC_ROW_OBJECT_CORE;
        row->world_y=(row->world_y&7)|((uint32_t)virtual_y<<8);
    }
    uint32_t *out=r->pixels+(size_t)ay*r->view.width;
    int first=core_only?r->view.core_x:0,end=core_only?r->view.core_x+256:r->view.width;
    if(r->zoom_hud && !r->advisor_frame && !r->city_overlay_frame && y>=0 && y<46)return;
    /* Project continuous land spans, then apply fixed UI ink. */
    bool deferred=r->defer_terrain && !PPU_forcedBlank(p);
    if(deferred) {
        for(int x=first;x<end;++x)out[x]=SC_TERRAIN_PIXEL;
        if(end>first)r->terrain.deferred+=(unsigned)(end-first);
    } else for(int x=first;x<end;++x)out[x]=ScTerrainPixel(&r->terrain,x,ay);
    if(!core_only || !r->zoom_hud || r->advisor_frame || r->city_overlay_frame || y<46 || y>=224)return;
    /* The toolbox is BG3 plus fixed UI objects. Recompose that artwork over
     * projected land, never the native city cache or its building roofs. */
    unsigned ranks[56]={0};
    if(p->screenEnabled[0]&4)for(int x=0;x<56;++x) {
        if((p->screenWindowed[0]&4) && window_contains(p,2,x))continue;
        bool high=false;unsigned ci=bg_sample(p,2,x,y+1,&high);
        if(ci) {
            out[r->view.core_x+x]=composite_color(p,ci,2,0,5,x);
            ranks[x]=high?(PPU_bg3priority(p)?15:3):1;
        }
    }
    if(p->screenEnabled[0]&16)for(int slot=108;slot>=34;--slot) {
        if(slot>38 && slot<64)continue; /* land sprites and navigation */
        int ox=sprite_x(p,slot);if(ox>=256)ox-=512;
        int dy=y-(p->oam[slot*2]>>8);
        int index=slot*2,size=sprite_sizes[PPU_objSize(p)][(p->highOam[index/8]>>(index%8+1))&1];
        if(dy<0 || dy>=size || ox>=56 || ox+size<=0)continue;
        unsigned rank=2+4*((p->oam[index+1]>>12)&3);
        for(int x=ox<0?0:ox;x<ox+size && x<56;++x) {
            if(rank<=ranks[x] || ((p->screenWindowed[0]&16) && window_contains(p,4,x)))continue;
            unsigned ci=sprite_pixel(p,slot,x-ox,dy);
            if(ci)out[r->view.core_x+x]=composite_color(p,ci,ci<192?6:4,0,5,x);
        }
    }
}
void ScRendererResetHistory(ScRenderer *r) {
    r->scroll_repair=false;r->staged_mismatches=0;
    r->scroll_valid=r->objects_valid=r->map_valid=r->map_hold=r->title_live=false;
    r->city_input=r->pointer_active=r->pointer_hud=r->pointer_hidden=false;
    memset(r->changed_cells,0,sizeof r->changed_cells);
}
void ScRendererResetCamera(ScRenderer *r) {
    r->scroll_x-=(int)lround(r->camera_x);r->scroll_y-=(int)lround(r->camera_y);
    r->camera_x=r->camera_y=0;r->object_grid_valid=false;
}
void ScRendererZoom(ScRenderer *r,double zoom) {
    if(!isfinite(zoom) || zoom<=0)return;
    unsigned old_step=zoom_step(r);
    int old_x=(int)lround(r->camera_x),old_y=(int)lround(r->camera_y);
    r->map_zoom=zoom;
    double delta=((double)old_step-zoom_step(r))/65536;
    double x=r->view.width*.5-r->view.core_x-(r->zoom_hud?56:0);
    double y=r->view.height*.5-r->view.core_y-(r->zoom_hud?46:0);
    r->camera_x+=x*delta;r->camera_y+=y*delta;
    r->scroll_x+=(int)lround(r->camera_x)-old_x;
    r->scroll_y+=(int)lround(r->camera_y)-old_y;
    r->object_grid_valid=false;
}
void ScRendererPan(ScRenderer *r,double dx,double dy) {
    if(!isfinite(dx) || !isfinite(dy) || (!dx && !dy)) return;
    double zoom=r->map_zoom>0?r->map_zoom:1;
    unsigned width=r->world && r->world->active?ScWorldWidth(r->world):120;
    unsigned height=r->world && r->world->active?ScWorldHeight(r->world):100;
    int old_x=(int)lround(r->camera_x),old_y=(int)lround(r->camera_y);
    int base_x=r->scroll_x+r->scroll_adjust_x-old_x;
    int base_y=r->scroll_y+r->scroll_adjust_y-old_y;
    /* Clamp to the actual visible world rectangle, never the cartridge's
     * 25x22-cell bounds. When the map fits inside the view, allow it to be
     * dragged between both edges rather than losing it in empty overscan. */
    double ox=r->zoom_hud?56:0,oy=r->zoom_hud?46:0;
    double left=r->zoom_hud && !r->view.core_x?ox:ox+(-r->view.core_x-ox)/zoom;
    double top=r->zoom_hud && !r->view.core_y?oy:oy+(-r->view.core_y-oy)/zoom;
    double right=ox+(r->view.width-r->view.core_x-ox)/zoom;
    double bottom=oy+(r->view.height-r->view.core_y-oy)/zoom;
    /* Equal overscan on all four sides lets the far-right tiles move clear
     * of the viewport just like the far-left ones, at every map size. */
    double slack_x=64/zoom,slack_y=64/zoom;
    double min_x=fmin(-left,width*8-right)-slack_x;
    double max_x=fmax(-left,width*8-right)+slack_x;
    double min_y=fmin(-top,height*8-bottom)-slack_y;
    double max_y=fmax(-top,height*8-bottom)+slack_y;
    /* A zoom change or loaded native camera may already lie beyond these
     * viewport bounds. Let the drag bring it back without an initial jump. */
    min_x=fmin(min_x,base_x+r->camera_x);max_x=fmax(max_x,base_x+r->camera_x);
    min_y=fmin(min_y,base_y+r->camera_y);max_y=fmax(max_y,base_y+r->camera_y);
    double x=fmax(min_x,fmin(max_x,base_x+r->camera_x+dx));
    double y=fmax(min_y,fmin(max_y,base_y+r->camera_y+dy));
    r->camera_x=x-base_x;r->camera_y=y-base_y;
    r->scroll_x+=(int)lround(r->camera_x)-old_x;
    r->scroll_y+=(int)lround(r->camera_y)-old_y;
    r->object_grid_valid=false;
}
void ScRendererBeginMapLoad(ScRenderer *r) {
    if (!r->map_valid || r->map_hold) return;
    r->map_hold=true; r->map_dark=false;
    r->map_age=0;
}
static void render_row(ScRenderer *r,const Ppu *p,const uint8_t *ram,int y) {
    if(r->zoom_frame) {r->city_frame=true;zoom_city_row(r,p,ram,y,false);return;}
    uint32_t *out=r->pixels+(size_t)(y+r->view.core_y)*r->view.width;
    bool city=city_live(r,p,ram);
    r->city_frame|=city;
    /* WRAM supplies cells, the PPU supplies the 2-pixel steps. At a tile
     * boundary WRAM can lag a frame; preserve measured PPU motion then. */
    int sx=r->scroll_x+r->scroll_adjust_x+scroll_delta(p->hScroll[1],r->scroll_h);
    int sy=r->scroll_y+r->scroll_adjust_y+scroll_delta(p->vScroll[1],r->scroll_v);
    if (city && r->map_hold) {
        sx=r->held_x; sy=r->held_y;
        /* Freeze city graphics, palette and camera, but use the live fade and
         * color-math/window controls. No guest PPU state is changed. */
        r->held_ppu->inidisp=p->inidisp;
        memcpy(r->held_ppu->brightnessMult,p->brightnessMult,sizeof p->brightnessMult);
        r->held_ppu->cgadsub=p->cgadsub; r->held_ppu->cgwsel=p->cgwsel;
        r->held_ppu->fixedColor=p->fixedColor;
        memcpy(r->held_ppu->screenEnabled,p->screenEnabled,sizeof p->screenEnabled);
        memcpy(r->held_ppu->screenWindowed,p->screenWindowed,sizeof p->screenWindowed);
        r->held_ppu->windowsel=p->windowsel; r->held_ppu->wbgobjlog=p->wbgobjlog;
        r->held_ppu->window1left=p->window1left; r->held_ppu->window1right=p->window1right;
        r->held_ppu->window2left=p->window2left; r->held_ppu->window2right=p->window2right;
        p=r->held_ppu;
    }
    uint16_t objects[SC_MAX_CANVAS], marks[SC_MAX_CANVAS];
    uint64_t measured;
    static int advisor_reference=-1;
    if(advisor_reference<0) {const char *e=getenv("SC_ADVISOR_GPU_REFERENCE");advisor_reference=e && *e=='1';}
    bool advisor_gpu=r->advisor_frame && !advisor_reference && ScNativePpuSupported(p);
    bool deferred=city && r->defer_terrain && (!r->advisor_frame || advisor_gpu) && !PPU_forcedBlank(p);
    measured=MEASURE_BEGIN(r);
    if(deferred) terrain_row(r,p,ram,y,sx,sy);
    MEASURE_END(r,SC_RENDER_TERRAIN,measured);
    if(deferred && advisor_gpu) {
        unsigned ay=y+r->view.core_y;
        if(y>=0 && y<224) {
            unsigned policy=r->terrain.rows[ay].math&~255u;
            capture_native_row(r,p,ay,y+1);
            r->terrain.rows[ay].math|=policy;
        }
        r->terrain.rows[ay].math|=SC_ROW_ADVISOR_BACKGROUND|(y>=0 && y<224?SC_ROW_ADVISOR_CORE:0);
        for(int x=0;x<r->view.width;++x) out[x]=SC_TERRAIN_PIXEL;
        r->terrain.deferred+=r->view.width;
        return;
    }
    static int object_reference=-1;
    if(object_reference<0) {const char *e=getenv("SC_GPU_OBJECT_REFERENCE");object_reference=e && *e=='1';}
    measured=MEASURE_BEGIN(r);
    bool gpu_objects=deferred && !object_reference && ScNativePpuSupported(p) &&
        (y<0 || y>=224 || r->native_line) &&
        (r->terrain.rows[y+r->view.core_y].math&SC_ROW_RAW_TERRAIN) && terrain_objects(r,p,y);
    if(city && !gpu_objects) object_row(r,p,y,objects);
    MEASURE_END(r,SC_RENDER_OBJECTS,measured);
    if(deferred) {
        ScTerrainOverlay *overlays=r->terrain.overlays+(size_t)(y+r->view.core_y)*r->view.width;
        bool hud=u16(ram,0x1d7)!=0;
        if(gpu_objects && hud && y<224) r->terrain.rows[y+r->view.core_y].math|=
            SC_ROW_CITY_HUD|(y<46?SC_ROW_HUD_TOP:0);
        if(gpu_objects) {
            /* Fill contiguous GPU spans, rather than branching per pixel on
             * native-core ownership. The intervening core is captured below. */
            int begin=r->view.width,end=r->view.width;
            if(y>=0 && y<224) {
                begin=r->view.core_x+((r->repaired_edges[y]&1)?8:0);
                end=r->view.core_x+((r->repaired_edges[y]&2)?248:256);
                if(begin>r->view.width) begin=r->view.width;
                if(end>r->view.width) end=r->view.width;
            }
            for(int x=0;x<begin;++x) out[x]=SC_TERRAIN_PIXEL;
            for(int x=end;x<r->view.width;++x) out[x]=SC_TERRAIN_PIXEL;
            r->terrain.deferred+=begin+r->view.width-end;
            return;
        }
        for(int x=0;x<r->view.width;++x) {
            int local=x-r->view.core_x;
            if(y>=0 && y<224 && local>=0 && local<256 &&
               !((local<8 && (r->repaired_edges[y]&1)) || (local>=248 && (r->repaired_edges[y]&2)))) continue;
            if(!gpu_objects) overlays[x].object=objects[x]|(hud && (y<46 || (local<56 && y<224))?UINT32_C(0x80000000):0);
            out[x]=SC_TERRAIN_PIXEL;++r->terrain.deferred;
        }
        return;
    }
    r->selector_row=NULL;
    if (!city && (r->selector_count || r->sign_count)) {
        host_sprites_row(r,p,y,marks); r->selector_row=marks;
    }
    int cached_tile=INT32_MIN;
    unsigned base_pixels[8],roof_pixels[8],warning_pixels[8];
    uint32_t margin_colors[2][128];
    bool margin_valid[2][128]={{false}};
    for (int x=0;x<r->view.width;++x) {
        int local=x-r->view.core_x;
        if (!r->advisor_frame && y>=0 && y<224 && local>=0 && local<256 &&
            !((local<8 && (r->repaired_edges[y]&1)) ||
              (local>=248 && (r->repaired_edges[y]&2)))) continue;
        if (!city) { out[x]=scenery(r,p,ram,local,y); continue; }
        unsigned warning,ci=0,over=0;
        if(!r->reference_terrain && !r->advisor_frame) {
            int mx=sx+local,my=sy+y+1,tile=mx&~7;
            if(tile!=cached_tile) {
                cached_tile=tile;
                bool warn=!r->map_hold && !(u16(ram,0x1d7) && y<46) &&
                    power_warning_cell(r,ram,tile,my);
                bool need_pixels=!deferred || warn;
                if(!need_pixels) {
                    int left=local-((unsigned)mx&7)+r->view.core_x;
                    for(int b=0;b<8;++b) if(left+b>=0 && left+b<r->view.width && objects[left+b]) need_pixels=true;
                }
                unsigned base_word=0,roof_word=0,base_bits=0,roof_bits=0;
                /* A zero CHR word can be a real tile; use the captured planes
                 * to decode transparencies and both flips without another map lookup. */
                if(need_pixels) {
                    base_bits=terrain_planes(r,p,ram,tile,my,false,&base_word);
                    roof_bits=terrain_planes(r,p,ram,tile+8,my+8,true,&roof_word);
                }
                for(unsigned b=0;b<8;++b) {
                    unsigned bit=base_word&0x4000?b:7-b;
                    unsigned rb=roof_word&0x4000?b:7-b;
                    unsigned v=((base_bits>>bit)&1)|(((base_bits>>(bit+8))&1)<<1)|
                        (((base_bits>>(bit+16))&1)<<2)|(((base_bits>>(bit+24))&1)<<3);
                    unsigned roof=((roof_bits>>rb)&1)|(((roof_bits>>(rb+8))&1)<<1)|
                        (((roof_bits>>(rb+16))&1)<<2)|(((roof_bits>>(rb+24))&1)<<3);
                    base_pixels[b]=v?v+((base_word>>10)&7)*16:0;
                    roof_pixels[b]=roof?roof+((roof_word>>10)&7)*16:0;
                    warning_pixels[b]=warn?tile_pixel(p,0x1376,PPU_bgTileAdr(p,0),tile+b,my,4,0):0;
                }
            }
            unsigned b=(unsigned)mx&7;
            ci=base_pixels[b];over=roof_pixels[b];warning=warning_pixels[b];
            /* HUD clipping can start inside a world tile. */
            if(u16(ram,0x1d7) && (y<46 || (local<56 && y<224))) warning=0;
        } else warning=power_warning_pixel(r,p,ram,local,y,sx,sy);
        if(deferred && !objects[x] && !warning) {
            out[x]=SC_TERRAIN_PIXEL;++r->terrain.deferred;continue;
        }
        if(r->reference_terrain || r->advisor_frame) {
            ci=cell_pixel(r,p,ram,sx+local,sy+y+1,false);
            over=cell_pixel(r,p,ram,sx+local+8,sy+y+9,true);
        }
        if (over) ci=over;
        int edge=local<0 ? 0 : local>255 ? 255 : local;
        if(!r->reference_terrain && !r->advisor_frame && !warning && !objects[x] && (local<0 || local>255)) {
            unsigned side=local>255;
            if(!margin_valid[side][ci]) {
                unsigned samples[2]={0,0};int layers[2]={5,5};
                for(unsigned sub=0;sub<2;++sub) {
                    if((p->screenEnabled[sub]&2) && (!(p->screenWindowed[sub]&2) || !window_contains(p,1,edge))) {
                        samples[sub]=ci;layers[sub]=ci?1:5;
                    } else if(sub && (p->screenEnabled[1]&4)) {
                        int yy=y<0?0:y>223?223:y;
                        samples[sub]=bg_pixel(p,2,edge,yy+1);layers[sub]=samples[sub]?2:5;
                    }
                }
                margin_colors[side][ci]=composite_color(p,samples[0],layers[0],samples[1],layers[1],edge);
                margin_valid[side][ci]=true;
            }
            out[x]=margin_colors[side][ci];continue;
        }
        if (r->advisor_frame) {
            /* Advice uses an opaque BG3/OBJ page on MAIN and the dimmed
             * BG1 HUD / BG2 city on SUB. Rebuild only that background here;
             * the original page pixels are placed independently after scanout.
             * Native city tiles retain their exact staging/timing in the core. */
            unsigned sub=ci; int sublayer=ci ? 1 : 5;
            bool city_priority=over!=0;
            bool in_core=local>=0 && local<256 && y>=0 && y<224;
            if (in_core && local>=8 && local<248) {
                sub=bg_sample(p,1,local,y+1,&city_priority);
                sublayer=sub ? 1 : 5;
            }
            /* The ROM windows the subscreen out beneath the old opaque
             * page. That occlusion belongs to the page's old location and
             * must not leave a black hole after moving it. The relocated
             * opaque pixels cover the new location at the end of the frame. */
            if (!(p->screenEnabled[1]&2)) {
                sub=0; sublayer=5;
            }
            if (in_core && (p->screenEnabled[1]&1)) {
                bool hud_priority=false;
                unsigned hud=bg_sample(p,0,local,y+1,&hud_priority);
                if (hud && (!sub || hud_priority || !city_priority)) { sub=hud; sublayer=0; }
            }
            out[x]=composite_color(p,0,5,sub,sublayer,edge);
            continue;
        }
        unsigned samples[2]={0,0}; int layers[2]={5,5};
        for (int sub=0;sub<2;++sub) {
            if ((p->screenEnabled[sub]&2) &&
                (!(p->screenWindowed[sub]&2) || !window_contains(p,1,edge))) {
                samples[sub]=ci; layers[sub]=ci ? 1 : 5;
            } else if (sub && (p->screenEnabled[1]&4)) {
                int yy=y<0 ? 0 : y>223 ? 223 : y;
                samples[sub]=bg_pixel(p,2,edge,yy+1); layers[sub]=samples[sub] ? 2 : 5;
            }
            if (warning && (p->screenEnabled[sub]&1) &&
                (!(p->screenWindowed[sub]&1) || !window_contains(p,0,edge))) {
                samples[sub]=warning; layers[sub]=0;
            }
            unsigned obj=objects[x]&255, priority=objects[x]>>8;
            if (obj && (!samples[sub] || priority>=(over ? 3u : 2u)) && (p->screenEnabled[sub]&16) &&
                (!(p->screenWindowed[sub]&16) || !window_contains(p,4,edge))) {
                samples[sub]=obj; layers[sub]=obj<192 ? 6 : 4;
            }
        }
        out[x]=composite_color(p,samples[0],layers[0],samples[1],layers[1],edge);
    }
}
/* A screen-wide vote prevents a tax-panel edge or cursor from becoming a
 * stripe across the margin. Sample finished pixels so fades and math agree. */
static void fill_flat_margins(ScRenderer *r) {
    if (r->city_frame || r->wood_layer>=0 || r->title_live) return;
    int flat=0, votes[224]={0}, best=0;
    uint32_t colors[224];
    for (int y=0;y<224;++y) {
        const uint32_t *row=r->pixels+(size_t)(r->view.core_y+y)*r->view.width+r->view.core_x;
        bool same=true;
        for (int x=1;x<8;++x) same&=row[x]==row[0] && row[255-x]==row[255];
        flat+=same; colors[y]=row[255];
        for (int previous=0;previous<=y;++previous) if (colors[previous]==colors[y]) {
            votes[previous]++; if (votes[previous]>votes[best]) best=previous;
        }
    }
    if (flat<168) return;
    for (int y=0;y<r->view.height;++y) for (int x=0;x<r->view.width;++x)
        if (x<r->view.core_x || x>=r->view.core_x+256 ||
            y<r->view.core_y || y>=r->view.core_y+224)
            r->pixels[(size_t)y*r->view.width+x]=colors[best];
}
static void capture_advisor_row(ScRenderer *r,const Ppu *p,int y,const uint32_t *native) {
    if(r->native_line) {
        unsigned ay=y+r->view.core_y;
        unsigned policy=r->terrain.rows[ay].math&~255u;
        capture_native_row(r,p,ay,y+1);
        r->terrain.rows[ay].math|=policy;
    }
    bool page_pixels[256]; int first=256,last=-1;
    for (int x=0;x<256;++x) {
        page_pixels[x]=(p->screenEnabled[0]&4) &&
            (!(p->screenWindowed[0]&4) || !window_contains(p,2,x)) && bg_pixel(p,2,x,y+1);
        if (page_pixels[x]) { if (x<first) first=x; last=x; }
    }
    for (int x=0;x<256;++x) {
        /* Black lettering can be transparent BG3 over the window-cleared
         * backdrop. It belongs to the opaque page too. Bound this fill by
         * the actual page span, so the city's clipped outer staging columns
         * do not become an extra black strip next to the centered panel. */
        bool page=page_pixels[x] || (x>=first && x<=last &&
            (p->screenWindowed[1]&3)==3 && window_contains(p,0,x) && window_contains(p,1,x));
        /* The stock PPU already resolved OAM order, sprite limits and flips
         * for this scanline. Read its result, without replaying or changing it. */
        bool obj=(p->screenEnabled[0]&16) &&
            (!(p->screenWindowed[0]&16) || !window_contains(p,4,x)) &&
            (ScObjPixel(p,x)&255);
        if(obj && r->city_overlay_frame) {
            obj=false;
            for(int slot=0;slot<109 && !obj;++slot) {
                int ox=sprite_x(p,slot);if(ox>=256)ox-=512;
                int dy=y-(p->oam[slot*2]>>8);
                if(dy>=0 && dy<64 && sprite_pixel(p,slot,x-ox,dy))obj=true;
            }
        }
        if(r->city_overlay_frame) {
            /* The native result may have BG1/BG2 roofs in front of a low
             * priority BG3 shadow. Lift the UI layers themselves, never that
             * already-composited city cache, into the zoomed scene. */
            bool high=false;
            unsigned ci=page_pixels[x]?bg_sample(p,2,x,y+1,&high):0;
            unsigned rank=ci?(high?(PPU_bg3priority(p)?15:3):1):0,layer=2;
            unsigned object=ScObjPixel(p,x),oi=object&255;
            /* Low-priority BG3 is the shadow on the subscreen. The projected
             * terrain already applies that colour math; it is not an opaque
             * grey panel to place in front of the city's roofs. */
            if(ci && rank<7 && (p->screenEnabled[0]&3)) {ci=0;page=false;}
            if(obj && oi && (object>>12)>rank) {
                ci=oi;layer=oi<192?6:4;
            }
            r->advisor_pixels[y*256+x]=(page || obj)?composite_color(p,ci,ci?layer:5,0,5,x):0;
            continue;
        }
        r->advisor_pixels[y*256+x]=(page || obj) ?
            r->native_line ? SC_RELOCATED_NATIVE_PIXEL|((unsigned)(y+r->view.core_y)<<8)|x :
            native[x]|0xff000000 : 0;
    }
}
static void place_advisor(ScRenderer *r) {
    int dx=(r->view.width-256)/2,dy=(r->view.height-224)/2;
    for (int y=0;y<224;++y) for (int x=0;x<256;++x) {
        uint32_t pixel=r->advisor_pixels[y*256+x];
        if (pixel) r->pixels[(size_t)(y+dy)*r->view.width+x+dx]=pixel;
    }
}
static bool in_rect(int x,int y,int left,int top,int width,int height) {
    return x>=left && x<left+width && y>=top && y<top+height;
}
static int right_shift(const ScRenderer *r) {
    return (r->split_hud || (r->pan_frame && r->view.width>256))?
        r->view.width-r->view.core_x-256:0;
}
static void arrow_shift(const ScRenderer *r,int slot,int *dx,int *dy) {
    *dx=*dy=0;
    if(slot==49 || slot==50) {
        if(slot==49) *dx=right_shift(r);
        *dy=(r->view.height-r->view.core_y-224)/2;
    } else {
        if(r->view.width>256)
        *dx=(r->view.width+r->view.core_x+(r->split_hud?56:0))/2-r->view.core_x-142;
        if (slot==52) *dy=r->view.height-r->view.core_y-224;
    }
}
bool ScRendererWindowToGuest(const ScRenderer *r,ScVideoRect d,
                            int ww,int wh,int dw,int dh,double x,double y,int *gx,int *gy,bool *navigation) {
    if (navigation) *navigation=false;
    if (ww<=0 || wh<=0 || d.w<=0 || d.h<=0) return false;
    double px=x*dw/ww,py=y*dh/wh;
    if (px<d.x || px>=d.x+d.w || py<d.y || py>=d.y+d.h) return false;
    int cx=(int)((px-d.x)*r->view.width/d.w)-r->view.core_x;
    int cy=(int)((py-d.y)*r->view.height/d.h)-r->view.core_y;
    /* Adviser pixels move independently of the anchored city/HUD. Hit-test
     * the same centered native panel that place_advisor draws. */
    if (r->advisor_frame) {
        cx+=r->view.core_x-(r->view.width-256)/2;
        cy+=r->view.core_y-(r->view.height-224)/2;
        if (!in_rect(cx,cy,0,0,256,224)) return false;
        *gx=cx; *gy=cy; return true;
    }
    int dx=right_shift(r);
    if (r->split_hud && in_rect(cx,cy,144+dx,0,112,46)) cx-=dx;
    else if (r->pan_frame && in_rect(cx,cy,190+dx,46,48,48)) {
        cx-=dx;
        if (navigation) *navigation=true;
    }
    else if (r->pan_frame) {
        const int slots[]={49,50,51,52},xs[]={214,70,134,134},ys[]={118,118,62,174};
        for (int i=0;i<4;++i) {
            int ax,ay; arrow_shift(r,slots[i],&ax,&ay);
            if (in_rect(cx,cy,xs[i]+ax,ys[i]+ay,16,16)) {
                cx-=ax; cy-=ay;
                if (navigation) *navigation=true;
                break;
            }
        }
    }
    if (!(r->city_input && !r->advisor_frame) &&
        (cx<0 || cx>=256 || cy<0 || cy>=224)) return false;
    *gx=cx; *gy=cy; return true;
}
bool ScRendererCityPoint(const ScRenderer *r,const uint8_t *ram,
                         int x,int y,int *wx,int *wy) {
    if (!r->city_input || r->advisor_frame || r->map_hold) return false;
    if (x+r->view.core_x<0 || x+r->view.core_x>=r->view.width ||
        y+r->view.core_y<0 || y+r->view.core_y>=r->view.height) return false;
    if (u16(ram,0x1d7) && ((y>=0 && y<46) || in_rect(x,y,0,46,56,178))) return false;
    int px=r->scroll_x+r->scroll_adjust_x+zoom_local(r,x,false),py=r->scroll_y+r->scroll_adjust_y+zoom_local(r,y,true);
    int width=r->world && r->world->active?ScWorldWidth(r->world):120;
    int height=r->world && r->world->active?ScWorldHeight(r->world):100;
    if (px<0 || py<0 || px>=width*8 || py>=height*8) return false;
    *wx=px/8; *wy=py/8; return true;
}
static uint32_t bare_city_pixel(const ScRenderer *r,const Ppu *p,const uint8_t *ram,int x,int y) {
    int sx=r->scroll_x+r->scroll_adjust_x,sy=r->scroll_y+r->scroll_adjust_y;
    unsigned ci=cell_pixel(r,p,ram,sx+x,sy+y+1,false);
    unsigned over=cell_pixel(r,p,ram,sx+x+8,sy+y+9,true);
    if (over) ci=over;
    return composite_color(p,ci,ci?1:5,0,5,x);
}
static bool changed_cell(const ScRenderer *r,int x,int y) {
    int width=r->world && r->world->active?ScWorldWidth(r->world):120;
    int height=r->world && r->world->active?ScWorldHeight(r->world):100;
    return x>=0 && y>=0 && x<width*8 && y<height*8 &&
        r->changed_cells[(y/8)*width+x/8];
}
/* Scrolling reuses a 32-column native staging map. Incoming columns can still
 * contain old terrain, roofs or lightning, even well inside the visible core.
 * Check tile words, once per eight-pixel span, against their actual world cell.
 * Keep UI out of this validation and stop once a complete frame agrees. */
static bool staged_city_matches(const ScRenderer *r,const Ppu *p,const uint8_t *ram,
                                int x,int y,int sx,int sy) {
    bool large=r->world && r->world->active;
    unsigned width=large?ScWorldWidth(r->world):120,height=large?ScWorldHeight(r->world):100;
    int wx=(sx+x)/8,wy=(sy+y+1)/8;
    if(sx+x<0 || sy+y+1<0 || (unsigned)wx>=width || (unsigned)wy>=height) return false;
    const uint8_t *map=large?r->world->tiles:ram+MAP;
    unsigned cell=u16(map,2*(wy*width+wx))&1023;
    if(cell>=CELL_TYPES || TILES+2*cell+1>=r->rom_size) return false;
    if(bg_word(p,1,x,y+1)!=u16(r->rom,TILES+2*cell)) return false;
    if(!(p->screenEnabled[0]&1)) return true;
    unsigned roof=0x2300;
    if((unsigned)(wx+1)<width && (unsigned)(wy+1)<height) {
        cell=u16(map,2*((wy+1)*width+wx+1))&1023;
        if(cell<CELL_TYPES) roof=u16(r->rom,OVERLAYS+2*cell);
    }
    unsigned staged=bg_word(p,0,x,y+1);
    return staged==roof || (staged==0x1376 && power_warning_cell(r,ram,sx+x,sy+y+1));
}
/* Construction and development update world cells before the SNES's small
 * tile cache reaches them. Draw those cells from the same source as the
 * margins, keeping native UI and the PPU's evaluated objects intact. Keep
 * tracking edited cells until a map load; their live CHR still animates. */
static void fresh_city_row(ScRenderer *r,const Ppu *p,const uint8_t *ram,int y) {
    if(r->zoom_frame) {zoom_city_row(r,p,ram,y,true);return;}
    if (!city_live(r,p,ram) || r->advisor_frame || r->map_hold ||
        !(p->screenEnabled[0]&2)) return;
    if(gpu_native_repair(r,p)) return;
    int sx=r->scroll_x+r->scroll_adjust_x+scroll_delta(p->hScroll[1],r->scroll_h);
    int sy=r->scroll_y+r->scroll_adjust_y+scroll_delta(p->vScroll[1],r->scroll_v);
    uint32_t *out=r->pixels+(size_t)(y+r->view.core_y)*r->view.width+r->view.core_x;
    int staged_cell=-1;bool cache_bad=false;
    bool cache_live=r->scroll_repair && !u16(ram,0xd7) && !u16(ram,0x379) && !ram[0x391];
    int check_cell=INT32_MIN,check_bg=INT32_MIN;
    bool clear_warning=false,warning_cell=false,edited=false;
    for (int x=0;x<256;++x) {
        bool land=!u16(ram,0x1d7) || (y>=46 && x>=56);
        if(cache_live && land) {
            int cell=(sx+x)/8;
            if(cell!=staged_cell) {
                cache_bad=!staged_city_matches(r,p,ram,x,y,sx,sy);staged_cell=cell;
            }
            if(cache_bad) ++r->staged_mismatches;
        } else cache_bad=false;
        int cell=(sx+x)&~7,bg=(x+p->hScroll[0])&~7;
        if(r->reference_terrain || cell!=check_cell || bg!=check_bg || x==56) {
            check_cell=cell;check_bg=bg;
            clear_warning=stale_power_warning(r,p,ram,x,y,sx,sy);
            warning_cell=power_warning_cell(r,ram,sx+x,sy+y+1) &&
                !(u16(ram,0x1d7) && (y<46 || (x<56 && y<224)));
            edited=changed_cell(r,sx+x,sy+y+1) || changed_cell(r,sx+x+8,sy+y+9);
        }
        if (!cache_bad && !clear_warning && !warning_cell && !edited) continue;
        if(r->defer_terrain && !PPU_forcedBlank(p)) {
            unsigned background=UINT32_C(0x80000000);
            if(r->native_line) background|=SC_OVERLAY_NATIVE_BG|
                (clear_warning || cache_bad?SC_OVERLAY_SKIP_BG1:0);
            else for(unsigned layer=0;layer<=2;layer+=2) {
                if(layer==0 && (clear_warning || cache_bad)) continue;
                if(!((p->screenEnabled[0]|p->screenEnabled[1])&(1<<layer))) continue;
                bool high=false;unsigned pixel=bg_sample(p,layer,x,y+1,&high);
                unsigned rank=layer==0?(high?12:8):(high?(PPU_bg3priority(p)?15:3):1);
                background|=(pixel|(rank<<8))<<(layer?16:0);
            }
            unsigned obj=ScObjPixel(p,x);
            unsigned object=(obj&255)|((obj>>12)<<8);
            if(u16(ram,0x1d7) && (y<46 || (x<56 && y<224))) object|=UINT32_C(0x80000000);
            size_t at=(size_t)(y+r->view.core_y)*r->view.width+r->view.core_x+x;
            r->terrain.overlays[at]=(ScTerrainOverlay){background,object};
            out[x]=SC_TERRAIN_PIXEL;++r->terrain.deferred;continue;
        }
        unsigned warning=power_warning_pixel(r,p,ram,x,y,sx,sy);
        unsigned ci=cell_pixel(r,p,ram,sx+x,sy+y+1,false);
        unsigned over=cell_pixel(r,p,ram,sx+x+8,sy+y+9,true);
        if (over) ci=over;
        unsigned samples[2]={0,0}; int owners[2]={5,5};
        for (int sub=0;sub<2;++sub) {
            unsigned rank=0;
            if ((p->screenEnabled[sub]&2) &&
                (!(p->screenWindowed[sub]&2) || !window_contains(p,1,x))) {
                samples[sub]=ci; owners[sub]=ci?1:5; rank=ci?(over?11:7):0;
            }
            for (int layer=0;layer<=2;layer+=2) {
                if (layer==0 && (clear_warning || cache_bad)) continue;
                if (!(p->screenEnabled[sub]&(1<<layer)) ||
                    ((p->screenWindowed[sub]&(1<<layer)) && window_contains(p,layer,x))) continue;
                bool high=false;
                unsigned pixel=bg_sample(p,layer,x,y+1,&high);
                unsigned z=layer==0?(high?12:8):(high?(PPU_bg3priority(p)?15:3):1);
                if (pixel && z>rank) { samples[sub]=pixel; owners[sub]=layer; rank=z; }
            }
            if (warning && rank<8 && (p->screenEnabled[sub]&1) &&
                (!(p->screenWindowed[sub]&1) || !window_contains(p,0,x))) {
                samples[sub]=warning; owners[sub]=0; rank=8;
            }
            unsigned obj=ScObjPixel(p,x);
            if ((obj&255) && (obj>>12)>rank && (p->screenEnabled[sub]&16) &&
                (!(p->screenWindowed[sub]&16) || !window_contains(p,4,x))) {
                samples[sub]=obj&255; owners[sub]=(obj&255)<192?6:4;
            }
        }
        out[x]=composite_color(p,samples[0],owners[0],samples[1],owners[1],x);
    }
}
/* Restore the scene underneath OAM's proxy cursor, including toolbar layers
 * and vehicles. objBuffer cannot be reused here: its winning pixel can be
 * the cursor itself. Resolve the remaining OAM slots in native priority order. */
static uint32_t without_pointer(const ScRenderer *r,const Ppu *p,const uint8_t *ram,int x,int y) {
    int sx=r->scroll_x+r->scroll_adjust_x,sy=r->scroll_y+r->scroll_adjust_y;
    unsigned ci=cell_pixel(r,p,ram,sx+x,sy+y+1,false);
    unsigned over=cell_pixel(r,p,ram,sx+x+8,sy+y+9,true);
    if (over) ci=over;
    unsigned warning=power_warning_pixel(r,p,ram,x,y,sx,sy);
    unsigned obj=0,attr=0;
    int first=PPU_objPriority(p)?(p->oamaddl&0xfe)/2:0;
    for (int i=0;i<128;++i) {
        int slot=(first+i)&127;
        if (slot<4 || (r->pan_frame && slot>=39 && slot<=52)) continue;
        int ox=sprite_x(p,slot); if (ox>=256) ox-=512;
        obj=sprite_pixel(p,slot,x-ox,(y-(p->oam[slot*2]>>8))&255);
        if (obj) { attr=p->oam[slot*2+1]; break; }
    }
    unsigned samples[2]={0,0}; int owners[2]={5,5};
    for (int sub=0;sub<2;++sub) {
        unsigned rank=0;
        if ((p->screenEnabled[sub]&2) && (!(p->screenWindowed[sub]&2) || !window_contains(p,1,x))) {
            samples[sub]=ci; owners[sub]=ci?1:5; rank=ci?(over?11:7):0;
        }
        for (int layer=0;layer<=2;layer+=2) {
            if (layer==0 && stale_power_warning(r,p,ram,x,y,sx,sy)) continue;
            if (!(p->screenEnabled[sub]&(1<<layer)) ||
                ((p->screenWindowed[sub]&(1<<layer)) && window_contains(p,layer,x))) continue;
            bool high=false;
            unsigned pixel=bg_sample(p,layer,x,y+1,&high);
            unsigned z=layer==0?(high?12:8):(high?(PPU_bg3priority(p)?15:3):1);
            if (pixel && z>rank) { samples[sub]=pixel; owners[sub]=layer; rank=z; }
        }
        if (warning && rank<8 && (p->screenEnabled[sub]&1) &&
            (!(p->screenWindowed[sub]&1) || !window_contains(p,0,x))) {
            samples[sub]=warning; owners[sub]=0; rank=8;
        }
        if (obj && 2+4*((attr>>12)&3)>rank && (p->screenEnabled[sub]&16) &&
            (!(p->screenWindowed[sub]&16) || !window_contains(p,4,x))) {
            samples[sub]=obj; owners[sub]=obj<192?6:4;
        }
    }
    return composite_color(p,samples[0],owners[0],samples[1],owners[1],x);
}
static void city_pointer(ScRenderer *r,const Ppu *p,const uint8_t *ram) {
    if (!r->city_input || r->advisor_frame || r->map_hold) return;
    int wx=0,wy=0;
    bool hud=r->pointer_active && r->pointer_hud;
    bool land=r->pointer_active && !hud && ScRendererCityPoint(r,ram,r->pointer_x,r->pointer_y,&wx,&wy);
    if (!land && !hud && !r->split_hud && !r->pointer_hidden) return;
    /* Move the entire UI cursor as one group, including rows below the HUD.
     * The ROM's hand is a 16px sprite; construction corners are 8px sprites. */
    int shift=r->split_hud && sprite_x(p,0)>=144 && sprite_x(p,0)<256 &&
        (p->oam[0]>>8)<46?right_shift(r):0;
    if (land || shift || hud || r->pointer_hidden) for (int slot=0;slot<4;++slot) {
        int ox=sprite_x(p,slot); if (ox>=256) ox-=512;
        for (int y=0;y<224;++y) {
            if (r->split_hud && y<46) continue; /* header already rebuilt */
            int row=(y-(p->oam[slot*2]>>8))&255;
            if (row>=64) continue;
            for (int x=0;x<64;++x) if (sprite_pixel(p,slot,x,row)) {
                int source=ox+x,ax=r->view.core_x+source,ay=r->view.core_y+y;
                /* Zoom reconstructs the land without proxy sprites. Only
                 * the separately composed sidebar has no proxy cursor either. */
                if (r->zoom_frame) continue;
                if (source>=0 && source<256 && ax>=0 && ax<r->view.width && ay>=0 && ay<r->view.height)
                    r->pixels[(size_t)ay*r->view.width+ax]=without_pointer(r,p,ram,source,y);
            }
        }
    }
    if(r->pointer_hidden) return;
    ScSelSprite sprites[8]; int count=0;
    if (land && !r->clipboard_cursor) {
        /* OAM can still contain the HUD hand or parked corners on the first
         * land frame. Get the selected tool's authentic outline from ROM. */
        unsigned tool=u16(ram,0x20d);
        if (tool>15) return;
        /* 01:8000 is one BYTE per tool, not a word/pointer table. */
        unsigned record=rom_read(r,0x018000+tool);
        count=ScSelector_Record(record,wx*8-r->scroll_x-r->scroll_adjust_x,
            wy*8-r->scroll_y-r->scroll_adjust_y,sprites,8,rom_read,r);
    } else if(!land && !hud) for (int slot=0;slot<4;++slot) {
        int ox=sprite_x(p,slot);
        if (ox>=256) ox-=512;
        int index=slot*2;
        sprites[count++]=(ScSelSprite){ox+shift,p->oam[index]>>8,
            p->oam[index+1]&255,p->oam[index+1]>>8,
            ((p->highOam[index/8]>>(index%8+1))&1)!=0,false};
    }
    for (int slot=count-1;slot>=0;--slot) {
        ScSelSprite *s=&sprites[slot];
        int size=sprite_sizes[PPU_objSize(p)][s->large?1:0];
        bool projected=land || (r->zoom_frame && s->tile!=0xec &&
            (!r->zoom_hud || (s->x>=56 && s->y>=46)));
        /* Parked native pieces must stay outside the native rectangle. */
        if (!land && !hud && (s->x-shift+size<=0 || s->x-shift>=256)) continue;
        int left=projected?(int)ceil(zoom_forward(r,s->x,false)):s->x;
        int top=projected?(int)ceil(zoom_forward(r,s->y,true)):s->y;
        int right=projected?(int)ceil(zoom_forward(r,s->x+size,false)):s->x+size;
        int bottom=projected?(int)ceil(zoom_forward(r,s->y+size,true)):s->y+size;
        for (int py=top;py<bottom;++py) for (int px=left;px<right;++px) {
            int x=projected?zoom_local(r,px,false)-s->x:px-s->x;
            int y=projected?zoom_local(r,py,true)-s->y:py-s->y;
            if(x<0 || y<0 || x>=size || y>=size)continue;
            unsigned ci=sprite_word_pixel(p,s->tile|(s->attr<<8),size,x,y);
            int ax=r->view.core_x+px;
            int ay=r->view.core_y+((projected || land || hud)?py:py&255);
            if (ci && ax>=0 && ax<r->view.width && ay>=0 && ay<r->view.height)
                r->pixels[(size_t)ay*r->view.width+ax]=composite_color(p,ci,ci<192?6:4,0,5,s->x+x);
        }
    }
    ScRendererHudPointer(r,p);
}
static uint32_t hud_ground(const Ppu *p) {
    if (PPU_forcedBlank(p)) return 0xff000000;
    return 0xff000000 | (uint32_t)p->brightnessMult[6]<<16 |
        (uint32_t)p->brightnessMult[4]<<8 | p->brightnessMult[1];
}
static bool population_host_draw(const ScRenderer *r) {
    return r->rom_is_us && r->population && r->population->valid &&
        (r->population->live || r->population->value>999999);
}
void ScRendererPopulationRow(const ScRenderer *r,const Ppu *p,ScViewport v,
                             bool split,int y,uint32_t *out) {
    if (!population_host_draw(r)) return;
    char digits[24]; snprintf(digits,sizeof digits,"%6llu",(unsigned long long)r->population->value);
    int count=(int)strlen(digits),extra=(count-6)*8;
    int shift=split?v.width-v.core_x-256:0;
    /* Narrow views have space beside the date, above the toolbar. Keep the
     * full-size font there rather than squeezing it into six digit slots. */
    int top=split && shift>=extra?22:10;
    int first=v.core_x+shift+211-count*8,icon=first-16;
    if (y>=22 && y<30) {
        int left=v.core_x+shift+147,right=v.core_x+shift+212;
        for (int x=left;x<right && x<v.width;++x) if (x>=0) out[x]=hud_ground(p);
    }
    if (y<top || y>=top+8) return;
    for (int x=icon;x<v.core_x+shift+212 && x<v.width;++x) if (x>=0) out[x]=hud_ground(p);
    for (int glyph=-2;glyph<count;++glyph) {
        if (glyph>=0 && digits[glyph]==' ') continue;
        unsigned attr=glyph<0?p->oam[(19+glyph+2)*2+1]:
            (p->oam[53]&0xff00)|rom_read((void *)r,0x0085e1+(unsigned)(digits[glyph]-'0'));
        int left=glyph<0?icon+(glyph+2)*8:first+glyph*8;
        for (int x=0;x<8;++x) {
            unsigned ci=sprite_word_pixel(p,attr,8,x,y-top);
            if (ci && left+x>=0 && left+x<v.width)
                out[left+x]=composite_color(p,ci,ci<192?6:4,0,5,203);
        }
    }
}
void ScRendererClipboardFont(ScRenderer *r,const uint8_t *font,size_t size) {
    r->clipboard_font_valid=font && size>=sizeof r->clipboard_font;
    if(r->clipboard_font_valid) memcpy(r->clipboard_font,font,sizeof r->clipboard_font);
}
ScVideoRect ScRendererClipboardButton(ScViewport v,unsigned button) {
    return (ScVideoRect){v.core_x+(button?152:112),v.core_y,button?44:36,10};
}
static void clipboard_text(const ScRenderer *r,const Ppu *p,ScViewport v,
    const char *s,int left,int top,int y,uint32_t *out,uint32_t color) {
    (void)p;
    if(y<top || y>=top+8) return;
    for(;*s;++s,left+=8) {
        const uint8_t *glyph=r->clipboard_font[(unsigned char)*s&127];
        unsigned row=y-top;
        for(int x=0;x<8;++x) {
            unsigned ci=((glyph[row*2]>>(7-x))&1)|(((glyph[row*2+1]>>(7-x))&1)<<1);
            if(!ci && left+x>=0 && left+x<v.width) out[left+x]=color;
        }
    }
}
void ScRendererHudPointer(ScRenderer *r,const Ppu *p) {
    if(r->pointer_hidden || !r->pointer_active || !r->pointer_hud || !r->city_input || r->advisor_frame || r->map_hold) return;
    /* 01:c641 emits the original 16px HUD hand using tile $31ec. Follow the
     * live mouse even when development has delayed the guest's OAM emitter. */
    for(int y=0;y<16;++y) for(int x=0;x<16;++x) {
        unsigned ci=sprite_word_pixel(p,0x31ec,16,x,y);
        int ax=r->view.core_x+r->pointer_x+x,ay=r->view.core_y+r->pointer_y+y;
        if(ci && ax>=0 && ax<r->view.width && ay>=0 && ay<r->view.height)
            r->pixels[(size_t)ay*r->view.width+ax]=composite_color(p,ci,ci<192?6:4,0,5,r->pointer_x+x);
    }
}
void ScRendererClipboardRow(const ScRenderer *r,const Ppu *p,ScViewport v,
    unsigned tool,bool available,uint64_t price,int y,uint32_t *out) {
    if(!r->clipboard_font_valid || PPU_forcedBlank(p)) return;
    uint32_t ink=0xff000000|(uint32_t)p->brightnessMult[31]<<16|
        (uint32_t)p->brightnessMult[27]<<8|p->brightnessMult[20];
    for(unsigned b=0;b<2;++b) {
        ScVideoRect rect=ScRendererClipboardButton(v,b);
        if(y<rect.y || y>=rect.y+rect.h) continue;
        bool selected=tool==b+1,enabled=!b || available;
        for(int x=rect.x;x<rect.x+rect.w && x<v.width;++x) if(x>=0)
            out[x]=(y==rect.y || y==rect.y+rect.h-1 || x==rect.x || x==rect.x+rect.w-1)?
                selected?ink:hud_ground(p):hud_ground(p);
        clipboard_text(r,p,v,b?"PASTE":"COPY",rect.x+2,rect.y+1,y,out,
            enabled?ink:0xff000000|(uint32_t)p->brightnessMult[12]*0x010101);
    }
    if(!tool || y<v.core_y+176 || y>=v.core_y+224) return;
    char digits[24];snprintf(digits,sizeof digits,"%llu",(unsigned long long)price);
    int count=(int)strlen(digits),right=v.core_x+56;
    if(tool==2 && right<v.core_x+20+count*8) right=v.core_x+20+count*8;
    for(int x=v.core_x+8;x<right && x<v.width;++x) if(x>=0) out[x]=hud_ground(p);
    clipboard_text(r,p,v,tool==1?"COPY":"PASTE",v.core_x+12,v.core_y+180,y,out,ink);
    clipboard_text(r,p,v,tool==1?"DRAG":"TOTAL",v.core_x+12,v.core_y+192,y,out,ink);
    if(tool==1 || y<v.core_y+204 || y>=v.core_y+212) return;
    clipboard_text(r,p,v,"$",v.core_x+12,v.core_y+204,y,out,ink);
    for(int g=0;g<count;++g) {
        unsigned attr=(p->oam[53]&0xff00)|rom_read((void *)r,0x0085e1+digits[g]-'0');
        for(int x=0;x<8;++x) {
            unsigned ci=sprite_word_pixel(p,attr,8,x,y-v.core_y-204);
            int target=v.core_x+20+g*8+x;
            if(ci && target>=0 && target<v.width) out[target]=composite_color(p,ci,ci<192?6:4,0,5,203);
        }
    }
}
ScVideoRect ScRendererMinimapView(const ScRenderer *r,const uint8_t *ram) {
    bool large=r->world && r->world->active;
    int width=(large?ScWorldWidth(r->world):120)*8,height=(large?ScWorldHeight(r->world):100)*8;
    int x0=r->scroll_x+r->scroll_adjust_x+
        zoom_local(r,r->view.core_x?-r->view.core_x:u16(ram,0x1d7)?56:0,false);
    int y0=r->scroll_y+r->scroll_adjust_y+
        zoom_local(r,r->view.core_y?-r->view.core_y:u16(ram,0x1d7)?46:0,true);
    int x1=r->scroll_x+r->scroll_adjust_x+zoom_local(r,r->view.width-r->view.core_x,false);
    int y1=r->scroll_y+r->scroll_adjust_y+zoom_local(r,r->view.height-r->view.core_y,true);
    if (x0<0) x0=0;
    if (y0<0) y0=0;
    if (x1>width) x1=width;
    if (y1>height) y1=height;
    if (x0>=x1 || y0>=y1) return (ScVideoRect){0,0,0,0};
    int left=x0*30/width,top=y0*25/height;
    return (ScVideoRect){r->view.core_x+200+right_shift(r)+left,
        r->view.core_y+56+top,(x1*30+width-1)/width-left,
        (y1*25+height-1)/height-top};
}
/* Preserve the original HUD artwork, but compose its independent groups at
 * the canvas edges. The minimap frame's sprites exclude the old position mark;
 * its new rectangle is projected from the same camera and world as the city. */
static void city_hud_row(ScRenderer *r,const Ppu *p,const uint8_t *ram,int y) {
    uint32_t *out=r->pixels+(size_t)(y+r->view.core_y)*r->view.width;
    int shift=right_shift(r),core=r->view.core_x;
    if (r->split_hud && y<46) {
        for (int x=0;x<r->view.width;++x) out[x]=hud_ground(p);
        /* Date/menu sprites stay at the left; RCI and financial sprites move
         * together. Capture no terrain from behind the native status text. */
        for (int slot=71;slot>=0;--slot) {
            if (slot<4 && r->city_input) continue;
            if (slot>33 && slot<64) continue;
            if (population_host_draw(r) && slot>=19 && slot<=26) continue;
            int ox=sprite_x(p,slot),row=(y-(p->oam[slot*2]>>8))&255;
            if (row>=64 || ox>=256) continue;
            int dx=(slot>=4 && slot<=10) || (slot>=19 && slot<=33)?shift:0;
            if (slot<4 && ox>=144) dx=shift;
            for (int x=0;x<64;++x) {
                int target=core+ox+x+dx;
                unsigned ci=sprite_pixel(p,slot,x,row);
                if (ci && target>=0 && target<r->view.width)
                    out[target]=composite_color(p,ci,ci<192?6:4,0,5,ox+x);
            }
        }
    }
    if (!r->pan_frame && !r->mouse_minimap_frame) return;
    if(!r->zoom_frame && r->pan_frame) for (int slot=39;slot<=52;++slot) {
        int ox=sprite_x(p,slot); if (ox>=256) ox-=512;
        int row=y-(p->oam[slot*2]>>8);
        if (row<0 || row>=64) continue;
        for (int x=0;x<64;++x) if (sprite_pixel(p,slot,x,row)) {
            int source=ox+x,target=core+source;
            if (target>=0 && target<r->view.width)
                out[target]=bare_city_pixel(r,p,ram,source,y);
        }
    }
    /* Earlier OAM slots win. The mini frame and arrows are opaque UI sprites. */
    if(r->mouse_minimap_frame && !r->pan_frame) {
        /* Ordinary tool captions reuse the native pan OAM slots. Draw the
         * same nine 16px frame tiles directly, keeping OAM and guest mode intact. */
        for(unsigned sy=0;sy<3;++sy) for(unsigned sx=0;sx<3;++sx) {
            int row=y-46-(int)sy*16;
            for(int x=0;x<16;++x) {
                unsigned ci=sprite_word_pixel(p,0x3166+sx*2+sy*32,16,x,row);
                int target=core+190+shift+(int)sx*16+x;
                if(ci && target>=0 && target<r->view.width)
                    out[target]=composite_color(p,ci,ci<192?6:4,0,5,190+sx*16+x);
            }
        }
    } else for (int slot=52;slot>=40;--slot) {
        int dx=0,dy=0;
        if (slot<=48) dx=shift; else arrow_shift(r,slot,&dx,&dy);
        int oy=p->oam[slot*2]>>8;
        /* Y=240/high X parks a direction at the map boundary. Enlarging the
         * canvas must not make those hidden native sprites visible again. */
        if(slot>=49 && (oy>=224 || sprite_x(p,slot)>=256)) continue;
        int row=y-dy-oy;
        if(row<0) continue;
        if (row>=64) continue;
        int ox=sprite_x(p,slot);
        for (int x=0;x<64;++x) {
            int target=core+ox+x+dx;
            unsigned ci=sprite_pixel(p,slot,x,row);
            if (ci && target>=0 && target<r->view.width)
                out[target]=composite_color(p,ci,ci<192?6:4,0,5,ox+x);
        }
    }
    ScVideoRect marker=ScRendererMinimapView(r,ram);
    int row=y+r->view.core_y,white=p->brightnessMult[31];
    uint32_t ink=PPU_forcedBlank(p)?0xff000000:0xff000000|(white*0x010101);
    if (row>=marker.y && row<marker.y+marker.h)
        for (int x=marker.x;x<marker.x+marker.w;++x)
            if (row==marker.y || row==marker.y+marker.h-1 || x==marker.x || x==marker.x+marker.w-1)
                out[x]=ink;
}
/* Same 120x100 view and color table as 02:899b/8b34. Show the entire
 * expanded city, instead of its first 120x100 corner. Keep UI at native scale. */
bool ScRendererMapPreviewVisible(const Ppu *p,const uint8_t *ram) {
    if(!p || !ram)return false;
    unsigned mode=u16(ram,0x14);
    /* $14 changes before the visible panel's DMA. Its original counter
     * cells and MAP SELECT text page identify the picture actually shown,
     * including the entry/exit frames and dirty-number edits. */
    return mode>=4 && mode<=7 && (p->screenEnabled[0]&2) &&
        PPU_bgTilemapAdr(p,2)==0x5000 &&
        (bg_word(p,1,200,152)&1023)==0x58 &&
        (bg_word(p,1,184,160)&1023)==0x55;
}
static void map_preview_row(ScRenderer *r,const Ppu *p,const uint8_t *ram,int y) {
    (void)ram;
    if(!r->map_preview.active || !r->map_preview_frame ||
        y<88 || y>=188)return;
    uint32_t *row=r->pixels+(size_t)(y+r->view.core_y)*r->view.width;
    unsigned palette=((bg_word(p,1,48,y+1)>>10)&7)*16;
    if(y==88)for(unsigned cell=0;cell<38;++cell)
        r->preview_colors[cell]=color(p,palette+rom_read(r,0x02948e + (cell>=0x14?0x14:cell)));
    for(unsigned x=0;x<120;++x) {
        unsigned cell=sc_mapgen_preview_cell(&r->map_preview,x,y-88);
        /* Native tree lookup entries also encode animation phases. A fixed
         * geographical overview uses the stable green forest entry, rather
         * than letting a tree variant acquire the water palette colour. */
        unsigned ink=palette+rom_read(r,0x02948e + (cell>=0x14?0x14:cell));
        unsigned object=ScObjPixel(p,48+x);
        if((p->screenEnabled[0]&16) && (object&255))ink=object&255;
        row[r->view.core_x+48+x]=color(p,ink);
    }
}
uint32_t ScRendererHandPixel(const Ppu *p,int x,int y) {
    unsigned ci=sprite_word_pixel(p,0x3f9e,16,x,y);
    return ci?color(p,ci):0;
}
static void map_number_row(ScRenderer *r,const Ppu *p,const uint8_t *ram,int y) {
    (void)ram;
    if(!r->map_preview_frame || y<136 || y>=192)return;
    uint32_t *row=r->pixels+(size_t)(y+r->view.core_y)*r->view.width;
    uint32_t paper=color(p,bg_sample(p,1,176,136,NULL));
    if(y<174)for(int x=176;x<232;++x)row[r->view.core_x+x]=paper;
    /* Reuse the native No. label and beveled counter cells, with two extra
     * columns. This layout is visible during generation too, not just after
     * the host preview is ready. */
    if(y>=152 && y<168) {
        for(int x=176;x<192;++x)
            row[r->view.core_x+x]=color(p,bg_sample(p,1,x+8,y+1,NULL));
        for(int x=SC_MAP_NUMBER_X;x<SC_MAP_NUMBER_X+SC_MAP_NUMBER_WIDTH;++x)
            row[r->view.core_x+x]=color(p,bg_sample(p,1,200+(x&7),y+1,NULL));
    }
    /* Native 8x8 digit graphics, fixed UI scale; five editable places fit
     * between the preview frame and the right side of the original panel. */
    if(y>=156 && y<164)for(unsigned i=0,place=10000;i<5;++i,place/=10)
        for(int x=0;x<8;++x) {
            unsigned ink=sprite_word_pixel(p,0x940+r->map_number/place%10,8,x,y-156);
            if(ink)row[r->view.core_x+SC_MAP_NUMBER_X+i*8+x]=color(p,ink);
        }
    /* The original range uses its compact BG lettering, not the white OBJ
     * counter digits. Copy its zero, tilde and nine glyphs without scaling. */
    if(y>=144 && y<152) {
        for(int x=0;x<5;++x)row[r->view.core_x+184+x]=color(p,bg_sample(p,1,192+x,y+1,NULL));
        for(int x=0;x<8;++x)row[r->view.core_x+192+x]=color(p,bg_sample(p,1,208+x,y+1,NULL));
        for(int i=0;i<5;++i)for(int x=0;x<5;++x)
            row[r->view.core_x+203+i*5+x]=color(p,bg_sample(p,1,216+x,y+1,NULL));
    }
    if(y>=176)for(int x=176;x<232;++x)row[r->view.core_x+x]=paper;
    if(y>=176)for(int x=SC_MAP_NUMBER_X;x<SC_MAP_NUMBER_X+SC_MAP_NUMBER_WIDTH;++x)
        row[r->view.core_x+x]=color(p,bg_sample(p,1,216+(x&7),y+1,NULL));
    for(int slot=0;slot<=127;slot+=127) {
        if(p->oam[slot*2+1]!=0x3f9e)continue;
        int ox=sprite_x(p,slot),dy=y-(p->oam[slot*2]>>8);
        if(dy<0 || dy>=16)continue;
        for(int x=176;x<240;++x) {
            unsigned ink=sprite_word_pixel(p,0x3f9e,16,x-ox,dy);
            if(ink)row[r->view.core_x+x]=color(p,ink);
        }
    }
}
void ScRendererLine(ScRenderer *r,const Ppu *p,const uint8_t *ram,int line,const uint32_t *native) {
    if (!r->pixels || !p || !ram || !native || line<0 || line>=224) return;
    if (line==0) {
        r->map_preview_frame=ScRendererMapPreviewVisible(p,ram);
        r->staged_mismatches=0;
        r->terrain.deferred=0;r->terrain.snapshots=0;
        r->terrain.city_words=0;memset(r->city_cache,0,sizeof r->city_cache);r->city_cache_next=0;
        r->object_grid_valid=false;
        r->city_frame=false;
        if (ram[0x14]==1) r->title_live=true;
        else if (ram[0x14]!=2 || PPU_forcedBlank(p) || !PPU_brightness(p)) r->title_live=false;
        find_wood(r,p,ram);
        /* Choose once per frame, after the live desk/layer classification.
         * $14 alone also labels tax, statistics and View Mode as city screens.
         * Keep gameplay's configured anchor, but center standalone screens. */
        r->view=r->gameplay_view;
        if (!city_live(r,p,ram)) {
            r->view.core_x=(r->view.width-256)/2;
            r->view.core_y=(r->view.height-224)/2;
        }
        /* The ROM's advisor/tutorial pages enable color math for the
         * backdrop only; their visible page/portrait pixels are opaque.
         * Keep the city/HUD anchor and move those pixels, never the composed
         * native rectangle (which would drag the city along with the page). */
        r->advisor_frame=city_live(r,p,ram) && (p->screenEnabled[0]&31)==20 &&
            (p->screenEnabled[1]&31)==3 && !(PPU_mathEnabled(p)&20) &&
            (r->map_zoom!=1 || r->camera_x || r->camera_y ||
             r->view.core_x!=(r->view.width-256)/2 || r->view.core_y!=(r->view.height-224)/2);
        bool hud=city_live(r,p,ram) && !r->advisor_frame && u16(ram,0x1d7) &&
            (p->screenEnabled[0]&3)==3 && !u16(ram,0x379);
        r->split_hud=hud && r->view.width>256;
        r->pan_frame=city_live(r,p,ram) && !r->advisor_frame && !u16(ram,0x379) &&
            (p->screenEnabled[0]&16) && (p->oam[81]&255)==0x66 && (p->oam[80]>>8)==46 &&
            sprite_x(p,40)<256; /* high X bit parks the hidden minimap */
        r->mouse_minimap_frame=hud && r->mouse_panning;
        r->city_input=city_live(r,p,ram) && !r->advisor_frame && !u16(ram,0x379) &&
            !u16(ram,0xd7) && !ram[0x391] && !ram[0xe3]; /* gift picker */
        /* Input/modal gates do not change the terrain's selected zoom.
         * Retain its pivot while overlays temporarily hide the HUD. */
        if(r->city_input || !city_live(r,p,ram))r->zoom_hud=hud;
        r->zoom_frame=city_live(r,p,ram) && !r->map_hold && r->map_zoom>0 && (fabs(r->map_zoom-1)>1e-9 || r->camera_x || r->camera_y || (r->vehicle_count && r->vehicles[r->vehicle_count-1].host));
        r->city_overlay_frame=r->zoom_frame && !r->city_input && !r->advisor_frame && !r->pan_frame;
        if(getenv("SC_CITY_VIEW_DIAG"))fprintf(stderr,
            "[city view] input=%d adviser=%d pan=%d zoom=%g applied=%d hud=%d masks=%u/%u modal=%u/%u/%u/%u\n",
            r->city_input,r->advisor_frame,r->pan_frame,r->map_zoom,r->zoom_frame,r->zoom_hud,
            p->screenEnabled[0],p->screenEnabled[1],u16(ram,0xd7),u16(ram,0x379),ram[0x391],ram[0xe3]);
        if(r->zoom_frame) {
            unsigned span=(unsigned)ceil(r->view.width*(double)zoom_step(r)/65536);
            /* Native HUD and menu sampling still addresses the full UI row
             * when terrain is magnified. Never shrink its tile allocation. */
            if(span<(unsigned)r->view.width)span=(unsigned)r->view.width;
            if(span>SC_MAX_MAP_SPAN)span=SC_MAX_MAP_SPAN;
            if(!ScTerrainResize(&r->terrain,r->view.width,r->view.height) || !ScTerrainSpanWidth(&r->terrain,span))r->zoom_frame=false;
        } else if(r->terrain.tiles)ScTerrainSpanWidth(&r->terrain,r->view.width);
        find_lights(r,p);
        r->selector_count=0;
        if (ScSelector_OnScreen(ram[0x14])) {
            int scroll=ScSelector_Scroll(p,rom_read,r);
            if (scroll>=0)
                r->selector_count=ScSelector_Sprites(r->selector,scroll,
                    ram[0x42]|((unsigned)ram[0x43]<<8),r->sylt,rom_read,r);
        }
        r->sign_count=ScTitleSign_Sprites(r->sign,SC_SIGN_MAX_SPRITES,rom_read,r);
        uint64_t measured=MEASURE_BEGIN(r);
        track_scroll(r,p,ram);
        track_objects(r,p,ram);
        track_map_swap(r,p,ram);
        MEASURE_END(r,SC_RENDER_TRACK,measured);
    }
    /* The 32-column guest tilemap stages incoming tiles in CRT overscan.
     * Reconstruct only those edge bands, and never cover UI or native OBJ. */
    r->repaired_edges[line]=0;
    if (r->view.width>256 && city_live(r,p,ram) && (p->screenEnabled[0]&2)) {
        if (!edge_has_overlay(p,line,0)) r->repaired_edges[line]|=1;
        if (!edge_has_overlay(p,line,248)) r->repaired_edges[line]|=2;
    }
    uint64_t measured=MEASURE_BEGIN(r);
    if (line==0) for (int y=-r->view.core_y;y<0;++y) render_row(r,p,ram,y);
    render_row(r,p,ram,line);
    MEASURE_END(r,SC_RENDER_ROWS,measured);
    measured=MEASURE_BEGIN(r);
    int first=(r->repaired_edges[line]&1) ? 8 : 0;
    int end=(r->repaired_edges[line]&2) ? 248 : 256;
    if (r->advisor_frame || r->city_overlay_frame) capture_advisor_row(r,p,line,native);
    else if(r->native_line) {
        unsigned policy=r->terrain.rows[line+r->view.core_y].math&~255u;
        capture_native_row(r,p,line+r->view.core_y,line+1);
        r->terrain.rows[line+r->view.core_y].math|=policy;
        uint32_t *out=r->pixels+(size_t)(line+r->view.core_y)*r->view.width+r->view.core_x;
        for(int x=first;x<end;++x) out[x]=SC_NATIVE_PIXEL;
        r->terrain.deferred+=end-first;
    } else memcpy(r->pixels+(size_t)(line+r->view.core_y)*r->view.width+r->view.core_x+first,
                  native+first,(size_t)(end-first)*sizeof(*native));
    MEASURE_END(r,SC_RENDER_NATIVE,measured);
    measured=MEASURE_BEGIN(r);
    fresh_city_row(r,p,ram,line);
    MEASURE_END(r,SC_RENDER_REPAIR,measured);
    measured=MEASURE_BEGIN(r);
    if (r->split_hud || r->pan_frame || r->mouse_minimap_frame) city_hud_row(r,p,ram,line);
    if (city_live(r,p,ram) && !r->advisor_frame && u16(ram,0x1d7) &&
        (p->screenEnabled[0]&3)==3 && !u16(ram,0x379))
        ScRendererPopulationRow(r,p,r->view,r->split_hud,line,
            r->pixels+(size_t)(line+r->view.core_y)*r->view.width);
    map_preview_row(r,p,ram,line);
    map_number_row(r,p,ram,line);
    MEASURE_END(r,SC_RENDER_HUD,measured);
    if (line==223) {
        measured=MEASURE_BEGIN(r);
        for (int y=224;y<r->view.height-r->view.core_y;++y) {
            render_row(r,p,ram,y);
            if(r->pan_frame) city_hud_row(r,p,ram,y);
        }
        fill_flat_margins(r);
        if (r->advisor_frame) place_advisor(r);
        else if(r->city_overlay_frame)for(int y=0;y<224;++y)for(int x=0;x<256;++x) {
            uint32_t pixel=r->advisor_pixels[y*256+x];
            if(pixel)r->pixels[(size_t)(y+r->view.core_y)*r->view.width+r->view.core_x+x]=pixel;
        }
        MEASURE_END(r,SC_RENDER_ROWS,measured);
        measured=MEASURE_BEGIN(r);
        city_pointer(r,p,ram);
        /* The selector's OAM starts with map pins, not a cursor. Use its
         * shared native menu hand across the entire expanded canvas. */
        /* Screen 2 also covers the title's exit fade. Its OBJ bank still
         * contains logo graphics, not the shared menu hand. */
        if(r->menu_pointer_active && !r->title_live &&
           (ScMouseUiArrowScreen(ram) || ScSelector_OnScreen(ram[0x14])) &&
           !PPU_forcedBlank(p) && (p->screenEnabled[0]&16))
            for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
                unsigned ci=sprite_word_pixel(p,0x3f9e,16,x,y);
                int ax=r->view.core_x+r->menu_pointer_x-SC_MOUSE_HAND_HOT_X+x;
                int ay=r->view.core_y+r->menu_pointer_y-SC_MOUSE_HAND_HOT_Y+y;
                if(ci && ax>=0 && ax<r->view.width && ay>=0 && ay<r->view.height)
                    r->pixels[(size_t)ay*r->view.width+ax]=color(p,ci);
            }
        MEASURE_END(r,SC_RENDER_POINTER,measured);
        if(!r->staged_mismatches && !u16(ram,0xd7) && !u16(ram,0x379) && !ram[0x391]) r->scroll_repair=false;
    }
}
