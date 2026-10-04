#include "sc_terrain.h"
#include <stdlib.h>
#include <string.h>
bool ScTerrainResize(ScTerrainFrame *f,unsigned width,unsigned height) {
    if(!width || !height || width>4096 || height>4096) return false;
    if(f->width==width && f->height==height && f->tiles) return true;
    unsigned stride=(width+14)/8;
    ScTerrainTile *tiles=calloc((size_t)stride*height,sizeof *tiles);
    uint32_t *palette=calloc((size_t)height*256,sizeof *palette);
    ScTerrainRow *rows=calloc(height,sizeof *rows);
    ScTerrainOverlay *overlays=calloc((size_t)width*height,sizeof *overlays);
    ScNativeRow *native=calloc(height,sizeof *native);
    if(!tiles || !palette || !rows || !overlays || !native) {
        free(tiles);free(palette);free(rows);free(overlays);free(native);return false;
    }
    ScTerrainDestroy(f);
    *f=(ScTerrainFrame){.width=width,.height=height,.stride=stride,.tiles=tiles,
        .palette=palette,.rows=rows,.overlays=overlays,.native=native};return true;
}
void ScTerrainDestroy(ScTerrainFrame *f) {
    free(f->tiles);free(f->palette);free(f->rows);free(f->overlays);free(f->native);free(f->resources);free(f->city);memset(f,0,sizeof *f);
}
bool ScTerrainSpanWidth(ScTerrainFrame *f,unsigned width) {
    if(!f || !f->tiles || !width || width>4096)return false;
    if(width<f->width)width=f->width;
    unsigned stride=(width+14)/8;
    if(stride==f->stride) return true;
    ScTerrainTile *tiles=calloc((size_t)stride*f->height,sizeof *tiles);
    if(!tiles) return false;
    free(f->tiles);f->tiles=tiles;f->stride=stride;return true;
}
static unsigned decode(uint32_t planes,unsigned bit) {
    return ((planes>>bit)&1)|(((planes>>(bit+8))&1)<<1)|
        (((planes>>(bit+16))&1)<<2)|(((planes>>(bit+24))&1)<<3);
}
static unsigned resource_word(const ScTerrainFrame *f,const ScTerrainRow *row,unsigned at) {
    unsigned pair=f->resources[row->chr_snapshot+((at&0x7fff)>>1)];
    return (pair>>((at&1)*16))&65535;
}
static unsigned city_cell(const ScTerrainFrame *f,unsigned at) {
    unsigned raw=(f->city[at/2]>>((at&1)*16))&65535;
    return (raw&1023)<958?raw:UINT32_MAX;
}
static unsigned captured_sub_bg(const ScTerrainFrame *f,const ScTerrainRow *row,unsigned x) {
    if(!(row->math&SC_ROW_GPU_SUB_BG)) return row->sub_bg[x];
    unsigned sx=(x+row->sub_bg[0])&1023,sy=row->sub_bg[1],size=row->sub_bg[4];
    unsigned adr=row->sub_bg[2]+((sy>>3)&31)*32+((sx>>3)&31);
    if((sx&256) && (size&1)) adr+=0x400;
    if((sy&256) && (size&2)) adr+=size&1?0x800:0x400;
    unsigned word=resource_word(f,row,adr),dy=word&0x8000?7-(sy&7):sy&7;
    unsigned planes=resource_word(f,row,row->sub_bg[3]+(word&1023)*8+dy);
    unsigned ci=decode(planes,word&0x4000?sx&7:7-(sx&7));
    return ci?ci+((word>>10)&7)*4:0;
}
static unsigned captured_object(const ScTerrainFrame *f,const ScTerrainRow *row,unsigned x) {
    if(row->reserved==UINT32_MAX) return 0;
    unsigned bucket=x/32,header=row->reserved;
    if(bucket>=f->city[header]) return 0;
    if(row->math&SC_ROW_OBJECT_GRID) bucket+=((row->world_y>>8)&255)/32*f->city[header];
    unsigned at=f->city[header+1+bucket*2],count=f->city[header+2+bucket*2],result=0;
    for(unsigned i=0;i<count;++i,at+=6) {
        const uint32_t *r=f->city+at;int dx=(int)x-(int32_t)r[0];
        if((int)x<(int32_t)r[5] || dx<0 || (unsigned)dx>=r[1]) continue;
        unsigned y=r[2],attr=r[3],size=r[1];
        if(row->math&SC_ROW_OBJECT_GRID) {
            int dy=(int16_t)(row->world_y>>8)-(int32_t)r[2];
            if(attr&0x10000) dy&=255;
            if(dy<0 || (unsigned)dy>=size) continue;
            y=(unsigned)dy;
        }
        if(attr&0x4000) dx=(int)size-1-dx;
        if(attr&0x8000) y=size-1-y;
        unsigned tile=(((attr&0xf0)+(y/8)*16)&255)|(((attr&15)+(unsigned)dx/8)&15);
        unsigned adr=r[4]+tile*16+(y&7),bit=7-((unsigned)dx&7);
        unsigned ci=decode(resource_word(f,row,adr)|(uint32_t)resource_word(f,row,adr+8)<<16,bit);
        if(ci) result=(ci+128+((attr>>9)&7)*16)|(((attr>>12)&3)<<8);
    }
    return result;
}
ScTerrainTile ScTerrainRawTile(const ScTerrainFrame *f,unsigned y,unsigned column) {
    ScTerrainTile t=f->tiles[(size_t)y*f->stride+column];
    const ScTerrainRow *row=f->rows+y;
    if(row->math&SC_ROW_CITY_SPANS) {
        t.base=city_cell(f,row->city_base+column+1);
        t.roof=city_cell(f,row->city_roof+column+2);
        t.reserved=city_cell(f,row->city_owner+column);
    }
    return t;
}
static ScTerrainTile resolve_tile(const ScTerrainFrame *f,const ScTerrainRow *row,ScTerrainTile t) {
    if(!(row->math&SC_ROW_RAW_TERRAIN)) return t;
    unsigned base=t.base==UINT32_MAX?0:f->resources[t.base&1023]&65535;
    unsigned roof=t.roof==UINT32_MAX?0x2300:f->resources[t.roof&1023]>>16;
    unsigned owner=t.reserved,id=owner&1023;
    bool warning=owner!=UINT32_MAX && !(owner&0x8000) &&
        (f->resources[1024+id]&1) && id!=0x27c && id!=0x28c;
    t.expected=base|(roof<<16);
    t.attributes|=((base>>10)&7)*16|(((roof>>10)&7)*16)<<8|
        (base&0x4000?1u<<16:0)|(roof&0x4000?1u<<17:0)|(warning?SC_TILE_POWER_WARNING:0);
    unsigned world_y=row->world_y&7;
    unsigned dy=(base&0x8000)?7-world_y:world_y;
    unsigned at=(row->chr_base+(base&1023)*16+dy)&0x7fff;
    t.base=t.base==UINT32_MAX?0:resource_word(f,row,at)|(uint32_t)resource_word(f,row,at+8)<<16;
    dy=(roof&0x8000)?7-world_y:world_y;
    at=(row->chr_base+(roof&1023)*16+dy)&0x7fff;
    t.roof=(roof&1023)==0x300?0:resource_word(f,row,at)|(uint32_t)resource_word(f,row,at+8)<<16;
    at=(row->warning_base+0x376*16+world_y)&0x7fff;
    t.reserved=warning?resource_word(f,row,at)|(uint32_t)resource_word(f,row,at+8)<<16:0;
    return t;
}
ScTerrainTile ScTerrainResolveTile(const ScTerrainFrame *f,unsigned y,ScTerrainTile t) {
    return resolve_tile(f,f->rows+y,t);
}
static bool contains_mode(const ScTerrainRow *row,unsigned layer,int x,bool logic) {
    unsigned flags=(row->windows>>(layer*4))&15;
    bool a=(x>=(int)(row->bounds&255) && x<=(int)((row->bounds>>8)&255))!=((flags&1)!=0);
    bool b=(x>=(int)((row->bounds>>16)&255) && x<=(int)(row->bounds>>24))!=((flags&4)!=0);
    if(!(flags&2)) return (flags&8)?b:false;
    if(!(flags&8)) return a;
    switch(logic?(row->logic>>(layer*2))&3:0) {
    case 0:return a||b;case 1:return a&&b;case 2:return a!=b;default:return a==b;
    }
}
static bool contains(const ScTerrainRow *row,unsigned layer,int x) {return contains_mode(row,layer,x,true);}
ScNativeTile ScTerrainNativeTile(const ScTerrainFrame *f,unsigned y,unsigned layer,unsigned column) {
    const ScNativeRow *native=f->native+y;
    if(!(native->flags&SC_NATIVE_RAW_BG)) return native->tiles[layer][column];
    const uint32_t *d=native->layer[layer];
    unsigned sx=(d[0]+column*8)&1023,sy=d[1],size=d[4];
    unsigned map=d[2]+((sy>>3)&31)*32+((sx>>3)&31);
    if((sx&256) && (size&1)) map+=0x400;
    if((sy&256) && (size&2)) map+=size&1?0x800:0x400;
    unsigned word=resource_word(f,f->rows+y,map),depth=layer==2?2:4;
    unsigned dy=word&0x8000?7-(sy&7):sy&7;
    unsigned at=d[3]+(word&1023)*(depth*4)+dy;
    return (ScNativeTile){resource_word(f,f->rows+y,at)|
        (depth==4?(uint32_t)resource_word(f,f->rows+y,at+8)<<16:0),word};
}
unsigned ScTerrainNativeObject(const ScTerrainFrame *f,unsigned y,unsigned x) {
    const ScNativeRow *row=f->native+y;
    unsigned count=(row->flags>>8)&127;
    while(count) {
        unsigned i=--count;const uint32_t *s=row->layer[i/30]+5+2*(i%30);
        int dx=(int)x-(int16_t)s[0];
        if(dx<(int)((s[1]>>16)&15) || dx>=(int)((s[1]>>20)&15)) continue;
        unsigned at=s[1]&32767,planes=resource_word(f,f->rows+y,at)|
            (uint32_t)resource_word(f,f->rows+y,at+8)<<16;
        unsigned value=decode(planes,s[1]&0x8000?(unsigned)dx:7-(unsigned)dx);
        if(value) {value+=(s[0]>>16);return (value&255)|((value>>12)<<8);}
    }
    return 0;
}
static unsigned native_sample(const ScTerrainFrame *f,unsigned y,unsigned x,unsigned layer,unsigned *rank) {
    const ScNativeRow *native=f->native+y;unsigned px=x+native->phase[layer];
    ScNativeTile captured=ScTerrainNativeTile(f,y,layer,px/8);const ScNativeTile *t=&captured;
    unsigned pixel=decode(t->planes,(t->attributes&0x4000)?px&7:7-(px&7));
    bool high=(t->attributes&0x2000)!=0;
    *rank=layer==0?(high?12:8):layer==1?(high?11:7):high?(native->flags&1?15:3):1;
    return pixel?pixel+((t->attributes>>10)&7)*(layer==2?4:16):0;
}
static bool native_repair(const ScTerrainFrame *f,unsigned x,unsigned y,
                          const ScTerrainTile *tile,ScTerrainOverlay *overlay) {
    const ScTerrainRow *row=f->rows+y;
    int local=(int)x-(int)row->core_x;
    if(!(row->math&SC_ROW_NATIVE_REPAIR) || local<0 || local>=256) return false;
    bool land=!(row->math&SC_ROW_CITY_HUD) || (!(row->math&SC_ROW_HUD_TOP) && local>=56);
    bool warning=land && (tile->attributes&SC_TILE_POWER_WARNING);
    unsigned native_bg=ScTerrainNativeTile(f,y,0,(local+f->native[y].phase[0])/8).attributes;
    bool clear=land && (tile->attributes&SC_TILE_CLEAR_WARNING) && native_bg==0x1376;
    unsigned base=tile->staged&65535,roof=tile->staged>>16;
    bool bad=(row->math&SC_ROW_STAGING_CHECK) && land &&
        (!(tile->attributes&SC_TILE_VALID) || base!=(tile->expected&65535) ||
         ((row->main&1) && roof!=(tile->expected>>16) && !(roof==0x1376 && warning)));
    if(!(tile->attributes&SC_TILE_EDITED) && !warning && !clear && !bad) return false;
    overlay->background=0x80000000|SC_OVERLAY_NATIVE_BG|((clear || bad)?SC_OVERLAY_SKIP_BG1:0);
    if(!land) overlay->object|=0x80000000;
    return true;
}
static uint32_t terrain_pixel(const ScTerrainFrame *f,unsigned x,unsigned y,
    const ScTerrainRow *row,unsigned virtual_x) {
    if(!f || !f->tiles || x>=f->width || y>=f->height) return 0xff000000;
    unsigned px=virtual_x+row->phase;
    ScTerrainTile resolved=resolve_tile(f,row,ScTerrainRawTile(f,y,px/8));
    const ScTerrainTile *t=&resolved;
    unsigned b=decode(t->base,t->attributes&(1<<16)?px&7:7-(px&7));
    unsigned roof=decode(t->roof,t->attributes&(1<<17)?px&7:7-(px&7));
    unsigned index=roof?roof+((t->attributes>>8)&127):b?b+(t->attributes&127):0;
    int edge=(int)x-(int)row->core_x;if(edge<0) edge=0;if(edge>255) edge=255;
    /* Same window truth table and integer colour arithmetic as the CPU
     * compositor, applied to captured data only. Used for fallback/captures. */
    bool windows[]={contains(row,1,edge),contains(row,5,edge)};
    ScTerrainOverlay overlay=f->overlays[(size_t)y*f->width+x];
    int local=(int)x-(int)row->core_x;
    if(!(row->math&SC_ROW_CITY_ZOOM) && local>=0 && local<256 && (f->native[y].flags&SC_NATIVE_RAW_OBJ))
        overlay.object=(overlay.object&~UINT32_C(0xfff))|ScTerrainNativeObject(f,y,local);
    if((row->math&SC_ROW_GPU_OBJECTS) && !((row->math&SC_ROW_OBJECT_CORE) && local>=0 && local<256)) {
        unsigned object_x=(row->math&SC_ROW_CITY_ZOOM)?(unsigned)(row->zoom_x+(int)virtual_x):x;
        overlay.object=captured_object(f,row,object_x)|
            ((row->math&SC_ROW_CITY_HUD) && ((row->math&SC_ROW_HUD_TOP) || local<56)?UINT32_C(0x80000000):0);
    }
    native_repair(f,x,y,t,&overlay);
    unsigned warning=overlay.object&0x80000000?0:decode(t->reserved,7-(px&7));
    if(warning) warning+=64;
    unsigned samples[2]={0,0},owners[2]={5,5};
    if(row->math&SC_ROW_ADVISOR_BACKGROUND) {
        /* The relocated opaque adviser page is applied after this background.
         * Its old subscreen windows must not cut a hole in the city. */
        int local=(int)x-(int)row->core_x;
        bool core=(row->math&SC_ROW_ADVISOR_CORE) && local>=0 && local<256;
        unsigned sub=index,priority=roof?11:7;
        if(core && local>=8 && local<248) sub=native_sample(f,y,local,1,&priority);
        if(!(row->sub&2)) sub=0;
        samples[1]=sub;owners[1]=sub?1:5;
        if(core && (row->sub&1)) {
            unsigned rank,hud=native_sample(f,y,local,0,&rank);
            if(hud && (!sub || rank==12 || priority!=11)) {samples[1]=hud;owners[1]=0;}
        }
    } else for(unsigned screen=0;screen<2;++screen) {
        unsigned mask=screen?row->sub:row->main,window=screen?row->window_sub:row->window_main,rank=0;
        if((mask&2) && (!(window&2) || !windows[0])) {
            samples[screen]=index;owners[screen]=index?1:5;rank=index?(roof?11:7):0;
        } else if(!(overlay.background&0x80000000) && screen && (mask&4)) {
            samples[screen]=captured_sub_bg(f,row,edge);owners[screen]=samples[screen]?2:5;
        }
        if(overlay.background&0x80000000) {
            for(unsigned layer=0;layer<=2;layer+=2) {
                unsigned shift=layer?16:0,pixel=(overlay.background>>shift)&255,z=(overlay.background>>(shift+8))&15;
                if(overlay.background&SC_OVERLAY_NATIVE_BG) {
                    pixel=layer==0 && (overlay.background&SC_OVERLAY_SKIP_BG1)?0:native_sample(f,y,edge,layer,&z);
                }
                if(pixel && z>rank && (mask&(1<<layer)) && (!(window&(1<<layer)) || !contains(row,layer,edge))) {
                    samples[screen]=pixel;owners[screen]=layer;rank=z;
                }
            }
        }
        if(warning && (!(overlay.background&0x80000000) || rank<8) && (mask&1) && (!(window&1) || !contains(row,0,edge))) {
            samples[screen]=warning;owners[screen]=0;rank=8;
        }
        unsigned obj=overlay.object&255,priority=(overlay.object>>8)&15;
        bool higher=overlay.background&0x80000000?priority>rank:!samples[screen] || priority>=(roof?3u:2u);
        if(obj && higher && (mask&16) && (!(window&16) || !contains(row,4,edge))) {
            samples[screen]=obj;owners[screen]=obj<192?6:4;
        }
    }
    unsigned main=samples[0],sub=samples[1];
    const uint32_t *palette=f->palette+(size_t)y*256;
    unsigned c=palette[main],clip=(row->control>>6)&3,prevent=(row->control>>4)&3;
    if(clip==3 || (clip==2 && windows[1]) || (clip==1 && !windows[1])) c=0;
    bool math=((row->math&63)&(1<<owners[0])) &&
        !(prevent==3 || (prevent==2 && windows[1]) || (prevent==1 && !windows[1]));
    unsigned second=(row->control&2) && sub?palette[sub]:row->fixed;
    uint32_t result=0xff000000;
    for(unsigned channel=0;channel<3;++channel) {
        int value=(c>>(channel*5))&31;
        if(math) {
            int operand=(second>>(channel*5))&31;
            value+=row->math&128?-operand:operand;
            if((row->math&64) && (sub || !(row->control&2))) value>>=1;
            if(value<0) value=0;
            if(value>31) value=31;
        }
        result|=row->brightness[value]<<(16-channel*8);
    }
    return result;
}

uint32_t ScTerrainPixel(const ScTerrainFrame *f,unsigned x,unsigned y) {
    if(!f || x>=f->width || y>=f->height) return 0xff000000;
    const ScTerrainRow *row=f->rows+y;
    unsigned vx=(row->math&SC_ROW_CITY_ZOOM)?
        (unsigned)(((uint64_t)x*row->zoom_step+row->zoom_fraction)>>16):x;
    return terrain_pixel(f,x,y,row,vx);
}
uint32_t ScTerrainZoomPixel(const ScTerrainFrame *f,unsigned x,unsigned y,
    unsigned fraction_x,unsigned fraction_y,int origin_y) {
    if(!f || x>=f->width || y>=f->height) return 0xff000000;
    ScTerrainRow row=f->rows[y];
    if((row.math&(SC_ROW_CITY_ZOOM|SC_ROW_CITY_SPANS))!=
        (SC_ROW_CITY_ZOOM|SC_ROW_CITY_SPANS)) return ScTerrainPixel(f,x,y);
    unsigned step=row.zoom_step;
    unsigned phase=(uint32_t)((int64_t)((int)y-origin_y)*step)&65535;
    unsigned delta=(phase+(unsigned)(((uint64_t)fraction_y*step)>>16))>>16;
    int target=(int16_t)(row.world_y>>8)+(int)delta;
    int chr=(row.world_y&7)+(int)delta;
    unsigned source=y;
    /* Adjacent captured rows own immutable city strips. At tile boundaries
     * borrow the strip covering the desired CHR row, preserving its snapshot.
     * Extremely small zooms can skip entire strips; retain that native row
     * rather than ever sampling another tile's data. */
    for(unsigned next=y+1;chr>=8 && next<f->height && next<=y+8;++next) {
        const ScTerrainRow *candidate=f->rows+next;
        if((candidate->math&(SC_ROW_CITY_ZOOM|SC_ROW_CITY_SPANS))!=
            (SC_ROW_CITY_ZOOM|SC_ROW_CITY_SPANS)) break;
        int candidate_chr=(int)(candidate->world_y&7)+target-(int16_t)(candidate->world_y>>8);
        if(candidate_chr>=0 && candidate_chr<8) {
            row=*candidate;source=next;chr=candidate_chr;break;
        }
    }
    if(chr>=8) {target=(int16_t)(row.world_y>>8);chr=row.world_y&7;}
    row.world_y=(unsigned)chr|((uint32_t)(uint16_t)target<<8);
    unsigned vx=(unsigned)(((uint64_t)x*row.zoom_step+row.zoom_fraction+
        (((uint64_t)fraction_x*row.zoom_step)>>16))>>16);
    return terrain_pixel(f,x,source,&row,vx);
}

uint32_t ScNativePixel(const ScTerrainFrame *f,unsigned x,unsigned y) {
    if(!f || !f->native || x>=f->width || y>=f->height) return 0xff000000;
    const ScTerrainRow *row=f->rows+y;const ScNativeRow *native=f->native+y;
    ScTerrainTile resolved=ScTerrainResolveTile(f,y,ScTerrainRawTile(f,y,(x+row->phase)/8));
    const ScTerrainTile *tile=&resolved;
    ScTerrainOverlay repaired=f->overlays[(size_t)y*f->width+x];
    if(native_repair(f,x,y,tile,&repaired)) return ScTerrainPixel(f,x,y);
    bool logic=!(native->flags&2); /* pinned fast renderer combines windows with OR */
    int edge=(int)x-(int)row->core_x;if(edge<0 || edge>=256) return 0xff000000;
    unsigned samples[2]={0,0},owners[2]={5,5};
    unsigned pixels[3],ranks[3];
    for(unsigned layer=0;layer<3;++layer) {
        pixels[layer]=native_sample(f,y,edge,layer,ranks+layer);
    }
    ScTerrainOverlay overlay=f->overlays[(size_t)y*f->width+x];
    if(native->flags&SC_NATIVE_RAW_OBJ)
        overlay.object=(overlay.object&~UINT32_C(0xfff))|ScTerrainNativeObject(f,y,edge);
    for(unsigned screen=0;screen<2;++screen) {
        unsigned mask=screen?row->sub:row->main,window=screen?row->window_sub:row->window_main,rank=0;
        for(unsigned layer=0;layer<3;++layer) {
            if(pixels[layer] && ranks[layer]>rank && (mask&(1<<layer)) &&
               (!(window&(1<<layer)) || !contains_mode(row,layer,edge,logic))) {
                samples[screen]=pixels[layer];owners[screen]=layer;rank=ranks[layer];
            }
        }
        unsigned obj=overlay.object&255,priority=(overlay.object>>8)&15;
        if(obj && priority>rank && (mask&16) && (!(window&16) || !contains_mode(row,4,edge,logic))) {
            samples[screen]=obj;owners[screen]=obj<192?6:4;
        }
    }
    const uint32_t *palette=f->palette+(size_t)y*256;
    unsigned sub=samples[1],c=palette[samples[0]],clip=(row->control>>6)&3,prevent=(row->control>>4)&3;
    bool win=contains_mode(row,5,edge,logic);
    if(clip==3 || (clip==2 && win) || (clip==1 && !win)) c=0;
    bool math=((row->math&63)&(1<<owners[0])) && !(prevent==3 || (prevent==2 && win) || (prevent==1 && !win));
    unsigned second=(row->control&2) && sub?palette[sub]:row->fixed;
    uint32_t result=0xff000000;
    for(unsigned channel=0;channel<3;++channel) {
        int value=(c>>(channel*5))&31;
        if(math) {
            int operand=(second>>(channel*5))&31;
            value+=row->math&128?-operand:operand;
            if((row->math&64) && (sub || !(row->control&2))) value>>=1;
            if(value<0) value=0;
            if(value>31) value=31;
        }
        result|=row->brightness[value]<<(16-channel*8);
    }
    return result;
}
uint32_t ScRelocatedNativePixel(const ScTerrainFrame *f,uint32_t pixel) {
    unsigned y=(pixel>>8)&4095,x=pixel&255;
    if(!f || !f->rows || y>=f->height) return 0xff000000;
    return ScNativePixel(f,f->rows[y].core_x+x,y);
}
