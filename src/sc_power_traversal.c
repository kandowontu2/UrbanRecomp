#include "sc_power_traversal.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <stdlib.h>

/* Connected electrical traversal, branch stack, neighbour tests and bitmap
 * updates in direct C. Preserve ordered writes, flags, stack shadows and the
 * exact caller deadline. Shared full-coordinate hooks stay at their original
 * boundaries. Final network publication remains with the main scheduler. */
static unsigned word(const uint8_t *r,unsigned p) {return r[p]|(unsigned)r[p+1]<<8;}
static uint64_t stage_spans,stage_clocks;
uint64_t ScPowerTraversalStageSpans(void) {return stage_spans;}
uint64_t ScPowerTraversalStageClocks(void) {return stage_clocks;}
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
bool ScPowerTraversalOwns(uint16_t pc) {
    return (pc>=0xaffd && pc<0xb0dd) || (pc>=0xb0e5 && pc<0xb120) || pc==0xb151;
}
static unsigned execute(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget,bool single) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_POWER_CONTINUATION_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || !rom || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x20 || c->dp>0x1fc0) return 0;
    unsigned cycles=0,cost,addr,value;
    for(;;) {
        if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
        switch(c->pc) {
        case 0x849e:case 0x8ff4:case 0xb120:
            if(c->pc==0x8ff4 && !w->huge) return cycles;
            if(6>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0x84c3:case 0x9034:
            if(6>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0xaffd: native_affd: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestPowerStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;++stage_spans;stage_clocks+=cost;break;}
            cost=4+(c->mf?0:1);
            if(0x0c57>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0c57;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb000;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b000;
        case 0xb000: native_b000: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestPowerStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;++stage_spans;stage_clocks+=cost;break;}
            cost=4+(c->mf?0:1);
            if(0x0c59>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0c59;
            compare(c,c->a,(c->mf?r[addr]:word(r,addr)),c->mf);
            c->pc=0xb003;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b003;
        case 0xb003: native_b003: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xb06a:0xb005;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_b06a;
            goto native_b005;
        case 0xb005: native_b005: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestPowerStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;++stage_spans;stage_clocks+=cost;break;}
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb007);c->sp-=2;c->pc=0xb0a3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0a3;
        case 0xb008: native_b008: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0004);
            c->pc=0xb00b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b00b;
        case 0xb00b: native_b00b: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xb00d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b00d;
        case 0xb00d: native_b00d: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestPowerStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;++stage_spans;stage_clocks+=cost;break;}
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0012)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0012)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xb00f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b00f;
        case 0xb00f: native_b00f: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xb013:0xb011;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_b013;
            goto native_b011;
        case 0xb011: native_b011: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0014)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0014)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xb013;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b013;
        case 0xb013: native_b013: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000e)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb015;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b015;
        case 0xb015: native_b015: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0012)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0012)&65535);
            compare(c,c->a,(c->mf?r[addr]:word(r,addr)),c->mf);
            c->pc=0xb017;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b017;
        case 0xb017: native_b017: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb019;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b019;
        case 0xb019: native_b019: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0014)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0014)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),true);
            c->pc=0xb01b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b01b;
        case 0xb01b: native_b01b: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xb031:0xb01d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_b031;
            goto native_b01d;
        case 0xb01d: native_b01d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x001a);
            c->pc=0xb020;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b020;
        case 0xb020: native_b020: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0381>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0381;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xb023;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b023;
        case 0xb023: native_b023: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0xb026;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b026;
        case 0xb026: native_b026: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0383>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0383;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xb029;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b029;
        case 0xb029: native_b029: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x012c);
            c->pc=0xb02c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b02c;
        case 0xb02c: native_b02c: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x038b>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x038b;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xb02f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b02f;
        case 0xb02f: native_b02f: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0xb06a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b06a;
        case 0xb031: native_b031: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb033;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b033;
        case 0xb033: native_b033: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb035);c->sp-=2;c->pc=0x8ff4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb036: native_b036: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestPowerStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;++stage_spans;stage_clocks+=cost;break;}
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb038);c->sp-=2;c->pc=0xb0e5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0e5;
        case 0xb039: native_b039: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xb03b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b03b;
        case 0xb03b: native_b03b: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xb03d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b03d;
        case 0xb03d: native_b03d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestPowerStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;++stage_spans;stage_clocks+=cost;break;}
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb03f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b03f;
        case 0xb03f: native_b03f: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0xb042;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b042;
        case 0xb042: native_b042: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xb05a:0xb044;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_b05a;
            goto native_b044;
        case 0xb044: native_b044: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb046;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b046;
        case 0xb046: native_b046: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0004,c->mf);
            c->pc=0xb049;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b049;
        case 0xb049: native_b049: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xb05a:0xb04b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_b05a;
            goto native_b04b;
        case 0xb04b: native_b04b: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb04d);c->sp-=2;c->pc=0xb06c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b06c;
        case 0xb04e: native_b04e: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xb056:0xb050;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_b056;
            goto native_b050;
        case 0xb050: native_b050: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xb052;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b052;
        case 0xb052: native_b052: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb054;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b054;
        case 0xb054: native_b054: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xb056;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b056;
        case 0xb056: native_b056: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xb058;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b058;
        case 0xb058: native_b058: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0xb03d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b03d;
        case 0xb05a: native_b05a: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestPowerStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;++stage_spans;stage_clocks+=cost;break;}
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb05c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b05c;
        case 0xb05c: native_b05c: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0xb05f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b05f;
        case 0xb05f: native_b05f: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xb064:0xb061;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_b064;
            goto native_b061;
        case 0xb061: native_b061: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb063);c->sp-=2;c->pc=0xb0be;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0be;
        case 0xb064: native_b064: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb066;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b066;
        case 0xb066: native_b066: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xb00d:0xb068;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_b00d;
            goto native_b068;
        case 0xb068: native_b068: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0xaffd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_affd;
        case 0xb06a: native_b06a: /* PLD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5;
            if(cost>budget-cycles) return cycles;
            c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);
            c->pc=0xb06b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b06b;
        case 0xb06b: native_b06b: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb06c: native_b06c: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestPowerStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;++stage_spans;stage_clocks+=cost;break;}
            cost=4+(c->xf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0xb06f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b06f;
        case 0xb06f: native_b06f: /* STX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0016)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0016)&65535);
            put(r,addr,c->x);
            c->pc=0xb071;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b071;
        case 0xb071: native_b071: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb073);c->sp-=2;c->pc=0x8ff4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb074: native_b074: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xb09a:0xb076;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_b09a;
            goto native_b076;
        case 0xb076: native_b076: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb078);c->sp-=2;c->pc=0xb0f8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0f8;
        case 0xb079: native_b079: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x0000,false);
            c->pc=0xb07c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b07c;
        case 0xb07c: native_b07c: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xb09a:0xb07e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_b09a;
            goto native_b07e;
        case 0xb07e: native_b07e: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb081;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b081;
        case 0xb081: native_b081: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb083);c->sp-=2;c->pc=0x849e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb084: native_b084: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x03ff);
            c->pc=0xb087;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b087;
        case 0xb087: native_b087: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xb088;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b088;
        case 0xb088: native_b088: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0xb08a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b08a;
        case 0xb08a: native_b08a: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0x84eb&0xff)+c->x>255)+(c->mf?0:1);
            if(c->x>=1024 || cost>budget-cycles) return cycles;
            load(c,(c->mf?rom[0x10000+(0x84eb + c->x)]:word(rom,0x10000+(0x84eb + c->x))));
            c->pc=0xb08d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b08d;
        case 0xb08d: native_b08d: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0xb08f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b08f;
        case 0xb08f: native_b08f: /* BPL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->n?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->n?0xb09a:0xb091;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->n) goto native_b09a;
            goto native_b091;
        case 0xb091: native_b091: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0016)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0016)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb093;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b093;
        case 0xb093: native_b093: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xb096;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b096;
        case 0xb096: native_b096: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0xb099;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b099;
        case 0xb099: native_b099: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb09a: native_b09a: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0016)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0016)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb09c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b09c;
        case 0xb09c: native_b09c: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xb09f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b09f;
        case 0xb09f: native_b09f: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0xb0a2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0a2;
        case 0xb0a2: native_b0a2: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb0a3: native_b0a3: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            if(w->huge) {
                if(6>budget-cycles) return cycles;
                ScWorldGuestStepPrepared(w,c,r);
                c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
            }
            cost=4+(c->xf?0:1);
            if(0x0c57>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0c57;
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0xb0a6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0a6;
        case 0xb0a6: native_b0a6: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xb0bd:0xb0a8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_b0bd;
            goto native_b0a8;
        case 0xb0a8: native_b0a8: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0xb0aa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0aa;
        case 0xb0aa: native_b0aa: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((int)c->x<0 || (unsigned)(int)c->x+(c->mf?1:2)>ScWorldFieldSizeWorld(w,17) || cost>budget-cycles) return cycles;
            addr=(int)c->x;
            load(c,(c->mf?w->fields[17][addr]:word(w->fields[17],addr)));
            c->pc=0xb0ae;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0ae;
        case 0xb0ae: native_b0ae: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xb0b1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0b1;
        case 0xb0b1: native_b0b1: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((int)c->x<0 || (unsigned)(int)c->x+(c->mf?1:2)>ScWorldFieldSizeWorld(w,18) || cost>budget-cycles) return cycles;
            addr=(int)c->x;
            load(c,(c->mf?w->fields[18][addr]:word(w->fields[18],addr)));
            c->pc=0xb0b5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0b5;
        case 0xb0b5: native_b0b5: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xb0b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0b8;
        case 0xb0b8: native_b0b8: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0xb0ba;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0ba;
        case 0xb0ba: native_b0ba: /* DEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6+(c->mf?0:2);
            if(0x0c57>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0c57;
            value=(c->mf?r[addr]:word(r,addr));
            --value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xb0bd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0bd;
        case 0xb0bd: native_b0bd: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb0be: native_b0be: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            if(w->huge) {
                if(6>budget-cycles) return cycles;
                ScWorldGuestStepPrepared(w,c,r);
                c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
            }
            cost=4+(c->xf?0:1);
            if(0x0c57>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0c57;
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0xb0c1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0c1;
        case 0xb0c1: native_b0c1: /* CPX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->x,ScWorldFieldWidth(w,17)-1,false);
            c->pc=0xb0c4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0c4;
        case 0xb0c4: native_b0c4: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xb0dc:0xb0c6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_b0dc;
            goto native_b0c6;
        case 0xb0c6: native_b0c6: /* INX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->x;nz(c,c->x,false);
            c->pc=0xb0c7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0c7;
        case 0xb0c7: native_b0c7: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0xb0c9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0c9;
        case 0xb0c9: native_b0c9: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb0cc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0cc;
        case 0xb0cc: native_b0cc: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((int)c->x<0 || (unsigned)(int)c->x+(c->mf?1:2)>ScWorldFieldSizeWorld(w,17) || cost>budget-cycles) return cycles;
            addr=(int)c->x;
            if(c->mf) w->fields[17][addr]=(uint8_t)c->a;else put(w->fields[17],addr,c->a);
            c->pc=0xb0d0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0d0;
        case 0xb0d0: native_b0d0: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb0d3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0d3;
        case 0xb0d3: native_b0d3: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((int)c->x<0 || (unsigned)(int)c->x+(c->mf?1:2)>ScWorldFieldSizeWorld(w,18) || cost>budget-cycles) return cycles;
            addr=(int)c->x;
            if(c->mf) w->fields[18][addr]=(uint8_t)c->a;else put(w->fields[18],addr,c->a);
            c->pc=0xb0d7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0d7;
        case 0xb0d7: native_b0d7: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0xb0d9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0d9;
        case 0xb0d9: native_b0d9: /* STX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(0x0c57>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0c57;
            put(r,addr,c->x);
            c->pc=0xb0dc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0dc;
        case 0xb0dc: native_b0dc: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb0e5: native_b0e5: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestPowerStageStep(w,c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;++stage_spans;stage_clocks+=cost;break;}
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb0e7);c->sp-=2;c->pc=0xb120;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb0e8: native_b0e8: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0xb0ea;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0ea;
        case 0xb0ea: native_b0ea: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)+(int16_t)(c->x-(uint16_t)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)):(int)c->x)<0 || (unsigned)(w->giant?(int)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)+(int16_t)(c->x-(uint16_t)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,5) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)+(int16_t)(c->x-(uint16_t)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)):(int)c->x);
            load(c,(c->mf?w->fields[5][addr]:word(w->fields[5],addr)));
            c->pc=0xb0ee;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0ee;
        case 0xb0ee: native_b0ee: /* ORA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xb0dd&255)+c->y>255)+(c->mf?0:1);
            if(c->y>7 || cost>budget-cycles) return cycles;
            load(c,c->a|(c->mf?rom[0x10000+0xb0dd+c->y]:word(rom,0x10000+0xb0dd+c->y)));
            c->pc=0xb0f1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0f1;
        case 0xb0f1: native_b0f1: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)+(int16_t)(c->x-(uint16_t)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)):(int)c->x)<0 || (unsigned)(w->giant?(int)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)+(int16_t)(c->x-(uint16_t)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,5) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)+(int16_t)(c->x-(uint16_t)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)):(int)c->x);
            if(c->mf) w->fields[5][addr]=(uint8_t)c->a;else put(w->fields[5],addr,c->a);
            c->pc=0xb0f5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0f5;
        case 0xb0f5: native_b0f5: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0xb0f7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0f7;
        case 0xb0f7: native_b0f7: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb0f8: native_b0f8: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0001;nz(c,c->y,false);
            c->pc=0xb0fb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0fb;
        case 0xb0fb: native_b0fb: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xb0fe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b0fe;
        case 0xb0fe: native_b0fe: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x027c,c->mf);
            c->pc=0xb101;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b101;
        case 0xb101: native_b101: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xb11f:0xb103;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_b11f;
            goto native_b103;
        case 0xb103: native_b103: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x028c,c->mf);
            c->pc=0xb106;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b106;
        case 0xb106: native_b106: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xb11f:0xb108;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_b11f;
            goto native_b108;
        case 0xb108: native_b108: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xb10a);c->sp-=2;c->pc=0xb120;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb10b: native_b10b: /* CPX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            if(w->giant) {
                cost=ScWorldContains(w,w->coord[2][0],w->coord[2][1])?2:3;
                if(cost>budget-cycles) return cycles;
                ScWorldGuestStepPrepared(w,c,r);goto native_b10e;
            }
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->x,ScWorldFieldSizeWorld(w,5),false);
            c->pc=0xb10e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b10e;
        case 0xb10e: native_b10e: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xb11f:0xb110;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_b11f;
            goto native_b110;
        case 0xb110: native_b110: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0xb112;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b112;
        case 0xb112: native_b112: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)+(int16_t)(c->x-(uint16_t)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)):(int)c->x)<0 || (unsigned)(w->giant?(int)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)+(int16_t)(c->x-(uint16_t)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,5) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)+(int16_t)(c->x-(uint16_t)(((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8)):(int)c->x);
            load(c,(c->mf?w->fields[5][addr]:word(w->fields[5],addr)));
            c->pc=0xb116;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b116;
        case 0xb116: native_b116: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xb0dd&255)+c->y>255)+(c->mf?0:1);
            if(c->y>7 || cost>budget-cycles) return cycles;
            load(c,c->a&(c->mf?rom[0x10000+0xb0dd+c->y]:word(rom,0x10000+0xb0dd+c->y)));
            c->pc=0xb119;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b119;
        case 0xb119: native_b119: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0xb11b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b11b;
        case 0xb11b: native_b11b: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x00ff);
            c->pc=0xb11e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b11e;
        case 0xb11e: native_b11e: /* TAY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->y=c->a;nz(c,c->y,false);
            c->pc=0xb11f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_b11f;
        case 0xb11f: native_b11f: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xb151: native_b151: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
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

/* The scheduler permits one original instruction to cross a beam event.
 * Atomic C uses that same rule; bounded batches never exceed their budget. */
unsigned ScPowerTraversalStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget) {
    return execute(w,c,r,rom,budget,false);
}
unsigned ScPowerTraversalInstructionStep(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom) {
    return execute(w,c,r,rom,12,true);
}
