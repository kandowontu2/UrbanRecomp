#include "sc_infrastructure.h"
#include "sc_world_guest.h"
#include "sc_native_specialize.h"
#include "snes/interp816.h"
#include <stdlib.h>

/* Connected road, bridge and rail upkeep, artwork and decay
 * in direct C. Preserve ordered writes, flags, stack shadows and the
 * exact caller deadline. Shared full-coordinate hooks stay at their original
 * boundaries. RNG and external simulation helpers retain their native boundaries. */
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
bool ScInfrastructureOwns(uint16_t pc) {
    return (pc>=0xa493 && pc<0xa6b8) || (pc>=0xa70c && pc<0xa7e0);
}
SC_NATIVE_SPECIALIZE unsigned execute(ScWorld *restrict w,Interp816 *restrict c,
    uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget,bool single) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_INFRASTRUCTURE_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || !rom || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x20 || c->dp>0x1fc0) return 0;
    unsigned cycles=0,cost,addr,value;ScWorldGuest mapped;
    for(;;) {
        if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
        switch(c->pc) {

        case 0xa493: native_a493: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xcf));
            c->pc=0xa495;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a495;
        case 0xa495: native_a495: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6+(c->mf?0:2);
            if(0x0e15>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0e15;
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa498;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a498;
        case 0xa498: native_a498: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa49b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a49b;
        case 0xa49b: native_a49b: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0080,c->mf);
            c->pc=0xa49e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a49e;
        case 0xa49e: native_a49e: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa4ec:0xa4a0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a4ec;
            goto native_a4a0;
        case 0xa4a0: native_a4a0: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0bc5>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0bc5;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa4a3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4a3;
        case 0xa4a3: native_a4a3: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x001e,c->mf);
            c->pc=0xa4a6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4a6;
        case 0xa4a6: native_a4a6: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa4e1:0xa4a8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a4e1;
            goto native_a4a8;
        case 0xa4a8: native_a4a8: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa4aa);c->sp-=2;c->pc=0x907e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa4ab: native_a4ab: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x01ff);
            c->pc=0xa4ae;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4ae;
        case 0xa4ae: native_a4ae: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa4e1:0xa4b0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a4e1;
            goto native_a4b0;
        case 0xa4b0: native_a4b0: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa4b3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4b3;
        case 0xa4b3: native_a4b3: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0x84eb&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0x84eb+c->y]:word(rom,0x10000+0x84eb+c->y))));
            c->pc=0xa4b6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4b6;
        case 0xa4b6: native_a4b6: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x0010);
            c->pc=0xa4b9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4b9;
        case 0xa4b9: native_a4b9: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa4e1:0xa4bb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a4e1;
            goto native_a4bb;
        case 0xa4bb: native_a4bb: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa4bd);c->sp-=2;c->pc=0x907e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa4be: native_a4be: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x001f);
            c->pc=0xa4c1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4c1;
        case 0xa4c1: native_a4c1: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0bc5>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0bc5;
            compare(c,c->a,(c->mf?r[addr]:word(r,addr)),c->mf);
            c->pc=0xa4c4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4c4;
        case 0xa4c4: native_a4c4: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa4e1:0xa4c6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a4e1;
            goto native_a4c6;
        case 0xa4c6: native_a4c6: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa4c9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4c9;
        case 0xa4c9: native_a4c9: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x000f);
            c->pc=0xa4cc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4cc;
        case 0xa4cc: native_a4cc: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0xa4cf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4cf;
        case 0xa4cf: native_a4cf: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa4d6:0xa4d1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a4d6;
            goto native_a4d1;
        case 0xa4d1: native_a4d1: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0003);
            c->pc=0xa4d4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4d4;
        case 0xa4d4: native_a4d4: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0xa4d9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4d9;
        case 0xa4d6: native_a4d6: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0028);
            c->pc=0xa4d9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4d9;
        case 0xa4d9: native_a4d9: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(0x0b49>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b49;
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0xa4dc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4dc;
        case 0xa4dc: native_a4dc: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            ScWorldGuestWrite(&mapped,mapped.address,(uint8_t)c->a);if(!c->mf) ScWorldGuestWrite(&mapped,mapped.address+1,(uint8_t)(c->a>>8));
            c->pc=0xa4e0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4e0;
        case 0xa4e0: native_a4e0: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa4e1: native_a4e1: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa4e4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4e4;
        case 0xa4e4: native_a4e4: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x000f);
            c->pc=0xa4e7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4e7;
        case 0xa4e7: native_a4e7: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0xa4ea;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4ea;
        case 0xa4ea: native_a4ea: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa4fb:0xa4ec;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a4fb;
            goto native_a4ec;
        case 0xa4ec: native_a4ec: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0e15>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0e15;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa4ef;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4ef;
        case 0xa4ef: native_a4ef: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa4f0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4f0;
        case 0xa4f0: native_a4f0: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0004,false);
            c->pc=0xa4f3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4f3;
        case 0xa4f3: native_a4f3: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0e15>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0e15;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa4f6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4f6;
        case 0xa4f6: native_a4f6: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa4f8);c->sp-=2;c->pc=0xa53b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a53b;
        case 0xa4f9: native_a4f9: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa53a:0xa4fb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a53a;
            goto native_a4fb;
        case 0xa4fb: native_a4fb: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa4fe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a4fe;
        case 0xa4fe: native_a4fe: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0080,c->mf);
            c->pc=0xa501;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a501;
        case 0xa501: native_a501: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa53a:0xa503;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a53a;
            goto native_a503;
        case 0xa503: native_a503: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0xa505;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a505;
        case 0xa505: native_a505: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa508;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a508;
        case 0xa508: native_a508: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa509;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a509;
        case 0xa509: native_a509: /* XBA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0xa50a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a50a;
        case 0xa50a: native_a50a: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa50d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a50d;
        case 0xa50d: native_a50d: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa50e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a50e;
        case 0xa50e: native_a50e: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa510);c->sp-=2;c->pc=0xa29a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa511: native_a511: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0030;nz(c,c->y,false);
            c->pc=0xa514;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a514;
        case 0xa514: native_a514: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0));
            c->pc=0xa518;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a518;
        case 0xa518: native_a518: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0064,c->mf);
            c->pc=0xa51a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a51a;
        case 0xa51a: native_a51a: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa526:0xa51c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a526;
            goto native_a51c;
        case 0xa51c: native_a51c: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0040;nz(c,c->y,false);
            c->pc=0xa51f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a51f;
        case 0xa51f: native_a51f: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            compare(c,c->a,0x00c8,c->mf);
            c->pc=0xa521;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a521;
        case 0xa521: native_a521: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa526:0xa523;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a526;
            goto native_a523;
        case 0xa523: native_a523: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0050;nz(c,c->y,false);
            c->pc=0xa526;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a526;
        case 0xa526: native_a526: /* STY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(0x0b41>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b41;
            put(r,addr,c->y);
            c->pc=0xa529;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a529;
        case 0xa529: native_a529: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0xa52b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a52b;
        case 0xa52b: native_a52b: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0xa52d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a52d;
        case 0xa52d: native_a52d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b87>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b87;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa530;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a530;
        case 0xa530: native_a530: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0xff0f);
            c->pc=0xa533;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a533;
        case 0xa533: native_a533: /* ORA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b41>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b41;
            load(c,c->a|(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa536;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a536;
        case 0xa536: native_a536: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            ScWorldGuestWrite(&mapped,mapped.address,(uint8_t)c->a);if(!c->mf) ScWorldGuestWrite(&mapped,mapped.address+1,(uint8_t)(c->a>>8));
            c->pc=0xa53a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a53a;
        case 0xa53a: native_a53a: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa53b: native_a53b: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa53e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a53e;
        case 0xa53e: native_a53e: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0355,c->mf);
            c->pc=0xa541;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a541;
        case 0xa541: native_a541: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa589:0xa543;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a589;
            goto native_a543;
        case 0xa543: native_a543: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa545);c->sp-=2;c->pc=0x907e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa546: native_a546: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x0003);
            c->pc=0xa549;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a549;
        case 0xa549: native_a549: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa585:0xa54b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a585;
            goto native_a54b;
        case 0xa54b: native_a54b: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa54d);c->sp-=2;c->pc=0xa70c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a70c;
        case 0xa54e: native_a54e: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0015,c->mf);
            c->pc=0xa551;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a551;
        case 0xa551: native_a551: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa585:0xa553;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a585;
            goto native_a553;
        case 0xa553: native_a553: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0xa556;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a556;
        case 0xa556: native_a556: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa558;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a558;
        case 0xa558: native_a558: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa559;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a559;
        case 0xa559: native_a559: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6e2&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            add(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6e2+c->y]:word(rom,0x10000+0xa6e2+c->y))),false);
            c->pc=0xa55c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a55c;
        case 0xa55c: native_a55c: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa55d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a55d;
        case 0xa55d: native_a55d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0));
            c->pc=0xa561;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a561;
        case 0xa561: native_a561: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6f0&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            compare(c,c->a,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6f0+c->y]:word(rom,0x10000+0xa6f0+c->y))),c->mf);
            c->pc=0xa564;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a564;
        case 0xa564: native_a564: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa585:0xa566;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a585;
            goto native_a566;
        case 0xa566: native_a566: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa567;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a567;
        case 0xa567: native_a567: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa568;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a568;
        case 0xa568: native_a568: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x000e,false);
            c->pc=0xa56b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a56b;
        case 0xa56b: native_a56b: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa556:0xa56d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a556;
            goto native_a56d;
        case 0xa56d: native_a56d: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0xa570;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a570;
        case 0xa570: native_a570: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa572;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a572;
        case 0xa572: native_a572: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa573;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a573;
        case 0xa573: native_a573: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6e2&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            add(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6e2+c->y]:word(rom,0x10000+0xa6e2+c->y))),false);
            c->pc=0xa576;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a576;
        case 0xa576: native_a576: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa577;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a577;
        case 0xa577: native_a577: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6fe&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6fe + c->y]:word(rom,0x10000+0xa6fe + c->y))));
            c->pc=0xa57a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a57a;
        case 0xa57a: native_a57a: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            ScWorldGuestWrite(&mapped,mapped.address,(uint8_t)c->a);if(!c->mf) ScWorldGuestWrite(&mapped,mapped.address+1,(uint8_t)(c->a>>8));
            c->pc=0xa57e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a57e;
        case 0xa57e: native_a57e: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa57f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a57f;
        case 0xa57f: native_a57f: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa580;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a580;
        case 0xa580: native_a580: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x000e,false);
            c->pc=0xa583;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a583;
        case 0xa583: native_a583: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa570:0xa585;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a570;
            goto native_a585;
        case 0xa585: native_a585: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0xa588;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a588;
        case 0xa588: native_a588: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa589: native_a589: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0354,c->mf);
            c->pc=0xa58c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a58c;
        case 0xa58c: native_a58c: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa5d6:0xa58e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a5d6;
            goto native_a58e;
        case 0xa58e: native_a58e: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa590);c->sp-=2;c->pc=0x907e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa591: native_a591: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x0003);
            c->pc=0xa594;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a594;
        case 0xa594: native_a594: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa585:0xa596;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a585;
            goto native_a596;
        case 0xa596: native_a596: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa598);c->sp-=2;c->pc=0xa70c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a70c;
        case 0xa599: native_a599: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0015,c->mf);
            c->pc=0xa59c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a59c;
        case 0xa59c: native_a59c: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa585:0xa59e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a585;
            goto native_a59e;
        case 0xa59e: native_a59e: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0xa5a1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5a1;
        case 0xa5a1: native_a5a1: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa5a3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5a3;
        case 0xa5a3: native_a5a3: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa5a4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5a4;
        case 0xa5a4: native_a5a4: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6b8&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            add(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6b8+c->y]:word(rom,0x10000+0xa6b8+c->y))),false);
            c->pc=0xa5a7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5a7;
        case 0xa5a7: native_a5a7: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa5a8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5a8;
        case 0xa5a8: native_a5a8: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0));
            c->pc=0xa5ac;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5ac;
        case 0xa5ac: native_a5ac: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6c6&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            compare(c,c->a,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6c6+c->y]:word(rom,0x10000+0xa6c6+c->y))),c->mf);
            c->pc=0xa5af;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5af;
        case 0xa5af: native_a5af: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa585:0xa5b1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a585;
            goto native_a5b1;
        case 0xa5b1: native_a5b1: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa5b2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5b2;
        case 0xa5b2: native_a5b2: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa5b3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5b3;
        case 0xa5b3: native_a5b3: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x000e,false);
            c->pc=0xa5b6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5b6;
        case 0xa5b6: native_a5b6: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa5a1:0xa5b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a5a1;
            goto native_a5b8;
        case 0xa5b8: native_a5b8: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0xa5bb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5bb;
        case 0xa5bb: native_a5bb: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa5bd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5bd;
        case 0xa5bd: native_a5bd: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa5be;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5be;
        case 0xa5be: native_a5be: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6b8&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            add(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6b8+c->y]:word(rom,0x10000+0xa6b8+c->y))),false);
            c->pc=0xa5c1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5c1;
        case 0xa5c1: native_a5c1: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa5c2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5c2;
        case 0xa5c2: native_a5c2: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6d4&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6d4+c->y]:word(rom,0x10000+0xa6d4+c->y))));
            c->pc=0xa5c5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5c5;
        case 0xa5c5: native_a5c5: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            ScWorldGuestWrite(&mapped,mapped.address,(uint8_t)c->a);if(!c->mf) ScWorldGuestWrite(&mapped,mapped.address+1,(uint8_t)(c->a>>8));
            c->pc=0xa5c9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5c9;
        case 0xa5c9: native_a5c9: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa5ca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5ca;
        case 0xa5ca: native_a5ca: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa5cb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5cb;
        case 0xa5cb: native_a5cb: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x000e,false);
            c->pc=0xa5ce;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5ce;
        case 0xa5ce: native_a5ce: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa5bb:0xa5d0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a5bb;
            goto native_a5d0;
        case 0xa5d0: native_a5d0: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0xa585;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a585;
        case 0xa5d2: native_a5d2: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0xa5d5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5d5;
        case 0xa5d5: native_a5d5: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa5d6: native_a5d6: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xcf));
            c->pc=0xa5d8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5d8;
        case 0xa5d8: native_a5d8: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa5db;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5db;
        case 0xa5db: native_a5db: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0080,c->mf);
            c->pc=0xa5de;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5de;
        case 0xa5de: native_a5de: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa5d2:0xa5e0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a5d2;
            goto native_a5e0;
        case 0xa5e0: native_a5e0: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa5e2);c->sp-=2;c->pc=0xa70c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a70c;
        case 0xa5e3: native_a5e3: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0012,c->mf);
            c->pc=0xa5e6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5e6;
        case 0xa5e6: native_a5e6: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa5f0:0xa5e8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a5f0;
            goto native_a5e8;
        case 0xa5e8: native_a5e8: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa5ea);c->sp-=2;c->pc=0x907e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa5eb: native_a5eb: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x0007);
            c->pc=0xa5ee;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5ee;
        case 0xa5ee: native_a5ee: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa5d2:0xa5f0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a5d2;
            goto native_a5f0;
        case 0xa5f0: native_a5f0: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa5f3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5f3;
        case 0xa5f3: native_a5f3: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x00ff);
            c->pc=0xa5f6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5f6;
        case 0xa5f6: native_a5f6: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0xa5f9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5f9;
        case 0xa5f9: native_a5f9: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa5d2:0xa5fb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a5d2;
            goto native_a5fb;
        case 0xa5fb: native_a5fb: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0076,c->mf);
            c->pc=0xa5fe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a5fe;
        case 0xa5fe: native_a5fe: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa5d2:0xa600;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a5d2;
            goto native_a600;
        case 0xa600: native_a600: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa603;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a603;
        case 0xa603: native_a603: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x00ff);
            c->pc=0xa606;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a606;
        case 0xa606: native_a606: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0xa609;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a609;
        case 0xa609: native_a609: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa5d2:0xa60b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a5d2;
            goto native_a60b;
        case 0xa60b: native_a60b: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0062,c->mf);
            c->pc=0xa60e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a60e;
        case 0xa60e: native_a60e: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa5d2:0xa610;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a5d2;
            goto native_a610;
        case 0xa610: native_a610: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa613;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a613;
        case 0xa613: native_a613: /* LSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&1)!=0;load(c,value>>1);
            c->pc=0xa614;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a614;
        case 0xa614: native_a614: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa664:0xa616;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a664;
            goto native_a616;
        case 0xa616: native_a616: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0xa618;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a618;
        case 0xa618: native_a618: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0));
            c->pc=0xa61c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a61c;
        case 0xa61c: native_a61c: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x03ff);
            c->pc=0xa61f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a61f;
        case 0xa61f: native_a61f: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0xa622;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a622;
        case 0xa622: native_a622: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa5d2:0xa624;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a5d2;
            goto native_a624;
        case 0xa624: native_a624: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0xa627;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a627;
        case 0xa627: native_a627: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa629;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a629;
        case 0xa629: native_a629: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa62a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a62a;
        case 0xa62a: native_a62a: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6e2&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            add(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6e2+c->y]:word(rom,0x10000+0xa6e2+c->y))),false);
            c->pc=0xa62d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a62d;
        case 0xa62d: native_a62d: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa62e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a62e;
        case 0xa62e: native_a62e: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0));
            c->pc=0xa632;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a632;
        case 0xa632: native_a632: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x000f);
            c->pc=0xa635;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a635;
        case 0xa635: native_a635: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b41>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b41;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa638;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a638;
        case 0xa638: native_a638: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6fe&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6fe + c->y]:word(rom,0x10000+0xa6fe + c->y))));
            c->pc=0xa63b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a63b;
        case 0xa63b: native_a63b: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x000f);
            c->pc=0xa63e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a63e;
        case 0xa63e: native_a63e: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b41>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b41;
            compare(c,c->a,(c->mf?r[addr]:word(r,addr)),c->mf);
            c->pc=0xa641;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a641;
        case 0xa641: native_a641: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa5d2:0xa643;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a5d2;
            goto native_a643;
        case 0xa643: native_a643: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa644;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a644;
        case 0xa644: native_a644: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa645;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a645;
        case 0xa645: native_a645: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x000e,false);
            c->pc=0xa648;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a648;
        case 0xa648: native_a648: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa627:0xa64a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a627;
            goto native_a64a;
        case 0xa64a: native_a64a: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0xa64d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a64d;
        case 0xa64d: native_a64d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa64f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a64f;
        case 0xa64f: native_a64f: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa650;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a650;
        case 0xa650: native_a650: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6e2&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            add(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6e2+c->y]:word(rom,0x10000+0xa6e2+c->y))),false);
            c->pc=0xa653;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a653;
        case 0xa653: native_a653: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa654;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a654;
        case 0xa654: native_a654: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6f0&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6f0+c->y]:word(rom,0x10000+0xa6f0+c->y))));
            c->pc=0xa657;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a657;
        case 0xa657: native_a657: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            ScWorldGuestWrite(&mapped,mapped.address,(uint8_t)c->a);if(!c->mf) ScWorldGuestWrite(&mapped,mapped.address+1,(uint8_t)(c->a>>8));
            c->pc=0xa65b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a65b;
        case 0xa65b: native_a65b: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa65c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a65c;
        case 0xa65c: native_a65c: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa65d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a65d;
        case 0xa65d: native_a65d: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x000e,false);
            c->pc=0xa660;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a660;
        case 0xa660: native_a660: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa64d:0xa662;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a64d;
            goto native_a662;
        case 0xa662: native_a662: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0xa6b0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6b0;
        case 0xa664: native_a664: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->xf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0xa666;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a666;
        case 0xa666: native_a666: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0));
            c->pc=0xa66a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a66a;
        case 0xa66a: native_a66a: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x03ff);
            c->pc=0xa66d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a66d;
        case 0xa66d: native_a66d: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0002,c->mf);
            c->pc=0xa670;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a670;
        case 0xa670: native_a670: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa6b4:0xa672;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a6b4;
            goto native_a672;
        case 0xa672: native_a672: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0xa675;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a675;
        case 0xa675: native_a675: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa677;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a677;
        case 0xa677: native_a677: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa678;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a678;
        case 0xa678: native_a678: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6b8&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            add(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6b8+c->y]:word(rom,0x10000+0xa6b8+c->y))),false);
            c->pc=0xa67b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a67b;
        case 0xa67b: native_a67b: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa67c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a67c;
        case 0xa67c: native_a67c: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0));
            c->pc=0xa680;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a680;
        case 0xa680: native_a680: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x000f);
            c->pc=0xa683;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a683;
        case 0xa683: native_a683: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b41>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b41;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa686;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a686;
        case 0xa686: native_a686: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6d4&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6d4+c->y]:word(rom,0x10000+0xa6d4+c->y))));
            c->pc=0xa689;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a689;
        case 0xa689: native_a689: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x000f);
            c->pc=0xa68c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a68c;
        case 0xa68c: native_a68c: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b41>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b41;
            compare(c,c->a,(c->mf?r[addr]:word(r,addr)),c->mf);
            c->pc=0xa68f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a68f;
        case 0xa68f: native_a68f: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa6b4:0xa691;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a6b4;
            goto native_a691;
        case 0xa691: native_a691: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa692;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a692;
        case 0xa692: native_a692: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa693;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a693;
        case 0xa693: native_a693: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x000e,false);
            c->pc=0xa696;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a696;
        case 0xa696: native_a696: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa675:0xa698;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a675;
            goto native_a698;
        case 0xa698: native_a698: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            c->y=(uint16_t)0x0000;nz(c,c->y,false);
            c->pc=0xa69b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a69b;
        case 0xa69b: native_a69b: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa69d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a69d;
        case 0xa69d: native_a69d: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa69e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a69e;
        case 0xa69e: native_a69e: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6b8&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            add(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6b8+c->y]:word(rom,0x10000+0xa6b8+c->y))),false);
            c->pc=0xa6a1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6a1;
        case 0xa6a1: native_a6a1: /* TAX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xa6a2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6a2;
        case 0xa6a2: native_a6a2: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0xa6c6&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0xa6c6+c->y]:word(rom,0x10000+0xa6c6+c->y))));
            c->pc=0xa6a5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6a5;
        case 0xa6a5: native_a6a5: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            ScWorldGuestWrite(&mapped,mapped.address,(uint8_t)c->a);if(!c->mf) ScWorldGuestWrite(&mapped,mapped.address+1,(uint8_t)(c->a>>8));
            c->pc=0xa6a9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6a9;
        case 0xa6a9: native_a6a9: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa6aa;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6aa;
        case 0xa6aa: native_a6aa: /* INY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            ++c->y;nz(c,c->y,false);
            c->pc=0xa6ab;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6ab;
        case 0xa6ab: native_a6ab: /* CPY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->xf?0:1);
            if(c->xf || cost>budget-cycles) return cycles;
            compare(c,c->y,0x000e,false);
            c->pc=0xa6ae;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6ae;
        case 0xa6ae: native_a6ae: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa69b:0xa6b0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a69b;
            goto native_a6b0;
        case 0xa6b0: native_a6b0: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0xa6b3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6b3;
        case 0xa6b3: native_a6b3: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa6b4: native_a6b4: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0000);
            c->pc=0xa6b7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a6b7;
        case 0xa6b7: native_a6b7: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa70c: native_a70c: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0xa70e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a70e;
        case 0xa70e: native_a70e: /* PHD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,c->dp);c->sp-=2;
            c->pc=0xa70f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a70f;
        case 0xa70f: native_a70f: /* PHA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xa710;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a710;
        case 0xa710: native_a710: /* TDC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->a=c->dp;nz(c,c->a,false);
            c->pc=0xa711;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a711;
        case 0xa711: native_a711: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0xa712;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a712;
        case 0xa712: native_a712: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0002,true);
            c->pc=0xa715;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a715;
        case 0xa715: native_a715: /* TCD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->dp=c->a;nz(c,c->dp,false);
            c->pc=0xa716;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a716;
        case 0xa716: native_a716: /* PLA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xa717;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a717;
        case 0xa717: native_a717: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0xa719;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a719;
        case 0xa719: native_a719: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0a65>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0a65;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa71c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a71c;
        case 0xa71c: native_a71c: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0xa71d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a71d;
        case 0xa71d: native_a71d: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            add(c,(c->mf?r[addr]:word(r,addr)),true);
            c->pc=0xa720;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a720;
        case 0xa720: native_a720: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa725:0xa722;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a725;
            goto native_a722;
        case 0xa722: native_a722: /* EOR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,c->a^0x00ff);
            c->pc=0xa724;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a724;
        case 0xa724: native_a724: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            load(c,c->a+1);
            c->pc=0xa725;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a725;
        case 0xa725: native_a725: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa727;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a727;
        case 0xa727: native_a727: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0a63>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0a63;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa72a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a72a;
        case 0xa72a: native_a72a: /* SEC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=true;
            c->pc=0xa72b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a72b;
        case 0xa72b: native_a72b: /* SBC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            add(c,(c->mf?r[addr]:word(r,addr)),true);
            c->pc=0xa72e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a72e;
        case 0xa72e: native_a72e: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa733:0xa730;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a733;
            goto native_a730;
        case 0xa730: native_a730: /* EOR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,c->a^0x00ff);
            c->pc=0xa732;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a732;
        case 0xa732: native_a732: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            load(c,c->a+1);
            c->pc=0xa733;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a733;
        case 0xa733: native_a733: /* CLC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xa734;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a734;
        case 0xa734: native_a734: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+((c->dp&255)!=0)+(c->mf?0:1);
            if(((c->dp+0x0000)&65535)>0x1ffe || cost>budget-cycles) return cycles;
            addr=((c->dp+0x0000)&65535);
            add(c,(c->mf?r[addr]:word(r,addr)),false);
            c->pc=0xa736;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a736;
        case 0xa736: native_a736: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0xa738;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a738;
        case 0xa738: native_a738: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x00ff);
            c->pc=0xa73b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a73b;
        case 0xa73b: native_a73b: /* PLD */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5;
            if(cost>budget-cycles) return cycles;
            c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);
            c->pc=0xa73c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a73c;
        case 0xa73c: native_a73c: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa73d: native_a73d: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xcf));
            c->pc=0xa73f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a73f;
        case 0xa73f: native_a73f: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6+(c->mf?0:2);
            if(0x0e17>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0e17;
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa742;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a742;
        case 0xa742: native_a742: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0a93>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0a93;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa745;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a745;
        case 0xa745: native_a745: /* ORA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0c0f>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0c0f;
            load(c,c->a|(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa748;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a748;
        case 0xa748: native_a748: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa74d:0xa74a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a74d;
            goto native_a74a;
        case 0xa74a: native_a74a: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa74c);c->sp-=2;c->pc=0xa78b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a78b;
        case 0xa74d: native_a74d: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0bc5>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0bc5;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa750;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a750;
        case 0xa750: native_a750: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x001e,c->mf);
            c->pc=0xa753;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a753;
        case 0xa753: native_a753: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa78a:0xa755;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a78a;
            goto native_a755;
        case 0xa755: native_a755: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa757);c->sp-=2;c->pc=0x907e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa758: native_a758: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x01ff);
            c->pc=0xa75b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a75b;
        case 0xa75b: native_a75b: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa78a:0xa75d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a78a;
            goto native_a75d;
        case 0xa75d: native_a75d: /* LDY */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            c->y=(uint16_t)word(r,addr);nz(c,c->y,false);
            c->pc=0xa760;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a760;
        case 0xa760: native_a760: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+((0x84eb&255)+c->y>255)+(c->mf?0:1);
            if(c->y>1023 || cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            load(c,(mapped.mapped?(mapped.data?(c->mf?mapped.data[0]:word(mapped.data,0)):0):(c->mf?rom[0x10000+0x84eb+c->y]:word(rom,0x10000+0x84eb+c->y))));
            c->pc=0xa763;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a763;
        case 0xa763: native_a763: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x0010);
            c->pc=0xa766;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a766;
        case 0xa766: native_a766: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa78a:0xa768;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a78a;
            goto native_a768;
        case 0xa768: native_a768: /* JSR */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xa76a);c->sp-=2;c->pc=0x907e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa76b: native_a76b: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x001f);
            c->pc=0xa76e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a76e;
        case 0xa76e: native_a76e: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0bc5>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0bc5;
            compare(c,c->a,(c->mf?r[addr]:word(r,addr)),c->mf);
            c->pc=0xa771;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a771;
        case 0xa771: native_a771: /* BCC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->c?0xa78a:0xa773;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->c) goto native_a78a;
            goto native_a773;
        case 0xa773: native_a773: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa776;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a776;
        case 0xa776: native_a776: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0072,c->mf);
            c->pc=0xa779;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a779;
        case 0xa779: native_a779: /* BCS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0xa780:0xa77b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(c->c) goto native_a780;
            goto native_a77b;
        case 0xa77b: native_a77b: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0003);
            c->pc=0xa77e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a77e;
        case 0xa77e: native_a77e: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0xa783;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a783;
        case 0xa780: native_a780: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0028);
            c->pc=0xa783;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a783;
        case 0xa783: native_a783: /* LDX */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->xf?0:1);
            if(0x0b49>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b49;
            c->x=(uint16_t)word(r,addr);nz(c,c->x,false);
            c->pc=0xa786;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a786;
        case 0xa786: native_a786: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            ScWorldGuestBeginPrepared(&mapped,w,c,rom,0x80000);
            ScWorldGuestWrite(&mapped,mapped.address,(uint8_t)c->a);if(!c->mf) ScWorldGuestWrite(&mapped,mapped.address+1,(uint8_t)(c->a>>8));
            c->pc=0xa78a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a78a;
        case 0xa78a: native_a78a: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa78b: native_a78b: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0add>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0add;
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa78e;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a78e;
        case 0xa78e: native_a78e: /* STZ */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0adf>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0adf;
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xa791;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a791;
        case 0xa791: native_a791: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b89>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b89;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa794;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a794;
        case 0xa794: native_a794: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0072,c->mf);
            c->pc=0xa797;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a797;
        case 0xa797: native_a797: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa7a1:0xa799;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a7a1;
            goto native_a799;
        case 0xa799: native_a799: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0xa79c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a79c;
        case 0xa79c: native_a79c: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0add>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0add;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa79f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a79f;
        case 0xa79f: native_a79f: /* BRA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->pc=0xa7ac;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7ac;
        case 0xa7a1: native_a7a1: /* CMP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            compare(c,c->a,0x0073,c->mf);
            c->pc=0xa7a4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7a4;
        case 0xa7a4: native_a7a4: /* BNE */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!c->z?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=!c->z?0xa7d9:0xa7a6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            if(!c->z) goto native_a7d9;
            goto native_a7a6;
        case 0xa7a6: native_a7a6: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0xa7a9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7a9;
        case 0xa7a9: native_a7a9: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0adf>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0adf;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa7ac;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7ac;
        case 0xa7ac: native_a7ac: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b85>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b85;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa7af;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7af;
        case 0xa7af: native_a7af: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x00ff);
            c->pc=0xa7b2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7b2;
        case 0xa7b2: native_a7b2: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xa7b3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7b3;
        case 0xa7b3: native_a7b3: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xa7b4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7b4;
        case 0xa7b4: native_a7b4: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xa7b5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7b5;
        case 0xa7b5: native_a7b5: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0004,false);
            c->pc=0xa7b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7b8;
        case 0xa7b8: native_a7b8: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0ae1>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0ae1;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa7bb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7bb;
        case 0xa7bb: native_a7bb: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0b86>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0b86;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xa7be;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7be;
        case 0xa7be: native_a7be: /* AND */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,c->a&0x00ff);
            c->pc=0xa7c1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7c1;
        case 0xa7c1: native_a7c1: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xa7c2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7c2;
        case 0xa7c2: native_a7c2: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xa7c3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7c3;
        case 0xa7c3: native_a7c3: /* ASL */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);
            c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xa7c4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7c4;
        case 0xa7c4: native_a7c4: /* ADC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            add(c,0x0004,false);
            c->pc=0xa7c7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7c7;
        case 0xa7c7: native_a7c7: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0ae3>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0ae3;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa7ca;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7ca;
        case 0xa7ca: native_a7ca: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=false || cost>budget-cycles) return cycles;
            load(c,0x0001);
            c->pc=0xa7cd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7cd;
        case 0xa7cd: native_a7cd: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0a93>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0a93;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa7d0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7d0;
        case 0xa7d0: native_a7d0: /* SEP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)|0x20));
            c->pc=0xa7d2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7d2;
        case 0xa7d2: native_a7d2: /* LDA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(c->mf!=true || cost>budget-cycles) return cycles;
            load(c,0x000b);
            c->pc=0xa7d4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7d4;
        case 0xa7d4: native_a7d4: /* STA */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(0x0006>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0006;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xa7d7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7d7;
        case 0xa7d7: native_a7d7: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xdf));
            c->pc=0xa7d9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7d9;
        case 0xa7d9: native_a7d9: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        case 0xa7da: native_a7da: /* REP */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            interp816_setFlags(c,(uint8_t)(interp816_getFlags(c)&0xcf));
            c->pc=0xa7dc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7dc;
        case 0xa7dc: native_a7dc: /* INC */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6+(c->mf?0:2);
            if(0x0e19>0x1ffe || cost>budget-cycles) return cycles;
            addr=0x0e19;
            value=(c->mf?r[addr]:word(r,addr));
            ++value;
            if(c->mf) r[addr]=(uint8_t)value;else put(r,addr,value);nz(c,value,c->mf);
            c->pc=0xa7df;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            goto native_a7df;
        case 0xa7df: native_a7df: /* RTS */
            if(c->xf || c->e || c->d || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost; if(single) return cycles;
            break;
        default:return cycles;
        }
    }
}

unsigned ScInfrastructureStep(ScWorld *restrict w,Interp816 *restrict c,uint8_t *restrict r,const uint8_t *restrict rom,unsigned budget) {return execute(w,c,r,rom,budget,false);}
unsigned ScInfrastructureInstructionStep(ScWorld *restrict w,Interp816 *restrict c,uint8_t *restrict r,const uint8_t *restrict rom) {return execute(w,c,r,rom,12,true);}
