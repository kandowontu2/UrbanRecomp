#include "sc_beam.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct {
    unsigned h,v,joy,frames,lines,hdma,irqs;
    bool hirq,virq;unsigned ht,vt;
} Beam;
static void tick(Beam *b) {
    b->joy=b->joy<=2?0:b->joy-2;
    if((b->hirq && (!b->virq || b->v==b->vt+1) && b->h==4*b->ht) ||
       (!b->hirq && b->virq && b->v==b->vt+1 && b->h==1024)) ++b->irqs;
    if(!b->h) {++b->lines;if(b->v==225) b->joy=4224;}
    if(b->h==1024 && b->v<225) ++b->hdma;
    b->h+=2;if(b->h==1364) {b->h=0;if(++b->v==262) {b->v=0;++b->frames;}}
}
int main(void) {
    unsigned cases=0;
    for(unsigned flags=0;flags<4;++flags) for(unsigned ht=0;ht<=342;ht+=19)
    for(unsigned start=0;start<12;++start) {
        Beam initial={.h=(start*113)%682*2,.v=(start*31)%262,.joy=start&1?4224:3,
            .hirq=flags&1,.virq=flags&2,.ht=ht,.vt=(start*31)%262};
        Beam expected=initial,actual=initial;
        for(unsigned chunk=0;chunk<128;++chunk) {
            unsigned clocks=(chunk*113+17)%4001;
            for(unsigned c=0;c<clocks;c+=2) tick(&expected);
            unsigned remaining=(clocks+1)&~1u;
            while(remaining) {
                unsigned idle=ScBeamIdleClocks(actual.h,actual.v,remaining,
                    actual.hirq,actual.virq,actual.ht,actual.vt);
                if(idle) {actual.joy=actual.joy<=idle?0:actual.joy-idle;actual.h+=idle;remaining-=idle;}
                else {tick(&actual);remaining-=2;}
            }
            assert(!memcmp(&expected,&actual,sizeof actual));
        }
        ++cases;
    }
    printf("PASS: %u event-driven beam sequences match the two-clock reference\n",cases);
}
