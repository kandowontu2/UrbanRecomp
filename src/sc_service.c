#include "sc_service.h"
#include "sc_world_guest.h"
#include "sc_native_specialize.h"
#include "snes/interp816.h"
#include <stdlib.h>

/* Complete police/fire word-field smoothing, setup, coordinate control
 * and publication in direct C. Preserve ordered writes, flags, stack shadows and the
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
bool ScServiceOwns(uint16_t pc) {
    return pc>=0xa14d && pc<0xa24b;
}
SC_NATIVE_SPECIALIZE unsigned execute(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget,bool single) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_SERVICE_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || !rom || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x20 || c->dp>0x1fc0) return 0;
    unsigned cycles=0,cost,addr,value;
    for(;;) {
        if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
        switch(c->pc) {
        case 0xa2d7:
            if(6>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0xa2f4:
            if(6>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0xa14d: native_a14d: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa14f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a14f;
        case 0xa14f: native_a14f: /* PHD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,c->dp);c->sp-=2;
            c->pc=0xa150;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a150;
        case 0xa150: native_a150: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xa151;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a151;
        case 0xa151: native_a151: /* TDC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->a=c->dp;nz(c,c->a,false);
            c->pc=0xa152;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a152;
        case 0xa152: native_a152: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0xa153;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a153;
        case 0xa153: native_a153: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0004,true);
            c->pc=0xa156;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a156;
        case 0xa156: native_a156: /* TCD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->dp=c->a;nz(c,c->dp,false);
            c->pc=0xa157;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a157;
        case 0xa157: native_a157: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xa158;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a158;
        case 0xa158: native_a158: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0xa15a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a15a;
        case 0xa15a: native_a15a: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->xf=false;
            c->pc=0xa15c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a15c;
        case 0xa15c: native_a15c: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0001)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0001)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa15e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a15e;
        case 0xa15e: native_a15e: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0003)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0003)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa160;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a160;
        case 0xa160: native_a160: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa162;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a162;
        case 0xa162: native_a162: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa164;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a164;
        case 0xa164: native_a164: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestServiceStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa166;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a166;
        case 0xa166: native_a166: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0xa167;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a167;
        case 0xa167: native_a167: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa169;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a169;
        case 0xa169: native_a169: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa16b);c->sp-=2;c->pc=0xa2d7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xa16c: native_a16c: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa16e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a16e;
        case 0xa16e: native_a16e: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xa16f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a16f;
        case 0xa16f: native_a16f: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa170;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a170;
        case 0xa170: native_a170: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0xa173;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a173;
        case 0xa173: native_a173: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa175;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a175;
        case 0xa175: native_a175: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa17c:0xa177;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a17c;
            goto native_a177;
        case 0xa177: native_a177: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa178;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a178;
        case 0xa178: native_a178: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-2)<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-2)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,10) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-2;
            add(c,(c->mf?w->fields[10][addr]:word(w->fields[10],addr)),false);
            c->pc=0xa17c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a17c;
        case 0xa17c: native_a17c: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,ScWorldFieldWidth(w,10)-1,false);
            c->pc=0xa17f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a17f;
        case 0xa17f: native_a17f: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa186:0xa181;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a186;
            goto native_a181;
        case 0xa181: native_a181: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa182;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a182;
        case 0xa182: native_a182: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+2)<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+2)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,10) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+2;
            add(c,(c->mf?w->fields[10][addr]:word(w->fields[10],addr)),false);
            c->pc=0xa186;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a186;
        case 0xa186: native_a186: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa188;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a188;
        case 0xa188: native_a188: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa18f:0xa18a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a18f;
            goto native_a18a;
        case 0xa18a: native_a18a: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa18b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a18b;
        case 0xa18b: native_a18b: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-(int)ScWorldFieldWidth(w,10)*2)<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-(int)ScWorldFieldWidth(w,10)*2)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,10) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-(int)ScWorldFieldWidth(w,10)*2;
            add(c,(c->mf?w->fields[10][addr]:word(w->fields[10],addr)),false);
            c->pc=0xa18f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a18f;
        case 0xa18f: native_a18f: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,ScWorldFieldHeight(w,10)-1,false);
            c->pc=0xa192;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a192;
        case 0xa192: native_a192: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa199:0xa194;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a199;
            goto native_a194;
        case 0xa194: native_a194: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa195;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a195;
        case 0xa195: native_a195: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+(int)ScWorldFieldWidth(w,10)*2)<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+(int)ScWorldFieldWidth(w,10)*2)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,10) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+(int)ScWorldFieldWidth(w,10)*2;
            add(c,(c->mf?w->fields[10][addr]:word(w->fields[10],addr)),false);
            c->pc=0xa199;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a199;
        case 0xa199: native_a199: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa19a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a19a;
        case 0xa19a: native_a19a: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa19b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a19b;
        case 0xa19b: native_a19b: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x))<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,10) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x);
            add(c,(c->mf?w->fields[10][addr]:word(w->fields[10],addr)),false);
            c->pc=0xa19f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a19f;
        case 0xa19f: native_a19f: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa1a0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1a0;
        case 0xa1a0: native_a1a0: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x))<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,16) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x);
            if(c->mf) w->fields[16][addr]=(uint8_t)c->a;else put(w->fields[16],addr,c->a);
            c->pc=0xa1a4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1a4;
        case 0xa1a4: native_a1a4: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0xa1a6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1a6;
        case 0xa1a6: native_a1a6: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestServiceStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa1a8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1a8;
        case 0xa1a8: native_a1a8: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa1aa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1aa;
        case 0xa1aa: native_a1aa: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldFieldWidth(w,10),c->mf);
            c->pc=0xa1ac;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1ac;
        case 0xa1ac: native_a1ac: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa164:0xa1ae;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a164;
            goto native_a1ae;
        case 0xa1ae: native_a1ae: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa1b0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1b0;
        case 0xa1b0: native_a1b0: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa1b2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1b2;
        case 0xa1b2: native_a1b2: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldFieldHeight(w,10),c->mf);
            c->pc=0xa1b4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1b4;
        case 0xa1b4: native_a1b4: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa162:0xa1b6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a162;
            goto native_a1b6;
        case 0xa1b6: native_a1b6: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa1b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1b8;
        case 0xa1b8: native_a1b8: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            c->x=(uint16_t)0x0000;nz(c,c->x,false);
            c->pc=0xa1bb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1bb;
        case 0xa1bb: native_a1bb: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestServiceStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=5+(c->mf?0:1);
            if(((w->colossal?(int)w->field_scan:(int)c->x))<0 || (unsigned)((w->colossal?(int)w->field_scan:(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,16) || cost>budget-cycles) return cycles;
            addr=(w->colossal?(int)w->field_scan:(int)c->x);
            load(c,(c->mf?w->fields[16][addr]:word(w->fields[16],addr)));
            c->pc=0xa1bf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1bf;
        case 0xa1bf: native_a1bf: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->colossal?(int)w->field_scan:(int)c->x))<0 || (unsigned)((w->colossal?(int)w->field_scan:(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,10) || cost>budget-cycles) return cycles;
            addr=(w->colossal?(int)w->field_scan:(int)c->x);
            if(c->mf) w->fields[10][addr]=(uint8_t)c->a;else put(w->fields[10],addr,c->a);
            c->pc=0xa1c3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1c3;
        case 0xa1c3: native_a1c3: /* INX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->x;nz(c,c->x,false);
            c->pc=0xa1c4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1c4;
        case 0xa1c4: native_a1c4: /* INX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->x;nz(c,c->x,false);
            c->pc=0xa1c5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1c5;
        case 0xa1c5: native_a1c5: /* CPX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;

            if(w->colossal) {
                unsigned minimum=w->field_scan+2==ScWorldFieldSizeWorld(w,16)?2:3;
                if(minimum>budget-cycles) return cycles;
                ScWorldGuestStepPrepared(w,c,r);goto native_a1c8;
            }
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->x,ScWorldFieldSizeWorld(w,16),false);
            c->pc=0xa1c8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1c8;
        case 0xa1c8: native_a1c8: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa1bb:0xa1ca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a1bb;
            goto native_a1ca;
        case 0xa1ca: native_a1ca: /* PLD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5;
            if(cost>budget-cycles) return cycles;
            c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);
            c->pc=0xa1cb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1cb;
        case 0xa1cb: native_a1cb: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            return cycles;
        case 0xa1cc: native_a1cc: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa1ce;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1ce;
        case 0xa1ce: native_a1ce: /* PHD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,c->dp);c->sp-=2;
            c->pc=0xa1cf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1cf;
        case 0xa1cf: native_a1cf: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xa1d0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1d0;
        case 0xa1d0: native_a1d0: /* TDC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->a=c->dp;nz(c,c->a,false);
            c->pc=0xa1d1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1d1;
        case 0xa1d1: native_a1d1: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0xa1d2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1d2;
        case 0xa1d2: native_a1d2: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0004,true);
            c->pc=0xa1d5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1d5;
        case 0xa1d5: native_a1d5: /* TCD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->dp=c->a;nz(c,c->dp,false);
            c->pc=0xa1d6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1d6;
        case 0xa1d6: native_a1d6: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xa1d7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1d7;
        case 0xa1d7: native_a1d7: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0xa1d9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1d9;
        case 0xa1d9: native_a1d9: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->xf=false;
            c->pc=0xa1db;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1db;
        case 0xa1db: native_a1db: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0001)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0001)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa1dd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1dd;
        case 0xa1dd: native_a1dd: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0003)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0003)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa1df;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1df;
        case 0xa1df: native_a1df: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa1e1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1e1;
        case 0xa1e1: native_a1e1: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa1e3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1e3;
        case 0xa1e3: native_a1e3: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestServiceStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa1e5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1e5;
        case 0xa1e5: native_a1e5: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0xa1e6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1e6;
        case 0xa1e6: native_a1e6: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa1e8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1e8;
        case 0xa1e8: native_a1e8: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa1ea);c->sp-=2;c->pc=0xa2d7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0xa1eb: native_a1eb: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa1ed;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1ed;
        case 0xa1ed: native_a1ed: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xa1ee;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1ee;
        case 0xa1ee: native_a1ee: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa1ef;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1ef;
        case 0xa1ef: native_a1ef: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0xa1f2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1f2;
        case 0xa1f2: native_a1f2: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa1f4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1f4;
        case 0xa1f4: native_a1f4: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa1fb:0xa1f6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a1fb;
            goto native_a1f6;
        case 0xa1f6: native_a1f6: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa1f7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1f7;
        case 0xa1f7: native_a1f7: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-2)<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-2)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,11) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-2;
            add(c,(c->mf?w->fields[11][addr]:word(w->fields[11],addr)),false);
            c->pc=0xa1fb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1fb;
        case 0xa1fb: native_a1fb: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,ScWorldFieldWidth(w,10)-1,false);
            c->pc=0xa1fe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a1fe;
        case 0xa1fe: native_a1fe: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa205:0xa200;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a205;
            goto native_a200;
        case 0xa200: native_a200: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa201;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a201;
        case 0xa201: native_a201: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+2)<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+2)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,11) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+2;
            add(c,(c->mf?w->fields[11][addr]:word(w->fields[11],addr)),false);
            c->pc=0xa205;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a205;
        case 0xa205: native_a205: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa207;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a207;
        case 0xa207: native_a207: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa20e:0xa209;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a20e;
            goto native_a209;
        case 0xa209: native_a209: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa20a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a20a;
        case 0xa20a: native_a20a: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-(int)ScWorldFieldWidth(w,11)*2)<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-(int)ScWorldFieldWidth(w,11)*2)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,11) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)-(int)ScWorldFieldWidth(w,11)*2;
            add(c,(c->mf?w->fields[11][addr]:word(w->fields[11],addr)),false);
            c->pc=0xa20e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a20e;
        case 0xa20e: native_a20e: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,ScWorldFieldHeight(w,10)-1,false);
            c->pc=0xa211;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a211;
        case 0xa211: native_a211: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0xa218:0xa213;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_a218;
            goto native_a213;
        case 0xa213: native_a213: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa214;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a214;
        case 0xa214: native_a214: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+(int)ScWorldFieldWidth(w,11)*2)<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+(int)ScWorldFieldWidth(w,11)*2)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,11) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x)+(int)ScWorldFieldWidth(w,11)*2;
            add(c,(c->mf?w->fields[11][addr]:word(w->fields[11],addr)),false);
            c->pc=0xa218;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a218;
        case 0xa218: native_a218: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa219;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a219;
        case 0xa219: native_a219: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa21a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a21a;
        case 0xa21a: native_a21a: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x))<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,11) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x);
            add(c,(c->mf?w->fields[11][addr]:word(w->fields[11],addr)),false);
            c->pc=0xa21e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a21e;
        case 0xa21e: native_a21e: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa21f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a21f;
        case 0xa21f: native_a21f: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x))<0 || (unsigned)((w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,16) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[2]*2+(int16_t)(c->x-(uint16_t)(w->field_anchor[2]*2)):(int)c->x);
            if(c->mf) w->fields[16][addr]=(uint8_t)c->a;else put(w->fields[16],addr,c->a);
            c->pc=0xa223;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a223;
        case 0xa223: native_a223: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0xa225;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a225;
        case 0xa225: native_a225: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestServiceStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa227;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a227;
        case 0xa227: native_a227: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa229;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a229;
        case 0xa229: native_a229: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldFieldWidth(w,10),c->mf);
            c->pc=0xa22b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a22b;
        case 0xa22b: native_a22b: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa1e3:0xa22d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a1e3;
            goto native_a22d;
        case 0xa22d: native_a22d: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa22f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a22f;
        case 0xa22f: native_a22f: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0002)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0002)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa231;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a231;
        case 0xa231: native_a231: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,ScWorldFieldHeight(w,10),c->mf);
            c->pc=0xa233;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a233;
        case 0xa233: native_a233: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa1e1:0xa235;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a1e1;
            goto native_a235;
        case 0xa235: native_a235: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xa237;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a237;
        case 0xa237: native_a237: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            c->x=(uint16_t)0x0000;nz(c,c->x,false);
            c->pc=0xa23a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a23a;
        case 0xa23a: native_a23a: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:ScWorldGuestServiceStageStep(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;break;}
            cost=5+(c->mf?0:1);
            if(((w->colossal?(int)w->field_scan:(int)c->x))<0 || (unsigned)((w->colossal?(int)w->field_scan:(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,16) || cost>budget-cycles) return cycles;
            addr=(w->colossal?(int)w->field_scan:(int)c->x);
            load(c,(c->mf?w->fields[16][addr]:word(w->fields[16],addr)));
            c->pc=0xa23e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a23e;
        case 0xa23e: native_a23e: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(((w->colossal?(int)w->field_scan:(int)c->x))<0 || (unsigned)((w->colossal?(int)w->field_scan:(int)c->x))+(c->mf?1:2)>ScWorldFieldSizeWorld(w,11) || cost>budget-cycles) return cycles;
            addr=(w->colossal?(int)w->field_scan:(int)c->x);
            if(c->mf) w->fields[11][addr]=(uint8_t)c->a;else put(w->fields[11],addr,c->a);
            c->pc=0xa242;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a242;
        case 0xa242: native_a242: /* INX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->x;nz(c,c->x,false);
            c->pc=0xa243;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a243;
        case 0xa243: native_a243: /* INX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->x;nz(c,c->x,false);
            c->pc=0xa244;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a244;
        case 0xa244: native_a244: /* CPX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;

            if(w->colossal) {
                unsigned minimum=w->field_scan+2==ScWorldFieldSizeWorld(w,16)?2:3;
                if(minimum>budget-cycles) return cycles;
                ScWorldGuestStepPrepared(w,c,r);goto native_a247;
            }
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->x,ScWorldFieldSizeWorld(w,16),false);
            c->pc=0xa247;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a247;
        case 0xa247: native_a247: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa23a:0xa249;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->z) goto native_a23a;
            goto native_a249;
        case 0xa249: native_a249: /* PLD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5;
            if(cost>budget-cycles) return cycles;
            c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);
            c->pc=0xa24a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_a24a;
        case 0xa24a: native_a24a: /* RTS */
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

unsigned ScServiceStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned budget) {
    return execute(w,c,r,rom,budget,false);
}
unsigned ScServiceInstructionStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom) {
    return execute(w,c,r,rom,12,true);
}
