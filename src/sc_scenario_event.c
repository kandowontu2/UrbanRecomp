#include "sc_scenario_event.h"
#include "snes/interp816.h"
static uint16_t word(const uint8_t *r,unsigned p) {return r[p]|(uint16_t)r[p+1]<<8;}
static void put(uint8_t *r,unsigned p,uint16_t v) {r[p]=(uint8_t)v;r[p+1]=(uint8_t)(v>>8);}
bool ScScenarioEventArm(ScScenarioEvent *e,uint8_t *ram,const ScWorld *w,unsigned scenario,uint16_t countdown) {
    if(!e || !ram || e->armed || !countdown || scenario>6)return false;
    e->mode=word(ram,0x3e);e->scenario=word(ram,0x40);e->countdown=word(ram,0xc0d);
    e->event=scenario;e->armed_countdown=countdown;e->armed=true;
    e->nuclear_x=e->nuclear_y=-1;
    if(scenario==4) {
        unsigned width=w && w->active?ScWorldWidth(w):120,height=w && w->active?ScWorldHeight(w):100;
        const uint8_t *tiles=w && w->active?w->tiles:ram+0x10200;
        /* Same first nuclear center as the native scan, with full coordinates.
         * Avoid millions of CPU edges just to find the manually chosen event. */
        for(unsigned i=0;i<width*height;++i)if((word(tiles,2*i)&1023)==0x27c) {
            e->nuclear_x=(int)(i%width);e->nuclear_y=(int)(i/width);break;
        }
    }
    put(ram,0x3e,3);put(ram,0x40,(uint16_t)scenario);put(ram,0xc0d,countdown);
    return true;
}
void ScScenarioEventStep(ScScenarioEvent *e,Interp816 *cpu,const uint8_t *ram,ScWorld *w) {
    if(!e || !e->armed || !cpu || cpu->k!=3 || !ram)return;
    /* These instruction-boundary hooks apply equally to compiled C and the
     * interpreter. No ROM mutation, fake population or persisted cheat edit.
     * Explicit manual events still run when random disasters are disabled. */
    if(cpu->pc==0xb85e)cpu->z=true;
    if(cpu->pc==0xb9bf && e->event==6)cpu->c=true;
    /* Explicit attacks must announce themselves even if routine city advice
     * was already queued. Leave the native announcement/close path intact. */
    if(cpu->pc==0xbe09 && cpu->a==(e->event==6?0x30:0x24))cpu->z=true;
    /* The UFO passes packed byte coordinates, unrelated to the full-map
     * simulation sweep. Anchor its damage locally, then restore that sweep. */
    if(e->event==6 && w && w->active) {
        if(cpu->pc==0xa9c2 && !e->damage_anchor) {
            e->damage_anchor=true;e->damage_x=w->coord[2][0];e->damage_y=w->coord[2][1];
            w->coord[2][0]=ram[0xaf9];w->coord[2][1]=ram[0xaf7];
        } else if(cpu->pc==0xa9c5 && e->damage_anchor) {
            w->coord[2][0]=e->damage_x;w->coord[2][1]=e->damage_y;e->damage_anchor=false;
        }
    }
    if(cpu->pc==0xbac3 && e->event==4) {
        if(e->nuclear_x<0)cpu->pc=0xbaf4; /* native no-plant RTS */
        else {
            cpu->a=(uint16_t)((e->nuclear_x&255)|((e->nuclear_y&255)<<8));
            if(w && w->active) {w->coord[2][0]=(int16_t)e->nuclear_x;w->coord[2][1]=(int16_t)e->nuclear_y;}
            cpu->pc=0xbd61; /* original removal/radiation/message handler */
        }
    }
    /* After the scenario handler, return through the original PLD/RTS instead
     * of also considering random/queued disasters on this manual event tick. */
    if(cpu->pc==0xb86e)cpu->pc=0xb914;
}
bool ScScenarioEventTick(ScScenarioEvent *e,uint8_t *ram) {
    if(!e || !e->armed || word(ram,0xc0d)==e->armed_countdown)return false;
    /* The native handler decrements only after its whole animation completes. */
    put(ram,0x3e,e->mode);put(ram,0x40,e->scenario);put(ram,0xc0d,e->countdown);
    e->armed=false;return true;
}
