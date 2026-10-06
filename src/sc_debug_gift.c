#include "sc_debug_gift.h"
const char *const ScDebugGiftNames[SC_DEBUG_GIFT_COUNT]={
    "YOUR HOUSE","BANK","AMUSEMENT PARK","ZOO","CASINO","LANDFILL",
    "POLICE HQ","FIRE HQ","FOUNTAIN","MARIO STATUE","EXPO","WINDMILL",
    "LIBRARY","LARGE PARK","TRAIN STATION"
};
bool ScDebugGiftButton(int x,int y) {return x>=32 && x<48 && y>=158 && y<174;}
void ScDebugGiftRestore(ScDebugGift *s,uint8_t *r) {
    if(s->replaced) {
        /* A full earned-gift queue temporarily lends one slot to the debug
         * choice. Restore it after placement/cancel, preserving all four. */
        uint8_t *slot=r+0x3f5+s->slot;
        if(!*slot || *slot==s->gift)*slot=s->original;
        s->replaced=false;
    }
}
bool ScDebugGiftGrant(ScDebugGift *s,uint8_t *r,unsigned gift) {
    if(!s || !r || !gift || gift>SC_DEBUG_GIFT_COUNT)return false;
    ScDebugGiftRestore(s,r);
    unsigned slot=0;while(slot<4 && r[0x3f5+slot])++slot;
    if(slot==4) {
        slot=0;s->replaced=true;s->original=r[0x3f5];
        s->slot=0;s->gift=(uint8_t)gift;
    }
    r[0x3f5+slot]=(uint8_t)gift;r[0x3f3]=(uint8_t)slot;r[0x3f4]=0;
    return true;
}
void ScDebugGiftTick(ScDebugGift *s,uint8_t *r) {
    if(s->replaced && ((r[0x20d]|(unsigned)r[0x20e]<<8)!=15 || !r[0x3f5+s->slot]))
        ScDebugGiftRestore(s,r);
}
