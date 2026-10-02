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
static unsigned word(const uint8_t *p) { return p[0] | ((unsigned)p[1]<<8); }
static void put(uint8_t *p,unsigned v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p,uint32_t v) { put(p,v); put(p+2,v>>16); }
static uint32_t get32(const uint8_t *p) { return word(p)|((uint32_t)word(p+2)<<16); }
void ScWorldReset(ScWorld *w) { memset(w,0,sizeof *w); }
bool ScWorldBounds(int x,int y) { return x>=0 && y>=0 && x<SC_WORLD_WIDTH && y<SC_WORLD_HEIGHT; }
uint16_t ScWorldCell(const ScWorld *w,int x,int y) {
    return ScWorldContains(w,x,y)?(uint16_t)word(w->tiles+2*(y*ScWorldWidth(w)+x)):0;
}
bool ScWorldPutCell(ScWorld *w,int x,int y,uint16_t v) {
    if (!ScWorldContains(w,x,y)) return false;
    put(w->tiles+2*(y*ScWorldWidth(w)+x),v); return true;
}
static void generate(ScWorld *w,ScMapGenPrng *prng,bool huge) {
    ScMapGenState state={0};
    ScWorldReset(w); w->huge=huge;
    if(huge) sc_mapgen_generate_huge(prng,&state); else sc_mapgen_generate_large(prng,&state);
    for (unsigned i=0;i<ScWorldCells(w);++i) put(w->tiles+i*2,state.map[i]);
    w->active=true;
}
void ScWorldGenerate(ScWorld *w,ScMapGenPrng *prng) { generate(w,prng,false); }
void ScWorldGenerateHuge(ScWorld *w,ScMapGenPrng *prng) { generate(w,prng,true); }
unsigned ScWorldFieldWidth(const ScWorld *w,unsigned f) {
    return f<SC_WORLD_FIELDS?ScWorldFields[f].width*(w && w->huge && f<17?2:1):0;
}
unsigned ScWorldFieldHeight(const ScWorld *w,unsigned f) {
    return f<SC_WORLD_FIELDS?ScWorldFields[f].height*(w && w->huge && f<17?2:1):0;
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
static size_t encoded_size(bool legacy) {
    size_t n=legacy?48+SC_WORLD_TILE_BYTES:64+SC_WORLD_MAX_TILE_BYTES;
    for(unsigned i=0;i<SC_WORLD_FIELDS;++i) n+=ScWorldFieldSize(i)*(legacy?1:i<17?4:2);
    return n;
}
size_t ScWorldEncodedSize(void) { return encoded_size(false); }
bool ScWorldEncode(const ScWorld *w,uint8_t *p,size_t size) {
    if (size!=ScWorldEncodedSize()) return false;
    memset(p,0,64); memcpy(p,"SCWORLD",7); p[7]=3;
    put(p+8,ScWorldWidth(w)); put(p+10,ScWorldHeight(w));
    p[12]=w->active; p[13]=w->huge; put32(p+16,w->map_anchor);
    put32(p+20,(uint32_t)size); put32(p+24,SC_WORLD_FIELDS);
    for(unsigned i=0;i<3;++i) put32(p+28+i*4,w->bank_anchor[i]);
    put(p+40,w->scan_x); put(p+42,w->scan_y);
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<2;++j) put(p+44+4*i+2*j,w->coord[i][j]);
    put(p+56,w->center_x);put(p+58,w->center_y);p[60]=w->center_valid;
    p[61]=w->journey;p[62]=w->journey_notice;p[63]=w->journey_announcing|(w->journey_target<<1);
    memcpy(p+64,w->tiles,SC_WORLD_MAX_TILE_BYTES);
    size_t at=64+SC_WORLD_MAX_TILE_BYTES;
    for(unsigned i=0;i<SC_WORLD_FIELDS;++i) {
        unsigned n=ScWorldFieldSize(i)*(i<17?4:2);memcpy(p+at,w->fields[i],n);at+=n;
    }
    return true;
}
bool ScWorldDecode(ScWorld *w,const uint8_t *p,size_t size) {
    if(size<48 || memcmp(p,"SCWORLD",7) || (p[7]!=2 && p[7]!=3)) return false;
    bool legacy=p[7]==2,huge=!legacy && p[13]!=0;
    unsigned width=huge?480:240,height=huge?400:200,bytes=width*height*2;
    if(size!=encoded_size(legacy) || word(p+8)!=width || word(p+10)!=height ||
        p[12]>1 || (!legacy && p[13]>1) ||
        (get32(p+16)>=bytes && get32(p+16)!=UINT32_MAX) ||
        get32(p+20)!=size || get32(p+24)!=SC_WORLD_FIELDS) return false;
    for(unsigned i=0;i<3;++i) if(get32(p+28+i*4)>=bytes && get32(p+28+i*4)!=UINT32_MAX) return false;
    if(!legacy && (word(p+40)>width || word(p+42)>height)) return false;
    if(!legacy && (p[60]>1 || (p[60] && (word(p+56)>=width || word(p+58)>=height)))) return false;
    if(!legacy && (p[61]>1 || p[62]>2 || p[63]>5 || (!p[61] && (p[62] || p[63])))) return false;
    ScWorldReset(w);w->active=p[12]!=0;w->huge=huge;w->map_anchor=get32(p+16);
    for(unsigned i=0;i<3;++i) w->bank_anchor[i]=get32(p+28+i*4);
    if(!legacy) {
        w->scan_x=word(p+40);w->scan_y=word(p+42);
        for(unsigned i=0;i<3;++i) for(unsigned j=0;j<2;++j) w->coord[i][j]=(int16_t)word(p+44+4*i+2*j);
        w->center_x=word(p+56);w->center_y=word(p+58);w->center_valid=p[60]!=0;
        w->journey=p[61]!=0;w->journey_notice=p[62];w->journey_announcing=(p[63]&1)!=0;
        w->journey_target=p[63]>>1;
    }
    size_t at=legacy?48:64,n=legacy?SC_WORLD_TILE_BYTES:SC_WORLD_MAX_TILE_BYTES;
    memcpy(w->tiles,p+at,n);at+=n;
    for(unsigned i=0;i<SC_WORLD_FIELDS;++i) {
        n=ScWorldFieldSize(i)*(legacy?1:i<17?4:2);memcpy(w->fields[i],p+at,n);at+=n;
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
    memset(p,0,ScWorldCitiesSize()); memcpy(p,"SCWCITY",7); p[7]=3;
    put32(p+8,(uint32_t)ScWorldEncodedSize()); put32(p+12,(uint32_t)ScWorldCitiesSize()); put32(p+16,2);
}
bool ScWorldCitiesValid(const uint8_t *p,size_t size) {
    return size==ScWorldCitiesSize() && !memcmp(p,"SCWCITY",7) && p[7]==3 &&
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
    size_t old=encoded_size(true);
    if(size!=24+2*(16+old) || memcmp(p,"SCWCITY",7) || p[7]!=2 || get32(p+8)!=old || get32(p+12)!=size || get32(p+16)!=2) return false;
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
