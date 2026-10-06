#include "sc_debug_gift.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(int argc,char **argv) {
    uint8_t ram[0x20000]={0};ScDebugGift state={0};
    /* Full image hitbox, independent of the native gift-enabled bit. */
    assert(ScDebugGiftButton(32,158) && ScDebugGiftButton(47,173));
    assert(!ScDebugGiftButton(31,166) && !ScDebugGiftButton(48,166));
    assert(!ScDebugGiftButton(40,157) && !ScDebugGiftButton(40,174));
    for(unsigned gift=1;gift<=SC_DEBUG_GIFT_COUNT;++gift) {
        memset(ram,0,sizeof ram);ram[0x28b+15]=0x80;
        assert(ScDebugGiftGrant(&state,ram,gift));
        assert(ram[0x3f5]==gift && ram[0x3f3]==0 && !state.replaced);
        assert(ScDebugGiftNames[gift-1][0]);
        for(unsigned i=0;i<4;++i)ram[0x3f5+i]=(uint8_t)(i+1);
        assert(ScDebugGiftGrant(&state,ram,gift));assert(state.replaced);
        assert(ram[0x3f5]==gift && ram[0x3f6]==2 && ram[0x3f7]==3 && ram[0x3f8]==4);
        ram[0x20d]=15;ScDebugGiftTick(&state,ram);assert(state.replaced);
        ram[0x3f5]=0;ScDebugGiftTick(&state,ram);assert(!state.replaced);
        assert(ram[0x3f5]==1); /* placed debug gift restores all earned gifts */
        assert(ScDebugGiftGrant(&state,ram,gift));ram[0x20d]=5;
        ScDebugGiftTick(&state,ram);assert(!state.replaced && ram[0x3f5]==1);
    }
    assert(!ScDebugGiftGrant(&state,ram,0) && !ScDebugGiftGrant(&state,ram,16));
    for(int height=224;height<=800;height+=288)for(int selected=0;selected<15;++selected) {
        ScGiftLayout l=ScDebugGiftLayout(448,height,selected);
        assert(l.x>=0 && l.y>=0 && l.x+l.w<=448 && l.y+l.h<=height);
        assert(selected/2>=l.first && selected/2<l.first+l.rows);
        int bx=l.x+8+(selected%2)*40,by=l.y+16+(selected/2-l.first)*40;
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)assert(ScDebugGiftHit(l,bx+x,by+y)==selected);
        assert(ScDebugGiftHit(l,bx-1,by+8)<0 && ScDebugGiftHit(l,bx+32,by+8)<0);
    }
    assert(ScDebugGiftMove(0,-1,0)==0 && ScDebugGiftMove(1,1,0)==1);
    assert(ScDebugGiftMove(0,0,1)==2 && ScDebugGiftMove(14,0,1)==14);
    if(argc==2) {
        uint8_t rom[0x80000],icons[15][1024];FILE *f=fopen(argv[1],"rb");assert(f);
        assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
        assert(ScDebugGiftIcons(icons,rom,sizeof rom));
        for(int i=0;i<15;++i) {unsigned ink=0;for(int j=0;j<1024;++j) {assert(icons[i][j]<16);ink+=icons[i][j]!=0;}assert(ink>50);}
        for(int i=0;i<15;++i)for(int j=0;j<i;++j)assert(memcmp(icons[i],icons[j],1024));
    }
    puts("PASS all 15 debug gifts: disabled button, full hitbox, pending gift preservation and cancellation");
}
