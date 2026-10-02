#include "sc_renderer.h"
#include "snes/ppu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { MAP = 0x10200, TILES = 0x156a9, OVERLAYS = TILES - 0x77c, CELL_TYPES = 0x77c/2 };
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
    unsigned width=large?(r->map_hold?(r->held_huge?480:240):ScWorldWidth(r->world)):120,height=large?(r->map_hold?(r->held_huge?400:200):ScWorldHeight(r->world)):100;
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
    if (!city_live(r,p,ram)) { r->scroll_valid=false; return; }
    int h=p->hScroll[1]&255,v=p->vScroll[1]&255;
    int x=(int16_t)u16(ram,0x1bd)*8+(h&7),y=(int16_t)u16(ram,0x1bf)*8+(v&7);
    if (r->scroll_valid) {
        int dx=scroll_delta(h,r->scroll_h),dy=scroll_delta(v,r->scroll_v);
        if (abs(dx)<32 && abs(dy)<32) {
            int ax=dx-(x-r->scroll_x),ay=dy-(y-r->scroll_y);
            if (ax%8==0) r->scroll_adjust_x+=ax;
            if (ay%8==0) r->scroll_adjust_y+=ay;
            if (!dx && !dy && x==r->scroll_x && y==r->scroll_y) ++r->scroll_still;
            else r->scroll_still=0;
            if (r->scroll_still>=8) r->scroll_adjust_x=r->scroll_adjust_y=0;
            if (r->scroll_adjust_x>8) r->scroll_adjust_x=8;
            if (r->scroll_adjust_x< -8) r->scroll_adjust_x=-8;
            if (r->scroll_adjust_y>8) r->scroll_adjust_y=8;
            if (r->scroll_adjust_y< -8) r->scroll_adjust_y=-8;
        } else r->scroll_adjust_x=r->scroll_adjust_y=0;
    } else r->scroll_adjust_x=r->scroll_adjust_y=r->scroll_still=0;
    r->scroll_valid=true; r->scroll_x=x; r->scroll_y=y; r->scroll_h=h; r->scroll_v=v;
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
    bool black=PPU_forcedBlank(p) || !PPU_brightness(p);
    if (r->map_valid) {
        for (unsigned i=0;i<bytes;i+=2) {
            unsigned raw=u16(map,i),delta=raw^u16(r->previous_map,i),tile=raw&1023;
            /* A flood changes power bits on ordinary terrain too. Only a
             * building owner has a warning to redraw for that change. */
            bool changed=(delta&1023)!=0 || ((delta&0x8000) &&
                r->rom && tile<CELL_TYPES && (r->rom[0x184eb+tile]&1));
            if (changed && !r->map_hold) {
                unsigned width=large?ScWorldWidth(r->world):120;
                r->changed_cells[i/2]=1;
                /* The power glyph belongs to the southeast footprint cell,
                 * even when only its owner's power bit changed. */
                if ((i/2)%width<width-1 && i/2+width+1<bytes/2)
                    r->changed_cells[i/2+width+1]=1;
            }
        }
        if (r->map_hold) {
            if (black) r->map_dark=true;
            if ((!black && r->map_dark) || ++r->map_age>900) {
                r->map_hold=false;
                memset(r->changed_cells,0,sizeof r->changed_cells);
            }
        }
    }
    memcpy(r->previous_map,map,bytes);
    if (!r->map_hold) {
        memcpy(r->held_map,map,bytes);
        memcpy(r->held_ppu,p,sizeof *p);
        r->held_x=r->scroll_x+r->scroll_adjust_x;
        r->held_y=r->scroll_y+r->scroll_adjust_y;
        r->held_large=large; r->held_huge=large && r->world->huge;
    }
    r->map_valid=true;
}
static void object_row(const ScRenderer *r,const Ppu *p,int y,uint16_t *pixels) {
    memset(pixels,0,(size_t)r->view.width*sizeof(*pixels));
    /* The vehicles the game drops once they are right of its 256 columns
     * (src/sc_vehicles.c): drawn first, so the OAM's own sprites win.
     * Rows as the PPU has them -- a sprite's row 0 is on line Y -- which the
     * guest's half of an object crossing the edge also follows. */
    for (int k=0;k<r->vehicle_count;++k) {
        const ScVehicleSprite *v=&r->vehicles[k];
        int size=sprite_sizes[PPU_objSize(p)][v->large ? 1 : 0];
        int row=y-v->y;
        if (row<0 || row>=size) continue;
        unsigned attr=p->oam[v->slot*2+1];
        for (int dx=0;dx<size;++dx) {
            int local=v->x+dx;
            if (local<256) continue;   /* the authentic columns are the core's */
            int x=local+r->view.core_x;
            if (x<0 || x>=r->view.width) continue;
            unsigned ci=sprite_word_pixel(p,attr,size,dx,row);
            if (ci) pixels[x]=(uint16_t)(ci|(((attr>>12)&3)<<8));
        }
    }
    int first=PPU_objPriority(p) ? (p->oamaddl&0xfe)/2 : 0;
    for (int rank=127;rank>=0;--rank) {
        int slot=(first+rank)&127;
        if (r->city_input && (r->pointer_active || r->split_hud) && slot<4) continue; /* composed once at the host endpoint */
        if (r->pan_frame && slot>=39 && slot<=52 && slot!=50) continue;
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
static bool stale_power_warning(const ScRenderer *r,const Ppu *p,const uint8_t *ram,
                                int x,int y,int sx,int sy) {
    if (bg_word(p,0,x,y+1)!=0x1376 || !r->rom) return false;
    if (u16(ram,0x1d7) && (y<46 || (x<56 && y<224))) return false;
    int wx=(sx+x)/8,wy=(sy+y+1)/8;
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
        if (!(p->screenEnabled[0]&16)) continue;
        for (int slot=0;slot<128;++slot) {
            int sx=sprite_x(p,slot); if (sx>=256) sx-=512;
            if (sprite_pixel(p,slot,x-sx,(y+1-(p->oam[slot*2]>>8))&255)) return true;
        }
    }
    return false;
}
void ScRendererInit(ScRenderer *r,const uint8_t *rom,size_t size,bool is_us) {
    memset(r,0,sizeof(*r)); r->rom=rom; r->rom_size=size; r->rom_is_us=is_us; r->wood_layer=-1;
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
    if (count>r->capacity) {
        uint32_t *pixels=realloc(r->pixels,count*sizeof(*pixels));
        if (!pixels) return false;
        r->pixels=pixels; r->capacity=count;
    }
    r->view=r->gameplay_view=v;
    memset(r->pixels,0,count*sizeof(*r->pixels));
    return true;
}
void ScRendererDestroy(ScRenderer *r) {
    free(r->pixels); free(r->held_ppu); free(r->advisor_pixels); memset(r,0,sizeof(*r));
}
void ScRendererResetHistory(ScRenderer *r) {
    r->scroll_valid=r->objects_valid=r->map_valid=r->map_hold=r->title_live=false;
    r->city_input=r->pointer_active=false;
    memset(r->changed_cells,0,sizeof r->changed_cells);
}
void ScRendererBeginMapLoad(ScRenderer *r) {
    if (!r->map_valid || r->map_hold) return;
    r->map_hold=true; r->map_dark=false;
    r->map_age=0;
}
static void render_row(ScRenderer *r,const Ppu *p,const uint8_t *ram,int y) {
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
    if (city) object_row(r,p,y,objects);
    r->selector_row=NULL;
    if (!city && (r->selector_count || r->sign_count)) {
        host_sprites_row(r,p,y,marks); r->selector_row=marks;
    }
    for (int x=0;x<r->view.width;++x) {
        int local=x-r->view.core_x;
        if (!r->advisor_frame && y>=0 && y<224 && local>=0 && local<256 &&
            !((local<8 && (r->repaired_edges[y]&1)) ||
              (local>=248 && (r->repaired_edges[y]&2)))) continue;
        if (!city) { out[x]=scenery(r,p,ram,local,y); continue; }
        unsigned ci=cell_pixel(r,p,ram,sx+local,sy+y+1,false);
        unsigned over=cell_pixel(r,p,ram,sx+local+8,sy+y+9,true);
        if (over) ci=over;
        unsigned warning=power_warning_pixel(r,p,ram,local,y,sx,sy);
        int edge=local<0 ? 0 : local>255 ? 255 : local;
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
            (p->objBuffer.data[x+kPpuExtraLeftRight]&255);
        r->advisor_pixels[y*256+x]=(page || obj) ? native[x]|0xff000000 : 0;
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
    if (r->view.width<=256) return;
    if (slot==49) *dx=right_shift(r);
    else {
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
    int dx=right_shift(r);
    if (r->split_hud && in_rect(cx,cy,144+dx,0,112,46)) cx-=dx;
    else if (r->pan_frame && in_rect(cx,cy,190+dx,46,48,48)) {
        cx-=dx;
        if (navigation) *navigation=true;
    }
    else if (r->pan_frame && r->view.width>256) {
        const int slots[]={49,51,52},xs[]={214,134,134},ys[]={118,62,174};
        for (int i=0;i<3;++i) {
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
    int px=r->scroll_x+r->scroll_adjust_x+x,py=r->scroll_y+r->scroll_adjust_y+y;
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
/* Construction and development update world cells before the SNES's small
 * tile cache reaches them. Draw those cells from the same source as the
 * margins, keeping native UI and the PPU's evaluated objects intact. Keep
 * tracking edited cells until a map load; their live CHR still animates. */
static void fresh_city_row(ScRenderer *r,const Ppu *p,const uint8_t *ram,int y) {
    if (!city_live(r,p,ram) || r->advisor_frame || r->map_hold ||
        !(p->screenEnabled[0]&2)) return;
    int sx=r->scroll_x+r->scroll_adjust_x+scroll_delta(p->hScroll[1],r->scroll_h);
    int sy=r->scroll_y+r->scroll_adjust_y+scroll_delta(p->vScroll[1],r->scroll_v);
    uint32_t *out=r->pixels+(size_t)(y+r->view.core_y)*r->view.width+r->view.core_x;
    for (int x=0;x<256;++x) {
        bool clear_warning=stale_power_warning(r,p,ram,x,y,sx,sy);
        bool warning_cell=power_warning_cell(r,ram,sx+x,sy+y+1) &&
            !(u16(ram,0x1d7) && (y<46 || (x<56 && y<224)));
        if (!clear_warning && !warning_cell && !changed_cell(r,sx+x,sy+y+1) && !changed_cell(r,sx+x+8,sy+y+9)) continue;
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
                if (layer==0 && clear_warning) continue;
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
            unsigned obj=p->objBuffer.data[x+kPpuExtraLeftRight];
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
        if (slot<4 || (r->pan_frame && slot>=39 && slot<=52 && slot!=50)) continue;
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
    bool land=r->pointer_active && ScRendererCityPoint(r,ram,r->pointer_x,r->pointer_y,&wx,&wy);
    if (!land && !r->split_hud) return;
    /* Move the entire UI cursor as one group, including rows below the HUD.
     * The ROM's hand is a 16px sprite; construction corners are 8px sprites. */
    int shift=r->split_hud && sprite_x(p,0)>=144 && sprite_x(p,0)<256 &&
        (p->oam[0]>>8)<46?right_shift(r):0;
    if (land || shift) for (int slot=0;slot<4;++slot) {
        int ox=sprite_x(p,slot); if (ox>=256) ox-=512;
        for (int y=0;y<224;++y) {
            if (r->split_hud && y<46) continue; /* header already rebuilt */
            int row=(y-(p->oam[slot*2]>>8))&255;
            if (row>=64) continue;
            for (int x=0;x<64;++x) if (sprite_pixel(p,slot,x,row)) {
                int source=ox+x,ax=r->view.core_x+source,ay=r->view.core_y+y;
                if (source>=0 && source<256 && ax>=0 && ax<r->view.width && ay>=0 && ay<r->view.height)
                    r->pixels[(size_t)ay*r->view.width+ax]=without_pointer(r,p,ram,source,y);
            }
        }
    }
    ScSelSprite sprites[8]; int count=0;
    if (land) {
        /* OAM can still contain the HUD hand or parked corners on the first
         * land frame. Get the selected tool's authentic outline from ROM. */
        unsigned tool=u16(ram,0x20d);
        if (tool>14) return;
        /* 01:8000 is one BYTE per tool, not a word/pointer table. */
        unsigned record=rom_read(r,0x018000+tool);
        count=ScSelector_Record(record,wx*8-r->scroll_x-r->scroll_adjust_x,
            wy*8-r->scroll_y-r->scroll_adjust_y,sprites,8,rom_read,r);
    } else for (int slot=0;slot<4;++slot) {
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
        /* Parked native pieces must stay outside the native rectangle. */
        if (!land && (s->x-shift+size<=0 || s->x-shift>=256)) continue;
        for (int y=0;y<size;++y) for (int x=0;x<size;++x) {
            unsigned ci=sprite_word_pixel(p,s->tile|(s->attr<<8),size,x,y);
            int ax=r->view.core_x+s->x+x,ay=r->view.core_y+(land?s->y+y:(s->y+y)&255);
            if (ci && ax>=0 && ax<r->view.width && ay>=0 && ay<r->view.height)
                r->pixels[(size_t)ay*r->view.width+ax]=composite_color(p,ci,ci<192?6:4,0,5,s->x+x);
        }
    }
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
ScVideoRect ScRendererMinimapView(const ScRenderer *r,const uint8_t *ram) {
    bool large=r->world && r->world->active;
    int width=(large?ScWorldWidth(r->world):120)*8,height=(large?ScWorldHeight(r->world):100)*8;
    int x0=r->scroll_x+r->scroll_adjust_x+
        (r->view.core_x?-r->view.core_x:u16(ram,0x1d7)?56:0);
    int y0=r->scroll_y+r->scroll_adjust_y+
        (r->view.core_y?-r->view.core_y:u16(ram,0x1d7)?46:0);
    int x1=r->scroll_x+r->scroll_adjust_x+r->view.width-r->view.core_x;
    int y1=r->scroll_y+r->scroll_adjust_y+r->view.height-r->view.core_y;
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
    if (!r->pan_frame) return;
    for (int slot=39;slot<=52;++slot) {
        if (slot==50) continue; /* left arrow stays beside the toolbar */
        int ox=sprite_x(p,slot); if (ox>=256) ox-=512;
        int row=(y-(p->oam[slot*2]>>8))&255;
        if (row>=64) continue;
        for (int x=0;x<64;++x) if (sprite_pixel(p,slot,x,row)) {
            int source=ox+x,target=core+source;
            if (target>=0 && target<r->view.width)
                out[target]=bare_city_pixel(r,p,ram,source,y);
        }
    }
    /* Earlier OAM slots win. The mini frame and arrows are opaque UI sprites. */
    for (int slot=52;slot>=40;--slot) {
        if (slot==50) continue;
        int dx=0,dy=0;
        if (slot<=48) dx=shift; else arrow_shift(r,slot,&dx,&dy);
        int row=(y-dy-(p->oam[slot*2]>>8))&255;
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
void ScRendererLine(ScRenderer *r,const Ppu *p,const uint8_t *ram,int line,const uint32_t *native) {
    if (!r->pixels || !p || !ram || !native || line<0 || line>=224) return;
    if (line==0) {
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
            (r->view.core_x!=(r->view.width-256)/2 || r->view.core_y!=(r->view.height-224)/2);
        bool hud=city_live(r,p,ram) && !r->advisor_frame && u16(ram,0x1d7) &&
            (p->screenEnabled[0]&3)==3 && !u16(ram,0x379);
        r->split_hud=hud && r->view.width>256;
        r->pan_frame=city_live(r,p,ram) && !r->advisor_frame && !u16(ram,0x379) &&
            (p->screenEnabled[0]&16) && (p->oam[81]&255)==0x66 && (p->oam[80]>>8)==46 &&
            sprite_x(p,40)<256; /* high X bit parks the hidden minimap */
        r->city_input=city_live(r,p,ram) && !r->advisor_frame && !u16(ram,0x379) &&
            !u16(ram,0xd7) && !ram[0x391];
        find_lights(r,p);
        r->selector_count=0;
        if (ScSelector_OnScreen(ram[0x14])) {
            int scroll=ScSelector_Scroll(p,rom_read,r);
            if (scroll>=0)
                r->selector_count=ScSelector_Sprites(r->selector,scroll,
                    ram[0x42]|((unsigned)ram[0x43]<<8),r->sylt,rom_read,r);
        }
        r->sign_count=ScTitleSign_Sprites(r->sign,SC_SIGN_MAX_SPRITES,rom_read,r);
        track_scroll(r,p,ram);
        track_objects(r,p,ram);
        track_map_swap(r,p,ram);
    }
    /* The 32-column guest tilemap stages incoming tiles in CRT overscan.
     * Reconstruct only those edge bands, and never cover UI or native OBJ. */
    r->repaired_edges[line]=0;
    if (r->view.width>256 && city_live(r,p,ram) && (p->screenEnabled[0]&2)) {
        if (!edge_has_overlay(p,line,0)) r->repaired_edges[line]|=1;
        if (!edge_has_overlay(p,line,248)) r->repaired_edges[line]|=2;
    }
    if (line==0) for (int y=-r->view.core_y;y<0;++y) render_row(r,p,ram,y);
    render_row(r,p,ram,line);
    int first=(r->repaired_edges[line]&1) ? 8 : 0;
    int end=(r->repaired_edges[line]&2) ? 248 : 256;
    if (r->advisor_frame) capture_advisor_row(r,p,line,native);
    else memcpy(r->pixels+(size_t)(line+r->view.core_y)*r->view.width+r->view.core_x+first,
                native+first,(size_t)(end-first)*sizeof(*native));
    fresh_city_row(r,p,ram,line);
    if (r->split_hud || r->pan_frame) city_hud_row(r,p,ram,line);
    if (city_live(r,p,ram) && !r->advisor_frame && u16(ram,0x1d7) &&
        (p->screenEnabled[0]&3)==3 && !u16(ram,0x379))
        ScRendererPopulationRow(r,p,r->view,r->split_hud,line,
            r->pixels+(size_t)(line+r->view.core_y)*r->view.width);
    if (line==223) {
        for (int y=224;y<r->view.height-r->view.core_y;++y) render_row(r,p,ram,y);
        fill_flat_margins(r);
        if (r->advisor_frame) place_advisor(r);
        city_pointer(r,p,ram);
    }
}
