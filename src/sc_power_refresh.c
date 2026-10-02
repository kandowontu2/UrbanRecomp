#include "sc_power_refresh.h"
#include "sc_construction.h"
#include <string.h>

static unsigned normal_initial(int speed) {
    return speed==0?800:speed==1?400:200;
}
void ScPowerRefreshReset(ScPowerRefresh *s) {
    memset(s,0,sizeof *s); s->game_speed=-1;
    s->clock_cells=12000;
    ScRefreshClockReset(&s->clock,200);
}
static void game_speed(ScPowerRefresh *s,int speed) {
    if (s->game_speed==speed) return;
    s->game_speed=speed;
    ScRefreshClockReset(&s->clock,normal_initial(speed));
}
void ScPowerRefreshObserve(ScPowerRefresh *s,uint64_t frame,int speed) {
    game_speed(s,speed); ScRefreshClockObserve(&s->clock,frame);
}
static void publish(ScPowerRefresh *s,uint8_t *tiles,unsigned cells) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for(unsigned i=0;i<cells;++i)
        tiles[2*i+1]=(uint8_t)((tiles[2*i+1]&127)|(s->bitmap[i/8]&(128>>(i&7))?128:0));
#else
    static const uint64_t flags[16]={
        0,UINT64_C(0x8000000000000000),UINT64_C(0x0000800000000000),UINT64_C(0x8000800000000000),
        UINT64_C(0x0000000080000000),UINT64_C(0x8000000080000000),UINT64_C(0x0000800080000000),UINT64_C(0x8000800080000000),
        UINT64_C(0x0000000000008000),UINT64_C(0x8000000000008000),UINT64_C(0x0000800000008000),UINT64_C(0x8000800000008000),
        UINT64_C(0x0000000080008000),UINT64_C(0x8000000080008000),UINT64_C(0x0000800080008000),UINT64_C(0x8000800080008000)
    };
    /* Four little-endian map words at a time. memcpy permits unaligned data
     * and avoids aliasing UB; mask only bit 15, leaving all metadata intact. */
    for(unsigned i=0;i<cells;i+=8) {
        unsigned bits=s->bitmap[i/8];uint64_t a,b;
        memcpy(&a,tiles+2*i,8);memcpy(&b,tiles+2*i+8,8);
        a=(a&UINT64_C(0x7fff7fff7fff7fff))|flags[bits>>4];
        b=(b&UINT64_C(0x7fff7fff7fff7fff))|flags[bits&15];
        memcpy(tiles+2*i,&a,8);memcpy(tiles+2*i+8,&b,8);
    }
#endif
}
bool ScPowerRefreshRestore(ScPowerRefresh *s,uint8_t *ram,ScWorld *world,
    const uint8_t *rom,size_t size,uint64_t frame) {
    ScPowerRefreshReset(s);
    bool large=world && world->active;
    unsigned cells=large?ScWorldCells(world):12000;
    if(!ScConstructionPowerBitmap(ram,world,rom,size,s->bitmap,sizeof s->bitmap)) return false;
    uint8_t *tiles=large?world->tiles:ram+0x10200;
    s->ready=true;s->large=large;s->huge=large && world->huge;s->clock_cells=cells;
    memcpy(s->topology_tiles,tiles,2*cells);
    game_speed(s,ram[0x193]);ScRefreshClockObserve(&s->clock,frame);
    publish(s,tiles,cells);
    memcpy(large?world->fields[5]:ram+0x1a598,s->bitmap,cells/8);
    return true;
}
bool ScPowerRefreshStep(ScPowerRefresh *s,uint8_t *ram,ScWorld *world,
    const uint8_t *rom,size_t size,uint64_t frame,int multiplier,bool bitmap_available) {
    bool large=world && world->active;
    unsigned cells=large?ScWorldCells(world):12000;
    if(s->clock_cells!=cells) {s->clock_cells=cells;s->game_speed=-1;s->ready=false;}
    game_speed(s,ram[0x193]);
    bool due=ScRefreshClockDue(&s->clock,frame,multiplier);
    uint8_t *tiles=large?world->tiles:ram+0x10200;
    bool same=s->ready && s->large==large && s->huge==(large && world->huge);
    if(same) {
        /* Exact, vectorizable comparison of tile IDs. Ignore native power
         * and metadata bits; a serial full-map hash dominated cached frames. */
        uint64_t different=0;
        for(unsigned i=0;i<2*cells;i+=8) {
            uint64_t current,previous;
            memcpy(&current,tiles+i,8);memcpy(&previous,s->topology_tiles+i,8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            different|=(current^previous)&UINT64_C(0xff03ff03ff03ff03);
#else
            different|=(current^previous)&UINT64_C(0x03ff03ff03ff03ff);
#endif
        }
        same=different==0;
    }
    bool solved=false;
    if (multiplier>1 && due && !same) {
        if (!ScConstructionPowerBitmap(ram,world,rom,size,s->bitmap,sizeof s->bitmap)) return false;
        s->ready=true; s->large=large; s->huge=large && world->huge;
        memcpy(s->topology_tiles,tiles,2*cells);same=solved=true;
    }
    if (!same) {if(multiplier<=1) s->ready=false;return false;}
    /* Native bitmap rebuilding must not temporarily depower an unchanged
     * network. Restore its settled flags from the last verified result. */
    publish(s,tiles,cells);
    if (bitmap_available)
        memcpy(large?world->fields[5]:ram+0x1a598,s->bitmap,cells/8);
    return solved;
}
