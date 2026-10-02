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
bool ScPowerRefreshStep(ScPowerRefresh *s,uint8_t *ram,ScWorld *world,
    const uint8_t *rom,size_t size,uint64_t frame,int multiplier,bool bitmap_available) {
    bool large=world && world->active;
    unsigned cells=large?ScWorldCells(world):12000;
    if(s->clock_cells!=cells) {s->clock_cells=cells;s->game_speed=-1;s->ready=false;}
    game_speed(s,ram[0x193]);
    bool due=ScRefreshClockDue(&s->clock,frame,multiplier);
    if (multiplier<=1) { s->ready=false; return false; }
    uint8_t *tiles=large?world->tiles:ram+0x10200;
    bool same=s->ready && s->large==large && s->huge==(large && world->huge);
    if(same) {
        /* Exact, vectorizable comparison of tile IDs. Ignore native power
         * and metadata bits; a serial full-map hash dominated cached frames. */
        unsigned different=0;
        for(unsigned i=0;i<cells;++i) {
            different|=tiles[2*i]^s->topology_tiles[2*i];
            different|=(tiles[2*i+1]^s->topology_tiles[2*i+1])&3;
        }
        same=different==0;
    }
    bool solved=false;
    if (due && !same) {
        if (!ScConstructionPowerBitmap(ram,world,rom,size,s->bitmap,sizeof s->bitmap)) return false;
        s->ready=true; s->large=large; s->huge=large && world->huge;
        memcpy(s->topology_tiles,tiles,2*cells);same=solved=true;
    }
    if (!same) return false;
    /* Native bitmap rebuilding must not temporarily depower an unchanged
     * network. Restore its settled flags from the last verified result. */
    for (unsigned i=0;i<cells;++i) {
        unsigned power=s->bitmap[i/8]&(128>>(i&7))?128:0;
        tiles[2*i+1]=(uint8_t)((tiles[2*i+1]&127)|power);
    }
    if (bitmap_available)
        memcpy(large?world->fields[5]:ram+0x1a598,s->bitmap,cells/8);
    return solved;
}
