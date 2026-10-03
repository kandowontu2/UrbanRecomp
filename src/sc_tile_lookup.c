#include "sc_tile_lookup.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <stdlib.h>

/* US tile-to-graphics lookup, including the three ROM graphics tables.
 * Direct C edges replace opcode decoding and mapped bus dispatch. Original
 * hardware multiply writes/reads stay on their real bus; scratch/stack writes
 * and full-coordinate anchors remain observable at every beam deadline. */
static unsigned word(const uint8_t *r,unsigned p) {return r[p]|(unsigned)r[p+1]<<8;}
static void put(uint8_t *r,unsigned p,unsigned v) {r[p]=(uint8_t)v;r[p+1]=(uint8_t)(v>>8);}
static void nz(Interp816 *c,unsigned v,bool byte) {c->z=(v&(byte?255:65535))==0;c->n=(v&(byte?128:32768))!=0;}
static void load(Interp816 *c,unsigned v) {c->a=c->mf?(c->a&0xff00)|(v&255):(uint16_t)v;nz(c,c->a,c->mf);}
static void compare(Interp816 *c,unsigned v) {unsigned mask=c->mf?255:65535,old=c->a&mask;v&=mask;c->c=old>=v;nz(c,old-v,c->mf);}
static unsigned setup(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    int x=(int16_t)word(r,0x1d3),y=(int16_t)word(r,0x1d5);
    bool valid=w->active?ScWorldContains(w,x,y):((x&255)<120 && (y&255)<100);
    unsigned cost=(w->active?37:41)+2*((c->dp&255)!=0);
    if(!valid || cost>budget) return 0;
    c->xf=false;c->mf=true;c->c=false;
    /* Retain the ordered PHA/scratch/PLA effects even when those addresses
     * alias. No earlier register or scratch value survives this setup. */
    r[c->sp]=(uint8_t)y;r[c->dp+0xb1]=r[c->dp+0xb3]&127;
    load(c,r[c->sp]);c->pc=0xc790;c->cyclesUsed=4;return cost;
}
static unsigned graphics(Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned budget) {
    unsigned entry=c->pc,cost=entry==0xc7ed?25:entry==0xc7f6?16:19;
    if(c->a>1023 || cost>budget) return 0;
    unsigned old_x=c->x,index=c->a*2;
    /* PHX leaves its bytes behind even after PLX restores the register. */
    if(entry==0xc7ed) put(r,c->sp-1,old_x);
    c->c=false;c->x=(uint16_t)index;
    c->a=(uint16_t)word(rom,(entry==0xc7ed?0x14f2d:entry==0xc7f6?0x156a9:0x15e25)+index);
    if(entry==0xc7fd) c->a|=0x2000;
    if(entry==0xc7ed) {c->x=(uint16_t)old_x;nz(c,c->x,false);} else nz(c,c->a,false);
    c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;return cost;
}
bool ScTileLookupOwns(uint16_t pc) {return pc>=0xc772 && pc<0xc807;}
static unsigned execute(ScWorld *w,Interp816 *c,uint8_t *r,
    const uint8_t *rom,size_t size,unsigned budget,bool single) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_TILE_LOOKUP_REFERENCE");reference=e && *e=='1';}
    /* Absolute operands in this routine are all below $2000. DBR $00 (the
     * live game), its LoROM mirrors, and $7e all address the same WRAM. */
    if(reference || !w || !c || !r || !rom || size<0x18000 || c->k!=1 ||
       ((c->db&0x7f)>=0x40 && c->db!=0x7e) ||
       c->e || c->d || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp>0x1f4a || !c->read || !c->write) return 0;
    unsigned cycles=0,cost,value,addr,operand;
    for(;;) switch(c->pc) {
        case 0xc772: native_c772: /* REP */
            if(false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:setup(w,c,r,budget-cycles);
            if(cost) {cycles+=cost;goto native_c790;}
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->xf=false;
            c->pc=0xc774;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c774;
        case 0xc774: native_c774: /* SEP */
            if(c->xf || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=true;
            c->pc=0xc776;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c776;
        case 0xc776: native_c776: /* LDA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x0001d3;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xc779;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c779;
        case 0xc779: native_c779: /* BMI */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!w->active && c->n?3:2);
            if(cost>budget-cycles) return cycles;
            if(w->active) c->n=false;
            c->pc=c->n?0x00c7d5:0xc77b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->n) goto native_c7d5;
            goto native_c77b;
        case 0xc77b: native_c77b: /* CMP */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            if(w->active) {
                int coordinate=(int16_t)word(r,0x1d3);
                cost=coordinate<0 || (unsigned)coordinate>=ScWorldWidth(w)?3:2;
                if(cost>budget-cycles) return cycles;
                ScWorldGuestStep(w,c,r);goto native_c77d;
            }
            cost=2+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            compare(c,0x000078);
            c->pc=0xc77d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c77d;
        case 0xc77d: native_c77d: /* BCS */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x00c7d5:0xc77f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_c7d5;
            goto native_c77f;
        case 0xc77f: native_c77f: /* LDA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x0001d5;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xc782;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c782;
        case 0xc782: native_c782: /* BMI */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(!w->active && c->n?3:2);
            if(cost>budget-cycles) return cycles;
            if(w->active) c->n=false;
            c->pc=c->n?0x00c7d5:0xc784;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->n) goto native_c7d5;
            goto native_c784;
        case 0xc784: native_c784: /* CMP */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            if(w->active) {
                int coordinate=(int16_t)word(r,0x1d5);
                cost=coordinate<0 || (unsigned)coordinate>=ScWorldHeight(w)?3:2;
                if(cost>budget-cycles) return cycles;
                ScWorldGuestStep(w,c,r);goto native_c786;
            }
            cost=2+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            compare(c,0x000064);
            c->pc=0xc786;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c786;
        case 0xc786: native_c786: /* BCS */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=(c->c?3:2);
            if(cost>budget-cycles) return cycles;
            c->pc=c->c?0x00c7d5:0xc788;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(c->c) goto native_c7d5;
            goto native_c788;
        case 0xc788: native_c788: /* PHA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xc789;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c789;
        case 0xc789: native_c789: /* LDA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1)+((c->dp&255)!=0);
            if(cost>budget-cycles) return cycles;
            addr=c->dp+0x0000b3;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xc78b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c78b;
        case 0xc78b: native_c78b: /* AND */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,c->a&0x00007f);
            c->pc=0xc78d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c78d;
        case 0xc78d: native_c78d: /* STA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1)+((c->dp&255)!=0);
            if(cost>budget-cycles) return cycles;
            addr=c->dp+0x0000b1;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xc78f;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c78f;
        case 0xc78f: native_c78f: /* PLA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xc790;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c790;
        case 0xc790: native_c790: /* STA */
            if(c->xf || !c->mf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            c->write(c->mem,0x004202,(uint8_t)c->a);
            c->pc=0xc794;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c794;
        case 0xc794: native_c794: /* LDA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,(w->active?ScWorldWidth(w)&255:120));
            c->pc=0xc796;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c796;
        case 0xc796: native_c796: /* STA */
            if(c->xf || !c->mf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            c->write(c->mem,0x004203,(uint8_t)c->a);
            c->pc=0xc79a;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c79a;
        case 0xc79a: native_c79a: /* PHA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xc79b;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c79b;
        case 0xc79b: native_c79b: /* PLA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xc79c;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c79c;
        case 0xc79c: native_c79c: /* NOP */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->pc=0xc79d;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c79d;
        case 0xc79d: native_c79d: /* LDA */
            if(c->xf || !c->mf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,c->read(c->mem,0x004217));
            c->pc=0xc7a1;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7a1;
        case 0xc7a1: native_c7a1: /* XBA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);
            c->pc=0xc7a2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7a2;
        case 0xc7a2: native_c7a2: /* LDA */
            if(c->xf || !c->mf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,c->read(c->mem,0x004216));
            c->pc=0xc7a6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7a6;
        case 0xc7a6: native_c7a6: /* PHA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xc7a7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7a7;
        case 0xc7a7: native_c7a7: /* LDA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1)+((c->dp&255)!=0);
            if(cost>budget-cycles) return cycles;
            addr=c->dp+0x0000b3;
            load(c,(c->mf?r[addr]:word(r,addr)));
            c->pc=0xc7a9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7a9;
        case 0xc7a9: native_c7a9: /* STA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1)+((c->dp&255)!=0);
            if(cost>budget-cycles) return cycles;
            addr=c->dp+0x0000b1;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xc7ab;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7ab;
        case 0xc7ab: native_c7ab: /* PLA */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xc7ac;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7ac;
        case 0xc7ac: native_c7ac: /* REP */
            if(c->xf || c->mf!=true || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->mf=false;
            c->pc=0xc7ae;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7ae;
        case 0xc7ae: native_c7ae: /* CLC */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->c=false;
            c->pc=0xc7af;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7af;
        case 0xc7af: native_c7af: /* ADC */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x0001d3;
            operand=(c->mf?r[addr]:word(r,addr));addr=c->a;value=operand+addr+c->c;c->v=(~(addr^operand)&(addr^value)&32768)!=0;c->c=value>65535;load(c,value);
            c->pc=0xc7b2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7b2;
        case 0xc7b2: native_c7b2: /* ASL */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xc7b3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7b3;
        case 0xc7b3: native_c7b3: /* TAX */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xc7b4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7b4;
        case 0xc7b4: native_c7b4: /* LDA */
            if(c->xf || c->mf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd || !w->active && c->x>0xfdfe) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(w->active) {ScWorldGuestStep(w,c,r);addr=w->bank_anchor[1];value=addr==UINT32_MAX?0:word(w->tiles,addr);} else value=word(r,0x10200+c->x);
            load(c,value);
            c->pc=0xc7b8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7b8;
        case 0xc7b8: native_c7b8: /* STA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x00013b;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xc7bb;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7bb;
        case 0xc7bb: native_c7bb: /* AND */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,c->a&0x0003ff);
            c->pc=0xc7be;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7be;
        case 0xc7be: native_c7be: /* PHA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xc7bf;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7bf;
        case 0xc7bf: native_c7bf: /* JSR */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xc7c1);c->sp-=2;c->pc=0x00c7f6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7f6;
        case 0xc7c2: native_c7c2: /* STA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x00013f;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xc7c5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7c5;
        case 0xc7c5: native_c7c5: /* PLA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xc7c6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7c6;
        case 0xc7c6: native_c7c6: /* PHA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            if(c->mf) r[c->sp--]=(uint8_t)c->a;else {put(r,c->sp-1,c->a);c->sp-=2;}
            c->pc=0xc7c7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7c7;
        case 0xc7c7: native_c7c7: /* JSR */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xc7c9);c->sp-=2;c->pc=0x00c7ed;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7ed;
        case 0xc7ca: native_c7ca: /* STA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x00013d;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xc7cd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7cd;
        case 0xc7cd: native_c7cd: /* PLA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            value=c->mf?r[c->sp+1]:word(r,c->sp+1);c->sp+=c->mf?1:2;load(c,value);
            c->pc=0xc7ce;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7ce;
        case 0xc7ce: native_c7ce: /* JSR */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            put(r,c->sp-1,0xc7d0);c->sp-=2;c->pc=0x00c7fd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7fd;
        case 0xc7d1: native_c7d1: /* STA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x000141;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xc7d4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7d4;
        case 0xc7d4: native_c7d4: /* RTS */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!ScTileLookupOwns(c->pc)) return cycles;break;
        case 0xc7d5: native_c7d5: /* REP */
            if(c->xf || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=3;
            if(cost>budget-cycles) return cycles;
            c->xf=false;
            c->mf=false;
            c->pc=0xc7d7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7d7;
        case 0xc7d7: native_c7d7: /* STZ */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x00013b;
            if(c->mf) r[addr]=(uint8_t)0;else put(r,addr,0);
            c->pc=0xc7da;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7da;
        case 0xc7da: native_c7da: /* LDA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,0x000300);
            c->pc=0xc7dd;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7dd;
        case 0xc7dd: native_c7dd: /* STA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x00013d;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xc7e0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7e0;
        case 0xc7e0: native_c7e0: /* LDA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,0x000301);
            c->pc=0xc7e3;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7e3;
        case 0xc7e3: native_c7e3: /* STA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x00013f;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xc7e6;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7e6;
        case 0xc7e6: native_c7e6: /* LDA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,0x00014b);
            c->pc=0xc7e9;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7e9;
        case 0xc7e9: native_c7e9: /* STA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            addr=0x000141;
            if(c->mf) r[addr]=(uint8_t)c->a;else put(r,addr,c->a);
            c->pc=0xc7ec;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7ec;
        case 0xc7ec: native_c7ec: /* RTS */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!ScTileLookupOwns(c->pc)) return cycles;break;
        case 0xc7ed: native_c7ed: /* PHX */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:graphics(c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;if(!ScTileLookupOwns(c->pc)) return cycles;break;}
            cost=3+1;
            if(cost>budget-cycles) return cycles;
            if(false) r[c->sp--]=(uint8_t)c->x;else {put(r,c->sp-1,c->x);c->sp-=2;}
            c->pc=0xc7ee;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7ee;
        case 0xc7ee: native_c7ee: /* ASL */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xc7ef;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7ef;
        case 0xc7ef: native_c7ef: /* TAX */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xc7f0;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7f0;
        case 0xc7f0: native_c7f0: /* LDA */
            if(c->xf || c->mf || c->x>2046 || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,word(rom,0x14f2d+c->x));
            c->pc=0xc7f4;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7f4;
        case 0xc7f4: native_c7f4: /* PLX */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=4+1;
            if(cost>budget-cycles) return cycles;
            c->x=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->x,false);
            c->pc=0xc7f5;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7f5;
        case 0xc7f5: native_c7f5: /* RTS */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!ScTileLookupOwns(c->pc)) return cycles;break;
        case 0xc7f6: native_c7f6: /* ASL */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:graphics(c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;if(!ScTileLookupOwns(c->pc)) return cycles;break;}
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xc7f7;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7f7;
        case 0xc7f7: native_c7f7: /* TAX */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xc7f8;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7f8;
        case 0xc7f8: native_c7f8: /* LDA */
            if(c->xf || c->mf || c->x>2046 || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,word(rom,0x156a9+c->x));
            c->pc=0xc7fc;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7fc;
        case 0xc7fc: native_c7fc: /* RTS */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!ScTileLookupOwns(c->pc)) return cycles;break;
        case 0xc7fd: native_c7fd: /* ASL */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=single?0:graphics(c,r,rom,budget-cycles);
            if(cost) {cycles+=cost;if(!ScTileLookupOwns(c->pc)) return cycles;break;}
            cost=2;
            if(cost>budget-cycles) return cycles;
            value=c->a&(c->mf?255:65535);c->c=(value&(c->mf?128:32768))!=0;load(c,value<<1);
            c->pc=0xc7fe;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7fe;
        case 0xc7fe: native_c7fe: /* TAX */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2;
            if(cost>budget-cycles) return cycles;
            c->x=c->a;nz(c,c->x,false);
            c->pc=0xc7ff;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c7ff;
        case 0xc7ff: native_c7ff: /* LDA */
            if(c->xf || c->mf || c->x>2046 || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=5+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,word(rom,0x15e25+c->x));
            c->pc=0xc803;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c803;
        case 0xc803: native_c803: /* ORA */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=2+(c->mf?0:1);
            if(cost>budget-cycles) return cycles;
            load(c,c->a|0x002000);
            c->pc=0xc806;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            goto native_c806;
        case 0xc806: native_c806: /* RTS */
            if(c->xf || c->mf!=false || c->sp<0x100 || c->sp>0x1ffd) return cycles;
            cost=6;
            if(cost>budget-cycles) return cycles;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;
            c->cyclesUsed=(uint8_t)cost;cycles+=cost;
            if(single) return cycles;
            if(!ScTileLookupOwns(c->pc)) return cycles;break;
        default:return cycles;
    }
}

/* The scheduler permits one original instruction to cross a beam event.
 * Atomic C uses that same rule; bounded batches never exceed their budget. */
unsigned ScTileLookupStep(ScWorld *w,Interp816 *c,uint8_t *r,
    const uint8_t *rom,size_t size,unsigned budget) {
    return execute(w,c,r,rom,size,budget,false);
}
unsigned ScTileLookupInstructionStep(ScWorld *w,Interp816 *c,uint8_t *r,
    const uint8_t *rom,size_t size) {
    return execute(w,c,r,rom,size,12,true);
}
