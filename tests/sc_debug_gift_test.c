#include "sc_debug_gift.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
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
    puts("PASS all 15 debug gifts: disabled button, full hitbox, pending gift preservation and cancellation");
}
