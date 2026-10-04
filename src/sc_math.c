#include "sc_math.h"
#include "snes/interp816.h"
#include <stdlib.h>

static unsigned word(const uint8_t *r,unsigned a) {return r[a]|(unsigned)r[a+1]<<8;}
static void put(uint8_t *r,unsigned a,unsigned v) {r[a]=(uint8_t)v;r[a+1]=(uint8_t)(v>>8);}
static uint32_t wide(const uint8_t *r,unsigned a) {return word(r,a)|(uint32_t)word(r,a+2)<<16;}
static void put32(uint8_t *r,unsigned a,uint32_t v) {put(r,a,v);put(r,a+2,v>>16);}
static void nz(Interp816 *c,unsigned v) {c->n=(v&32768)!=0;c->z=(v&65535)==0;}
static unsigned divide16_setup(Interp816 *c,uint8_t *r,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_DIV16_SETUP_REFERENCE");reference=e && *e=='1';}
    if(reference || !c->read || c->dp<0x28 || c->sp<0x108 || c->sp>0x1ffd) return 0;
    unsigned next_dp=c->dp-8,cost=104+6*((next_dp&255)!=0);
    if(cost>budget || (c->sp+2>=next_dp && c->sp-3<=next_dp+7)) return 0;
    unsigned ret=word(r,c->sp+1);
    /* Inline byte offsets are ROM operands, never device registers. */
    if(ret<0x7000 || ret>0xfff9) return 0;
    unsigned src0=c->read(c->mem,0x030001+ret),src1=c->read(c->mem,0x030002+ret);
    unsigned dest=c->read(c->mem,0x030003+ret);
    if((unsigned)c->dp+src0>0x1ffe || (unsigned)c->dp+src1>0x1ffe || (unsigned)c->dp+dest>0x1ffe) return 0;
    put(r,c->sp+1,ret+3);put(r,c->sp-1,c->dp);put(r,c->sp-3,ret+3);
    /* Read operands after the original stack shadows and in their original
     * order, retaining valid overlapping direct-page input layouts. */
    put(r,next_dp+2,word(r,next_dp+8+src0));
    put(r,next_dp+4,word(r,next_dp+8+src1));
    put(r,next_dp,dest);put(r,next_dp+6,0);
    c->sp-=2;c->dp=(uint16_t)next_dp;c->a=(uint16_t)dest;c->x=(uint16_t)src1;c->y=16;
    c->mf=c->xf=false;c->c=true;c->v=c->n=c->z=false;c->pc=0xa406;c->cyclesUsed=3;
    return cost;
}
/* A beam deadline can split a multiply iteration. Keep the intermediate
 * 16-bit shifts, carries and scratch in C as well as complete iterations. */
static unsigned multiply16_resume(Interp816 *c,uint8_t *r,unsigned budget) {
    unsigned cycles=0,dp=(c->dp&255)!=0;
    for(;;) {
        unsigned entry=c->pc,cost,value,old;
        if(entry<=0xa341 && (!c->x || c->x>16)) return cycles;
        switch(entry) {
        case 0xa32e:case 0xa330:case 0xa332:case 0xa33f:cost=7+dp;break;
        case 0xa336:case 0xa339:case 0xa33b:cost=4+dp;break;
        case 0xa334:case 0xa33d:cost=c->c?2:3;break;
        case 0xa338:case 0xa341:cost=2;break;
        case 0xa342:cost=c->z?2:3;break;
        default:return cycles;
        }
        if(cost>budget-cycles) return cycles;
        switch(entry) {
        case 0xa32e:case 0xa330:case 0xa332: {
            value=c->dp+(entry==0xa32e?6:entry==0xa330?8:4);old=word(r,value);
            unsigned result=((old<<1)|(entry==0xa330 && c->c))&65535;
            put(r,value,result);c->c=(old&32768)!=0;nz(c,result);c->pc+=2;break;
        }
        case 0xa334:case 0xa33d:c->pc=c->c?(uint16_t)(entry+2):0xa341;break;
        case 0xa336:c->a=(uint16_t)word(r,c->dp+6);nz(c,c->a);c->pc+=2;break;
        case 0xa338:c->c=false;++c->pc;break;
        case 0xa339: {
            old=c->a;value=word(r,c->dp+2);unsigned sum=old+value+c->c;
            c->a=(uint16_t)sum;c->c=sum>65535;c->v=((~(old^value))&(old^sum)&32768)!=0;
            nz(c,c->a);c->pc+=2;break;
        }
        case 0xa33b:put(r,c->dp+6,c->a);c->pc+=2;break;
        case 0xa33f:value=c->dp+8;put(r,value,word(r,value)+1);nz(c,word(r,value));c->pc+=2;break;
        case 0xa341:--c->x;nz(c,c->x);++c->pc;break;
        case 0xa342:c->pc=c->z?0xa344:0xa32e;break;
        }
        c->cyclesUsed=(uint8_t)cost;cycles+=cost;
    }
}
/* The divide loop often begins near a beam deadline. Resumable C stages
 * retain its intermediate carry, remainder and flags instead of falling back
 * to the interpreter for the remainder of that iteration. */
static bool divide_driver_enabled(void);
static unsigned divide16(Interp816 *c,uint8_t *r,unsigned budget,bool single) {
    if(c->pc<=0xa415 && (c->y>16 || (!c->y && c->pc!=0xa415))) return 0;
    unsigned cycles=0,dp=(c->dp&255)!=0;
    for(;;) {
        unsigned entry=c->pc,cost,value,old;
        /* Fuse an uninterrupted restoring-divide iteration into arithmetic
         * on two words. Residual continuations below retain every shorter
         * deadline; the calendar and interrupt cost is still the original. */
        if(!single && entry==0xa406 && c->y && divide_driver_enabled()) {
            unsigned dividend=word(r,c->dp+2),divisor=word(r,c->dp+4);
            unsigned remainder=((word(r,c->dp+6)<<1)|(dividend>>15))&65535;
            bool subtract=remainder>=divisor;
            cost=22+4*dp+(subtract?10+2*dp:3)+2+(c->y==1?2:3);
            if(cost<=budget-cycles) {
                put(r,c->dp+2,(dividend<<1)|c->c);c->a=(uint16_t)remainder;
                if(subtract) {
                    c->a=(uint16_t)(remainder-divisor);
                    c->v=((remainder^divisor)&(remainder^c->a)&32768)!=0;
                }
                put(r,c->dp+6,c->a);c->c=subtract;--c->y;nz(c,c->y);
                c->pc=c->y?0xa406:0xa417;c->cyclesUsed=c->y?3:2;cycles+=cost;
                continue;
            }
        }
        switch(entry) {
        case 0xa406:case 0xa408:case 0xa417:cost=7+dp;break;
        case 0xa40a:case 0xa40c:case 0xa410:case 0xa412:
        case 0xa419:case 0xa41b:cost=4+dp;break;
        case 0xa41d:cost=5+dp;break;
        case 0xa40e:cost=c->c?2:3;break;
        case 0xa414:cost=2;break;
        case 0xa415:cost=c->z?2:3;break;
        case 0xa41f:cost=5;break;
        case 0xa420:cost=6;break;
        default:return cycles;
        }
        if(cost>budget-cycles) return cycles;
        if((entry==0xa41f || entry==0xa420) && (c->sp<0x100 || c->sp>0x1ffd)) return cycles;
        if(entry==0xa41d && ((unsigned)c->dp+8+c->x>0x1ffe)) return cycles;
        switch(entry) {
        case 0xa406:case 0xa408:case 0xa417: {
            value=c->dp+(entry==0xa408?6:2);old=word(r,value);
            unsigned result=((old<<1)|c->c)&65535;
            put(r,value,result);c->c=(old&32768)!=0;nz(c,result);c->pc+=2;break;
        }
        case 0xa40a:c->a=(uint16_t)word(r,c->dp+6);nz(c,c->a);c->pc+=2;break;
        case 0xa40c:value=word(r,c->dp+4);c->c=c->a>=value;nz(c,c->a-value);c->pc+=2;break;
        case 0xa40e:c->pc=c->c?0xa410:0xa414;break;
        case 0xa410: {
            old=c->a;value=word(r,c->dp+4);int difference=(int)old-(int)value-!c->c;
            c->a=(uint16_t)difference;c->c=difference>=0;
            c->v=((old^value)&(old^c->a)&32768)!=0;nz(c,c->a);c->pc+=2;break;
        }
        case 0xa412:put(r,c->dp+6,c->a);c->pc+=2;break;
        case 0xa414:--c->y;nz(c,c->y);++c->pc;break;
        case 0xa415:c->pc=c->z?0xa417:0xa406;break;
        case 0xa419:c->x=(uint16_t)word(r,c->dp);nz(c,c->x);c->pc+=2;break;
        case 0xa41b:c->a=(uint16_t)word(r,c->dp+2);nz(c,c->a);c->pc+=2;break;
        case 0xa41d:put(r,c->dp+8+c->x,c->a);c->pc+=2;break;
        case 0xa41f:c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp);++c->pc;break;
        case 0xa420:c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;break;
        }
        c->cyclesUsed=(uint8_t)cost;cycles+=cost;
        if(single) return cycles;
        if(entry==0xa420) return cycles; /* The caller gets its own hooks. */
    }
}
static bool divide_driver_enabled(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_DIV16_DRIVER_REFERENCE");reference=e && *e=='1';}
    return !reference;
}
bool ScMathDivideOwns(unsigned pc) {return pc>=0xa3cf && pc<=0xa420;}
static bool math_context(const Interp816 *c,const uint8_t *r) {
    return c && r && c->k==3 && c->db==3 && !c->e && !c->d &&
        !c->waiting && !c->stopped && !c->nmiWanted && !(c->irqWanted && !c->i) &&
        c->dp>=0x20 && c->dp<=0x1fe0;
}
/* The complete setup above is still useful when it fits. A beam/IRQ deadline
 * otherwise leaves its continuation here, with ordered stack shadows and
 * operand reads. No opcode fetch or interpreter dispatch is needed. */
static unsigned divide16_driver(Interp816 *c,uint8_t *r,unsigned budget,bool single) {
    if(!divide_driver_enabled() || !math_context(c,r) || !c->read) return 0;
    static int setup_reference=-1,body_reference=-1;
    if(setup_reference<0) {const char *e=getenv("SC_DIV16_SETUP_REFERENCE");setup_reference=e && *e=='1';}
    if(body_reference<0) {const char *e=getenv("SC_DIV16_REFERENCE");body_reference=e && *e=='1';}
    if(c->pc>=0xa406) {
        if(body_reference || c->mf || c->xf) return 0;
        return divide16(c,r,budget,single);
    }
    if(setup_reference || (c->pc!=0xa3cf && (c->mf || c->xf))) return 0;
    if(c->pc==0xa3cf) {
        if(c->dp<0x28 || c->sp<0x108 || c->sp>0x1ffd) return 0;
        unsigned at=c->dp-8,ret=word(r,c->sp+1);
        if((c->sp+2>=at && c->sp-3<=at+7) || ret<0x7000 || ret>0xfff9) return 0;
        for(unsigned i=1;i<=3;++i)
            if(c->dp+c->read(c->mem,0x030000+ret+i)>0x1ffe) return 0;
        if(!single) {unsigned cost=divide16_setup(c,r,budget);if(cost) return cost;}
    }
    unsigned cycles=0;
    for(;;) {
        unsigned entry=c->pc,cost,value,old;
        switch(entry) {
        case 0xa3cf:case 0xa3d8:case 0xa3d4:case 0xa3de:
        case 0xa3e6:case 0xa3f1:case 0xa3fc:case 0xa403:cost=3;break;
        case 0xa3d1:case 0xa3e2:cost=5;break;
        case 0xa3d7:case 0xa3da:case 0xa3db:cost=4;break;
        case 0xa3d2:case 0xa3d3:case 0xa3dc:case 0xa3dd:
        case 0xa3e1:case 0xa3e9:case 0xa3f4:cost=2;break;
        case 0xa3e3:case 0xa3ee:case 0xa3f9:cost=6;break;
        case 0xa3ea:case 0xa3f5:cost=5+((c->dp&255)!=0);break;
        case 0xa3ec:case 0xa3f7:case 0xa3ff:case 0xa401:cost=4+((c->dp&255)!=0);break;
        default:return cycles;
        }
        if(cost>budget-cycles) return cycles;
        if((entry==0xa3d1 || entry==0xa3e2) && (c->sp<0x100 || c->sp>0x1ffd)) return cycles;
        if((entry==0xa3d7 || entry==0xa3da || entry==0xa3db) && (c->sp<0x102 || c->sp>0x1fff)) return cycles;
        if((entry==0xa3e3 || entry==0xa3ee || entry==0xa3f9) && (c->y<0x7000 || c->y>0xfff9)) return cycles;
        if((entry==0xa3ea || entry==0xa3f5) && c->dp+8+c->x>0x1ffe) return cycles;
        if(entry==0xa3e1 && (c->a<0x20 || c->a>0x1fe0)) return cycles;
        switch(entry) {
        case 0xa3cf:c->mf=c->xf=false;c->pc=0xa3d1;break;
        case 0xa3d1:case 0xa3e2:c->a=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->a);++c->pc;break;
        case 0xa3d2:c->y=c->a;nz(c,c->y);++c->pc;break;
        case 0xa3d3:c->c=false;++c->pc;break;
        case 0xa3d4:
            old=c->a;value=old+3+c->c;c->a=(uint16_t)value;c->c=value>65535;
            c->v=((~(old^3))&(old^value)&32768)!=0;nz(c,c->a);c->pc+=3;break;
        case 0xa3d7:case 0xa3da:case 0xa3db:
            put(r,c->sp-1,entry==0xa3da?c->dp:c->a);c->sp-=2;++c->pc;break;
        case 0xa3d8:c->mf=false;c->pc+=2;break;
        case 0xa3dc:c->a=c->dp;nz(c,c->a);++c->pc;break;
        case 0xa3dd:c->c=true;++c->pc;break;
        case 0xa3de:
            old=c->a;value=old+0xfff7+c->c;c->a=(uint16_t)value;c->c=value>65535;
            c->v=((old^8)&(old^value)&32768)!=0;nz(c,c->a);c->pc+=3;break;
        case 0xa3e1:c->dp=c->a;nz(c,c->dp);++c->pc;break;
        case 0xa3e3:case 0xa3ee:case 0xa3f9:
            value=0x030000+c->y+(entry==0xa3e3?1:entry==0xa3ee?2:3);
            c->a=(uint16_t)(c->read(c->mem,value)|(unsigned)c->read(c->mem,value+1)<<8);
            nz(c,c->a);c->pc+=3;break;
        case 0xa3e6:case 0xa3f1:case 0xa3fc:c->a&=255;nz(c,c->a);c->pc+=3;break;
        case 0xa3e9:case 0xa3f4:c->x=c->a;nz(c,c->x);++c->pc;break;
        case 0xa3ea:case 0xa3f5:c->a=(uint16_t)word(r,c->dp+8+c->x);nz(c,c->a);c->pc+=2;break;
        case 0xa3ec:case 0xa3f7:case 0xa3ff:case 0xa401:
            put(r,c->dp+(entry==0xa3ec?2:entry==0xa3f7?4:entry==0xa401?6:0),entry==0xa401?0:c->a);
            c->pc+=2;break;
        case 0xa403:c->y=16;nz(c,16);c->pc=0xa406;break;
        }
        c->cyclesUsed=(uint8_t)cost;cycles+=cost;
        if(single || entry==0xa403) return cycles;
    }
}
static unsigned rng_stages(Interp816 *c,uint8_t *r,unsigned budget) {
    if(c->pc==0x9035 || c->pc==0x904d || c->pc==0x905b ||
       c->pc==0x907e || c->pc==0x9092 || c->pc==0x90a0 || c->pc==0x90a6) {
        static int reference=-1;
        if(reference<0) {const char *e=getenv("SC_RNG_REFERENCE");reference=e && *e=='1';}
        if(reference) return 0;
        if(c->pc==0x9035 || c->pc==0x907e) {
            bool range=c->pc==0x9035;
            unsigned next_dp=c->dp-(range?6:2),cost=(range?52:42)+(range?2:1)*((next_dp&255)!=0);
            if(cost>budget || c->sp<0x108 || c->sp>0x1fff ||
               (c->sp>=next_dp && c->sp-(range?6:4)<=next_dp+(range?3:1))) return 0;
            unsigned saved_x=c->xf?c->x&255:c->x,saved_y=c->xf?c->y&255:c->y;
            r[c->sp]=interp816_getFlags(c);put(r,c->sp-2,c->dp);
            put(r,c->sp-4,saved_x);if(range) put(r,c->sp-6,saved_y);
            c->y=(uint16_t)saved_y;
            c->sp-=range?7:5;c->dp=(uint16_t)next_dp;
            if(range) {++c->a;put(r,next_dp+2,c->a);}
            put(r,next_dp,0);c->mf=c->xf=false;c->c=true;c->v=false;
            c->x=12;nz(c,12);c->pc=range?0x904d:0x9092;c->cyclesUsed=3;
            return cost;
        }
        if(c->pc==0x90a6) {
            if(budget<6 || c->sp<0x100 || c->sp>0x1ffd) return 0;
            c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;
            return 6;
        }
        if(c->mf || c->xf) return 0;
        if(c->pc==0x905b) {
            unsigned cost=18+((c->dp&255)!=0);
            if(cost>budget || c->sp<0x108 || c->sp>0x1fff) return 0;
            put(r,0xccf,c->a);c->a=0x7fff;nz(c,c->a);put(r,c->dp,c->a);
            put(r,c->sp-1,0x9065);c->sp-=2;c->pc=0xa3cf;c->cyclesUsed=6;
            return cost;
        }
        if(c->pc==0x90a0) {
            if(budget<19 || c->sp<0x100 || c->sp>0x1ffa) return 0;
            put(r,0xccf,c->a);c->x=(uint16_t)word(r,c->sp+1);c->dp=(uint16_t)word(r,c->sp+3);
            c->sp+=5;interp816_setFlags(c,r[c->sp]);c->pc=0x90a6;c->cyclesUsed=4;
            return 19;
        }
        if(!c->x || c->x>12 || (c->x&1)) return 0;
        unsigned cycles=0,dp=(c->dp&255)!=0;
        while(c->x) {
            unsigned cost=(c->x==2?26:27)+2*dp;
            if(cost>budget-cycles) break;
            unsigned source=word(r,0xccd+c->x);
            put(r,0xccf+c->x,source);
            /* Read the scratch after the state shift, as the original ADC
             * does. This also preserves overlapping direct-page layouts. */
            unsigned old=word(r,c->dp),sum=source+old+c->c;
            put(r,c->dp,sum);c->a=(uint16_t)sum;
            c->c=sum>65535;c->v=((~(source^old))&(source^sum)&32768)!=0;
            c->x-=2;nz(c,c->x);c->cyclesUsed=c->x?3:2;cycles+=cost;
        }
        if(cycles && !c->x) c->pc=c->pc==0x904d?0x905b:0x90a0;
        return cycles;
    }
    return 0;
}
static bool rng_driver_enabled(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_RNG_DRIVER_REFERENCE");reference=e && *e=='1';}
    return !reference;
}
bool ScMathRngOwns(unsigned pc) {return pc>=0x9035 && pc<=0x90a6;}
/* The two additive generators share their state array, but have different
 * scratch frames and epilogues. Retain every shorter deadline in C, including
 * the range helper's continuations between its two division calls. Division
 * has a different map clock scale, so each JSR yields to the scheduler. */
static unsigned rng_driver(Interp816 *c,uint8_t *r,unsigned budget,bool single) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_RNG_REFERENCE");reference=e && *e=='1';}
    if(reference || !rng_driver_enabled() || !math_context(c,r) || c->sp<0x108 || c->sp>0x1fff) return 0;
    unsigned cycles=0;
    for(;;) {
        unsigned entry=c->pc,cost,at=0,value,old;
        if(!ScMathRngOwns(entry) || c->dp<0x20 || c->dp>0x1fe0) return cycles;
        bool neutral=entry==0x9035 || entry==0x9036 || entry==0x9041 ||
            entry==0x907c || entry==0x907d || entry==0x907e || entry==0x907f ||
            entry==0x908a || entry==0x90a5 || entry==0x90a6;
        if(!neutral && c->mf) return cycles;
        bool early=(entry>=0x9038 && entry<=0x9040) || (entry>=0x9081 && entry<=0x9089);
        if(!neutral && !early && c->xf) return cycles;
        if((entry==0x904d || entry==0x9092) && (!c->x || c->x>12 || (c->x&1))) return cycles;
        if(!single) {
            unsigned fused=rng_stages(c,r,budget-cycles);
            if(fused) {
                cycles+=fused;
                if(entry==0x90a6 || c->pc==0xa3cf) return cycles;
                continue;
            }
        }
        unsigned dp=(c->dp&255)!=0;
        switch(entry) {
        case 0x9035:case 0x907e:cost=3;break;
        case 0x9036:case 0x9041:case 0x907f:case 0x908a:cost=3;break;
        case 0x9038:case 0x9081:cost=4;break;
        case 0x9039:case 0x9082:case 0x9043:case 0x9044:case 0x908c:cost=4;break;
        case 0x903a:case 0x903b:case 0x903f:case 0x9045:case 0x9057:case 0x9058:
        case 0x9083:case 0x9084:case 0x9088:case 0x909c:case 0x909d:cost=2;break;
        case 0x903c:case 0x904a:case 0x905e:case 0x906c:case 0x9085:case 0x908f:cost=3;break;
        case 0x9040:case 0x9089:case 0x9079:case 0x907a:case 0x907b:
        case 0x90a3:case 0x90a4:cost=5;break;
        case 0x9046:case 0x9048:case 0x9053:case 0x9055:case 0x9061:
        case 0x906f:case 0x9077:case 0x908d:case 0x9098:case 0x909a:cost=4+dp;break;
        case 0x904d:case 0x9092:cost=5;at=0xccd+c->x;break;
        case 0x9050:case 0x9095:cost=7;at=0xccf+c->x;break;
        case 0x905b:case 0x9069:case 0x90a0:cost=5;break;
        case 0x9059:case 0x909e:cost=c->z?2:3;break;
        case 0x9063:case 0x9071:case 0x907d:case 0x90a6:cost=6;break;
        case 0x907c:case 0x90a5:cost=4;break;
        default:return cycles;
        }
        if(cost>budget-cycles || (at && at>0x1ffe)) return cycles;
        if((entry==0x9038 || entry==0x9039 || entry==0x9043 || entry==0x9044 ||
            entry==0x9081 || entry==0x9082 || entry==0x908c || entry==0x9063 || entry==0x9071) &&
            (c->sp<0x102 || c->sp>0x1fff)) return cycles;
        if((entry==0x9040 || entry==0x9089 || entry==0x9079 || entry==0x907a ||
            entry==0x907b || entry==0x90a3 || entry==0x90a4 || entry==0x907d || entry==0x90a6) &&
            c->sp>0x1ffd) return cycles;
        if((entry==0x907c || entry==0x90a5) && c->sp>0x1ffe) return cycles;
        if((entry==0x903f || entry==0x9088) && (c->a<0x20 || c->a>0x1fe0)) return cycles;
        switch(entry) {
        case 0x9035:case 0x907e:r[c->sp--]=interp816_getFlags(c);++c->pc;break;
        case 0x9036:case 0x907f:
            interp816_setFlags(c,interp816_getFlags(c)&~0x20);c->pc+=2;break;
        case 0x9041:case 0x908a:
            interp816_setFlags(c,interp816_getFlags(c)&~0x30);c->pc+=2;break;
        case 0x9038:case 0x9081:put(r,c->sp-1,c->dp);c->sp-=2;++c->pc;break;
        case 0x9039:case 0x9082:case 0x9043:case 0x9044:case 0x908c:
            put(r,c->sp-1,(entry==0x9043 || entry==0x908c)?c->x:entry==0x9044?c->y:c->a);
            c->sp-=2;++c->pc;break;
        case 0x903a:case 0x9083:c->a=c->dp;nz(c,c->a);++c->pc;break;
        case 0x903b:case 0x9084:c->c=true;++c->pc;break;
        case 0x903c:case 0x9085:
            old=c->a;value=entry==0x903c?6:2;
            {int difference=(int)old-(int)value-!c->c;
             c->a=(uint16_t)difference;c->c=difference>=0;
             c->v=((old^value)&(old^c->a)&32768)!=0;}
            nz(c,c->a);c->pc+=3;break;
        case 0x903f:case 0x9088:c->dp=c->a;nz(c,c->dp);++c->pc;break;
        case 0x9040:case 0x9089:c->a=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->a);++c->pc;break;
        case 0x9045:++c->a;nz(c,c->a);++c->pc;break;
        case 0x9046:put(r,c->dp+2,c->a);c->pc+=2;break;
        case 0x9048:case 0x908d:put(r,c->dp,0);c->pc+=2;break;
        case 0x904a:case 0x908f:c->x=12;nz(c,c->x);c->pc+=3;break;
        case 0x904d:case 0x9092:c->a=(uint16_t)word(r,at);nz(c,c->a);c->pc+=3;break;
        case 0x9050:case 0x9095:put(r,at,c->a);c->pc+=3;break;
        case 0x9053:case 0x9098:
            old=c->a;value=word(r,c->dp);
            {unsigned sum=old+value+c->c;c->a=(uint16_t)sum;c->c=sum>65535;
             c->v=((~(old^value))&(old^sum)&32768)!=0;}
            nz(c,c->a);c->pc+=2;break;
        case 0x9055:case 0x9061:case 0x906f:case 0x909a:put(r,c->dp,c->a);c->pc+=2;break;
        case 0x9057:case 0x9058:case 0x909c:case 0x909d:--c->x;nz(c,c->x);++c->pc;break;
        case 0x9059:case 0x909e:c->pc=c->z?(uint16_t)(entry+2):entry==0x9059?0x904d:0x9092;break;
        case 0x905b:case 0x90a0:put(r,0xccf,c->a);c->pc+=3;break;
        case 0x905e:c->a=0x7fff;nz(c,c->a);c->pc+=3;break;
        case 0x9063:case 0x9071:put(r,c->sp-1,entry+2);c->sp-=2;c->pc=0xa3cf;break;
        case 0x9069:c->a=(uint16_t)word(r,0xccf);nz(c,c->a);c->pc+=3;break;
        case 0x906c:c->a&=0x7fff;nz(c,c->a);c->pc+=3;break;
        case 0x9077:c->a=(uint16_t)word(r,c->dp);nz(c,c->a);c->pc+=2;break;
        case 0x9079:c->y=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->y);++c->pc;break;
        case 0x907a:case 0x90a3:c->x=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->x);++c->pc;break;
        case 0x907b:case 0x90a4:c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp);++c->pc;break;
        case 0x907c:case 0x90a5:interp816_setFlags(c,r[++c->sp]);++c->pc;break;
        case 0x907d:case 0x90a6:c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;break;
        }
        c->cyclesUsed=(uint8_t)cost;cycles+=cost;
        if(single || c->pc==0xa3cf || entry==0x907d || entry==0x90a6) return cycles;
    }
}
unsigned ScMathInstructionStep(Interp816 *c,uint8_t *r) {
    if(!c) return 0;
    if(ScMathRngOwns(c->pc)) return rng_driver(c,r,UINT32_MAX,true);
    if(ScMathDivideOwns(c->pc)) return divide16_driver(c,r,UINT32_MAX,true);
    return 0;
}
unsigned ScMathBatchStep(Interp816 *c,uint8_t *r,unsigned budget) {
    unsigned entry=c?c->pc:0,cycles=ScMathStep(c,r,budget);
    if(cycles && entry>=0xa3cf && entry<0xa406 && c->pc==0xa406 && divide_driver_enabled())
        cycles+=ScMathStep(c,r,budget-cycles);
    if(!cycles || entry<0x9035 || entry>0x90a6) return cycles;
    while(cycles<budget && c->pc>=0x9035 && c->pc<=0x90a6 && entry!=0x90a6) {
        entry=c->pc;unsigned cost=ScMathStep(c,r,budget-cycles);
        if(!cost) break;cycles+=cost;
    }
    return cycles;
}
unsigned ScMathStep(Interp816 *c,uint8_t *r,unsigned budget) {
    if(!math_context(c,r)) return 0;
    if(ScMathDivideOwns(c->pc) && divide_driver_enabled()) return divide16_driver(c,r,budget,false);
    if(c->pc==0xa3cf) return divide16_setup(c,r,budget);
    if(ScMathRngOwns(c->pc)) {
        if(rng_driver_enabled()) return rng_driver(c,r,budget,false);
        return rng_stages(c,r,budget);
    }
    if(c->mf || c->xf) return 0;
    static int multiply_reference=-1;
    if(multiply_reference<0) {const char *e=getenv("SC_MUL16_REFERENCE");multiply_reference=e && *e=='1';}
    if(!multiply_reference && c->pc>0xa32e && c->pc<=0xa342)
        return multiply16_resume(c,r,budget);
    if(c->pc>=0xa406 && c->pc<=0xa420) {
        static int reference=-1;
        if(reference<0) {const char *e=getenv("SC_DIV16_REFERENCE");reference=e && *e=='1';}
        if(!reference) return divide16(c,r,budget,false);
        if(c->pc!=0xa406) return 0; /* Previous complete-iteration C path below. */
    }
    unsigned entry=c->pc,at=c->dp,dp=(at&255)!=0,cycles=0;
    bool multiply=entry==0xa32e || entry==0xa395;
    unsigned limit=entry==0xa32e || entry==0xa406?16:32;
    if(entry!=0xa32e && entry!=0xa395 && entry!=0xa406 && entry!=0xa462) return 0;
    unsigned count=multiply?c->x:c->y;
    if(!count || count>limit) return 0;
    while(count) {
        unsigned cost,accumulator=c->a;bool carry=c->c,overflow=c->v;
        if(entry==0xa32e) {
            unsigned a=word(r,at+2),b=word(r,at+4);
            uint32_t product=wide(r,at+6)<<1;
            bool add=(b&32768)!=0;
            unsigned low=product&65535,sum=low+a;
            cost=21+3*dp+(add?2:3)+2+(count==1?2:3);
            carry=false;
            if(add) {
                cost+=4+dp+2+4+dp+4+dp+(sum>65535?2+7+dp:3);
                accumulator=sum&65535;carry=sum>65535;
                overflow=((~(low^a))&(low^sum)&32768)!=0;
                product+=a;
            }
            if(cost>budget-cycles) break;
            put(r,at+4,b<<1);put32(r,at+6,product);
        } else if(entry==0xa395) {
            uint32_t a=wide(r,at+4),b=wide(r,at+8);
            uint64_t product=((uint64_t)wide(r,at+12)|((uint64_t)wide(r,at+16)<<32))<<1;
            bool add=(b&UINT32_C(0x80000000))!=0;
            cost=42+6*dp+(add?2:3)+2+(count==1?2:3);carry=false;
            if(add) {
                uint64_t sum=(uint32_t)product+(uint64_t)a;
                unsigned low=(product&65535)+(a&65535);
                unsigned old_high=((uint32_t)product>>16),a_high=a>>16;
                unsigned high=old_high+a_high+(low>65535);
                accumulator=high&65535;carry=sum>UINT32_MAX;
                overflow=((~(old_high^a_high))&(old_high^high)&32768)!=0;
                cost+=26+6*dp+(carry?2+7+dp+(((product>>32)&65535)==65535?2+7+dp:3):3);
                product+=a;
            }
            if(cost>budget-cycles) break;
            put32(r,at+8,b<<1);put32(r,at+12,(uint32_t)product);put32(r,at+16,(uint32_t)(product>>32));
        } else if(entry==0xa406) {
            unsigned dividend=word(r,at+2),divisor=word(r,at+4);
            unsigned remainder=((word(r,at+6)<<1)|(dividend>>15))&65535;
            dividend=((dividend<<1)|carry)&65535;
            carry=remainder>=divisor;
            cost=7+dp+7+dp+4+dp+4+dp+(carry?2+4+dp+4+dp:3)+2+(count==1?2:3);
            accumulator=remainder;
            if(carry) {
                unsigned difference=(remainder-divisor)&65535;
                overflow=((remainder^divisor)&(remainder^difference)&32768)!=0;
                accumulator=remainder=difference;
            }
            if(cost>budget-cycles) break;
            put(r,at+2,dividend);put(r,at+6,remainder);
        } else {
            uint32_t dividend=wide(r,at+2),divisor=wide(r,at+6);
            uint32_t remainder=(wide(r,at+10)<<1)|(dividend>>31);
            dividend=(dividend<<1)|carry;
            unsigned hi=remainder>>16,div_hi=divisor>>16,borrow=(remainder&65535)<(divisor&65535);
            unsigned difference=(hi-div_hi-borrow)&65535;
            carry=remainder>=divisor;accumulator=difference;
            overflow=((hi^div_hi)&(hi^difference)&32768)!=0;
            cost=28+4*dp+4+dp+4+dp+4+dp+4+dp+(carry?2+24+6*dp:3)+2+(count==1?2:3);
            if(carry) remainder-=divisor;
            if(cost>budget-cycles) break;
            put32(r,at+2,dividend);put32(r,at+10,remainder);
        }
        cycles+=cost;--count;
        c->a=(uint16_t)accumulator;c->c=carry;c->v=overflow;
        if(multiply) c->x=(uint16_t)count;else c->y=(uint16_t)count;
        nz(c,count);c->cyclesUsed=count?3:2;
    }
    if(!cycles && entry==0xa32e && !multiply_reference) return multiply16_resume(c,r,budget);
    if(cycles && !count) c->pc=entry==0xa32e?0xa344:entry==0xa395?0xa3bb:entry==0xa406?0xa417:0xa483;
    return cycles;
}
