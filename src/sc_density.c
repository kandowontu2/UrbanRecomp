#include "sc_density.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <stdlib.h>

/* Density collection, centroid accounting, field publication and initialization
 * in direct C. Preserve ordered writes, flags, stack shadows and the
 * exact caller deadline. Shared full-coordinate hooks stay at their original
 * boundaries. Smoothing and arithmetic helpers retain their scheduler boundaries. */
static unsigned word(const uint8_t *r,unsigned p) {return r[p]|(unsigned)r[p+1]<<8;}
static void put(uint8_t *r,unsigned p,unsigned v) {r[p]=(uint8_t)v;r[p+1]=(uint8_t)(v>>8);}
static void nz(Interp816 *c,unsigned v,bool byte) {c->n=(v&(byte?128:32768))!=0;c->z=(v&(byte?255:65535))==0;}
static void load(Interp816 *c,unsigned v) {c->a=c->mf?(c->a&0xff00)|(v&255):(uint16_t)v;nz(c,c->a,c->mf);}
static void compare(Interp816 *c,unsigned old,unsigned v,bool byte) {
    unsigned mask=byte?255:65535;old&=mask;v&=mask;c->c=old>=v;nz(c,old-v,byte);
}
static void add(Interp816 *c,unsigned v,bool subtract) {
    unsigned mask=c->mf?255:65535,sign=c->mf?128:32768,old=c->a&mask;v&=mask;
    unsigned sum=old+(subtract?(v^mask):v)+c->c;
    c->v=((subtract?(old^v):~(old^v))&(old^sum)&sign)!=0;c->c=sum>mask;load(c,sum);
}
bool ScDensityOwns(uint16_t pc) {
    return (pc>=0x9ad7 && pc<0x9b99) || (pc>=0x9b9c && pc<0x9ba8) || (pc>=0x9bab && pc<0x9c3e);
}
static unsigned execute(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget,bool single) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_DENSITY_FAMILY_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || !rom || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x20 || c->dp>0x1fc0) return 0;
    unsigned cycles=0,cost,addr,value;
    for(;;) {
        if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
        switch(c->pc) {
        case 0x849e:case 0xa29a:
            if(6>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0x84c3:case 0xa2b8:
            if(6>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0x9a3e:
            if(single) return 0;
            cost=ScWorldGuestHousingStep(w,c,r,budget-cycles);
            if(!cost) return cycles;cycles+=cost;break;
        case 0x9ad7: native_9ad7: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0x9ad9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ad9;
        case 0x9ad9: native_9ad9: /* PHD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,c->dp);c->sp-=2;
            c->pc=0x9ada;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ada;
        case 0x9ada: native_9ada: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0x9adb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9adb;
        case 0x9adb: native_9adb: /* TDC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->a=c->dp;nz(c,c->a,false);
            c->pc=0x9adc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9adc;
        case 0x9adc: native_9adc: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0x9add;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9add;
        case 0x9add: native_9add: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0014,true);
            c->pc=0x9ae0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ae0;
        case 0x9ae0: native_9ae0: /* TCD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->dp=c->a;nz(c,c->dp,false);
            c->pc=0x9ae1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ae1;
        case 0x9ae1: native_9ae1: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0x9ae2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ae2;
        case 0x9ae2: native_9ae2: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->xf=false;
            c->pc=0x9ae4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ae4;
        case 0x9ae4: native_9ae4: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9ae6);c->sp-=2;c->pc=0xa13b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9ae7: native_9ae7: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9ae9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ae9;
        case 0x9ae9: native_9ae9: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9aeb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9aeb;
        case 0x9aeb: native_9aeb: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9aed;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9aed;
        case 0x9aed: native_9aed: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0006)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0006)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9aef;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9aef;
        case 0x9aef: native_9aef: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9af1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9af1;
        case 0x9af1: native_9af1: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000a)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000a)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9af3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9af3;
        case 0x9af3: native_9af3: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0012)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0012)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9af5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9af5;
        case 0x9af5: native_9af5: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9af7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9af7;
        case 0x9af7: native_9af7: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestDensityStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=3;
            if(cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            c->mf=true;
            c->pc=0x9af9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9af9;
        case 0x9af9: native_9af9: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0012)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0012)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9afb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9afb;
        case 0x9afb: native_9afb: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x9afc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9afc;
        case 0x9afc: native_9afc: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9afe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9afe;
        case 0x9afe: native_9afe: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9b00);c->sp-=2;c->pc=0x849e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9b01: native_9b01: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0x9b03;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b03;
        case 0x9b03: native_9b03: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x03ff);
            c->pc=0x9b06;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b06;
        case 0x9b06: native_9b06: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9b09;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b09;
        case 0x9b09: native_9b09: /* TAY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->y=c->a;nz(c,c->y,false);
            c->pc=0x9b0a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b0a;
        case 0x9b0a: native_9b0a: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0x84eb&255)+c->y>255)+(c->mf?0:1);
            if(c->y>=1024 || cost>budget-cycles) return cycles;
            load(c,(c->mf?rom[0x10000+0x84eb+c->y]:word(rom,0x10000+0x84eb+c->y)));
            c->pc=0x9b0d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b0d;
        case 0x9b0d: native_9b0d: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x0001);
            c->pc=0x9b10;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b10;
        case 0x9b10: native_9b10: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9b5a:0x9b12;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9b5a;
            goto native_9b12;
        case 0x9b12: native_9b12: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestDensityStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0x9b14;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b14;
        case 0x9b14: native_9b14: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b16;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b16;
        case 0x9b16: native_9b16: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9b19;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b19;
        case 0x9b19: native_9b19: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0012)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0012)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b1b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b1b;
        case 0x9b1b: native_9b1b: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9b1e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b1e;
        case 0x9b1e: native_9b1e: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0x9b20;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b20;
        case 0x9b20: native_9b20: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9b22);c->sp-=2;c->pc=0x9bd5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bd5;
        case 0x9b23: native_9b23: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9b24;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b24;
        case 0x9b24: native_9b24: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9b25;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b25;
        case 0x9b25: native_9b25: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9b26;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b26;
        case 0x9b26: native_9b26: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x00fe,c->mf);
            c->pc=0x9b29;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b29;
        case 0x9b29: native_9b29: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9b2e:0x9b2b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9b2e;
            goto native_9b2b;
        case 0x9b2b: native_9b2b: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x00fe);
            c->pc=0x9b2e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b2e;
        case 0x9b2e: native_9b2e: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0x9b30;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b30;
        case 0x9b30: native_9b30: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0x9b31;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b31;
        case 0x9b31: native_9b31: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0012)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0012)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b33;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b33;
        case 0x9b33: native_9b33: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9b34;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b34;
        case 0x9b34: native_9b34: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x9b35;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b35;
        case 0x9b35: native_9b35: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b37;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b37;
        case 0x9b37: native_9b37: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9b38;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b38;
        case 0x9b38: native_9b38: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9b3a);c->sp-=2;c->pc=0xa29a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9b3b: native_9b3b: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0x9b3c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b3c;
        case 0x9b3c: native_9b3c: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)(w->field_anchor[0])+(int16_t)(c->x-(uint16_t)(w->field_anchor[0])):(int)c->x)<0 || (unsigned)(w->giant?(int)(w->field_anchor[0])+(int16_t)(c->x-(uint16_t)(w->field_anchor[0])):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,13) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)(w->field_anchor[0])+(int16_t)(c->x-(uint16_t)(w->field_anchor[0])):(int)c->x);
            if(c->mf) w->fields[13][addr]=(uint8_t)c->a;else put(w->fields[13],addr,c->a);
            c->pc=0x9b40;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b40;
        case 0x9b40: native_9b40: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0x9b42;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b42;
        case 0x9b42: native_9b42: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b44;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b44;
        case 0x9b44: native_9b44: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x9b45;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b45;
        case 0x9b45: native_9b45: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),false);
            c->pc=0x9b47;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b47;
        case 0x9b47: native_9b47: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9b49;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b49;
        case 0x9b49: native_9b49: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9b4d:0x9b4b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9b4d;
            goto native_9b4b;
        case 0x9b4b: native_9b4b: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9b4d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b4d;
        case 0x9b4d: native_9b4d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b4f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b4f;
        case 0x9b4f: native_9b4f: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x9b50;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b50;
        case 0x9b50: native_9b50: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0012)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0012)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),false);
            c->pc=0x9b52;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b52;
        case 0x9b52: native_9b52: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9b54;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b54;
        case 0x9b54: native_9b54: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9b58:0x9b56;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9b58;
            goto native_9b56;
        case 0x9b56: native_9b56: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0006)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0006)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9b58;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b58;
        case 0x9b58: native_9b58: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9b5a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b5a;
        case 0x9b5a: native_9b5a: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestDensityStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0x9b5c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b5c;
        case 0x9b5c: native_9b5c: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;

            if(w->huge) {
                /* The redirected compare/advance hook consumes no clocks.
                 * Do not expose its writes unless the next opcode fits. */
                unsigned minimum=word(r,c->dp+18)+((word(r,c->dp+16)+1)==ScWorldWidth(w))>=ScWorldHeight(w)?6:3;
                if(minimum>budget-cycles) return cycles;
                ScWorldGuestStep(w,c,r);break;
            }
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9b5e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b5e;
        case 0x9b5e: native_9b5e: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b60;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b60;
        case 0x9b60: native_9b60: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldWidth(w),c->mf);
            c->pc=0x9b62;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b62;
        case 0x9b62: native_9b62: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x9af7:0x9b64;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_9af7;
            goto native_9b64;
        case 0x9b64: native_9b64: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0012)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0012)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9b66;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b66;
        case 0x9b66: native_9b66: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0012)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0012)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b68;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b68;
        case 0x9b68: native_9b68: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldHeight(w),c->mf);
            c->pc=0x9b6a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b6a;
        case 0x9b6a: native_9b6a: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x9af5:0x9b6c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_9af5;
            goto native_9b6c;
        case 0x9b6c: native_9b6c: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9b6e);c->sp-=2;c->pc=0xa02f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9b6f: native_9b6f: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            put(r,c->sp-1,0x9b71);c->sp-=2;c->pc=0xa0b5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9b72: native_9b72: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9b74);c->sp-=2;c->pc=0xa02f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9b75: native_9b75: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0x9b77;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b77;
        case 0x9b77: native_9b77: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            c->x=(uint16_t)0x0000;nz(c,c->x,false);
            c->pc=0x9b7a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b7a;
        case 0x9b7a: native_9b7a: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestDensityStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=5+(c->mf?0:1);
            if((w->giant?(int)(w->field_scan)+(int16_t)(c->x-(uint16_t)(w->field_scan)):(int)c->x)<0 || (unsigned)(w->giant?(int)(w->field_scan)+(int16_t)(c->x-(uint16_t)(w->field_scan)):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,14) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)(w->field_scan)+(int16_t)(c->x-(uint16_t)(w->field_scan)):(int)c->x);
            load(c,(c->mf?w->fields[14][addr]:word(w->fields[14],addr)));
            c->pc=0x9b7e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b7e;
        case 0x9b7e: native_9b7e: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9b7f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b7f;
        case 0x9b7f: native_9b7f: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9b83:0x9b81;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9b83;
            goto native_9b81;
        case 0x9b81: native_9b81: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x00ff);
            c->pc=0x9b83;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b83;
        case 0x9b83: native_9b83: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)(w->field_scan)+(int16_t)(c->x-(uint16_t)(w->field_scan)):(int)c->x)<0 || (unsigned)(w->giant?(int)(w->field_scan)+(int16_t)(c->x-(uint16_t)(w->field_scan)):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,3) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)(w->field_scan)+(int16_t)(c->x-(uint16_t)(w->field_scan)):(int)c->x);
            if(c->mf) w->fields[3][addr]=(uint8_t)c->a;else put(w->fields[3],addr,c->a);
            c->pc=0x9b87;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b87;
        case 0x9b87: native_9b87: /* INX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->x;nz(c,c->x,false);
            c->pc=0x9b88;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b88;
        case 0x9b88: native_9b88: /* CPX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;

            if(w->huge) {
                /* The redirected compare/advance hook consumes no clocks.
                 * Do not expose its writes unless the next opcode fits. */
                unsigned minimum=(w->field_scan+1==ScWorldFieldSizeWorld(w,0)?2:3);
                if(minimum>budget-cycles) return cycles;
                ScWorldGuestStep(w,c,r);break;
            }
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->x,ScWorldFieldSizeWorld(w,0),false);
            c->pc=0x9b8b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b8b;
        case 0x9b8b: native_9b8b: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x9b7a:0x9b8d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_9b7a;
            goto native_9b8d;
        case 0x9b8d: native_9b8d: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9b8f);c->sp-=2;c->pc=0xa24b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9b90: native_9b90: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b92;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b92;
        case 0x9b92: native_9b92: /* ORA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000a)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000a)&65535);
            load(c,c->a|(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9b94;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b94;
        case 0x9b94: native_9b94: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9bb4:0x9b96;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9bb4;
            goto native_9b96;
        case 0x9b96: native_9b96: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9b98);c->sp-=2;c->pc=0xa421;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9b9c: native_9b9c: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0x9b9e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9b9e;
        case 0x9b9e: native_9b9e: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000c)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000c)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9ba0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ba0;
        case 0x9ba0: native_9ba0: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0ba9>0x1ffe || cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            addr=0x0ba9;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9ba3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ba3;
        case 0x9ba3: native_9ba3: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0x9ba5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ba5;
        case 0x9ba5: native_9ba5: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9ba7);c->sp-=2;c->pc=0xa421;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9bab: native_9bab: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0x9bad;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bad;
        case 0x9bad: native_9bad: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000c)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000c)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9baf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9baf;
        case 0x9baf: native_9baf: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0baa>0x1ffe || cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            addr=0x0baa;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9bb2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bb2;
        case 0x9bb2: native_9bb2: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9bc0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bc0;
        case 0x9bb4: native_9bb4: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0x9bb6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bb6;
        case 0x9bb6: native_9bb6: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            load(c,0x003c);
            c->pc=0x9bb8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bb8;
        case 0x9bb8: native_9bb8: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0ba9>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0ba9;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9bbb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bbb;
        case 0x9bbb: native_9bbb: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0032);
            c->pc=0x9bbd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bbd;
        case 0x9bbd: native_9bbd: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0baa>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0baa;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9bc0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bc0;
        case 0x9bc0: native_9bc0: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0ba9>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0ba9;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9bc3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bc3;
        case 0x9bc3: native_9bc3: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9bc4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bc4;
        case 0x9bc4: native_9bc4: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0bab>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0bab;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9bc7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bc7;
        case 0x9bc7: native_9bc7: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0baa>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0baa;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9bca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bca;
        case 0x9bca: native_9bca: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9bcb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bcb;
        case 0x9bcb: native_9bcb: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0bac>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0bac;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9bce;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bce;
        case 0x9bce: native_9bce: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0x9bd0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bd0;
        case 0x9bd0: native_9bd0: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0cde>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0cde;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9bd3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bd3;
        case 0x9bd3: native_9bd3: /* PLD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5;
            if(cost>budget-cycles) return cycles;
            c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);
            c->pc=0x9bd4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bd4;
        case 0x9bd4: native_9bd4: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9bd5: native_9bd5: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->xf=false;
            c->pc=0x9bd7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bd7;
        case 0x9bd7: native_9bd7: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9bda;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bda;
        case 0x9bda: native_9bda: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0376,c->mf);
            c->pc=0x9bdd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bdd;
        case 0x9bdd: native_9bdd: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9be3:0x9bdf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9be3;
            goto native_9bdf;
        case 0x9bdf: native_9bdf: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0030);
            c->pc=0x9be2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9be2;
        case 0x9be2: native_9be2: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9be3: native_9be3: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0084,c->mf);
            c->pc=0x9be6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9be6;
        case 0x9be6: native_9be6: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x9beb:0x9be8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_9beb;
            goto native_9be8;
        case 0x9be8: native_9be8: /* JMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9a3e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9beb: native_9beb: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0137,c->mf);
            c->pc=0x9bee;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bee;
        case 0x9bee: native_9bee: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9bf3:0x9bf0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9bf3;
            goto native_9bf0;
        case 0x9bf0: native_9bf0: /* JMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x842f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9bf3: native_9bf3: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x01f4,c->mf);
            c->pc=0x9bf6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bf6;
        case 0x9bf6: native_9bf6: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9c00:0x9bf8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9c00;
            goto native_9bf8;
        case 0x9bf8: native_9bf8: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9bfa);c->sp-=2;c->pc=0x8456;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9bfb: native_9bfb: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9bfc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bfc;
        case 0x9bfc: native_9bfc: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9bfd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bfd;
        case 0x9bfd: native_9bfd: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9bfe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9bfe;
        case 0x9bfe: native_9bfe: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9c10;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c10;
        case 0x9c00: native_9c00: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0249,c->mf);
            c->pc=0x9c03;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c03;
        case 0x9c03: native_9c03: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9c0d:0x9c05;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9c0d;
            goto native_9c05;
        case 0x9c05: native_9c05: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9c07);c->sp-=2;c->pc=0x847a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9c08: native_9c08: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9c09;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c09;
        case 0x9c09: native_9c09: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9c0a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c0a;
        case 0x9c0a: native_9c0a: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9c0b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c0b;
        case 0x9c0b: native_9c0b: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9c10;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c10;
        case 0x9c0d: native_9c0d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0x9c10;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c10;
        case 0x9c10: native_9c10: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9c11: native_9c11: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0x9c13;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c13;
        case 0x9c13: native_9c13: /* PHD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,c->dp);c->sp-=2;
            c->pc=0x9c14;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c14;
        case 0x9c14: native_9c14: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0x9c15;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c15;
        case 0x9c15: native_9c15: /* TDC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->a=c->dp;nz(c,c->a,false);
            c->pc=0x9c16;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c16;
        case 0x9c16: native_9c16: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0x9c17;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c17;
        case 0x9c17: native_9c17: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0024,true);
            c->pc=0x9c1a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c1a;
        case 0x9c1a: native_9c1a: /* TCD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->dp=c->a;nz(c,c->dp,false);
            c->pc=0x9c1b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c1b;
        case 0x9c1b: native_9c1b: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0x9c1c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c1c;
        case 0x9c1c: native_9c1c: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->xf=false;
            c->pc=0x9c1e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c1e;
        case 0x9c1e: native_9c1e: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            c->x=(uint16_t)0x0000;nz(c,c->x,false);
            c->pc=0x9c21;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c21;
        case 0x9c21: native_9c21: /* TXA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            load(c,c->x);
            c->pc=0x9c22;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c22;
        case 0x9c22: native_9c22: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestDensityStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=5+(c->mf?0:1);
            if((w->giant?(int)(w->field_scan)+(int16_t)(c->x-(uint16_t)(w->field_scan)):(int)c->x)<0 || (unsigned)(w->giant?(int)(w->field_scan)+(int16_t)(c->x-(uint16_t)(w->field_scan)):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,15) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)(w->field_scan)+(int16_t)(c->x-(uint16_t)(w->field_scan)):(int)c->x);
            if(c->mf) w->fields[15][addr]=(uint8_t)c->a;else put(w->fields[15],addr,c->a);
            c->pc=0x9c26;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c26;
        case 0x9c26: native_9c26: /* INX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->x;nz(c,c->x,false);
            c->pc=0x9c27;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c27;
        case 0x9c27: native_9c27: /* INX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->x;nz(c,c->x,false);
            c->pc=0x9c28;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c28;
        case 0x9c28: native_9c28: /* CPX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;

            if(w->colossal) {
                /* The redirected compare/advance hook consumes no clocks.
                 * Do not expose its writes unless the next opcode fits. */
                unsigned minimum=(w->field_scan+2==ScWorldFieldSizeWorld(w,15)?2:3);
                if(minimum>budget-cycles) return cycles;
                ScWorldGuestStep(w,c,r);break;
            }
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->x,ScWorldFieldSizeWorld(w,15),false);
            c->pc=0x9c2b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c2b;
        case 0x9c2b: native_9c2b: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x9c22:0x9c2d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_9c22;
            goto native_9c2d;
        case 0x9c2d: native_9c2d: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9c2f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c2f;
        case 0x9c2f: native_9c2f: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0006)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0006)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9c31;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c31;
        case 0x9c31: native_9c31: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0018)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0018)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9c33;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c33;
        case 0x9c33: native_9c33: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001a)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001a)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9c35;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c35;
        case 0x9c35: native_9c35: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0x9c37;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c37;
        case 0x9c37: native_9c37: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000a)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            addr=((c->dp+0x000a)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9c39;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c39;
        case 0x9c39: native_9c39: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            addr=((c->dp+0x0008)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9c3b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9c3b;
        case 0x9c3b: native_9c3b: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestDensityStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=6;
            if(cost>budget-cycles) return cycles;
            ScWorldGuestStep(w,c,r);
            put(r,c->sp-1,0x9c3d);c->sp-=2;c->pc=0x9cdf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        default:return cycles;
        }
    }
}

/* The scheduler permits one original instruction to cross a beam event.
 * Atomic C uses that same rule; bounded batches never exceed their budget. */
unsigned ScDensityStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget) {
    return execute(w,c,r,rom,budget,false);
}
unsigned ScDensityInstructionStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom) {
    return execute(w,c,r,rom,12,true);
}
