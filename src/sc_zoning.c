#include "sc_zoning.h"
#include "sc_world_guest.h"
#include "sc_native_specialize.h"
#include "snes/interp816.h"
#include <stdlib.h>

/* Connected zone growth, decline, density checks and tile mutations in direct C. Preserve ordered writes, flags, stack shadows and the
 * exact caller deadline. Shared full-coordinate hooks stay at their original
 * boundaries. Drawing and RNG helpers retain their scheduler boundaries. */
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
bool ScZoningOwns(uint16_t pc) {
    return (pc>=0x9468 && pc<0x961d) || (pc>=0x9653 && pc<0x9733) || (pc>=0x9745 && pc<0x97c9);
}
SC_NATIVE_SPECIALIZE unsigned execute(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget,bool single,bool accelerated) {
    static int reference=-1;
    /* The beam-clock instruction family stays opt-in. Stationary-clock extra
     * attempts connect it to their native driver; an explicit reference keeps
     * either caller on the preceding path for matched comparisons. */
    if(reference<0) {const char *e=getenv("SC_ZONING_REFERENCE");reference=e && *e=='0'?0:e && *e=='1'?1:2;}
    if((reference && (!accelerated || reference==1)) || !w || !w->active || !c || !r || !rom || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x20 || c->dp>0x1fc0) return 0;
    unsigned cycles=0,cost,addr,value;ScWorldGuest mapped;
    /* This body only clears X width and changes M width. Emulation and
     * decimal mode cannot change here; retain their context guard at span
     * entry, while every edge still checks stack bounds and its deadline. */
    for(;;) {
        if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
        switch(c->pc) {
        case 0x849e:case 0x84c4:case 0xa29a:
            if(6>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0x84c3:case 0x84ea:case 0xa2b8:
            if(6>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0x9468: native_9468: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x946a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_946a;
        case 0x946a: native_946a: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x946d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_946d;
        case 0x946d: native_946d: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x946e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_946e;
        case 0x946e: native_946e: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x946f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_946f;
        case 0x946f: native_946f: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9472;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9472;
        case 0x9472: native_9472: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9473;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9473;
        case 0x9473: native_9473: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9475);c->sp-=2;c->pc=0xa29a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9476: native_9476: /* LDY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0x9479;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9479;
        case 0x9479: native_9479: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x947d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_947d;
        case 0x947d: native_947d: /* SEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0x947e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_947e;
        case 0x947e: native_947e: /* SBC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            add(c,value,true);
            c->pc=0x9482;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9482;
        case 0x9482: native_9482: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9493:0x9484;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9493;
            goto native_9484;
        case 0x9484: native_9484: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x001e,c->mf);
            c->pc=0x9486;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9486;
        case 0x9486: native_9486: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9493:0x9488;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9493;
            goto native_9488;
        case 0x9488: native_9488: /* INY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0x9489;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9489;
        case 0x9489: native_9489: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0050,c->mf);
            c->pc=0x948b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_948b;
        case 0x948b: native_948b: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9493:0x948d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9493;
            goto native_948d;
        case 0x948d: native_948d: /* INY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0x948e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_948e;
        case 0x948e: native_948e: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0096,c->mf);
            c->pc=0x9490;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9490;
        case 0x9490: native_9490: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9493:0x9492;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9493;
            goto native_9492;
        case 0x9492: native_9492: /* INY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0x9493;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9493;
        case 0x9493: native_9493: /* TYA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            load(c,c->y);
            c->pc=0x9494;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9494;
        case 0x9494: native_9494: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9495: native_9495: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x9497;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9497;
        case 0x9497: native_9497: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x949a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_949a;
        case 0x949a: native_949a: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x949b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_949b;
        case 0x949b: native_949b: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x949c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_949c;
        case 0x949c: native_949c: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x949f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_949f;
        case 0x949f: native_949f: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x94a0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94a0;
        case 0x94a0: native_94a0: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x94a2);c->sp-=2;c->pc=0xa29a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x94a3: native_94a3: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x94a7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94a7;
        case 0x94a7: native_94a7: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0081,c->mf);
            c->pc=0x94a9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94a9;
        case 0x94a9: native_94a9: /* BCS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9503:0x94ab;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9503;
            goto native_94ab;
        case 0x94ab: native_94ab: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x94ad;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94ad;
        case 0x94ad: native_94ad: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x94b0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94b0;
        case 0x94b0: native_94b0: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0084,c->mf);
            c->pc=0x94b3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94b3;
        case 0x94b3: native_94b3: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x94e6:0x94b5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_94e6;
            goto native_94b5;
        case 0x94b5: native_94b5: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x94b7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94b7;
        case 0x94b7: native_94b7: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0008,c->mf);
            c->pc=0x94ba;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94ba;
        case 0x94ba: native_94ba: /* BCS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x94ca:0x94bc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_94ca;
            goto native_94bc;
        case 0x94bc: native_94bc: /* PHX */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->xf?0:1);
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,c->x);c->sp-=2;
            c->pc=0x94bd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94bd;
        case 0x94bd: native_94bd: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x94bf);c->sp-=2;c->pc=0x97c9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x94c0: native_94c0: /* PLX */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(cost>budget-cycles) return cycles;
            c->x=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->x,false);
            c->pc=0x94c1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94c1;
        case 0x94c1: native_94c1: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x94c3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94c3;
        case 0x94c3: native_94c3: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0x94c5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94c5;
        case 0x94c5: native_94c5: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x94c7);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x94c8: native_94c8: /* BRA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9503;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9503;
        case 0x94ca: native_94ca: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x94cc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94cc;
        case 0x94cc: native_94cc: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x94d0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94d0;
        case 0x94d0: native_94d0: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0041,c->mf);
            c->pc=0x94d2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94d2;
        case 0x94d2: native_94d2: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9503:0x94d4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9503;
            goto native_94d4;
        case 0x94d4: native_94d4: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x94d6);c->sp-=2;c->pc=0x9468;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9468;
        case 0x94d7: native_94d7: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x94d8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94d8;
        case 0x94d8: native_94d8: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0x94da;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94da;
        case 0x94da: native_94da: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x94dc);c->sp-=2;c->pc=0x98b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x94dd: native_94dd: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x94df;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94df;
        case 0x94df: native_94df: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0008);
            c->pc=0x94e1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94e1;
        case 0x94e1: native_94e1: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x94e3);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x94e4: native_94e4: /* BRA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9503;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9503;
        case 0x94e6: native_94e6: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x94e8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94e8;
        case 0x94e8: native_94e8: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x94ea;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94ea;
        case 0x94ea: native_94ea: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0028,c->mf);
            c->pc=0x94ed;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94ed;
        case 0x94ed: native_94ed: /* BCS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9504:0x94ef;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9504;
            goto native_94ef;
        case 0x94ef: native_94ef: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x94f1);c->sp-=2;c->pc=0x9468;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9468;
        case 0x94f2: native_94f2: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x94f3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94f3;
        case 0x94f3: native_94f3: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x94f5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94f5;
        case 0x94f5: native_94f5: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x94f6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94f6;
        case 0x94f6: native_94f6: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x94f7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94f7;
        case 0x94f7: native_94f7: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x94f8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94f8;
        case 0x94f8: native_94f8: /* DEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:0);
            if(cost>budget-cycles) return cycles;
            load(c,c->a-1);
            c->pc=0x94f9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94f9;
        case 0x94f9: native_94f9: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x94fb);c->sp-=2;c->pc=0x98b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x94fc: native_94fc: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x94fe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_94fe;
        case 0x94fe: native_94fe: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0008);
            c->pc=0x9500;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9500;
        case 0x9500: native_9500: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9502);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9503: native_9503: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9504: native_9504: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xcf));
            c->pc=0x9506;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9506;
        case 0x9506: native_9506: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9509;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9509;
        case 0x9509: native_9509: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0120,c->mf);
            c->pc=0x950c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_950c;
        case 0x950c: native_950c: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x9566:0x950e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_9566;
            goto native_950e;
        case 0x950e: native_950e: /* LDX */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(0x0b49>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b49;
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0x9511;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9511;
        case 0x9511: native_9511: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x9515;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9515;
        case 0x9515: native_9515: /* AND */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x03ff);
            c->pc=0x9518;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9518;
        case 0x9518: native_9518: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0120,c->mf);
            c->pc=0x951b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_951b;
        case 0x951b: native_951b: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x953c:0x951d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_953c;
            goto native_951d;
        case 0x951d: native_951d: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0376);
            c->pc=0x9520;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9520;
        case 0x9520: native_9520: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9522);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9523: native_9523: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9526;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9526;
        case 0x9526: native_9526: /* PHA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0x9527;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9527;
        case 0x9527: native_9527: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x952a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_952a;
        case 0x952a: native_952a: /* CLC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x952b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_952b;
        case 0x952b: native_952b: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0003,false);
            c->pc=0x952e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_952e;
        case 0x952e: native_952e: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9531;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9531;
        case 0x9531: native_9531: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x037f);
            c->pc=0x9534;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9534;
        case 0x9534: native_9534: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9536);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9537: native_9537: /* PLA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0x9538;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9538;
        case 0x9538: native_9538: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x953b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_953b;
        case 0x953b: native_953b: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x953c: native_953c: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x9540;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9540;
        case 0x9540: native_9540: /* AND */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x03ff);
            c->pc=0x9543;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9543;
        case 0x9543: native_9543: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0120,c->mf);
            c->pc=0x9546;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9546;
        case 0x9546: native_9546: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x9566:0x9548;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_9566;
            goto native_9548;
        case 0x9548: native_9548: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0388);
            c->pc=0x954b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_954b;
        case 0x954b: native_954b: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x954d);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x954e: native_954e: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9551;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9551;
        case 0x9551: native_9551: /* PHA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0x9552;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9552;
        case 0x9552: native_9552: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9555;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9555;
        case 0x9555: native_9555: /* CLC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x9556;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9556;
        case 0x9556: native_9556: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0300,false);
            c->pc=0x9559;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9559;
        case 0x9559: native_9559: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x955c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_955c;
        case 0x955c: native_955c: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0391);
            c->pc=0x955f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_955f;
        case 0x955f: native_955f: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9561);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9562: native_9562: /* PLA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0x9563;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9563;
        case 0x9563: native_9563: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9566;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9566;
        case 0x9566: native_9566: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9567: native_9567: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x9569;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9569;
        case 0x9569: native_9569: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x956c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_956c;
        case 0x956c: native_956c: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x956d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_956d;
        case 0x956d: native_956d: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x956e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_956e;
        case 0x956e: native_956e: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9571;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9571;
        case 0x9571: native_9571: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9572;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9572;
        case 0x9572: native_9572: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9574);c->sp-=2;c->pc=0xa29a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9575: native_9575: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x9579;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9579;
        case 0x9579: native_9579: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x957a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_957a;
        case 0x957a: native_957a: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x957b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_957b;
        case 0x957b: native_957b: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x957c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_957c;
        case 0x957c: native_957c: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x957d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_957d;
        case 0x957d: native_957d: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x957e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_957e;
        case 0x957e: native_957e: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            compare(c,c->a,(c->mf?r[addr]:word(r,addr)),c->mf);
            c->pc=0x9580;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9580;
        case 0x9580: native_9580: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9598:0x9582;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9598;
            goto native_9582;
        case 0x9582: native_9582: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9584;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9584;
        case 0x9584: native_9584: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0005,c->mf);
            c->pc=0x9586;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9586;
        case 0x9586: native_9586: /* BCS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9599:0x9588;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9599;
            goto native_9588;
        case 0x9588: native_9588: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x958a);c->sp-=2;c->pc=0x9468;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9468;
        case 0x958b: native_958b: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x958c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_958c;
        case 0x958c: native_958c: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x958e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_958e;
        case 0x958e: native_958e: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9590);c->sp-=2;c->pc=0x98dd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9591: native_9591: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x9593;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9593;
        case 0x9593: native_9593: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0008);
            c->pc=0x9595;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9595;
        case 0x9595: native_9595: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9597);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9598: native_9598: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9599: native_9599: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xcf));
            c->pc=0x959b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_959b;
        case 0x959b: native_959b: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x959e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_959e;
        case 0x959e: native_959e: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x01ef,c->mf);
            c->pc=0x95a1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95a1;
        case 0x95a1: native_95a1: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x95fb:0x95a3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_95fb;
            goto native_95a3;
        case 0x95a3: native_95a3: /* LDX */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(0x0b49>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b49;
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0x95a6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95a6;
        case 0x95a6: native_95a6: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x95aa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95aa;
        case 0x95aa: native_95aa: /* AND */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x03ff);
            c->pc=0x95ad;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95ad;
        case 0x95ad: native_95ad: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x01ef,c->mf);
            c->pc=0x95b0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95b0;
        case 0x95b0: native_95b0: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x95d1:0x95b2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_95d1;
            goto native_95b2;
        case 0x95b2: native_95b2: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x039a);
            c->pc=0x95b5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95b5;
        case 0x95b5: native_95b5: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x95b7);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x95b8: native_95b8: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x95bb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95bb;
        case 0x95bb: native_95bb: /* PHA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0x95bc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95bc;
        case 0x95bc: native_95bc: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x95bf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95bf;
        case 0x95bf: native_95bf: /* CLC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x95c0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95c0;
        case 0x95c0: native_95c0: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0003,false);
            c->pc=0x95c3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95c3;
        case 0x95c3: native_95c3: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x95c6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95c6;
        case 0x95c6: native_95c6: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x03a3);
            c->pc=0x95c9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95c9;
        case 0x95c9: native_95c9: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x95cb);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x95cc: native_95cc: /* PLA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0x95cd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95cd;
        case 0x95cd: native_95cd: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x95d0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95d0;
        case 0x95d0: native_95d0: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x95d1: native_95d1: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x95d5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95d5;
        case 0x95d5: native_95d5: /* AND */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x03ff);
            c->pc=0x95d8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95d8;
        case 0x95d8: native_95d8: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x01ef,c->mf);
            c->pc=0x95db;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95db;
        case 0x95db: native_95db: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x95fb:0x95dd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_95fb;
            goto native_95dd;
        case 0x95dd: native_95dd: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x03ac);
            c->pc=0x95e0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95e0;
        case 0x95e0: native_95e0: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x95e2);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x95e3: native_95e3: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x95e6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95e6;
        case 0x95e6: native_95e6: /* PHA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0x95e7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95e7;
        case 0x95e7: native_95e7: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x95ea;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95ea;
        case 0x95ea: native_95ea: /* CLC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x95eb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95eb;
        case 0x95eb: native_95eb: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0300,false);
            c->pc=0x95ee;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95ee;
        case 0x95ee: native_95ee: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x95f1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95f1;
        case 0x95f1: native_95f1: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x03b5);
            c->pc=0x95f4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95f4;
        case 0x95f4: native_95f4: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x95f6);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x95f7: native_95f7: /* PLA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0x95f8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95f8;
        case 0x95f8: native_95f8: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x95fb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95fb;
        case 0x95fb: native_95fb: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x95fc: native_95fc: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x95fe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_95fe;
        case 0x95fe: native_95fe: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9600;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9600;
        case 0x9600: native_9600: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0004,c->mf);
            c->pc=0x9603;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9603;
        case 0x9603: native_9603: /* BCS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x961c:0x9605;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_961c;
            goto native_9605;
        case 0x9605: native_9605: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x9607;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9607;
        case 0x9607: native_9607: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9609);c->sp-=2;c->pc=0x907e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x960a: native_960a: /* AND */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x0001);
            c->pc=0x960d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_960d;
        case 0x960d: native_960d: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x960f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_960f;
        case 0x960f: native_960f: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x9610;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9610;
        case 0x9610: native_9610: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9612;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9612;
        case 0x9612: native_9612: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9614);c->sp-=2;c->pc=0x9906;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9615: native_9615: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x9617;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9617;
        case 0x9617: native_9617: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0008);
            c->pc=0x9619;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9619;
        case 0x9619: native_9619: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x961b);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x961c: native_961c: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9653: native_9653: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x011c);
            c->pc=0x9656;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9656;
        case 0x9656: native_9656: /* JMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9659: native_9659: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x965b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_965b;
        case 0x965b: native_965b: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x965e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_965e;
        case 0x965e: native_965e: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x037a,c->mf);
            c->pc=0x9661;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9661;
        case 0x9661: native_9661: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9653:0x9663;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9653;
            goto native_9663;
        case 0x9663: native_9663: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x038c,c->mf);
            c->pc=0x9666;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9666;
        case 0x9666: native_9666: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9653:0x9668;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9653;
            goto native_9668;
        case 0x9668: native_9668: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0383,c->mf);
            c->pc=0x966b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_966b;
        case 0x966b: native_966b: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x96d5:0x966d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_96d5;
            goto native_966d;
        case 0x966d: native_966d: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0395,c->mf);
            c->pc=0x9670;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9670;
        case 0x9670: native_9670: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x96d5:0x9672;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_96d5;
            goto native_9672;
        case 0x9672: native_9672: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x9674;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9674;
        case 0x9674: native_9674: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9676;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9676;
        case 0x9676: native_9676: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x96d5:0x9678;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_96d5;
            goto native_9678;
        case 0x9678: native_9678: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0010,c->mf);
            c->pc=0x967a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_967a;
        case 0x967a: native_967a: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x96d7:0x967c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_96d7;
            goto native_967c;
        case 0x967c: native_967c: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x00f8);
            c->pc=0x967e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_967e;
        case 0x967e: native_967e: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9680);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9681: native_9681: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x9683;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9683;
        case 0x9683: native_9683: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9686;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9686;
        case 0x9686: native_9686: /* LDY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0084;nz(c,c->y,false);
            c->pc=0x9689;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9689;
        case 0x9689: native_9689: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x968b);c->sp-=2;c->pc=0x84c4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x968c: native_968c: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x968e);c->sp-=2;c->pc=0x9468;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9468;
        case 0x968f: native_968f: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x9691;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9691;
        case 0x9691: native_9691: /* AND */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x00ff);
            c->pc=0x9694;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9694;
        case 0x9694: native_9694: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0006)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0006)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9696;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9696;
        case 0x9696: native_9696: /* ASL */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9697;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9697;
        case 0x9697: native_9697: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0006)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0006)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),false);
            c->pc=0x9699;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9699;
        case 0x9699: native_9699: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0089,false);
            c->pc=0x969c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_969c;
        case 0x969c: native_969c: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0006)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0006)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x969e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_969e;
        case 0x969e: native_969e: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x96a0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96a0;
        case 0x96a0: native_96a0: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x96a3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96a3;
        case 0x96a3: native_96a3: /* DEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:0);
            if(cost>budget-cycles) return cycles;
            load(c,c->a-1);
            c->pc=0x96a4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96a4;
        case 0x96a4: native_96a4: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x96a5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96a5;
        case 0x96a5: native_96a5: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x96a8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96a8;
        case 0x96a8: native_96a8: /* DEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:0);
            if(cost>budget-cycles) return cycles;
            load(c,c->a-1);
            c->pc=0x96a9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96a9;
        case 0x96a9: native_96a9: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x96ab);c->sp-=2;c->pc=0x849e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x96ac: native_96ac: /* STX */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            put(r,addr,c->x);
            c->pc=0x96ae;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96ae;
        case 0x96ae: native_96ae: /* LDY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0x96b1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96b1;
        case 0x96b1: native_96b1: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x96b3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96b3;
        case 0x96b3: native_96b3: /* CLC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x96b4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96b4;
        case 0x96b4: native_96b4: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0x9733&255)+c->y>255)+(c->mf?0:1);
            if(c->y>=18 || cost>budget-cycles) return cycles;
            value=(c->mf?rom[0x10000+0x9733+c->y]:word(rom,0x10000+0x9733+c->y));if(!c->mf && value!=65535) value=(value/240)*(2*ScWorldWidth(w))+value%240;
            add(c,value,false);
            c->pc=0x96b7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96b7;
        case 0x96b7: native_96b7: /* TAX */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0x96b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96b8;
        case 0x96b8: native_96b8: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x96bc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96bc;
        case 0x96bc: native_96bc: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0084,c->mf);
            c->pc=0x96bf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96bf;
        case 0x96bf: native_96bf: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x96ce:0x96c1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_96ce;
            goto native_96c1;
        case 0x96c1: native_96c1: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0002);
            c->pc=0x96c4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96c4;
        case 0x96c4: native_96c4: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x96c6);c->sp-=2;c->pc=0x9035;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x96c7: native_96c7: /* CLC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x96c8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96c8;
        case 0x96c8: native_96c8: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0006)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0006)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),false);
            c->pc=0x96ca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96ca;
        case 0x96ca: native_96ca: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            ScWorldGuestWrite(&mapped,mapped.address,(uint8_t)c->a);if(!c->mf) ScWorldGuestWrite(&mapped,mapped.address+1,(uint8_t)(c->a>>8));
            c->pc=0x96ce;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96ce;
        case 0x96ce: native_96ce: /* INY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0x96cf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96cf;
        case 0x96cf: native_96cf: /* INY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0x96d0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96d0;
        case 0x96d0: native_96d0: /* CPY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x0012,false);
            c->pc=0x96d3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96d3;
        case 0x96d3: native_96d3: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x96b1:0x96d5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_96b1;
            goto native_96d5;
        case 0x96d5: native_96d5: /* BRA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9732;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9732;
        case 0x96d7: native_96d7: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x96f3:0x96d9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_96f3;
            goto native_96d9;
        case 0x96d9: native_96d9: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x96db;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96db;
        case 0x96db: native_96db: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x96dd);c->sp-=2;c->pc=0x9468;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9468;
        case 0x96de: native_96de: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x96df;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96df;
        case 0x96df: native_96df: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x96e1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96e1;
        case 0x96e1: native_96e1: /* SEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0x96e2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96e2;
        case 0x96e2: native_96e2: /* SBC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            add(c,0x0018,true);
            c->pc=0x96e4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96e4;
        case 0x96e4: native_96e4: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x96e5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96e5;
        case 0x96e5: native_96e5: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x96e6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96e6;
        case 0x96e6: native_96e6: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x96e7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96e7;
        case 0x96e7: native_96e7: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x96e9);c->sp-=2;c->pc=0x98b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x96ea: native_96ea: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x96ec;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96ec;
        case 0x96ec: native_96ec: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x00f8);
            c->pc=0x96ee;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96ee;
        case 0x96ee: native_96ee: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x96f0);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x96f1: native_96f1: /* BRA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9732;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9732;
        case 0x96f3: native_96f3: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x00ff);
            c->pc=0x96f5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96f5;
        case 0x96f5: native_96f5: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x96f7);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x96f8: native_96f8: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x96fa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96fa;
        case 0x96fa: native_96fa: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x96fd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96fd;
        case 0x96fd: native_96fd: /* DEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:0);
            if(cost>budget-cycles) return cycles;
            load(c,c->a-1);
            c->pc=0x96fe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96fe;
        case 0x96fe: native_96fe: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x96ff;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_96ff;
        case 0x96ff: native_96ff: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9702;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9702;
        case 0x9702: native_9702: /* DEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:0);
            if(cost>budget-cycles) return cycles;
            load(c,c->a-1);
            c->pc=0x9703;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9703;
        case 0x9703: native_9703: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9705);c->sp-=2;c->pc=0x849e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9706: native_9706: /* STX */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            put(r,addr,c->x);
            c->pc=0x9708;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9708;
        case 0x9708: native_9708: /* LDY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0x970b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_970b;
        case 0x970b: native_970b: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x970d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_970d;
        case 0x970d: native_970d: /* CLC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x970e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_970e;
        case 0x970e: native_970e: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0x9733&255)+c->y>255)+(c->mf?0:1);
            if(c->y>=18 || cost>budget-cycles) return cycles;
            value=(c->mf?rom[0x10000+0x9733+c->y]:word(rom,0x10000+0x9733+c->y));if(!c->mf && value!=65535) value=(value/240)*(2*ScWorldWidth(w))+value%240;
            add(c,value,false);
            c->pc=0x9711;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9711;
        case 0x9711: native_9711: /* TAX */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0x9712;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9712;
        case 0x9712: native_9712: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            value=mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0;
            load(c,value);
            c->pc=0x9716;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9716;
        case 0x9716: native_9716: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0089,c->mf);
            c->pc=0x9719;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9719;
        case 0x9719: native_9719: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x972b:0x971b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_972b;
            goto native_971b;
        case 0x971b: native_971b: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0095,c->mf);
            c->pc=0x971e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_971e;
        case 0x971e: native_971e: /* BCS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x972b:0x9720;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_972b;
            goto native_9720;
        case 0x9720: native_9720: /* TYA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            load(c,c->y);
            c->pc=0x9721;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9721;
        case 0x9721: native_9721: /* LSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9722;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9722;
        case 0x9722: native_9722: /* ADC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0080,false);
            c->pc=0x9725;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9725;
        case 0x9725: native_9725: /* STA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            ScWorldGuestWrite(&mapped,mapped.address,(uint8_t)c->a);if(!c->mf) ScWorldGuestWrite(&mapped,mapped.address+1,(uint8_t)(c->a>>8));
            c->pc=0x9729;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9729;
        case 0x9729: native_9729: /* BRA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9732;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9732;
        case 0x972b: native_972b: /* INY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0x972c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_972c;
        case 0x972c: native_972c: /* INY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0x972d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_972d;
        case 0x972d: native_972d: /* CPY */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x0012,false);
            c->pc=0x9730;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9730;
        case 0x9730: native_9730: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x970b:0x9732;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_970b;
            goto native_9732;
        case 0x9732: native_9732: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9745: native_9745: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x01eb);
            c->pc=0x9748;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9748;
        case 0x9748: native_9748: /* JMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x974b: native_974b: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x974d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_974d;
        case 0x974d: native_974d: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9750;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9750;
        case 0x9750: native_9750: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x039e,c->mf);
            c->pc=0x9753;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9753;
        case 0x9753: native_9753: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9745:0x9755;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9745;
            goto native_9755;
        case 0x9755: native_9755: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x03b0,c->mf);
            c->pc=0x9758;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9758;
        case 0x9758: native_9758: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9745:0x975a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9745;
            goto native_975a;
        case 0x975a: native_975a: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x03a7,c->mf);
            c->pc=0x975d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_975d;
        case 0x975d: native_975d: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9793:0x975f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9793;
            goto native_975f;
        case 0x975f: native_975f: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x03b9,c->mf);
            c->pc=0x9762;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9762;
        case 0x9762: native_9762: /* BEQ */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9793:0x9764;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9793;
            goto native_9764;
        case 0x9764: native_9764: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x9766;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9766;
        case 0x9766: native_9766: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9768;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9768;
        case 0x9768: native_9768: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0x976b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_976b;
        case 0x976b: native_976b: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9781:0x976d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9781;
            goto native_976d;
        case 0x976d: native_976d: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x976f);c->sp-=2;c->pc=0x9468;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9468;
        case 0x9770: native_9770: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x9771;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9771;
        case 0x9771: native_9771: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9773;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9773;
        case 0x9773: native_9773: /* DEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:0);
            if(cost>budget-cycles) return cycles;
            load(c,c->a-1);
            c->pc=0x9774;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9774;
        case 0x9774: native_9774: /* DEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:0);
            if(cost>budget-cycles) return cycles;
            load(c,c->a-1);
            c->pc=0x9775;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9775;
        case 0x9775: native_9775: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9777);c->sp-=2;c->pc=0x98dd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9778: native_9778: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x977a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_977a;
        case 0x977a: native_977a: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x00f8);
            c->pc=0x977c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_977c;
        case 0x977c: native_977c: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x977e);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x977f: native_977f: /* BRA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9793;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9793;
        case 0x9781: native_9781: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0001,c->mf);
            c->pc=0x9784;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9784;
        case 0x9784: native_9784: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x9793:0x9786;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_9793;
            goto native_9786;
        case 0x9786: native_9786: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0137);
            c->pc=0x9789;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9789;
        case 0x9789: native_9789: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x978b);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x978c: native_978c: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x978e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_978e;
        case 0x978e: native_978e: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x00f8);
            c->pc=0x9790;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9790;
        case 0x9790: native_9790: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9792);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9793: native_9793: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9794: native_9794: /* REP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x9796;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9796;
        case 0x9796: native_9796: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9798;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9798;
        case 0x9798: native_9798: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0x979b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_979b;
        case 0x979b: native_979b: /* BCC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x97b6:0x979d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_97b6;
            goto native_979d;
        case 0x979d: native_979d: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x979f);c->sp-=2;c->pc=0x907e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x97a0: native_97a0: /* AND */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x0001);
            c->pc=0x97a3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97a3;
        case 0x97a3: native_97a3: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x97a5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97a5;
        case 0x97a5: native_97a5: /* XBA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x97a6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97a6;
        case 0x97a6: native_97a6: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x97a8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97a8;
        case 0x97a8: native_97a8: /* DEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:0);
            if(cost>budget-cycles) return cycles;
            load(c,c->a-1);
            c->pc=0x97a9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97a9;
        case 0x97a9: native_97a9: /* DEC */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:0);
            if(cost>budget-cycles) return cycles;
            load(c,c->a-1);
            c->pc=0x97aa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97aa;
        case 0x97aa: native_97aa: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x97ac);c->sp-=2;c->pc=0x9906;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x97ad: native_97ad: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x97af;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97af;
        case 0x97af: native_97af: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x00f8);
            c->pc=0x97b1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97b1;
        case 0x97b1: native_97b1: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x97b3);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x97b4: native_97b4: /* BRA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x97c8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97c8;
        case 0x97b6: native_97b6: /* CMP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0001,c->mf);
            c->pc=0x97b9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97b9;
        case 0x97b9: native_97b9: /* BNE */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0x97c8:0x97bb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_97c8;
            goto native_97bb;
        case 0x97bb: native_97bb: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x01f4);
            c->pc=0x97be;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97be;
        case 0x97be: native_97be: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x97c0);c->sp-=2;c->pc=0x9940;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x97c1: native_97c1: /* SEP */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x97c3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97c3;
        case 0x97c3: native_97c3: /* LDA */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x00f8);
            c->pc=0x97c5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_97c5;
        case 0x97c5: native_97c5: /* JSR */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x97c7);c->sp-=2;c->pc=0x961d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x97c8: native_97c8: /* RTS */
            if(c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        default:return cycles;
        }
    }
}

unsigned ScZoningStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget) {
    return execute(w,c,r,rom,budget,false,false);
}
unsigned ScZoningInstructionStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom) {
    return execute(w,c,r,rom,12,true,false);
}
unsigned ScZoningAcceleratedStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget) {
    return execute(w,c,r,rom,budget,false,true);
}

unsigned ScZoningQualityStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_ZONE_QUALITY_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || c->pc!=0x9468 || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->sp<0x102 || c->sp>0x1ffd || (c->sp>=0xb3b && c->sp<=0xb41)) return 0;
    /* Match the byte-coordinate helper's nearest full-coordinate lift. All
     * reads precede publication, including the original nested stack shadow. */
    int x=r[0xb85]/2,y=r[0xb86]/2;
    if(w->huge) {
        int rx=w->coord[2][0]/2,ry=w->coord[2][1]/2;
        int dx=(x-rx)%128,dy=(y-ry)%128;
        if(dx>64) dx-=128;if(dx<-64) dx+=128;
        if(dy>64) dy-=128;if(dy<-64) dy+=128;
        x=rx+dx;y=ry+dy;
    }
    unsigned width=ScWorldFieldWidth(w,0);
    if(x<0 || y<0 || (unsigned)x>=width || (unsigned)y>=ScWorldFieldHeight(w,0)) return 0;
    unsigned index=(unsigned)y*width+(unsigned)x;
    unsigned land=w->fields[0][index],pollution=w->fields[2][index];
    unsigned difference=(land-pollution)&255;
    bool borrow=land<pollution;
    unsigned tier=borrow || difference<30?0:difference<80?1:difference<150?2:3;
    unsigned cost=borrow?56:tier==0?60:tier==1?66:tier==2?72:73;
    if(cost>budget) return 0;
    put(r,c->sp-1,0x9475);put(r,0xb3f,x);put(r,0xb3d,y);
    w->field_anchor[0]=index;c->x=(uint16_t)index;c->y=(uint16_t)tier;
    c->a=(uint16_t)((index&0xff00)|tier);c->mf=true;
    c->v=((land^pollution)&(land^difference)&128)!=0;
    c->c=tier==3;c->n=false;c->z=tier==0;
    c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;
    return cost;
}
/* Zone control is a transaction: evaluate a whole decision against immutable
 * fields and publish once after the deadline preflight. Helpers retain their
 * exact call boundaries, so scanout and interrupts observe the original CPU,
 * stack shadows and scratch. No instruction decoder runs in these paths. */
typedef struct ZoneControl {
    Interp816 cpu;
    unsigned cycles, count, address[8], value[8];
    bool field;
    unsigned field_index, field_x, field_y;
} ZoneControl;
static void control_word(ZoneControl *s,unsigned address,unsigned value) {
    s->address[s->count]=address;s->value[s->count++]=value;
}
static void control_push(ZoneControl *s,unsigned value) {
    control_word(s,s->cpu.sp-1,value);s->cpu.sp-=2;
}
static void control_call(ZoneControl *s,unsigned call,unsigned target) {
    control_push(s,call+2);s->cpu.pc=target;s->cpu.cyclesUsed=6;s->cycles+=6;
}
static void control_return(ZoneControl *s,const uint8_t *r) {
    s->cpu.pc=(uint16_t)(word(r,s->cpu.sp+1)+1);s->cpu.sp+=2;
    s->cpu.cyclesUsed=6;s->cycles+=6;
}
static bool control_field(ZoneControl *s,const ScWorld *w,const uint8_t *r,unsigned call) {
    int x=r[0xb85]/2,y=r[0xb86]/2;
    if(w->huge) {
        int rx=w->coord[2][0]/2,ry=w->coord[2][1]/2;
        int dx=(x-rx)%128,dy=(y-ry)%128;
        if(dx>64) dx-=128;if(dx<-64) dx+=128;
        if(dy>64) dy-=128;if(dy<-64) dy+=128;
        x=rx+dx;y=ry+dy;
    }
    if(x<0 || y<0 || (unsigned)x>=ScWorldFieldWidth(w,0) ||
        (unsigned)y>=ScWorldFieldHeight(w,0)) return false;
    unsigned index=y*ScWorldFieldWidth(w,0)+x;
    control_word(s,s->cpu.sp-1,call+2);
    s->field=true;s->field_index=index;s->field_x=x;s->field_y=y;
    s->cpu.a=s->cpu.x=(uint16_t)index;s->cpu.mf=true;
    nz(&s->cpu,index,false);s->cpu.c=false;s->cycles+=30;
    return true;
}
unsigned ScZoningControlStep(ScWorld *w,Interp816 *cpu,uint8_t *r,
                             const uint8_t *rom,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_ZONE_CONTROL_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !cpu || !r || !rom || !budget ||
       cpu->k!=3 || cpu->db!=3 || cpu->e || cpu->d || cpu->xf ||
       cpu->waiting || cpu->stopped || cpu->nmiWanted || (cpu->irqWanted && !cpu->i) ||
       cpu->dp<0x1000 || cpu->dp>0x1fc0 || cpu->sp<0x1008 || cpu->sp>0x1ffd ||
       (cpu->sp>=cpu->dp && cpu->sp-5<=cpu->dp+10)) return 0;
    ZoneControl s={.cpu=*cpu};Interp816 *c=&s.cpu;
    unsigned entry=c->pc,dp=(c->dp&255)!=0;
    switch(entry) {
    case 0x9495:case 0x9567: {
        bool commercial=entry==0x9567;
        if(!control_field(&s,w,r,commercial?0x9572:0x94a0)) return 0;
        load(c,w->fields[commercial?0:2][s.field_index]);s.cycles+=5;
        if(commercial) {
            unsigned value=c->a&255;c->c=(value&16)!=0;load(c,value>>5);s.cycles+=10;
            compare(c,c->a,r[c->dp],true);s.cycles+=3+dp;
            if(!c->c) {s.cycles+=3;control_return(&s,r);break;}
            s.cycles+=2;load(c,r[c->dp]);s.cycles+=3+dp;
            compare(c,c->a,5,true);s.cycles+=2;
            if(c->c) {s.cycles+=3;c->pc=0x9599;c->cyclesUsed=3;break;}
            s.cycles+=2;control_call(&s,0x9588,0x9468);break;
        }
        compare(c,c->a,129,true);s.cycles+=2;
        if(c->c) {s.cycles+=3;control_return(&s,r);break;}
        s.cycles+=2+3;c->mf=false;load(c,word(r,0xb89));s.cycles+=5;
        compare(c,c->a,0x84,false);s.cycles+=3;
        if(!c->z) {
            s.cycles+=3+3;load(c,word(r,c->dp));s.cycles+=4+dp;
            compare(c,c->a,40,false);s.cycles+=3;
            if(c->c) {s.cycles+=3;c->pc=0x9504;c->cyclesUsed=3;}
            else {s.cycles+=2;control_call(&s,0x94ef,0x9468);}
        } else {
            s.cycles+=2;load(c,word(r,c->dp));s.cycles+=4+dp;
            compare(c,c->a,8,false);s.cycles+=3;
            if(!c->c) {s.cycles+=2;control_push(&s,c->x);s.cycles+=4;control_call(&s,0x94bd,0x97c9);}
            else {
                s.cycles+=3+3;c->mf=true;load(c,w->fields[3][s.field_index]);s.cycles+=5;
                compare(c,c->a,65,true);s.cycles+=2;
                if(!c->c) {s.cycles+=3;control_return(&s,r);}
                else {s.cycles+=2;control_call(&s,0x94d4,0x9468);}
            }
        }
        break;
    }
    case 0x9504:case 0x9599: {
        /* Mature neighbours merge in the original east-before-south order.
         * Keep the two redraw calls separate: the first footprint may change
         * the second call's power and occupancy tests. */
        bool commercial=entry==0x9599;c->mf=c->xf=false;s.cycles+=3;
        load(c,word(r,0xb89));s.cycles+=5;
        unsigned owner=commercial?0x1ef:0x120;
        compare(c,c->a,owner,false);s.cycles+=3;
        if(!c->z) {s.cycles+=3;control_return(&s,r);break;}
        s.cycles+=2;c->x=(uint16_t)word(r,0xb49);nz(c,c->x,false);s.cycles+=5;
        int logical=(int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+6;
        unsigned neighbor=w->map_anchor!=UINT32_MAX && logical>=0 &&
            (unsigned)logical+2<=ScWorldCells(w)*2?word(w->tiles,logical):0;
        load(c,neighbor&1023);s.cycles+=6+3;
        compare(c,c->a,owner,false);s.cycles+=3;
        if(c->z) {
            s.cycles+=2;load(c,commercial?0x39a:0x376);s.cycles+=3;
            control_call(&s,commercial?0x95b5:0x9520,0x9940);break;
        }
        s.cycles+=3;
        logical=(int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+6*ScWorldWidth(w);
        neighbor=w->map_anchor!=UINT32_MAX && logical>=0 &&
            (unsigned)logical+2<=ScWorldCells(w)*2?word(w->tiles,logical):0;
        load(c,neighbor&1023);s.cycles+=6+3;
        compare(c,c->a,owner,false);s.cycles+=3;
        if(!c->z) {s.cycles+=3;control_return(&s,r);break;}
        s.cycles+=2;load(c,commercial?0x3ac:0x388);s.cycles+=3;
        control_call(&s,commercial?0x95e0:0x954b,0x9940);break;
    }
    case 0x9523:case 0x954e:case 0x95b8:case 0x95e3: {
        if(c->mf) return 0;
        bool commercial=entry>=0x95b8,vertical=entry==0x954e || entry==0x95e3;
        load(c,word(r,0xb85));s.cycles+=5;control_push(&s,c->a);s.cycles+=4;
        load(c,word(r,0xb85));s.cycles+=5;c->c=false;s.cycles+=2;
        add(c,vertical?0x300:3,false);s.cycles+=3;control_word(&s,0xb85,c->a);s.cycles+=5;
        load(c,commercial?(vertical?0x3b5:0x3a3):(vertical?0x391:0x37f));s.cycles+=3;
        control_call(&s,commercial?(vertical?0x95f4:0x95c9):(vertical?0x955f:0x9534),0x9940);
        break;
    }
    case 0x9537:case 0x9562:case 0x95cc:case 0x95f7:
        if(c->mf) return 0;
        load(c,word(r,c->sp+1));c->sp+=2;s.cycles+=5;
        control_word(&s,0xb85,c->a);s.cycles+=5;control_return(&s,r);break;
    case 0x95fc:case 0x9794: {
        bool decline=entry==0x9794;c->mf=false;s.cycles+=3;
        load(c,word(r,c->dp));s.cycles+=4+dp;
        compare(c,c->a,decline?2:4,false);s.cycles+=3;
        if(decline?!c->c:c->c) {
            s.cycles+=3;
            if(!decline) {control_return(&s,r);break;}
            compare(c,c->a,1,false);s.cycles+=3;
            if(!c->z) {s.cycles+=3;control_return(&s,r);}
            else {s.cycles+=2;load(c,0x1f4);s.cycles+=3;control_call(&s,0x97be,0x9940);}
        } else {
            s.cycles+=2;if(!decline) s.cycles+=3;
            control_call(&s,decline?0x979d:0x9607,0x907e);
        }
        break;
    }
    case 0x974b:case 0x9659: {
        bool residential=entry==0x9659;c->mf=false;s.cycles+=3;
        load(c,word(r,0xb89));s.cycles+=5;
        const unsigned checks_res[]={0x37a,0x38c,0x383,0x395};
        const unsigned checks_com[]={0x39e,0x3b0,0x3a7,0x3b9};
        const unsigned *checks=residential?checks_res:checks_com;
        bool matched=false;
        for(unsigned i=0;i<4;++i) {
            compare(c,c->a,checks[i],false);s.cycles+=3;
            if(c->z) {
                s.cycles+=3;matched=true;
                if(i<2) {load(c,residential?0x11c:0x1eb);s.cycles+=3+3;c->pc=0x9940;c->cyclesUsed=3;}
                else {if(residential) s.cycles+=3;control_return(&s,r);}
                break;
            }
            s.cycles+=2;
        }
        if(matched) break;
        s.cycles+=3;c->mf=residential;
        load(c,residential?r[c->dp]:word(r,c->dp));s.cycles+=residential?3+dp:4+dp;
        if(residential) {
            if(c->z) {s.cycles+=3+3;control_return(&s,r);break;}
            s.cycles+=2;compare(c,c->a,16,true);s.cycles+=2;
            if(c->z) {s.cycles+=2;load(c,248);s.cycles+=2;control_call(&s,0x967e,0x961d);}
            else {
                s.cycles+=3;
                if(!c->c) {s.cycles+=3;load(c,255);s.cycles+=2;control_call(&s,0x96f5,0x961d);}
                else {s.cycles+=2+3;control_call(&s,0x96db,0x9468);}
            }
        } else {
            compare(c,c->a,2,false);s.cycles+=3;
            if(c->c) {s.cycles+=2;control_call(&s,0x976d,0x9468);}
            else {
                s.cycles+=3;compare(c,c->a,1,false);s.cycles+=3;
                if(!c->z) {s.cycles+=3;control_return(&s,r);}
                else {s.cycles+=2;load(c,0x137);s.cycles+=3;control_call(&s,0x9789,0x9940);}
            }
        }
        break;
    }
    case 0x94d7:case 0x94f2:case 0x958b:case 0x9770:case 0x96de: {
        if(!c->mf) return 0;
        c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);s.cycles+=3;
        if(entry==0x94d7) {load(c,0);s.cycles+=2;control_call(&s,0x94da,0x98b8);}
        else {
            load(c,r[c->dp]);s.cycles+=3+dp;
            if(entry==0x96de) {c->c=true;add(c,24,true);s.cycles+=2+2;}
            if(entry==0x94f2 || entry==0x96de) {
                unsigned value=c->a&255;c->c=(value&4)!=0;load(c,value>>3);s.cycles+=6;
            }
            unsigned decrements=entry==0x94f2?1:entry==0x9770?2:0;
            for(unsigned i=0;i<decrements;++i) {load(c,c->a-1);s.cycles+=2;}
            control_call(&s,entry==0x94f2?0x94f9:entry==0x96de?0x96e7:entry==0x958b?0x958e:0x9775,
                entry==0x94f2 || entry==0x96de?0x98b8:0x98dd);
        }
        break;
    }
    case 0x960a:case 0x97a0: {
        if(c->mf) return 0;load(c,c->a&1);s.cycles+=3;
        c->mf=true;s.cycles+=3;c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);s.cycles+=3;
        load(c,r[c->dp]);s.cycles+=3+dp;
        if(entry==0x97a0) {load(c,c->a-1);load(c,c->a-1);s.cycles+=4;}
        control_call(&s,entry==0x960a?0x9612:0x97aa,0x9906);break;
    }
    case 0x94c0:case 0x94dd:case 0x94fc:case 0x9591:case 0x9615:
    case 0x9778:case 0x978c:case 0x97ad:case 0x97c1:case 0x96ea: {
        if(entry==0x94c0) {c->x=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->x,false);s.cycles+=5;}
        c->mf=true;s.cycles+=3;
        load(c,entry==0x94c0?1:entry>=0x9778 || entry==0x96ea?248:8);s.cycles+=2;
        unsigned call=entry==0x94c0?0x94c5:entry==0x94dd?0x94e1:entry==0x94fc?0x9500:
            entry==0x9591?0x9595:entry==0x9615?0x9619:entry==0x96ea?0x96ee:entry==0x9778?0x977c:
            entry==0x978c?0x9790:entry==0x97ad?0x97b1:0x97c5;
        control_call(&s,call,0x961d);break;
    }
    case 0x94c8:case 0x94e4:case 0x977f:case 0x97b4:case 0x96d5:case 0x96f1:
        s.cycles+=3;control_return(&s,r);break;
    case 0x9503:case 0x9566:case 0x9598:case 0x95fb:case 0x961c:case 0x9732:case 0x9793:case 0x97c8:
        control_return(&s,r);break;
    default:return 0;
    }
    if(s.cycles>budget) return 0;
    for(unsigned i=0;i<s.count;++i) put(r,s.address[i],s.value[i]);
    if(s.field) {put(r,0xb3f,s.field_x);put(r,0xb3d,s.field_y);w->field_anchor[0]=s.field_index;}
    *cpu=s.cpu;return s.cycles;
}
