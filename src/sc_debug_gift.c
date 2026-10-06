#include "sc_debug_gift.h"
#include "sc_decomp.h"
#include <stdlib.h>
#include <string.h>
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
static uint8_t gift_rom_read(void *ctx,uint32_t address) {
    return ((const uint8_t *)ctx)[((address>>16)&15)*0x8000+(address&0x7fff)];
}
bool ScDebugGiftIcons(uint8_t icons[SC_DEBUG_GIFT_COUNT][32*32],const uint8_t *rom,size_t size) {
    if(!icons || !rom || size!=0x80000)return false;
    uint8_t *scratch=calloc(1,0x20000);if(!scratch)return false;
    ScDecompResult result;
    sc_decomp_run(scratch,gift_rom_read,(void *)rom,0x0a,0xc4cf,0x1000,&result);
    if(result.bad || result.bytes_out!=144*32) {free(scratch);return false;}
    memset(icons,0,SC_DEBUG_GIFT_COUNT*32*32);
    for(unsigned gift=1;gift<=SC_DEBUG_GIFT_COUNT;++gift) {
        unsigned ptr=rom[0xcd8a+gift*2]|(unsigned)rom[0xcd8b+gift*2]<<8;
        if(ptr<0x8000 || ptr+16>0x10000) {free(scratch);return false;}
        for(unsigned tile=0;tile<16;++tile) {
            unsigned index=rom[ptr+tile];if(index==255)continue;
            if(index>=144) {free(scratch);return false;}
            const uint8_t *chr=scratch+0x9000+index*32;
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
                unsigned ci=0;
                for(unsigned plane=0;plane<4;++plane)
                    ci|=((chr[(plane/2)*16+y*2+(plane&1)]>>(7-x))&1)<<plane;
                icons[gift-1][((tile/4)*8+y)*32+(tile%4)*8+x]=(uint8_t)ci;
            }
        }
    }
    free(scratch);return true;
}
ScGiftLayout ScDebugGiftLayout(int width,int height,int selected) {
    /* Keep the original two columns, 32px images and 8px gaps. Grow upward
     * from the original popup's bottom, scrolling when eight rows won't fit. */
    int rows=(height-56)/40;if(rows<2)rows=2;if(rows>8)rows=8;
    int h=40*rows+40,y=height-h-8;if(y<8)y=8;
    int x=56;if(x+88>width-8)x=width-96;
    int first=selected/2-rows+1;if(first<0)first=0;if(first>8-rows)first=8-rows;
    return (ScGiftLayout){x,y,88,h,rows,first};
}
int ScDebugGiftHit(ScGiftLayout l,int x,int y) {
    x-=l.x+8;y-=l.y+16;
    if(x<0 || y<0 || x>=72 || y>=l.rows*40 || x%40>=32 || y%40>=32)return -1;
    int gift=(l.first+y/40)*2+x/40;
    return gift<SC_DEBUG_GIFT_COUNT?gift:-1;
}
int ScDebugGiftMove(int selected,int dx,int dy) {
    if(dx) {int next=selected+dx;if(next>=0 && next<SC_DEBUG_GIFT_COUNT && next/2==selected/2)selected=next;}
    if(dy) {int next=selected+dy*2;if(next>=0 && next<SC_DEBUG_GIFT_COUNT)selected=next;}
    return selected;
}
