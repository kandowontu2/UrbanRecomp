#include "sc_world.h"
#include <string.h>
#include <stdlib.h>

/* US WRAM field layouts, established from 03:a29a/a2b9/a2d7 and their
 * consumers. $5fc0..$6aff contain temporal graph histories, not spatial
 * fields; they deliberately retain their original layout. */
const ScWorldField ScWorldFields[SC_WORLD_FIELDS] = {
    {0x6b00,60,50,120,100,1}, /* land value */
    {0x76b8,60,50,120,100,1}, /* crime */
    {0x8270,60,50,120,100,1}, /* pollution */
    {0x8e28,60,50,120,100,1}, /* population density */
    {0x99e0,60,50,120,100,1}, /* transport visits */
    {0xa598,15,100,30,200,1}, /* packed power, eight cells per byte */
    {0xab74,30,25,60,50,1},
    {0xae62,15,13,30,25,2}, /* growth */
    {0xafe8,15,13,30,25,1}, /* police coverage */
    {0xb0ab,15,13,30,25,1}, /* fire coverage */
    {0xb16e,15,13,30,25,2}, /* police diffusion */
    {0xb2f4,15,13,30,25,2}, /* fire diffusion */
    {0xb47a,15,13,30,25,2}, /* terrain quality */
    {0xb600,60,50,120,100,1}, /* pollution scratch */
    {0xc1b8,60,50,120,100,1}, /* traffic scratch */
    {0xcd70,30,25,60,50,1}, /* density scratch */
    {0xd05e,15,13,30,25,2}, /* shared diffusion scratch */
    {0xd1e4,5001,1,20001,1,1}, /* power traversal X, one-based */
    {0xe56c,5001,1,20001,1,1}  /* power traversal Y, one-based */
};
static const ScWorld *tracked_world;
static uint64_t state_epoch;
uint64_t ScWorldStateEpoch(void) {return state_epoch;}
static uint64_t tile_revision,tile_revisions[SC_WORLD_TILE_CHUNKS];
void ScWorldTilesTouch(const ScWorld *w,unsigned offset,unsigned bytes) {
    if(w!=tracked_world || !bytes || offset>=SC_WORLD_MAX_TILE_BYTES) return;
    unsigned end=bytes>SC_WORLD_MAX_TILE_BYTES-offset?SC_WORLD_MAX_TILE_BYTES:offset+bytes;
    uint64_t revision=++tile_revision;
    for(unsigned i=offset/SC_WORLD_TILE_CHUNK_BYTES;i<=(end-1)/SC_WORLD_TILE_CHUNK_BYTES;++i)
        tile_revisions[i]=revision;
}
/* Apply packed electrical state without touching tile IDs or other metadata.
 * A publication is atomic to scanout; one revision per changed chunk preserves
 * render invalidation without millions of redundant per-cell notifications. */
void ScWorldPublishPower(ScWorld *w,uint8_t *tiles,const uint8_t *bitmap,unsigned first,unsigned end) {
    if(!tiles || !bitmap || first>=end || end>SC_WORLD_MAX_CELLS) return;
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
    static const uint64_t flags[16]={
        0,UINT64_C(0x8000000000000000),UINT64_C(0x0000800000000000),UINT64_C(0x8000800000000000),
        UINT64_C(0x0000000080000000),UINT64_C(0x8000000080000000),UINT64_C(0x0000800080000000),UINT64_C(0x8000800080000000),
        UINT64_C(0x0000000000008000),UINT64_C(0x8000000000008000),UINT64_C(0x0000800000008000),UINT64_C(0x8000800000008000),
        UINT64_C(0x0000000080008000),UINT64_C(0x8000000080008000),UINT64_C(0x0000800080008000),UINT64_C(0x8000800080008000)
    };
#endif
    unsigned i=first;
    while(i<end) {
        unsigned begin=i,limit=(i/(SC_WORLD_TILE_CHUNK_BYTES/2)+1)*(SC_WORLD_TILE_CHUNK_BYTES/2);
        if(limit>end) limit=end;
        bool changed=false;
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
        while(i<limit && (i&7)) {
            unsigned value=(tiles[2*i+1]&127)|(bitmap[i/8]&(128>>(i&7))?128:0);
            if(value!=tiles[2*i+1]) {tiles[2*i+1]=(uint8_t)value;changed=true;}++i;
        }
        for(;i+8<=limit;i+=8) {
            unsigned bits=bitmap[i/8];uint64_t old_a,old_b;
            memcpy(&old_a,tiles+2*i,8);memcpy(&old_b,tiles+2*i+8,8);
            uint64_t a=(old_a&UINT64_C(0x7fff7fff7fff7fff))|flags[bits>>4];
            uint64_t b=(old_b&UINT64_C(0x7fff7fff7fff7fff))|flags[bits&15];
            if(a!=old_a || b!=old_b) {
                memcpy(tiles+2*i,&a,8);memcpy(tiles+2*i+8,&b,8);changed=true;
            }
        }
#endif
        for(;i<limit;++i) {
            unsigned value=(tiles[2*i+1]&127)|(bitmap[i/8]&(128>>(i&7))?128:0);
            if(value!=tiles[2*i+1]) {tiles[2*i+1]=(uint8_t)value;changed=true;}
        }
        if(w && changed) ScWorldTilesTouch(w,2*begin,2*(limit-begin));
    }
}
const uint64_t *ScWorldTileRevisions(const ScWorld *w) {
    if(w!=tracked_world) {
        tracked_world=w;
        ScWorldTilesTouch(w,0,SC_WORLD_MAX_TILE_BYTES);
    }
    return tile_revisions;
}
static unsigned word(const uint8_t *p) { return p[0] | ((unsigned)p[1]<<8); }
static void put(uint8_t *p,unsigned v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p,uint32_t v) { put(p,v); put(p+2,v>>16); }
static uint32_t get32(const uint8_t *p) { return word(p)|((uint32_t)word(p+2)<<16); }
void ScWorldReset(ScWorld *w) { ++state_epoch;memset(w,0,sizeof *w);ScWorldTilesTouch(w,0,SC_WORLD_MAX_TILE_BYTES); }
bool ScWorldBounds(int x,int y) { return x>=0 && y>=0 && x<SC_WORLD_WIDTH && y<SC_WORLD_HEIGHT; }
uint16_t ScWorldCell(const ScWorld *w,int x,int y) {
    return ScWorldContains(w,x,y)?(uint16_t)word(w->tiles+2*(y*ScWorldWidth(w)+x)):0;
}
bool ScWorldPutCell(ScWorld *w,int x,int y,uint16_t v) {
    if (!ScWorldContains(w,x,y)) return false;
    unsigned at=2*(y*ScWorldWidth(w)+x);
    if(word(w->tiles+at)!=v) {put(w->tiles+at,v);ScWorldTilesTouch(w,at,2);}
    return true;
}
static void generate_mode(ScWorld *w,ScMapGenPrng *prng,unsigned size,unsigned style) {
    ScMapGenState *state=calloc(1,sizeof *state);
    if(!state) return;
    ScWorldReset(w); w->huge=size>=2;w->giant=size>=3;w->colossal=size>=4;w->mega=size==5;
    if(style)sc_mapgen_generate_style(prng,state,size,style);
    else if(size==5) sc_mapgen_generate_mega(prng,state);
    else if(size==4) sc_mapgen_generate_colossal(prng,state);
    else if(size==3) sc_mapgen_generate_giant(prng,state);
    else if(size==2) sc_mapgen_generate_huge(prng,state);
    else sc_mapgen_generate_large(prng,state);
    for (unsigned i=0;i<ScWorldCells(w);++i) put(w->tiles+i*2,state->map[i]);
    free(state);w->active=true;
}
static void generate(ScWorld *w,ScMapGenPrng *prng,unsigned size) {generate_mode(w,prng,size,0);}
void ScWorldGenerate(ScWorld *w,ScMapGenPrng *prng) { generate(w,prng,1); }
void ScWorldGenerateHuge(ScWorld *w,ScMapGenPrng *prng) { generate(w,prng,2); }
void ScWorldGenerateGiant(ScWorld *w,ScMapGenPrng *prng) { generate(w,prng,3); }
void ScWorldGenerateMega(ScWorld *w,ScMapGenPrng *prng) { generate(w,prng,5); }
void ScWorldGenerateColossal(ScWorld *w,ScMapGenPrng *prng) { generate(w,prng,4); }
void ScWorldGenerateSeeded(ScWorld *w,unsigned size,ScMapGenPrng *prng) { generate(w,prng,size<1?1:size>5?5:size); }
void ScWorldApplyMapNumber(ScWorld *w,unsigned number) {
    if(number!=31337 || !w->active)return;
    for(unsigned i=0;i<ScWorldCells(w);++i) {
        unsigned tile=word(w->tiles+2*i)&0x3ff;
        if(tile && tile<20)put(w->tiles+2*i,0);
    }
    ScWorldTilesTouch(w,0,ScWorldCells(w)*2);
}
void ScWorldGenerateNumbered(ScWorld *w,unsigned size,unsigned number) {
    ScMapGenState *state=calloc(1,sizeof *state);if(!state)return;
    ScWorldReset(w);w->huge=size>=2;w->giant=size>=3;w->colossal=size>=4;w->mega=size==5;
    sc_mapgen_generate_numbered(state,size,number);
    for(unsigned i=0;i<ScWorldCells(w);++i)put(w->tiles+2*i,state->map[i]);
    free(state);w->active=true;
}
void ScWorldGenerateStyled(ScWorld *w,unsigned size,ScMapGenPrng *prng,unsigned style) {generate_mode(w,prng,size<1?1:size>5?5:size,style);}
unsigned ScWorldFieldWidth(const ScWorld *w,unsigned f) {
    return f<SC_WORLD_FIELDS?(w && w->giant && f>=17?65535:ScWorldFields[f].width*(f<17?ScWorldScale(w):1)):0;
}
unsigned ScWorldFieldHeight(const ScWorld *w,unsigned f) {
    return f<SC_WORLD_FIELDS?ScWorldFields[f].height*(f<17?ScWorldScale(w):1):0;
}
unsigned ScWorldFieldSizeWorld(const ScWorld *w,unsigned f) {
    return f<SC_WORLD_FIELDS?ScWorldFieldWidth(w,f)*ScWorldFieldHeight(w,f)*ScWorldFields[f].element_bytes*(w && w->huge && f>=17?2:1):0;
}
unsigned ScWorldFieldSize(unsigned f) {
    return f<SC_WORLD_FIELDS?ScWorldFields[f].width*ScWorldFields[f].height*ScWorldFields[f].element_bytes:0;
}
bool ScWorldFieldResolve(uint16_t base,unsigned *field,int *offset) {
    for (unsigned i=0;i<SC_WORLD_FIELDS;++i) {
        const ScWorldField *f=&ScWorldFields[i];
        int d=(int)base-f->base;
        /* Known neighbouring bases belong to the three smoothing arrays.
         * Accept only their five-point stencil; do not reinterpret unrelated
         * guest memory just because it happens to be near an array. */
        int stride=f->stock_width*f->element_bytes;
        if (d && !(i==10 || i==11 || i==13 || i==14 || i==15) ) continue;
        if (d && d!= -stride && d!=stride && d!= -f->element_bytes && d!=f->element_bytes) continue;
        if (field) *field=i;
        if (offset) *offset=d== -stride?-(int)(f->width*f->element_bytes):
                           d==stride?(int)(f->width*f->element_bytes):d;
        return true;
    }
    return false;
}
static unsigned encoded_field_size(unsigned version,unsigned f) {
    if(version>=4 && f>=17) return 65535*2;
    return ScWorldFieldSize(f)*(version==2?1:f<17?(version==3?4:version==4?16:version==5?64:256):2);
}
static size_t encoded_size(unsigned version) {
    size_t n=version==2?48+SC_WORLD_TILE_BYTES:(version==3?64:80)+(version==3?384000:version==4?1536000:version==5?6144000:SC_WORLD_MAX_TILE_BYTES);
    for(unsigned i=0;i<SC_WORLD_FIELDS;++i) n+=encoded_field_size(version,i);
    return n;
}
size_t ScWorldEncodedVersionSize(unsigned v) {return v>=2 && v<=6?encoded_size(v):0;}
size_t ScWorldEncodedSize(void) { return encoded_size(6); }
bool ScWorldAdvanceScan(ScWorld *w) {
    unsigned width=ScWorldWidth(w),height=ScWorldHeight(w);
    if(w->scan_spread) {
        /* All expanded widths are multiples of sixteen. width/4+1 and
         * 3*width/4+1 are modular inverses. This visits every cell once,
         * while each short batch reaches all four quadrants of the city.
         * The live physical coordinates also encode progress, so no queued
         * jobs or lost iterator state are needed when saving/loading. */
        unsigned column=(w->scan_x*(3*width/4+1))%width;
        unsigned row=(w->scan_y+height-(column*(height/8+1))%height)%height;
        if(++column==width) {column=0;++row;}
        if(row==height) {w->scan_x=0;w->scan_y=height;return false;}
        w->scan_x=(column*(width/4+1))%width;
        w->scan_y=(row+column*(height/8+1))%height;
        return true;
    }
    if(++w->scan_x<width)return true;
    w->scan_x=0;return ++w->scan_y<height;
}
bool ScWorldEncode(const ScWorld *w,uint8_t *p,size_t size) {
    if (size!=ScWorldEncodedSize() || (w->development_speed && !ScWorldDevelopmentSpeedValid(w->development_speed))) return false;
    memset(p,0,80); memcpy(p,"SCWORLD",7); p[7]=6;
    put(p+8,ScWorldWidth(w)); put(p+10,ScWorldHeight(w));
    p[12]=w->active; p[13]=w->mega?4:w->colossal?3:w->giant?2:w->huge?1:0; p[14]=w->test_city; p[15]=w->scan_spread | (w->development_speed<<1); put32(p+16,w->map_anchor);
    put32(p+20,(uint32_t)size); put32(p+24,SC_WORLD_FIELDS);
    for(unsigned i=0;i<3;++i) put32(p+28+i*4,w->bank_anchor[i]);
    put(p+40,w->scan_x); put(p+42,w->scan_y);
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<2;++j) put(p+44+4*i+2*j,w->coord[i][j]);
    put(p+56,w->center_x);put(p+58,w->center_y);p[60]=w->center_valid;
    p[61]=w->journey;p[62]=w->journey_notice;p[63]=w->journey_announcing|(w->journey_target<<1);
    for(unsigned i=0;i<3;++i) put32(p+64+4*i,w->field_anchor[i]);
    put32(p+76,w->field_scan);
    memcpy(p+80,w->tiles,SC_WORLD_MAX_TILE_BYTES);
    size_t at=80+SC_WORLD_MAX_TILE_BYTES;
    for(unsigned i=0;i<SC_WORLD_FIELDS;++i) {
        unsigned n=encoded_field_size(6,i);memcpy(p+at,w->fields[i],n);at+=n;
    }
    return true;
}
bool ScWorldDecode(ScWorld *w,const uint8_t *p,size_t size) {
    if(size<48 || memcmp(p,"SCWORLD",7) || (p[7]<2 || p[7]>6)) return false;
    bool legacy=p[7]==2,huge=!legacy && p[13]!=0,giant=p[7]>=4 && p[13]>=2,colossal=p[7]>=5 && p[13]>=3,mega=p[7]>=6 && p[13]==4;
    unsigned width=mega?3840:colossal?1920:giant?960:huge?480:240,height=mega?3200:colossal?1600:giant?800:huge?400:200,bytes=width*height*2;
    if(size!=encoded_size(p[7]) || word(p+8)!=width || word(p+10)!=height ||
        p[12]>1 || (p[7]>=5 && (p[14]>1 || (p[7]==5?p[15]>1:((p[15]>>1)!=0 && !ScWorldDevelopmentSpeedValid(p[15]>>1))) || (p[14] && (!p[12] || !colossal)))) || (!legacy && p[13]>(p[7]>=6?4:p[7]==5?3:p[7]==4?2:1)) ||
        (get32(p+16)>=bytes && get32(p+16)!=UINT32_MAX) ||
        get32(p+20)!=size || get32(p+24)!=SC_WORLD_FIELDS) return false;
    for(unsigned i=0;i<3;++i) if(get32(p+28+i*4)>=bytes && get32(p+28+i*4)!=UINT32_MAX) return false;
    if(!legacy && (word(p+40)>width || word(p+42)>height)) return false;
    if(!legacy && p[60]>1) return false;
    if(!legacy && (p[61]>1 || p[62]>2 || p[63]>5 || (!p[61] && (p[62] || p[63])))) return false;
    ScWorldReset(w);w->active=p[12]!=0;w->huge=huge;w->giant=giant;w->colossal=colossal;w->mega=mega;w->map_anchor=get32(p+16);
    w->test_city=p[7]>=5 && p[14]!=0;
    w->scan_spread=p[7]>=5 && (p[15]&1)!=0;
    w->development_speed=p[7]>=6?p[15]>>1:0;
    for(unsigned i=0;i<3;++i) w->bank_anchor[i]=get32(p+28+i*4);
    if(!legacy) {
        w->scan_x=word(p+40);w->scan_y=word(p+42);
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<2;++j) w->coord[i][j]=(int16_t)word(p+44+4*i+2*j);
        w->center_x=word(p+56);w->center_y=word(p+58);w->center_valid=p[60]!=0;
        /* Older dense-city scans could wrap their 16-bit owner count and
         * serialize an impossible center. The map and field data remain valid;
         * discard only that derived position until the next corrected scan. */
        if(w->center_x>=width || w->center_y>=height) {
            w->center_valid=false;w->center_x=width/2;w->center_y=height/2;
        }
        w->journey=p[61]!=0;w->journey_notice=p[62];w->journey_announcing=(p[63]&1)!=0;
        w->journey_target=p[63]>>1;
    }
    if(p[7]>=4) {for(unsigned i=0;i<3;++i) w->field_anchor[i]=get32(p+64+4*i);w->field_scan=get32(p+76);}
    size_t at=legacy?48:p[7]==3?64:80,n=legacy?SC_WORLD_TILE_BYTES:p[7]==3?384000:p[7]==4?1536000:p[7]==5?6144000:SC_WORLD_MAX_TILE_BYTES;
    memcpy(w->tiles,p+at,n);at+=n;
    for(unsigned i=0;i<SC_WORLD_FIELDS;++i) {
        n=encoded_field_size(p[7],i);memcpy(w->fields[i],p+at,n);at+=n;
    }
    return true;
}
void ScWorldMirror(const ScWorld *w,uint8_t *r) {
    if (!w->active) return;
    for (unsigned y=0;y<100;++y)
        memcpy(r+0x10200+y*240,w->tiles+y*ScWorldWidth(w)*2,240);
}
size_t ScWorldCitiesSize(void) { return 24+2*(16+ScWorldEncodedSize()); }
void ScWorldCitiesInit(uint8_t *p) {
    memset(p,0,ScWorldCitiesSize()); memcpy(p,"SCWCITY",7); p[7]=6;
    put32(p+8,(uint32_t)ScWorldEncodedSize()); put32(p+12,(uint32_t)ScWorldCitiesSize()); put32(p+16,2);
}
bool ScWorldCitiesValid(const uint8_t *p,size_t size) {
    return size==ScWorldCitiesSize() && !memcmp(p,"SCWCITY",7) && p[7]==6 &&
        get32(p+8)==ScWorldEncodedSize() && get32(p+12)==size && get32(p+16)==2;
}
static uint32_t city_hash(const uint8_t *r,unsigned slot) {
    unsigned begin=slot?0x4000:0x10,end=slot?0x7ff0:0x4000;
    uint32_t h=2166136261u;
    for (unsigned i=begin;i<end;++i) h=(h^r[i])*16777619u;
    return h;
}
static uint64_t payload_hash_size(const uint8_t *p,size_t size) {
    uint64_t h=UINT64_C(14695981039346656037);
    for (size_t i=0;i<size;++i) h=(h^p[i])*UINT64_C(1099511628211);
    return h;
}
bool ScWorldCitySave(uint8_t *p,const uint8_t *r,unsigned slot,const ScWorld *w) {
    if (slot>1 || !ScWorldCitiesValid(p,ScWorldCitiesSize())) return false;
    p+=24+slot*(16+ScWorldEncodedSize());
    if (!ScWorldEncode(w,p+16,ScWorldEncodedSize())) return false;
    put32(p,city_hash(r,slot)); p[4]=1;
    uint64_t hash=payload_hash_size(p+16,ScWorldEncodedSize());
    for (unsigned i=0;i<8;++i) p[8+i]=(uint8_t)(hash>>(i*8));
    return true;
}
bool ScWorldCityLoad(ScWorld *w,const uint8_t *p,size_t size,const uint8_t *r,unsigned slot) {
    if (slot>1 || !ScWorldCitiesValid(p,size)) return false;
    p+=24+slot*(16+ScWorldEncodedSize());
    uint64_t hash=0;
    for (unsigned i=0;i<8;++i) hash|=(uint64_t)p[8+i]<<(i*8);
    return p[4]==1 && get32(p)==city_hash(r,slot) && hash==payload_hash_size(p+16,ScWorldEncodedSize()) &&
        ScWorldDecode(w,p+16,ScWorldEncodedSize());
}

bool ScWorldCitiesUpgrade(uint8_t *out,const uint8_t *p,size_t size) {
    if(ScWorldCitiesValid(p,size)) {memcpy(out,p,size);return true;}
    if(size<24) return false;
    size_t old=encoded_size(p[7]);
    if(size!=24+2*(16+old) || memcmp(p,"SCWCITY",7) || (p[7]<2 || p[7]>5) || get32(p+8)!=old || get32(p+12)!=size || get32(p+16)!=2) return false;
    ScWorld *w=malloc(sizeof *w);if(!w) return false;
    ScWorldCitiesInit(out);
    for(unsigned slot=0;slot<2;++slot) {
        const uint8_t *src=p+24+slot*(16+old);uint8_t *dst=out+24+slot*(16+ScWorldEncodedSize());
        if(!src[4]) continue;
        uint64_t hash=0;for(unsigned i=0;i<8;++i) hash|=(uint64_t)src[8+i]<<(8*i);
        if(src[4]!=1 || hash!=payload_hash_size(src+16,old) || !ScWorldDecode(w,src+16,old)) {free(w);return false;}
        memcpy(dst,src,8);ScWorldEncode(w,dst+16,ScWorldEncodedSize());
        hash=payload_hash_size(dst+16,ScWorldEncodedSize());for(unsigned i=0;i<8;++i) dst[8+i]=(uint8_t)(hash>>(8*i));
    }
    free(w);return true;
}
