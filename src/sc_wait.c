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

bool ScWaitOwns(unsigned pc) {
    return pc==0x930d || pc==0x930f || pc==0x9311 || pc==0x9313 || pc==0x9315 || pc==0x9317;
}
static unsigned driver(Interp816 *c,uint8_t *r,unsigned budget,bool single) {
    static int reference=-1;
    if(reference<0) {
        const char *e=getenv("SC_WAIT_DRIVER_REFERENCE"),*old=getenv("SC_WAIT_REFERENCE");
        reference=(e && *e=='1') || (old && *old=='1');
    }
    if(reference || !c || !r || c->k || c->dp>0x1f38 || c->waiting || c->stopped ||
       c->nmiWanted || (c->irqWanted && !c->i) || (c->pc!=0x930d && !c->mf)) return 0;
    unsigned cycles=0;
    while(ScWaitOwns(c->pc)) {
        unsigned entry=c->pc,cost,dp=(c->dp&255)!=0;
        if(!single && (entry==0x9311 || entry==0x9313 || entry==0x9315)) {
            unsigned fused=ScWaitStep(c,r,budget-cycles);
            if(fused) {cycles+=fused;continue;}
        }
        switch(entry) {
        case 0x930d:cost=3;break;
        case 0x930f:cost=3+dp;break;
        case 0x9311:cost=5+dp;break;
        case 0x9313:cost=3+dp;break;
        case 0x9315:cost=c->z?3:2;break;
        case 0x9317:cost=6;break;
        default:return cycles;
        }
        if(cost>budget-cycles) return cycles;
        if(entry==0x9317 && (c->e?(c->sp<0x100 || c->sp>0x1ff):c->sp>0x1ffd)) return cycles;
        switch(entry) {
        case 0x930d:interp816_setFlags(c,interp816_getFlags(c)|0x20);c->pc=0x930f;break;
        case 0x930f:r[c->dp+0xb9]=0;c->pc=0x9311;break;
        case 0x9311:
            ++r[c->dp+0xc7];c->n=(r[c->dp+0xc7]&128)!=0;c->z=r[c->dp+0xc7]==0;c->pc=0x9313;break;
        case 0x9313:
            c->a=(c->a&0xff00)|r[c->dp+0xb9];c->n=(c->a&128)!=0;c->z=(c->a&255)==0;c->pc=0x9315;break;
        case 0x9315:c->pc=c->z?0x9311:0x9317;break;
        case 0x9317: {
            /* The emulation stack wraps within page 1, including a low byte
             * at $01ff and a high byte at $0100. Native mode is linear WRAM. */
            unsigned lo=c->e?0x100|((c->sp+1)&255):c->sp+1;
            unsigned hi=c->e?0x100|((c->sp+2)&255):c->sp+2;
            c->pc=(uint16_t)((r[lo]|(unsigned)r[hi]<<8)+1);
            c->sp=(uint16_t)(c->e?0x100|((c->sp+2)&255):c->sp+2);break;
        }
        }
        c->cyclesUsed=(uint8_t)cost;cycles+=cost;
        if(single || entry==0x9317) return cycles;
    }
    return cycles;
}
unsigned ScWaitDriverStep(Interp816 *c,uint8_t *r,unsigned budget) {return driver(c,r,budget,false);}
unsigned ScWaitInstructionStep(Interp816 *c,uint8_t *r) {return driver(c,r,UINT32_MAX,true);}
