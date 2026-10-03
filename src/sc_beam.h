#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Advance only an interval with no scanline, HDMA, IRQ or wrap event. The
 * caller processes each boundary with its existing hardware event handler. */
static inline unsigned ScBeamIdleClocks(unsigned h,unsigned v,unsigned remaining,
    bool hirq,bool virq,unsigned ht,unsigned vt) {
    if(!h || h==1024 || h>=1362) return 0;
    unsigned end=h<1024?1024:1362;
    if(hirq && (!virq || v==vt+1)) {
        unsigned irq=4*ht;
        if(irq==h) return 0;
        if(irq>h && irq<end) end=irq;
    }
    unsigned clocks=end-h;
    if(clocks>remaining) clocks=remaining;
    return clocks&~1u;
}
