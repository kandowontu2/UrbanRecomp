#include "sc_smoothing.h"
#include "sc_world_guest.h"
#include "sc_native_specialize.h"
#include "snes/interp816.h"
#include <stdlib.h>

/* Complete alternating-field smoothing, setup and coordinate control
 * in direct C. Preserve ordered writes, flags, stack shadows and the
 * exact caller deadline. Shared full-coordinate hooks stay at their original
 * boundaries. Whole-field GPU publication retains the same beam deadlines. */
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
bool ScSmoothingOwns(uint16_t pc) {
    return pc>=0xa02f && pc<0xa13b;
}
SC_NATIVE_SPECIALIZE unsigned execute(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget,bool single) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_SMOOTHING_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || !rom || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x20 || c->dp>0x1fc0) return 0;
    unsigned cycles=0,cost,addr,value;
    for(;;) {
        if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
        switch(c->pc) {
        case 0xa29a:
            if(6>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0xa2b8:
            if(6>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0xa02f: native_a02f: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa031;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a031;
        case 0xa031: native_a031: /* PHD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,c->dp);c->sp-=2;
            c->pc=0xa032;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a032;
        case 0xa032: native_a032: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xa033;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a033;
        case 0xa033: native_a033: /* TDC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->a=c->dp;nz(c,c->a,false);
            c->pc=0xa034;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a034;
        case 0xa034: native_a034: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0xa035;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a035;
        case 0xa035: native_a035: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0006,true);
            c->pc=0xa038;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a038;
        case 0xa038: native_a038: /* TCD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->dp=c->a;nz(c,c->dp,false);
            c->pc=0xa039;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a039;
        case 0xa039: native_a039: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xa03a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a03a;
        case 0xa03a: native_a03a: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->xf=false;
            c->pc=0xa03c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a03c;
        case 0xa03c: native_a03c: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa03e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a03e;
        case 0xa03e: native_a03e: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa040;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a040;
        case 0xa040: native_a040: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestSmoothingStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=3;
            if(cost>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            c->mf=true;
            c->pc=0xa042;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a042;
        case 0xa042: native_a042: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa044;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a044;
        case 0xa044: native_a044: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0xa045;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a045;
        case 0xa045: native_a045: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa047;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a047;
        case 0xa047: native_a047: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa049);c->sp-=2;c->pc=0xa29a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xa04a: native_a04a: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa04c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a04c;
        case 0xa04c: native_a04c: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0xa04e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a04e;
        case 0xa04e: native_a04e: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa050;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a050;
        case 0xa050: native_a050: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa057:0xa052;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a057;
            goto native_a052;
        case 0xa052: native_a052: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa053;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a053;
        case 0xa053: native_a053: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-1)<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-1)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,13) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-1;
            add(c,(c->mf?w->fields[13][addr]:word(w->fields[13],addr)),false);
            c->pc=0xa057;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a057;
        case 0xa057: native_a057: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,ScWorldFieldWidth(w,13)-1,false);
            c->pc=0xa05a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a05a;
        case 0xa05a: native_a05a: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa065:0xa05c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a065;
            goto native_a05c;
        case 0xa05c: native_a05c: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa05d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a05d;
        case 0xa05d: native_a05d: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+1)<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+1)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,13) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+1;
            add(c,(c->mf?w->fields[13][addr]:word(w->fields[13],addr)),false);
            c->pc=0xa061;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a061;
        case 0xa061: native_a061: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa065:0xa063;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a065;
            goto native_a063;
        case 0xa063: native_a063: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa065;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a065;
        case 0xa065: native_a065: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa067;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a067;
        case 0xa067: native_a067: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa072:0xa069;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a072;
            goto native_a069;
        case 0xa069: native_a069: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa06a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a06a;
        case 0xa06a: native_a06a: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-(int)ScWorldFieldWidth(w,13))<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-(int)ScWorldFieldWidth(w,13))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,13) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-(int)ScWorldFieldWidth(w,13);
            add(c,(c->mf?w->fields[13][addr]:word(w->fields[13],addr)),false);
            c->pc=0xa06e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a06e;
        case 0xa06e: native_a06e: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa072:0xa070;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a072;
            goto native_a070;
        case 0xa070: native_a070: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa072;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a072;
        case 0xa072: native_a072: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,ScWorldFieldHeight(w,13)-1,false);
            c->pc=0xa075;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a075;
        case 0xa075: native_a075: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa080:0xa077;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a080;
            goto native_a077;
        case 0xa077: native_a077: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa078;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a078;
        case 0xa078: native_a078: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(int)ScWorldFieldWidth(w,13))<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(int)ScWorldFieldWidth(w,13))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,13) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(int)ScWorldFieldWidth(w,13);
            add(c,(c->mf?w->fields[13][addr]:word(w->fields[13],addr)),false);
            c->pc=0xa07c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a07c;
        case 0xa07c: native_a07c: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa080:0xa07e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a080;
            goto native_a07e;
        case 0xa07e: native_a07e: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa080;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a080;
        case 0xa080: native_a080: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa081;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a081;
        case 0xa081: native_a081: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x))<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,13) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x);
            add(c,(c->mf?w->fields[13][addr]:word(w->fields[13],addr)),false);
            c->pc=0xa085;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a085;
        case 0xa085: native_a085: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa089:0xa087;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a089;
            goto native_a087;
        case 0xa087: native_a087: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa089;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a089;
        case 0xa089: native_a089: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa08b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a08b;
        case 0xa08b: native_a08b: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa08d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a08d;
        case 0xa08d: native_a08d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa08f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a08f;
        case 0xa08f: native_a08f: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa090;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a090;
        case 0xa090: native_a090: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa091;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a091;
        case 0xa091: native_a091: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x00fa,c->mf);
            c->pc=0xa094;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a094;
        case 0xa094: native_a094: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa099:0xa096;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a099;
            goto native_a096;
        case 0xa096: native_a096: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x00fa);
            c->pc=0xa099;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a099;
        case 0xa099: native_a099: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0xa09b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a09b;
        case 0xa09b: native_a09b: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x))<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,14) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x);
            if(c->mf) w->fields[14][addr]=(uint8_t)c->a;else put(w->fields[14],addr,c->a);
            c->pc=0xa09f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a09f;
        case 0xa09f: native_a09f: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestSmoothingStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa0a1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0a1;
        case 0xa0a1: native_a0a1: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa0a3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0a3;
        case 0xa0a3: native_a0a3: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa0a5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0a5;
        case 0xa0a5: native_a0a5: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldFieldWidth(w,13),c->mf);
            c->pc=0xa0a8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0a8;
        case 0xa0a8: native_a0a8: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa040:0xa0aa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a040;
            goto native_a0aa;
        case 0xa0aa: native_a0aa: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa0ac;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0ac;
        case 0xa0ac: native_a0ac: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa0ae;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0ae;
        case 0xa0ae: native_a0ae: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldFieldHeight(w,13),c->mf);
            c->pc=0xa0b1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0b1;
        case 0xa0b1: native_a0b1: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa03e:0xa0b3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a03e;
            goto native_a0b3;
        case 0xa0b3: native_a0b3: /* PLD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5;
            if(cost>budget-cycles) return cycles;
            c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);
            c->pc=0xa0b4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0b4;
        case 0xa0b4: native_a0b4: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            return cycles;
        case 0xa0b5: native_a0b5: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa0b7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0b7;
        case 0xa0b7: native_a0b7: /* PHD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,c->dp);c->sp-=2;
            c->pc=0xa0b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0b8;
        case 0xa0b8: native_a0b8: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xa0b9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0b9;
        case 0xa0b9: native_a0b9: /* TDC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->a=c->dp;nz(c,c->a,false);
            c->pc=0xa0ba;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0ba;
        case 0xa0ba: native_a0ba: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0xa0bb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0bb;
        case 0xa0bb: native_a0bb: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0006,true);
            c->pc=0xa0be;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0be;
        case 0xa0be: native_a0be: /* TCD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->dp=c->a;nz(c,c->dp,false);
            c->pc=0xa0bf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0bf;
        case 0xa0bf: native_a0bf: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xa0c0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0c0;
        case 0xa0c0: native_a0c0: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->xf=false;
            c->pc=0xa0c2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0c2;
        case 0xa0c2: native_a0c2: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa0c4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0c4;
        case 0xa0c4: native_a0c4: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa0c6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0c6;
        case 0xa0c6: native_a0c6: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestSmoothingStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=3;
            if(cost>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            c->mf=true;
            c->pc=0xa0c8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0c8;
        case 0xa0c8: native_a0c8: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa0ca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0ca;
        case 0xa0ca: native_a0ca: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0xa0cb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0cb;
        case 0xa0cb: native_a0cb: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa0cd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0cd;
        case 0xa0cd: native_a0cd: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa0cf);c->sp-=2;c->pc=0xa29a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xa0d0: native_a0d0: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa0d2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0d2;
        case 0xa0d2: native_a0d2: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0xa0d4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0d4;
        case 0xa0d4: native_a0d4: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa0d6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0d6;
        case 0xa0d6: native_a0d6: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa0dd:0xa0d8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a0dd;
            goto native_a0d8;
        case 0xa0d8: native_a0d8: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa0d9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0d9;
        case 0xa0d9: native_a0d9: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-1)<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-1)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,14) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-1;
            add(c,(c->mf?w->fields[14][addr]:word(w->fields[14],addr)),false);
            c->pc=0xa0dd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0dd;
        case 0xa0dd: native_a0dd: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,ScWorldFieldWidth(w,13)-1,false);
            c->pc=0xa0e0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0e0;
        case 0xa0e0: native_a0e0: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa0eb:0xa0e2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a0eb;
            goto native_a0e2;
        case 0xa0e2: native_a0e2: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa0e3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0e3;
        case 0xa0e3: native_a0e3: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+1)<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+1)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,14) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+1;
            add(c,(c->mf?w->fields[14][addr]:word(w->fields[14],addr)),false);
            c->pc=0xa0e7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0e7;
        case 0xa0e7: native_a0e7: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa0eb:0xa0e9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a0eb;
            goto native_a0e9;
        case 0xa0e9: native_a0e9: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa0eb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0eb;
        case 0xa0eb: native_a0eb: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa0ed;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0ed;
        case 0xa0ed: native_a0ed: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa0f8:0xa0ef;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a0f8;
            goto native_a0ef;
        case 0xa0ef: native_a0ef: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa0f0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0f0;
        case 0xa0f0: native_a0f0: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-(int)ScWorldFieldWidth(w,13))<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-(int)ScWorldFieldWidth(w,13))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,14) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)-(int)ScWorldFieldWidth(w,13);
            add(c,(c->mf?w->fields[14][addr]:word(w->fields[14],addr)),false);
            c->pc=0xa0f4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0f4;
        case 0xa0f4: native_a0f4: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa0f8:0xa0f6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a0f8;
            goto native_a0f6;
        case 0xa0f6: native_a0f6: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa0f8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0f8;
        case 0xa0f8: native_a0f8: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,ScWorldFieldHeight(w,13)-1,false);
            c->pc=0xa0fb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0fb;
        case 0xa0fb: native_a0fb: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa106:0xa0fd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a106;
            goto native_a0fd;
        case 0xa0fd: native_a0fd: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa0fe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a0fe;
        case 0xa0fe: native_a0fe: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(int)ScWorldFieldWidth(w,13))<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(int)ScWorldFieldWidth(w,13))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,14) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(int)ScWorldFieldWidth(w,13);
            add(c,(c->mf?w->fields[14][addr]:word(w->fields[14],addr)),false);
            c->pc=0xa102;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a102;
        case 0xa102: native_a102: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa106:0xa104;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a106;
            goto native_a104;
        case 0xa104: native_a104: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa106;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a106;
        case 0xa106: native_a106: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa107;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a107;
        case 0xa107: native_a107: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x))<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,14) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x);
            add(c,(c->mf?w->fields[14][addr]:word(w->fields[14],addr)),false);
            c->pc=0xa10b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a10b;
        case 0xa10b: native_a10b: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa10f:0xa10d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a10f;
            goto native_a10d;
        case 0xa10d: native_a10d: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0005)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0005)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa10f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a10f;
        case 0xa10f: native_a10f: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa111;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a111;
        case 0xa111: native_a111: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa113;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a113;
        case 0xa113: native_a113: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa115;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a115;
        case 0xa115: native_a115: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa116;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a116;
        case 0xa116: native_a116: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa117;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a117;
        case 0xa117: native_a117: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x00fa,c->mf);
            c->pc=0xa11a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a11a;
        case 0xa11a: native_a11a: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa11f:0xa11c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_a11f;
            goto native_a11c;
        case 0xa11c: native_a11c: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x00fa);
            c->pc=0xa11f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a11f;
        case 0xa11f: native_a11f: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0xa121;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a121;
        case 0xa121: native_a121: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x))<0 || (unsigned)((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,13) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x);
            if(c->mf) w->fields[13][addr]=(uint8_t)c->a;else put(w->fields[13],addr,c->a);
            c->pc=0xa125;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a125;
        case 0xa125: native_a125: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestSmoothingStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa127;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a127;
        case 0xa127: native_a127: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa129;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a129;
        case 0xa129: native_a129: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa12b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a12b;
        case 0xa12b: native_a12b: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldFieldWidth(w,13),c->mf);
            c->pc=0xa12e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a12e;
        case 0xa12e: native_a12e: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa0c6:0xa130;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a0c6;
            goto native_a130;
        case 0xa130: native_a130: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa132;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a132;
        case 0xa132: native_a132: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa134;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a134;
        case 0xa134: native_a134: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldFieldHeight(w,13),c->mf);
            c->pc=0xa137;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a137;
        case 0xa137: native_a137: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa0c4:0xa139;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a0c4;
            goto native_a139;
        case 0xa139: native_a139: /* PLD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5;
            if(cost>budget-cycles) return cycles;
            c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);
            c->pc=0xa13a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a13a;
        case 0xa13a: native_a13a: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            return cycles;
        default:return cycles;
        }
    }
}

unsigned ScSmoothingStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned budget) {
    return execute(w,c,r,rom,budget,false);
}
unsigned ScSmoothingInstructionStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom) {
    return execute(w,c,r,rom,12,true);
}
