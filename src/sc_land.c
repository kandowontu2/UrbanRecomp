#include "sc_land.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <stdlib.h>

/* Connected land-value, tile-statistics and pollution classification in C.
 * Direct control flow replaces ROM decode and mapped bus calls, retaining
 * every original interrupted boundary, stack shadow and ordered field write.
 * Whole-cell C kernels remain the first choice; these continuations also
 * resume cells cut by a beam/IRQ deadline. No speculative writes on rejection. */
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

bool ScLandOwns(uint16_t pc) {
    return (pc>=0x9cdf && pc<=0x9e60) || pc==0x9e61 || pc==0x9e8d;
}
static unsigned execute(ScWorld *restrict w,Interp816 *restrict c,uint8_t *restrict r,
                    unsigned budget,bool single) {
    static int reference=-1,stage_reference=-1;
    if(reference<0) {const char *e=getenv("SC_LAND_CONTINUATION_REFERENCE");reference=e && *e=='1';}
    if(stage_reference<0) {const char *e=getenv("SC_LAND_STAGE_REFERENCE");stage_reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x20 || c->dp>0x1fc0) return 0;
    unsigned cycles=0,cost,addr,value;
    for(;;) {
        if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
        switch(c->pc) {
        case 0x849e:case 0xa29a:case 0xa2b9:case 0x9e61:
            if(6>budget-cycles) return cycles;
            ScWorldGuestStepPrepared(w,c,r);
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0x84c3:case 0xa2b8:case 0xa2d6:case 0x9e8d:
            if(6>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(single) return cycles;break;
        case 0x9cdf: native_9cdf: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            if(w->huge) ScWorldGuestStepPrepared(w,c,r);
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x9ce1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ce1;
        case 0x9ce1: native_9ce1: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0022)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0022)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9ce3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ce3;
        case 0x9ce3: native_9ce3: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000e)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9ce5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ce5;
        case 0x9ce5: native_9ce5: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9ce7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ce7;
        case 0x9ce7: native_9ce7: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x9ce9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ce9;
        case 0x9ce9: native_9ce9: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000a)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000a)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9ceb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ceb;
        case 0x9ceb: native_9ceb: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9cec;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9cec;
        case 0x9cec: native_9cec: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x9ced;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ced;
        case 0x9ced: native_9ced: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9cef;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9cef;
        case 0x9cef: native_9cef: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9cf0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9cf0;
        case 0x9cf0: native_9cf0: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9cf2);c->sp-=2;c->pc=0x849e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9cf3: native_9cf3: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9cf5);c->sp-=2;c->pc=0x9dca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dca;
        case 0x9cf6: native_9cf6: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(w->map_anchor==UINT32_MAX || ((int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+2)<0 || (unsigned)((int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+2)+(c->mf?1:2)>ScWorldCells(w)*2 || cost>budget-cycles) return cycles;
            addr=(int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+2;
            load(c,(c->mf?w->tiles[addr]:word(w->tiles,addr)));
            c->pc=0x9cfa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9cfa;
        case 0x9cfa: native_9cfa: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9cfc);c->sp-=2;c->pc=0x9dca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dca;
        case 0x9cfd: native_9cfd: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(w->map_anchor==UINT32_MAX || ((int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+2*ScWorldWidth(w))<0 || (unsigned)((int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+2*ScWorldWidth(w))+(c->mf?1:2)>ScWorldCells(w)*2 || cost>budget-cycles) return cycles;
            addr=(int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+2*ScWorldWidth(w);
            load(c,(c->mf?w->tiles[addr]:word(w->tiles,addr)));
            c->pc=0x9d01;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d01;
        case 0x9d01: native_9d01: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9d03);c->sp-=2;c->pc=0x9dca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dca;
        case 0x9d04: native_9d04: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(w->map_anchor==UINT32_MAX || ((int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+2*ScWorldWidth(w)+2)<0 || (unsigned)((int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+2*ScWorldWidth(w)+2)+(c->mf?1:2)>ScWorldCells(w)*2 || cost>budget-cycles) return cycles;
            addr=(int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+2*ScWorldWidth(w)+2;
            load(c,(c->mf?w->tiles[addr]:word(w->tiles,addr)));
            c->pc=0x9d08;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d08;
        case 0x9d08: native_9d08: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9d0a);c->sp-=2;c->pc=0x9dca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dca;
        case 0x9d0b: native_9d0b: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0022)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0022)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d0d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d0d;
        case 0x9d0d: native_9d0d: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x00ff,c->mf);
            c->pc=0x9d10;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d10;
        case 0x9d10: native_9d10: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9d15:0x9d12;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9d15;
            goto native_9d12;
        case 0x9d12: native_9d12: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x00ff);
            c->pc=0x9d15;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d15;
        case 0x9d15: native_9d15: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0022)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0022)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9d17;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d17;
        case 0x9d17: native_9d17: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x9d19;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d19;
        case 0x9d19: native_9d19: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000a)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000a)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d1b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d1b;
        case 0x9d1b: native_9d1b: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9d1c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d1c;
        case 0x9d1c: native_9d1c: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x9d1d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d1d;
        case 0x9d1d: native_9d1d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d1f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d1f;
        case 0x9d1f: native_9d1f: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9d20;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d20;
        case 0x9d20: native_9d20: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9d22);c->sp-=2;c->pc=0xa2b9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9d23: native_9d23: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)w->field_anchor[1]+(int16_t)(c->x-(uint16_t)w->field_anchor[1]):(int)c->x)<0 || (unsigned)(w->giant?(int)w->field_anchor[1]+(int16_t)(c->x-(uint16_t)w->field_anchor[1]):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,15) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[1]+(int16_t)(c->x-(uint16_t)w->field_anchor[1]):(int)c->x);
            load(c,(c->mf?w->fields[15][addr]:word(w->fields[15],addr)));
            c->pc=0x9d27;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d27;
        case 0x9d27: native_9d27: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x9d28;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d28;
        case 0x9d28: native_9d28: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0022)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0022)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),false);
            c->pc=0x9d2a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d2a;
        case 0x9d2a: native_9d2a: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)w->field_anchor[1]+(int16_t)(c->x-(uint16_t)w->field_anchor[1]):(int)c->x)<0 || (unsigned)(w->giant?(int)w->field_anchor[1]+(int16_t)(c->x-(uint16_t)w->field_anchor[1]):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,15) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[1]+(int16_t)(c->x-(uint16_t)w->field_anchor[1]):(int)c->x);
            if(c->mf) w->fields[15][addr]=(uint8_t)c->a;else put(w->fields[15],addr,c->a);
            c->pc=0x9d2e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d2e;
        case 0x9d2e: native_9d2e: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x9d30;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d30;
        case 0x9d30: native_9d30: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000e)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d32;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d32;
        case 0x9d32: native_9d32: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x00fa,c->mf);
            c->pc=0x9d35;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d35;
        case 0x9d35: native_9d35: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9d3a:0x9d37;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9d3a;
            goto native_9d37;
        case 0x9d37: native_9d37: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x00fa);
            c->pc=0x9d3a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d3a;
        case 0x9d3a: native_9d3a: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000e)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9d3c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d3c;
        case 0x9d3c: native_9d3c: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x9d3e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d3e;
        case 0x9d3e: native_9d3e: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000a)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000a)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d40;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d40;
        case 0x9d40: native_9d40: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x9d41;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d41;
        case 0x9d41: native_9d41: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d43;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d43;
        case 0x9d43: native_9d43: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9d45);c->sp-=2;c->pc=0xa29a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9d46: native_9d46: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000e)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d48;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d48;
        case 0x9d48: native_9d48: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)<0 || (unsigned)(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,13) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x);
            if(c->mf) w->fields[13][addr]=(uint8_t)c->a;else put(w->fields[13],addr,c->a);
            c->pc=0x9d4c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d4c;
        case 0x9d4c: native_9d4c: /* PHX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->xf?0:1);
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,c->x);c->sp-=2;
            c->pc=0x9d4d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d4d;
        case 0x9d4d: native_9d4d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d4f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d4f;
        case 0x9d4f: native_9d4f: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9dc2:0x9d51;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9dc2;
            goto native_9d51;
        case 0x9d51: native_9d51: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000a)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000a)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d53;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d53;
        case 0x9d53: native_9d53: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x9d54;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d54;
        case 0x9d54: native_9d54: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d56;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d56;
        case 0x9d56: native_9d56: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9d58);c->sp-=2;c->pc=0x9e61;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9d59: native_9d59: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001e)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9d5b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d5b;
        case 0x9d5b: native_9d5b: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0022);
            c->pc=0x9d5d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d5d;
        case 0x9d5d: native_9d5d: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0x9d5e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d5e;
        case 0x9d5e: native_9d5e: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001e)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),true);
            c->pc=0x9d60;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d60;
        case 0x9d60: native_9d60: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9d61;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d61;
        case 0x9d61: native_9d61: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0x9d62;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d62;
        case 0x9d62: native_9d62: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001e)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9d64;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d64;
        case 0x9d64: native_9d64: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001f)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001f)&65535);
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0x9d66;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d66;
        case 0x9d66: native_9d66: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000a)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000a)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d68;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d68;
        case 0x9d68: native_9d68: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9d69;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d69;
        case 0x9d69: native_9d69: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0x9d6a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d6a;
        case 0x9d6a: native_9d6a: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0008)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0008)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d6c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d6c;
        case 0x9d6c: native_9d6c: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0x9d6d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d6d;
        case 0x9d6d: native_9d6d: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9d6f);c->sp-=2;c->pc=0xa2b9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9d70: native_9d70: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001e)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d72;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d72;
        case 0x9d72: native_9d72: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x9d73;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d73;
        case 0x9d73: native_9d73: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)w->field_anchor[1]+(int16_t)(c->x-(uint16_t)w->field_anchor[1]):(int)c->x)<0 || (unsigned)(w->giant?(int)w->field_anchor[1]+(int16_t)(c->x-(uint16_t)w->field_anchor[1]):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,6) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[1]+(int16_t)(c->x-(uint16_t)w->field_anchor[1]):(int)c->x);
            add(c,(c->mf?w->fields[6][addr]:word(w->fields[6],addr)),false);
            c->pc=0x9d77;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d77;
        case 0x9d77: native_9d77: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9d7b:0x9d79;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9d7b;
            goto native_9d79;
        case 0x9d79: native_9d79: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x001f)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001f)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9d7b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d7b;
        case 0x9d7b: native_9d7b: /* PLX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(cost>budget-cycles) return cycles;
            c->x=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->x,false);
            c->pc=0x9d7c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d7c;
        case 0x9d7c: native_9d7c: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0x9d7d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d7d;
        case 0x9d7d: native_9d7d: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)<0 || (unsigned)(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,2) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x);
            add(c,(c->mf?w->fields[2][addr]:word(w->fields[2],addr)),true);
            c->pc=0x9d81;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d81;
        case 0x9d81: native_9d81: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001e)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9d83;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d83;
        case 0x9d83: native_9d83: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9d87:0x9d85;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9d87;
            goto native_9d85;
        case 0x9d85: native_9d85: /* DEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x001f)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001f)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            --value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9d87;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d87;
        case 0x9d87: native_9d87: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)<0 || (unsigned)(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,1) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x);
            load(c,(c->mf?w->fields[1][addr]:word(w->fields[1],addr)));
            c->pc=0x9d8b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d8b;
        case 0x9d8b: native_9d8b: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x00be,c->mf);
            c->pc=0x9d8d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d8d;
        case 0x9d8d: native_9d8d: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9d9a:0x9d8f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9d9a;
            goto native_9d8f;
        case 0x9d8f: native_9d8f: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001e)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d91;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d91;
        case 0x9d91: native_9d91: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0x9d92;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d92;
        case 0x9d92: native_9d92: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            add(c,0x0014,true);
            c->pc=0x9d94;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d94;
        case 0x9d94: native_9d94: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001e)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9d96;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d96;
        case 0x9d96: native_9d96: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9d9a:0x9d98;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9d9a;
            goto native_9d98;
        case 0x9d98: native_9d98: /* DEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x001f)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001f)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            --value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9d9a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d9a;
        case 0x9d9a: native_9d9a: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x9d9c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d9c;
        case 0x9d9c: native_9d9c: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x001e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x001e)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9d9e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9d9e;
        case 0x9d9e: native_9d9e: /* BPL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->n?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->n?0x9da5:0x9da0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->n) goto native_9da5;
            goto native_9da0;
        case 0x9da0: native_9da0: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0x9da3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9da3;
        case 0x9da3: native_9da3: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9dad;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dad;
        case 0x9da5: native_9da5: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x00fa,c->mf);
            c->pc=0x9da8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9da8;
        case 0x9da8: native_9da8: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9dad:0x9daa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9dad;
            goto native_9daa;
        case 0x9daa: native_9daa: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x00fa);
            c->pc=0x9dad;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dad;
        case 0x9dad: native_9dad: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0x9daf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9daf;
        case 0x9daf: native_9daf: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)<0 || (unsigned)(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,0) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x);
            if(c->mf) w->fields[0][addr]=(uint8_t)c->a;else put(w->fields[0],addr,c->a);
            c->pc=0x9db3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9db3;
        case 0x9db3: native_9db3: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0x9db5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9db5;
        case 0x9db5: native_9db5: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x9db6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9db6;
        case 0x9db6: native_9db6: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),false);
            c->pc=0x9db8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9db8;
        case 0x9db8: native_9db8: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0004)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0004)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9dba;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dba;
        case 0x9dba: native_9dba: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9dbe:0x9dbc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9dbe;
            goto native_9dbc;
        case 0x9dbc: native_9dbc: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0006)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0006)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9dbe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dbe;
        case 0x9dbe: native_9dbe: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0018)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0018)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9dc0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dc0;
        case 0x9dc0: native_9dc0: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9dc9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dc9;
        case 0x9dc2: native_9dc2: /* PLX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(cost>budget-cycles) return cycles;
            c->x=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->x,false);
            c->pc=0x9dc3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dc3;
        case 0x9dc3: native_9dc3: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0x9dc5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dc5;
        case 0x9dc5: native_9dc5: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if((w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)<0 || (unsigned)(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x)+(c->mf?1:2)>ScWorldFieldSizeWorld(w,0) || cost>budget-cycles) return cycles;
            addr=(w->giant?(int)w->field_anchor[0]+(int16_t)(c->x-(uint16_t)w->field_anchor[0]):(int)c->x);
            if(c->mf) w->fields[0][addr]=(uint8_t)c->a;else put(w->fields[0],addr,c->a);
            c->pc=0x9dc9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dc9;
        case 0x9dc9: native_9dc9: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9dca: native_9dca: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            if(!single && !stage_reference) {
                unsigned span=ScWorldGuestLandStageStep(w,c,r,budget-cycles);
                if(span) {cycles+=span;goto native_9e0b;}
            }
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xcf));
            c->pc=0x9dcc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dcc;
        case 0x9dcc: native_9dcc: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x03ff);
            c->pc=0x9dcf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dcf;
        case 0x9dcf: native_9dcf: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9e0b:0x9dd1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9e0b;
            goto native_9dd1;
        case 0x9dd1: native_9dd1: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0028,c->mf);
            c->pc=0x9dd4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dd4;
        case 0x9dd4: native_9dd4: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9ddf:0x9dd6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9ddf;
            goto native_9dd6;
        case 0x9dd6: native_9dd6: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0022)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0022)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0x9dd8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dd8;
        case 0x9dd8: native_9dd8: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x000f,false);
            c->pc=0x9ddb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ddb;
        case 0x9ddb: native_9ddb: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0022)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0022)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9ddd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9ddd;
        case 0x9ddd: native_9ddd: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9e0b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e0b;
        case 0x9ddf: native_9ddf: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x02bf,c->mf);
            c->pc=0x9de2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9de2;
        case 0x9de2: native_9de2: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9dfa:0x9de4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9dfa;
            goto native_9de4;
        case 0x9de4: native_9de4: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0354,c->mf);
            c->pc=0x9de7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9de7;
        case 0x9de7: native_9de7: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9dfa:0x9de9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9dfa;
            goto native_9de9;
        case 0x9de9: native_9de9: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0307,c->mf);
            c->pc=0x9dec;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dec;
        case 0x9dec: native_9dec: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9dfa:0x9dee;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9dfa;
            goto native_9dee;
        case 0x9dee: native_9dee: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0310,c->mf);
            c->pc=0x9df1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9df1;
        case 0x9df1: native_9df1: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9dfa:0x9df3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9dfa;
            goto native_9df3;
        case 0x9df3: native_9df3: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0x9df4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9df4;
        case 0x9df4: native_9df4: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x00ff);
            c->pc=0x9df7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9df7;
        case 0x9df7: native_9df7: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0022)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0022)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9df9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9df9;
        case 0x9df9: native_9df9: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0x9dfa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dfa;
        case 0x9dfa: native_9dfa: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0x9dfb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dfb;
        case 0x9dfb: native_9dfb: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0x9dfd);c->sp-=2;c->pc=0x9e0c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e0c;
        case 0x9dfe: native_9dfe: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0x9dff;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9dff;
        case 0x9dff: native_9dff: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000e)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),false);
            c->pc=0x9e01;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e01;
        case 0x9e01: native_9e01: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x000e)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x000e)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0x9e03;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e03;
        case 0x9e03: native_9e03: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0x9e04;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e04;
        case 0x9e04: native_9e04: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0030,c->mf);
            c->pc=0x9e07;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e07;
        case 0x9e07: native_9e07: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9e0b:0x9e09;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9e0b;
            goto native_9e09;
        case 0x9e09: native_9e09: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+((c->dp&255)!=0)+(c->mf?0:2);
            if(((c->dp+0x0010)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0010)&65535);
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0x9e0b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e0b;
        case 0x9e0b: native_9e0b: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            break;
        case 0x9e0c: native_9e0c: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            if(!single && !stage_reference) {
                unsigned span=ScWorldGuestLandStageStep(w,c,r,budget-cycles);
                if(span) {cycles+=span;break;}
            }
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xcf));
            c->pc=0x9e0e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e0e;
        case 0x9e0e: native_9e0e: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x003c;nz(c,c->y,false);
            c->pc=0x9e11;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e11;
        case 0x9e11: native_9e11: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x007f,c->mf);
            c->pc=0x9e14;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e14;
        case 0x9e14: native_9e14: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9e5f:0x9e16;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9e5f;
            goto native_9e16;
        case 0x9e16: native_9e16: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0xffd8;nz(c,c->y,false);
            c->pc=0x9e19;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e19;
        case 0x9e19: native_9e19: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0364,c->mf);
            c->pc=0x9e1c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e1c;
        case 0x9e1c: native_9e1c: /* BEQ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->z?0x9e5f:0x9e1e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->z) goto native_9e5f;
            goto native_9e1e;
        case 0x9e1e: native_9e1e: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0060,c->mf);
            c->pc=0x9e21;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e21;
        case 0x9e21: native_9e21: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9e35:0x9e23;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9e35;
            goto native_9e23;
        case 0x9e23: native_9e23: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0019;nz(c,c->y,false);
            c->pc=0x9e26;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e26;
        case 0x9e26: native_9e26: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0050,c->mf);
            c->pc=0x9e29;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e29;
        case 0x9e29: native_9e29: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9e5f:0x9e2b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9e5f;
            goto native_9e2b;
        case 0x9e2b: native_9e2b: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x000a;nz(c,c->y,false);
            c->pc=0x9e2e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e2e;
        case 0x9e2e: native_9e2e: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0040,c->mf);
            c->pc=0x9e31;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e31;
        case 0x9e31: native_9e31: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x9e5f:0x9e33;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_9e5f;
            goto native_9e33;
        case 0x9e33: native_9e33: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0x9e5c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e5c;
        case 0x9e35: native_9e35: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0x9e38;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e38;
        case 0x9e38: native_9e38: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x01fd,c->mf);
            c->pc=0x9e3b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e3b;
        case 0x9e3b: native_9e3b: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9e5f:0x9e3d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9e5f;
            goto native_9e3d;
        case 0x9e3d: native_9e3d: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0032;nz(c,c->y,false);
            c->pc=0x9e40;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e40;
        case 0x9e40: native_9e40: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0245,c->mf);
            c->pc=0x9e43;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e43;
        case 0x9e43: native_9e43: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9e5f:0x9e45;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9e5f;
            goto native_9e45;
        case 0x9e45: native_9e45: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x003c;nz(c,c->y,false);
            c->pc=0x9e48;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e48;
        case 0x9e48: native_9e48: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0267,c->mf);
            c->pc=0x9e4b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e4b;
        case 0x9e4b: native_9e4b: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9e5c:0x9e4d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9e5c;
            goto native_9e4d;
        case 0x9e4d: native_9e4d: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0277,c->mf);
            c->pc=0x9e50;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e50;
        case 0x9e50: native_9e50: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9e5f:0x9e52;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9e5f;
            goto native_9e52;
        case 0x9e52: native_9e52: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0287,c->mf);
            c->pc=0x9e55;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e55;
        case 0x9e55: native_9e55: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9e5c:0x9e57;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9e5c;
            goto native_9e57;
        case 0x9e57: native_9e57: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x02ba,c->mf);
            c->pc=0x9e5a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e5a;
        case 0x9e5a: native_9e5a: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0x9e5f:0x9e5c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!c->c) goto native_9e5f;
            goto native_9e5c;
        case 0x9e5c: native_9e5c: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0x9e5f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e5f;
        case 0x9e5f: native_9e5f: /* TYA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            load(c,c->y);
            c->pc=0x9e60;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_9e60;
        case 0x9e60: native_9e60: /* RTS */
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
unsigned ScLandStep(ScWorld *restrict w,Interp816 *restrict c,uint8_t *restrict r,
                    unsigned budget) {
    return execute(w,c,r,budget,false);
}
unsigned ScLandInstructionStep(ScWorld *restrict w,Interp816 *restrict c,uint8_t *restrict r) {
    return execute(w,c,r,12,true);
}
