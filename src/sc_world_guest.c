#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <string.h>

static unsigned word(const uint8_t *r,unsigned p) { return r[p]|((unsigned)r[p+1]<<8); }
static void put(uint8_t *r,unsigned p,unsigned v) { r[p]=(uint8_t)v; r[p+1]=(uint8_t)(v>>8); }
static void nz(Interp816 *c,unsigned v,bool byte) {
    c->z=(v&(byte?255:65535))==0; c->n=(v&(byte?128:32768))!=0;
}
void ScWorldGuestStep(ScWorld *w,Interp816 *c,uint8_t *r) {
    if (!w->active) return;
    if (c->k==1) {
        if (c->pc==0x8abf || c->pc==0x8acb) {
            int camera=(int16_t)word(r,c->pc==0x8abf?0x1bd:0x1bf);
            if (camera<0) camera=0;
            c->a=(uint16_t)((c->pc==0x8abf?200:56)+camera/8);
            nz(c,c->a,false);
        }
        /* The placement helper packs Y in the low byte, X in the high byte. */
        if (c->pc==0xba3f || c->pc==0xbdce || c->pc==0xbe07) {
            bool read=c->pc==0xba3f;
            unsigned x=read?c->a>>8:word(r,0x219),y=read?c->a&255:word(r,0x21b);
            unsigned offset=2*(y*SC_WORLD_WIDTH+x);
            w->bank_anchor[1]=ScWorldBounds(x,y)?offset:UINT32_MAX;
            c->x=(uint16_t)offset; c->mf=false;
            if (read) { put(r,0x79,y*SC_WORLD_WIDTH); c->a=ScWorldCell(w,x,y)&1023; c->pc=0xba7f; }
            else { c->a=(uint16_t)word(r,0x215); ScWorldPutCell(w,x,y,c->a); c->pc=c->pc==0xbdce?0xbe06:0xbe3f; }
            nz(c,c->a,false); c->c=offset>65535;
        } else if (c->pc==0xc7b4 || c->pc==0xbe71 || c->pc==0xbe82) {
            unsigned pos=c->pc==0xc7b4?0x1d3:0x233;
            int x=(int16_t)word(r,pos),y=(int16_t)word(r,pos+2);
            unsigned offset=2*(y*SC_WORLD_WIDTH+x);
            w->bank_anchor[1]=ScWorldBounds(x,y)?offset:UINT32_MAX;
            c->x=(uint16_t)offset;
        } else if (c->pc==0xb6db || c->pc==0xb6e9 || c->pc==0xb714 ||
                   c->pc==0xb722 || c->pc==0xb8af || c->pc==0xb8bf ||
                   c->pc==0xc779 || c->pc==0xc782) {
            /* Byte coordinates >=128 are valid; the following unsigned CMP
             * rejects underflow ($ff) as well as the expanded upper bound. */
            c->pc+=2;
        }
        return;
    }
    if (c->k!=3) return;
    if (c->pc==0xb120) {
        unsigned row=r[0xb86]*30;
        put(r,0xc5b,row); c->x=(uint16_t)(row+r[0xb85]/8);
        c->y=c->a=r[0xb85]&7; c->mf=false; c->c=false;
        nz(c,c->a,false); c->pc=0xb151;
    } else if (c->pc==0xb37e || c->pc==0xb39a) {
        c->n=(c->pc==0xb37e?c->y:c->x)==255;
    } else if (c->pc==0xade7) {
        w->map_anchor=0;
    } else if (c->pc==0xadee) {
        if (c->x==(uint16_t)(w->map_anchor+2)) w->map_anchor+=2;
    } else if (c->pc==0xae17) {
        unsigned end=w->map_anchor+2;
        c->z=c->c=end==SC_WORLD_TILE_BYTES; c->n=false; c->pc=0xae1a;
    } else if (c->pc==0xaca7 || c->pc==0xacda || c->pc==0xad09 || c->pc==0xbaca) {
        unsigned x=word(r,c->dp),y=word(r,c->dp+4),offset=2*(y*SC_WORLD_WIDTH+x);
        w->map_anchor=ScWorldBounds(x,y)?offset:UINT32_MAX; c->x=(uint16_t)offset;
    } else if (c->pc==0x849e || c->pc==0x84c4) {
        unsigned x=c->a&255,y=c->a>>8;
        unsigned offset=2*(y*SC_WORLD_WIDTH+x);
        bool valid=ScWorldBounds(x,y);
        w->map_anchor=valid?offset:UINT32_MAX;
        put(r,0xb3f,x*2); put(r,0xb3d,y*32);
        c->x=(uint16_t)offset; c->mf=false;
        if (c->pc==0x849e) {
            c->a=valid?ScWorldCell(w,x,y):0; c->pc=0x84c3; nz(c,c->a,false);
        } else {
            if (valid) ScWorldPutCell(w,x,y,c->y);
            /* The native writer finishes with TYA/STA, returning the tile. */
            c->a=c->y; c->pc=0x84ea; nz(c,c->a,false);
        }
        c->c=offset>65535;
    } else if (c->pc==0xb152) {
        /* A byte offset for 48,000 tiles cannot be represented by native X.
         * Apply the original MSB-first power bitmap to every full-size cell. */
        for (unsigned i=0;i<SC_WORLD_CELLS;++i) {
            unsigned tile=word(w->tiles,i*2)&0x7fff;
            if (w->fields[5][i/8]&(128>>(i&7))) tile|=0x8000;
            put(w->tiles,i*2,tile);
        }
        put(r,0x23f,0); c->a=0; c->x=(uint16_t)SC_WORLD_TILE_BYTES;
        c->y=SC_WORLD_CELLS/8; c->mf=false; c->c=true;
        nz(c,c->dp,false); c->pc=0xb1a4;
    } else if (c->pc==0x82b9) {
        /* The native sweep keeps a byte offset in its DP frame. Reconstruct
         * it from the full unsigned coordinates before each load; the second
         * bank of cells must not alias the first after offset 65,535. */
        unsigned offset=2*(r[0xb86]*SC_WORLD_WIDTH+r[0xb85]);
        put(r,c->dp,offset); w->map_anchor=offset;
    } else if (c->pc==0xa29a || c->pc==0xa2b9 || c->pc==0xa2d7) {
        unsigned stride=c->pc==0xa29a?120:c->pc==0xa2b9?60:30;
        unsigned x=c->a&255,y=c->a>>8,offset=y*stride+x;
        put(r,0xb3f,x); put(r,0xb3d,y);
        c->a=c->x=(uint16_t)offset; c->mf=true; nz(c,offset,false);
        c->c=false;
        c->pc=c->pc==0xa29a?0xa2b8:c->pc==0xa2b9?0xa2d6:0xa2f4;
    }
}
void ScWorldGuestVehicles(ScWorld *w,Interp816 *c,const uint8_t *r,uint8_t my) {
    if (!w->active || c->k!=0) return;
    unsigned x,y;
    if (c->pc==0xb077) { x=word(r,0xa5d); y=word(r,0xa5b); }
    else if (c->pc==0xb4dd) { x=word(r,0xae1)/8; y=word(r,0xae3)/8; }
    else if (c->pc==0xb719) { x=word(r,0xa61); y=r[0xa5f]; }
    else if (c->pc==0xb314) {
        unsigned row=2*my*SC_WORLD_WIDTH;
        int full=(int)row+(int16_t)(c->x-(uint16_t)row);
        w->bank_anchor[0]=full>=0 && full<SC_WORLD_TILE_BYTES?(uint32_t)full:UINT32_MAX;
        return;
    } else if (c->pc==0xb8f3 || c->pc==0xb968 || c->pc==0xb9ab) {
        int delta=(int16_t)c->a;
        int row=delta>=0?(delta+120)/240:(delta-120)/240;
        c->a=(uint16_t)(delta+row*240); return;
    } else if (c->pc==0xb8f6 || c->pc==0xb96b || c->pc==0xb9ae) {
        unsigned row=2*my*SC_WORLD_WIDTH;
        int full=(int)row+(int16_t)(c->a-(uint16_t)row);
        w->bank_anchor[0]=full>=0 && full<SC_WORLD_TILE_BYTES?(uint32_t)full:UINT32_MAX;
        c->c=full<0 || full>=SC_WORLD_TILE_BYTES; c->z=false; c->n=c->c; c->pc+=3; return;
    } else return;
    unsigned offset=2*(y*SC_WORLD_WIDTH+x);
    w->bank_anchor[0]=ScWorldBounds(x,y)?offset:UINT32_MAX; c->x=(uint16_t)offset;
}
/* Verified map geometry only; preserve currency, tile IDs and time lengths. */
static unsigned dimension(uint32_t pc) {
    switch (pc) {
    case 0x00ac85: return 240;
    case 0x00ac9d: return 200;
    case 0x00aefb: return 239;
    case 0x00af6b: return 199;
    case 0x00afde: return 240;
    case 0x00aff7: return 200;
    case 0x00b05f: return 240;
    case 0x00b29d: return 240;
    case 0x00b2f6: return 240;
    case 0x00b4c2: return 240;
    case 0x00b706: return 240;
    case 0x00b7ed: return 200;
    case 0x00b806: return 240;
    case 0x00b9f8: return 240;
    case 0x01a08e: return 215;
    case 0x01a09a: return 178;
    case 0x01a0a9: return 210;
    case 0x01a0b5: return 174;
    case 0x01a698: case 0x01a69d: return 210;
    case 0x01a6b1: case 0x01a6b6: return 172;
    case 0x01b3a7: return 240;
    case 0x01b3ae: return 240;
    case 0x01b3d3: return 200;
    case 0x01b3da: return 200;
    case 0x01b447: return 240;
    case 0x01b44f: return 200;
    case 0x01b492: return 240;
    case 0x01b49a: return 200;
    case 0x01b551: return 240;
    case 0x01b559: return 200;
    case 0x01b6dd: return 240;
    case 0x01b6eb: return 200;
    case 0x01b716: return 240;
    case 0x01b724: return 200;
    case 0x01b7cd: return 240;
    case 0x01b7e9: return 200;
    case 0x01b8b1: return 240;
    case 0x01b8c1: return 200;
    case 0x01be51: return 240;
    case 0x01c200: return 200;
    case 0x01c28d: return 240;
    case 0x01c77b: return 240;
    case 0x01c784: return 200;
    case 0x01c794: return 240;
    case 0x038291: return 1500;
    case 0x038349: return 240;
    case 0x038356: return 200;
    case 0x038919: return 12000;
    case 0x038946: return 1500;
    case 0x03900a: return 239;
    case 0x039014: return 199;
    case 0x039886: return 200;
    case 0x03988f: return 240;
    case 0x039acc: return 750;
    case 0x039b60: return 240;
    case 0x039b68: return 200;
    case 0x039b88: return 12000;
    case 0x039c28: return 3000;
    case 0x039c44: return 120;
    case 0x039c4c: return 100;
    case 0x039cb6: return 120;
    case 0x039cbf: return 100;
    case 0x039f4d: return 120;
    case 0x039f59: return 100;
    case 0x039f97: return 750;
    case 0x039fd2: return 59;
    case 0x039fed: return 49;
    case 0x03a021: return 60;
    case 0x03a029: return 50;
    case 0x03a057: return 119;
    case 0x03a072: return 99;
    case 0x03a0a5: return 120;
    case 0x03a0ae: return 100;
    case 0x03a0dd: return 119;
    case 0x03a0f8: return 99;
    case 0x03a12b: return 120;
    case 0x03a134: return 100;
    case 0x03a147: return 12000;
    case 0x03a17c: return 29;
    case 0x03a18f: return 24;
    case 0x03a1aa: return 30;
    case 0x03a1b2: return 25;
    case 0x03a1c5: return 1500;
    case 0x03a1fb: return 29;
    case 0x03a20e: return 24;
    case 0x03a229: return 30;
    case 0x03a231: return 25;
    case 0x03a244: return 1500;
    case 0x03a28c: return 30;
    case 0x03a294: return 25;
    case 0x03a9c8: return 240;
    case 0x03a9cd: return 200;
    case 0x03acbd: return 198;
    case 0x03accf: return 199;
    case 0x03acec: return 238;
    case 0x03acfe: return 239;
    case 0x03ad1f: return 198;
    case 0x03afc7: return 6000;
    case 0x03b0c1: return 20000;
    case 0x03b10b: return 6000;
    case 0x03b385: return 239;
    case 0x03b38f: return 199;
    case 0x03b420: return 240;
    case 0x03b425: return 200;
    case 0x03b692: return 12000;
    case 0x03bae6: return 240;
    case 0x03baef: return 200;
    case 0x03bc9f: return 239;
    case 0x03bca7: return 199;
    default: return 0;
    }
}
static bool map_base(unsigned base) {
    switch (base) {
    case 0x001a: case 0x010c: case 0x010e: case 0x01fc: case 0x01fe:
    case 0x0200: case 0x0202: case 0x0204: case 0x0206:
    case 0x02f0: case 0x02f2: case 0x02f4:
    case 0x03e0: case 0x03e2: case 0x03e4:
    case 0x04ca: case 0x04d0: case 0x04d6: case 0x07a0: return true;
    default: return false;
    }
}
void ScWorldGuestBegin(ScWorldGuest *g,ScWorld *w,const Interp816 *c,
                       const uint8_t *rom,size_t size) {
    memset(g,0,sizeof *g);
    if (!w->active || c->pc<0x8000) return;
    if (c->k==3 && c->pc>=0xcf80) return; /* original-size native save codecs */
    size_t p=(size_t)(c->k&0x7f)*32768+c->pc-0x8000;
    if (p+3>=size) return;
    unsigned op=rom[p],base=word(rom,(unsigned)p+1),index=0,bank=c->db;
    uint32_t pc=((uint32_t)c->k<<16)|c->pc;
    unsigned dim=(op==0xa9 || op==0xc9 || op==0xe0 || op==0xc0)?dimension(pc):0;
    if (dim) {
        bool index_width=op==0xe0 || op==0xc0;
        g->bytes=(index_width?c->xf:c->mf)?1:2;
        g->immediate[0]=(uint8_t)dim; g->immediate[1]=(uint8_t)(dim>>8);
        g->data=g->immediate; g->mapped=true; g->address=pc+1; return;
    }
    bool indexed=false;
    switch (op) {
    case 0x1f: case 0x3f: case 0x5f: case 0x7f:
    case 0x9f: case 0xbf: case 0xdf: case 0xff:
        index=c->x; indexed=true; /* fall through */
    case 0x0f: case 0x2f: case 0x4f: case 0x6f:
    case 0x8f: case 0xaf: case 0xcf: case 0xef:
        bank=rom[p+3]; g->address=((uint32_t)bank<<16)+base+index; break;
    case 0x19: case 0x39: case 0x59: case 0x79:
    case 0x99: case 0xb9: case 0xd9: case 0xf9:
        index=c->y; indexed=true;
        g->address=((uint32_t)bank<<16)|((base+index)&65535); break;
    case 0x1d: case 0x3d: case 0x5d: case 0x7d:
    case 0x9d: case 0xbd: case 0xdd: case 0xfd:
        index=c->x; indexed=true;
        g->address=((uint32_t)bank<<16)|((base+index)&65535); break;
    default: return;
    }
    if (bank!=0x7f) return;
    g->bytes=c->mf?1:2;
    if (map_base(base)) {
        if (c->k==3 && c->pc>=0xcf80) return; /* native map codecs, handled separately */
        int offset=(int)base-0x200;
        int row=offset>=0?(offset+120)/240:(offset-120)/240;
        offset+=row*240; /* double row pitch; retain the column displacement */
        uint32_t anchor=c->k==3?w->map_anchor:c->k<3?w->bank_anchor[c->k]:UINT32_MAX;
        int logical=(int)anchor;
        if (indexed) logical+=(int16_t)(index-(uint16_t)anchor);
        else logical=0;
        logical+=offset;
        if (c->k==2) {
            unsigned cell=index/2;
            logical=2*((cell/120)*2*SC_WORLD_WIDTH+(cell%120)*2)+offset;
            anchor=0;
        }
        g->mapped=true;
        if (anchor!=UINT32_MAX && logical>=0 &&
            (unsigned)logical+g->bytes<=SC_WORLD_TILE_BYTES) g->data=w->tiles+logical;
    } else {
        unsigned field; int displacement;
        if (!ScWorldFieldResolve((uint16_t)base,&field,&displacement)) return;
        if (c->k==2 && field==5) {
            /* An overview pixel represents two cells along each axis. Packed
             * power bytes need bit resampling, rather than a wider byte stride. */
            for (unsigned i=0;i<g->bytes;++i) {
                unsigned original=index+i,y=original/15*2,x=original%15*16;
                unsigned bits=0;
                if (y<SC_WORLD_HEIGHT) for (unsigned bit=0;bit<8;++bit) {
                    unsigned cell=y*SC_WORLD_WIDTH+x+bit*2;
                    if (w->fields[5][cell/8]&(128>>(cell&7))) bits|=128>>bit;
                }
                g->immediate[i]=(uint8_t)bits;
            }
            g->mapped=true; g->data=g->immediate; return;
        }
        int logical=(int)index+displacement;
        if (c->k==2) {
            const ScWorldField *f=&ScWorldFields[field];
            unsigned cell=index/f->element_bytes;
            unsigned y=cell/f->stock_width*2,x=cell%f->stock_width*2;
            if (y>=f->height) y=f->height-1;
            logical=(y*f->width+x)*f->element_bytes+displacement;
        }
        g->mapped=true;
        if (logical>=0 && (unsigned)logical+g->bytes<=ScWorldFieldSize(field))
            g->data=w->fields[field]+logical;
    }
}
bool ScWorldGuestRead(const ScWorldGuest *g,uint32_t a,uint8_t *v) {
    if (!g->mapped || a<g->address || a-g->address>=g->bytes) return false;
    *v=g->data?g->data[a-g->address]:0; return true;
}
bool ScWorldGuestWrite(ScWorldGuest *g,uint32_t a,uint8_t v) {
    if (!g->mapped || a<g->address || a-g->address>=g->bytes) return false;
    if (g->data) g->data[a-g->address]=v;
    return true;
}
