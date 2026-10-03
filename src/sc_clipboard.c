#include "sc_clipboard.h"
#include <stdlib.h>
#include <string.h>

static unsigned word(const uint8_t *p,unsigned at) {return p[at]|(unsigned)p[at+1]<<8;}
static void put(uint8_t *p,unsigned at,unsigned v) {p[at]=(uint8_t)v;p[at+1]=(uint8_t)(v>>8);}
static int width(const ScWorld *w) {return w && w->active?ScWorldWidth(w):120;}
static int height(const ScWorld *w) {return w && w->active?ScWorldHeight(w):100;}
static unsigned cell(const uint8_t *ram,const ScWorld *w,int x,int y) {
    if(x<0 || y<0 || x>=width(w) || y>=height(w)) return 0;
    return word(w && w->active?w->tiles:ram+0x10200,2*(y*width(w)+x));
}
static void set(uint8_t *ram,ScWorld *w,int x,int y,unsigned v) {
    if(w && w->active) ScWorldPutCell(w,x,y,v);
    else put(ram+0x10200,2*(y*width(w)+x),v);
}
static unsigned property(const uint8_t *rom,unsigned t) {return t<958?rom[0x184eb+t]:0;}
static bool reward(unsigned tile) {return tile>=0x2bb && tile<0x354;}
/* Artwork also identifies a footprint when its centre marker has been
 * replaced. Free-zone houses and disaster debris are single-cell artwork;
 * a surviving zone centre, found first, still selects their whole zone. */
static unsigned art_footprint(unsigned tile,unsigned *offset,unsigned *owner) {
    unsigned base,k;
    if(tile>=0x80 && tile<0x89) {base=0x80;k=3;}
    else if(tile>=0x95 && tile<0x257) {base=0x95+(tile-0x95)/9*9;k=3;}
    else if(tile>=0x257 && tile<0x297) {base=0x257+(tile-0x257)/16*16;k=4;}
    else if(tile>=0x297 && tile<0x2bb) {base=0x297;k=6;}
    else if(reward(tile)) {base=0x2bb+(tile-0x2bb)/9*9;k=3;}
    else if(tile>=0x366 && tile<0x376) {base=0x366;k=4;}
    else if(tile>=0x376 && tile<958) {base=0x376+(tile-0x376)/9*9;k=3;}
    else return 0;
    *offset=tile-base;*owner=base+(k==6?14:k==4?5:4);return k;
}
static unsigned footprint(unsigned owner) {
    return owner==0x2a5?6:owner==0x25c || owner==0x26c || owner==0x27c ||
        owner==0x28c || owner==0x36b?4:3;
}
static unsigned object_tool(unsigned owner) {
    if(owner==0x249) return 8;
    if(owner==0x252) return 9;
    if(owner==0x25c || owner==0x36b) return 10;
    if(owner==0x26c) return 11;
    if(owner==0x2a5) return 12;
    if(owner==0x27c) return 13;
    if(owner==0x28c) return 14;
    return owner<0x137 || (owner>=0x376 && owner<0x39a)?5:
        owner<0x1f4 || owner>=0x39a?6:7;
}
static unsigned price(const uint8_t *rom,unsigned tile) {
    if(reward(tile)) return 0;
    if(property(rom,tile)&1) return word(rom,0xbb58+2*object_tool(tile));
    if(tile==0x26 || tile==0x27) return word(rom,0xbb58+8);
    unsigned value=0;
    if(tile>=0x30 && tile<0x80 && (tile&15)!=15) {
        if(tile<0x60 || tile>=0x7d) value+=word(rom,0xbb58+2);
        if(tile>=0x70 || tile==0x6d || tile==0x6e) value+=word(rom,0xbb58+4);
        if((tile>=0x60 && tile<0x70) || (tile<0x60 && (tile&15)>=13))
            value+=word(rom,0xbb58+6);
        if(property(rom,tile)&8) value*=2; /* native bridge/water surcharge */
    }
    return value;
}
void ScClipboardClear(ScClipboard *c) {
    free(c->tiles);free(c->mask);memset(c,0,sizeof *c);
}
bool ScClipboardCopy(ScClipboard *c,const uint8_t *ram,const ScWorld *w,
    const uint8_t *rom,size_t size,int x0,int y0,int x1,int y1) {
    if(!c || !ram || !rom || size!=0x80000) return false;
    if(x0>x1) {int t=x0;x0=x1;x1=t;}
    if(y0>y1) {int t=y0;y0=y1;y1=t;}
    int mw=width(w),mh=height(w);
    if(x0<0 || y0<0 || x1>=mw || y1>=mh) return false;
    /* Any centre owning an intersecting six-tile footprint is within five
     * cells. Keep the original rectangle fixed while discovering owners: a
     * dense city must not recursively expand into neighbouring buildings. */
    int ax=x0>5?x0-5:0,ay=y0>5?y0-5:0;
    int bx=x1+5<mw?x1+5:mw-1,by=y1+5<mh?y1+5:mh-1;
    ScClipboard next={0};next.width=bx-ax+1;next.height=by-ay+1;
    size_t n=(size_t)next.width*next.height;
    next.tiles=calloc(n,sizeof *next.tiles);next.mask=calloc(n,1);
    uint8_t *owned=calloc(n,1),*excluded=calloc(n,1);
    if(!next.tiles || !next.mask || !owned || !excluded) goto fail;
    for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) next.mask[(y-ay)*next.width+x-ax]=1;
    for(int cy=ay;cy<=by;++cy) for(int cx=ax;cx<=bx;++cx) {
        unsigned t=cell(ram,w,cx,cy)&1023;
        if(!(property(rom,t)&1)) continue;
        int k=footprint(t),shift=k==6?2:1,l=cx-shift,top=cy-shift;
        if(l>x1 || top>y1 || l+k<=x0 || top+k<=y0) continue;
        if(l<0 || top<0 || l+k>mw || top+k>mh) goto fail;
        bool special=reward(t);
        if(special) ++next.excluded;
        for(int y=top;y<top+k;++y) for(int x=l;x<l+k;++x) {
            if(x<ax || y<ay || x>bx || y>by) goto fail;
            unsigned i=(y-ay)*next.width+x-ax;
            owned[i]=1;
            if(special) excluded[i]=1;else next.mask[i]=1;
        }
    }
    /* Recover only objects touched by the original outline. Expanding this
     * pass to the growing mask would recursively select the entire city. */
    for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
        unsigned i=(y-ay)*next.width+x-ax,t=cell(ram,w,x,y)&1023;
        if(owned[i]) continue;
        unsigned offset,owner,k=art_footprint(t,&offset,&owner);
        if(!k) continue;
        int l=x-offset%k,top=y-offset/k;
        if(l<0 || top<0 || l+(int)k>mw || top+(int)k>mh) {
            next.mask[i]=0;continue;
        }
        if(reward(t)) ++next.excluded;
        else next.price+=price(rom,owner);
        for(unsigned dy=0;dy<k;++dy) for(unsigned dx=0;dx<k;++dx) {
            unsigned at=(top+dy-ay)*next.width+l+dx-ax;
            owned[at]=1;
            if(reward(t)) excluded[at]=1;else next.mask[at]=1;
        }
    }
    int left=next.width,top=next.height,right=-1,bottom=-1;
    for(int y=0;y<next.height;++y) for(int x=0;x<next.width;++x) {
        unsigned i=y*next.width+x,t=cell(ram,w,ax+x,ay+y)&1023;
        if(excluded[i] || reward(t)) next.mask[i]=0;
        if(!next.mask[i]) continue;
        /* Unowned single houses/debris are valid cells. Unsupported remnants
         * outside the original outline must not reject all the other objects. */
        if(t>=0x95 && t<958 && !owned[i] && !(t>=0x354 && t<=0x365)) {
            next.mask[i]=0;continue;
        }
        next.tiles[i]=(uint16_t)(cell(ram,w,ax+x,ay+y)&0x7fff);
        next.price+=price(rom,t);++next.count;
        if(x<left) left=x;if(x>right) right=x;if(y<top) top=y;if(y>bottom) bottom=y;
    }
    if(!next.count) goto fail;
    /* Compact the bounding rectangle but keep holes, especially rewards. */
    int old_width=next.width,new_width=right-left+1,new_height=bottom-top+1;
    for(int y=0;y<new_height;++y) {
        memmove(next.tiles+y*new_width,next.tiles+(y+top)*old_width+left,new_width*sizeof *next.tiles);
        memmove(next.mask+y*new_width,next.mask+(y+top)*old_width+left,new_width);
    }
    next.source_x=ax+left;next.source_y=ay+top;next.width=new_width;next.height=new_height;
    free(owned);free(excluded);ScClipboardClear(c);*c=next;return true;
fail:
    free(owned);free(excluded);ScClipboardClear(&next);return false;
}
bool ScClipboardFits(const ScClipboard *c,const uint8_t *ram,const ScWorld *w,
    const uint8_t *rom,size_t size,int x,int y) {
    if(!c || !c->count || !c->tiles || !c->mask || !ram || !rom || size!=0x80000) return false;
    if(x<0 || y<0 || x>width(w)-c->width || y>height(w)-c->height) return false;
    for(int dy=0;dy<c->height;++dy) for(int dx=0;dx<c->width;++dx) {
        unsigned i=dy*c->width+dx;if(!c->mask[i]) continue;
        unsigned t=cell(ram,w,x+dx,y+dy)&1023;
        if(t>=0x30 && t!=0x365) return false;
    }
    return true;
}
static bool table_has(const uint8_t *rom,unsigned base,unsigned tile) {
    for(unsigned i=0;i<128;++i) {unsigned t=word(rom,base+2*i);if(t==tile) return true;if(t==0xffff) break;}
    return false;
}
/* Exact 01:b705/b829/b62f join masks in C. Crossings and bridges retain their
 * two fixed axes; ordinary neighbours receive the ROM's own connection art. */
static void join(uint8_t *ram,ScWorld *w,const uint8_t *rom,int x,int y) {
    if(x<0 || y<0 || x>=width(w) || y>=height(w)) return;
    unsigned raw=cell(ram,w,x,y),t=raw&1023,low=t&15;
    if(t<0x30 || t>=0x80 || low<2 || low>=13) return;
    unsigned table=t<0x60?0x8318:t>=0x70?0x8378:0x83ae,mask=0;
    for(unsigned d=0;d<4;++d) {
        int nx=x+(int8_t)rom[0x1b61+d],ny=y+(int8_t)rom[0x1b65+d];
        if(nx<0 || ny<0 || nx>=width(w) || ny>=height(w)) continue;
        unsigned neighbour=cell(ram,w,nx,ny)&1023;
        bool connected=table_has(rom,table,neighbour);
        if(table==0x83ae && neighbour>=0x80 && (neighbour<0x354 || neighbour>=0x366)) connected=true;
        if(connected) mask|=1u<<d;
    }
    unsigned base=t<0x60?0x30:t>=0x70?0x70:0x60;
    set(ram,w,x,y,(raw&~1023u)|base+rom[0x8020+mask]);
}
ScBuildResult ScClipboardPaste(const ScClipboard *c,uint8_t *ram,ScWorld *w,
    const uint8_t *rom,size_t size,int x,int y) {
    if(!ScClipboardFits(c,ram,w,rom,size,x,y)) return SC_BUILD_INVALID;
    uint64_t available=word(ram,0xb9d)|(unsigned)ram[0xb9f]<<16;
    uint64_t cost=ram[0x425]&2?0:c->price;
    if(cost>available) return SC_BUILD_FUNDS;
    for(int dy=0;dy<c->height;++dy) for(int dx=0;dx<c->width;++dx) {
        unsigned i=dy*c->width+dx;if(c->mask[i]) set(ram,w,x+dx,y+dy,c->tiles[i]);
    }
    for(int dy=-1;dy<=c->height;++dy) for(int dx=-1;dx<=c->width;++dx) {
        /* Only the copied area and immediate neighbours can change joins. */
        bool near=false;
        static const int offsets[5][2]={{0,0},{1,0},{-1,0},{0,1},{0,-1}};
        for(unsigned k=0;k<5;++k) {
            int ax=dx+offsets[k][0],ay=dy+offsets[k][1];
            if(ax>=0 && ay>=0 && ax<c->width && ay<c->height && c->mask[ay*c->width+ax]) {near=true;break;}
        }
        if(near) join(ram,w,rom,x+dx,y+dy);
    }
    put(ram,0xb9d,(unsigned)(available-cost));ram[0xb9f]=(uint8_t)((available-cost)>>16);
    put(ram,0x23f,0xffff); /* native placement's map-change notification */
    return SC_BUILD_OK;
}
