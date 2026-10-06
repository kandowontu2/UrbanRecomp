#include "sc_scenario_event.h"
#include "snes/interp816.h"
static uint16_t word(const uint8_t *r,unsigned p) {return r[p]|(uint16_t)r[p+1]<<8;}
bool ScScenarioMenuLive(const uint8_t *r) {
    return (word(r,0x14)&0x7fff)==0 && word(r,0x1df)==2 && word(r,0x379) && word(r,0xab5);
}
int ScScenarioMenuButton(int x,int y) {
    if(y<SC_DISASTER_BUTTON_Y || y>=SC_DISASTER_BUTTON_Y+SC_DISASTER_BUTTON_H)return -1;
    if(x>=44 && x<44+SC_DISASTER_BUTTON_W)return 0;
    if(x>=82 && x<82+SC_DISASTER_BUTTON_W)return 1;
    return -1;
}
void ScScenarioMenuStep(Interp816 *cpu,const uint8_t *ram) {
    if(cpu->k!=1 || cpu->pc!=0xaecc || !ScScenarioMenuLive(ram) || !(word(ram,0xc9)&0x8000))return;
    int choice=ScScenarioMenuButton(word(ram,0x1eb),word(ram,0x1ed));
    if(choice>=0) {cpu->a=(uint16_t)(14+choice);cpu->mf=false;cpu->pc=0xaee4;}
}
