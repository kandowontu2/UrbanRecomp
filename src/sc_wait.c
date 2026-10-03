#include "sc_wait.h"
#include "snes/interp816.h"
#include <stdlib.h>

unsigned ScWaitStep(Interp816 *c,uint8_t *r,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_WAIT_REFERENCE");reference=e && *e=='1';}
    if(reference || !c || !r || c->k || !c->mf || c->dp>0x1f38 ||
       c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i)) return 0;
    unsigned cycles=0,dp=(c->dp&255)!=0,counter=c->dp+0xc7,ready=c->dp+0xb9;
    while(c->pc==0x9311 || c->pc==0x9313 || c->pc==0x9315) {
        /* No device can change the ready byte before the caller's deadline.
         * Fuse complete INC/LDA/BEQ iterations, including counter byte wrap.
         * Keep residual instructions resumable at the original boundaries. */
        if(c->pc==0x9311 && !r[ready]) {
            unsigned step=11+2*dp,iterations=(budget-cycles)/step;
            if(iterations) {
                r[counter]=(uint8_t)(r[counter]+iterations);c->a&=0xff00;
                c->n=false;c->z=true;c->cyclesUsed=3;cycles+=iterations*step;
            }
        }
        unsigned cost=c->pc==0x9311?5+dp:c->pc==0x9313?3+dp:c->z?3:2;
        if(cost>budget-cycles) break;
        switch(c->pc) {
        case 0x9311:
            ++r[counter];c->n=(r[counter]&128)!=0;c->z=r[counter]==0;c->pc=0x9313;break;
        case 0x9313:
            c->a=(c->a&0xff00)|r[ready];c->n=(r[ready]&128)!=0;c->z=r[ready]==0;c->pc=0x9315;break;
        case 0x9315:c->pc=c->z?0x9311:0x9317;break;
        }
        cycles+=cost;c->cyclesUsed=(uint8_t)cost;
    }
    return cycles;
}
