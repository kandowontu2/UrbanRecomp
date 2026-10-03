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
static unsigned divide16(Interp816 *c,uint8_t *r,unsigned budget) {
    if(c->pc<=0xa415 && (c->y>16 || (!c->y && c->pc!=0xa415))) return 0;
    unsigned cycles=0,dp=(c->dp&255)!=0;
    for(;;) {
        unsigned entry=c->pc,cost,value,old;
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
        if(entry==0xa420) return cycles; /* The caller gets its own hooks. */
    }
}
unsigned ScMathBatchStep(Interp816 *c,uint8_t *r,unsigned budget) {
    unsigned entry=c?c->pc:0,cycles=ScMathStep(c,r,budget);
    if(!cycles || entry<0x9035 || entry>0x90a6) return cycles;
    while(cycles<budget && c->pc>=0x9035 && c->pc<=0x90a6 && entry!=0x90a6) {
        entry=c->pc;unsigned cost=ScMathStep(c,r,budget-cycles);
        if(!cost) break;cycles+=cost;
    }
    return cycles;
}
unsigned ScMathStep(Interp816 *c,uint8_t *r,unsigned budget) {
    if(!c || !r || c->k!=3 || c->db!=3 || c->e || c->d ||
        c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
        c->dp<0x20 || c->dp>0x1fe0) return 0;
    if(c->pc==0xa3cf) return divide16_setup(c,r,budget);
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
    if(c->mf || c->xf) return 0;
    static int multiply_reference=-1;
    if(multiply_reference<0) {const char *e=getenv("SC_MUL16_REFERENCE");multiply_reference=e && *e=='1';}
    if(!multiply_reference && c->pc>0xa32e && c->pc<=0xa342)
        return multiply16_resume(c,r,budget);
    if(c->pc>=0xa406 && c->pc<=0xa420) {
        static int reference=-1;
        if(reference<0) {const char *e=getenv("SC_DIV16_REFERENCE");reference=e && *e=='1';}
        if(!reference) return divide16(c,r,budget);
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
