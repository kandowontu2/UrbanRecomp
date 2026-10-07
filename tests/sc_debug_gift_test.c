#include "sc_debug_gift.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(int argc,char **argv) {
    uint8_t ram[0x20000]={0};ScDebugGift state={0};
    uint8_t padded[1024]={0};
    for(int y=3;y<27;++y)for(int x=4;x<28;++x)padded[y*32+x]=1;
    ScGiftBounds bounds=ScDebugGiftIconBounds(padded);
    assert(bounds.x==4 && bounds.y==3 && bounds.w==24 && bounds.h==24);
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
    for(int width=256;width<=448;width+=192)for(int height=224;height<=800;height+=288)
        for(int selected=0;selected<15;++selected) {
        ScGiftLayout l=ScDebugGiftLayout(width,height);
        assert(l.x>=0 && l.y>=0 && l.x+l.w<=width && l.y+l.h<=height);
        assert(l.cols==4 && l.rows==4 && l.cols*l.rows>=SC_DEBUG_GIFT_COUNT);
        int bx=l.x+8+(selected%l.cols)*40,by=l.y+16+(selected/l.cols)*40;
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)assert(ScDebugGiftHit(l,bx+x,by+y)==selected);
        assert(ScDebugGiftHit(l,bx-1,by+8)<0 && ScDebugGiftHit(l,bx+32,by+8)<0);
        /* The unused bottom-right cell cannot grant a sixteenth gift. */
        assert(ScDebugGiftHit(l,l.x+8+3*40+8,l.y+16+3*40+8)<0);
    }
    assert(ScDebugGiftMove(0,-1,0)==0 && ScDebugGiftMove(3,1,0)==3);
    assert(ScDebugGiftMove(0,0,1)==4 && ScDebugGiftMove(14,0,1)==14);
    assert(ScDebugGiftMove(11,0,1)==14 && ScDebugGiftMove(14,0,-1)==10);
    assert(ScDebugGiftMove(14,1,0)==14 && ScDebugGiftMove(4,-1,0)==4);
    if(argc==2) {
        uint8_t rom[0x80000],icons[15][1024];FILE *f=fopen(argv[1],"rb");assert(f);
        assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
        assert(ScDebugGiftIcons(icons,rom,sizeof rom));
        for(int i=0;i<15;++i) {
            unsigned ink=0;ScGiftBounds b=ScDebugGiftIconBounds(icons[i]);
            assert(b.w>0 && b.h>0 && b.x+b.w<=32 && b.y+b.h<=32);
            for(int j=0;j<1024;++j) {
                assert(icons[i][j]<16);ink+=icons[i][j]!=0;
                if(icons[i][j])assert(j%32>=b.x && j%32<b.x+b.w && j/32>=b.y && j/32<b.y+b.h);
            }
            assert(ink>50);
        }
        bounds=ScDebugGiftIconBounds(icons[0]);
        assert(bounds.x==0 && bounds.y==0 && bounds.w==24 && bounds.h==24);
        for(int i=0;i<15;++i)for(int j=0;j<i;++j)assert(memcmp(icons[i],icons[j],1024));
    }
    puts("PASS all 15 debug gifts: disabled button, full hitbox, pending gift preservation and cancellation");
}
