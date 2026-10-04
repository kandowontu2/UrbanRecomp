#include "sc_development.h"
#include "sc_world_guest.h"
#include "sc_zoning.h"
#include "sc_math.h"
#include <string.h>
#include <stdlib.h>
#include "snes/interp816.h"

static uint64_t (*profile_clock)(void);
static uint64_t profile_frequency,profile_calls[65536],profile_ticks[65536],profile_max[65536];
void ScDevelopmentSetProfileClock(uint64_t (*clock)(void),uint64_t frequency) {
    profile_clock=frequency?clock:NULL;profile_frequency=frequency;
    memset(profile_calls,0,sizeof profile_calls);memset(profile_ticks,0,sizeof profile_ticks);
    memset(profile_max,0,sizeof profile_max);
}
void ScDevelopmentReportProfile(FILE *out) {
    if(!profile_clock || !out) return;
    uint16_t sites[24]={0};
    for(unsigned pc=0;pc<65536;++pc) {
        if(!profile_calls[pc]) continue;
        for(unsigned n=0;n<24;++n) if(profile_ticks[pc]>profile_ticks[sites[n]]) {
            memmove(sites+n+1,sites+n,(23-n)*sizeof *sites);sites[n]=(uint16_t)pc;break;
        }
    }
    for(unsigned n=0;n<24 && profile_calls[sites[n]];++n) {
        unsigned pc=sites[n];fprintf(out,"[development profile] pc=%04x calls=%llu total-ms=%.3f max-us=%.3f\n",
            pc,(unsigned long long)profile_calls[pc],profile_ticks[pc]*1000.0/profile_frequency,
            profile_max[pc]*1000000.0/profile_frequency);
    }
}

static unsigned word(const uint8_t *r, unsigned p) {
    return r[p] | ((unsigned)r[p+1]<<8);
}
static void put(uint8_t *r, unsigned p, unsigned v) {
    r[p]=(uint8_t)v; r[p+1]=(uint8_t)(v>>8);
}
void ScDevelopmentReset(ScDevelopment *s) { memset(s,0,sizeof *s); }
static bool free_house_reference(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_FREE_HOUSE_REFERENCE");reference=e && *e=='1';}
    return reference!=0;
}
unsigned ScDevelopmentNativeStep(Interp816 *c,uint8_t *ram) {
    if(c->k!=3 || c->db!=3 || c->mf || c->xf || c->e || c->d ||
       c->nmiWanted || (c->irqWanted && !c->i)) return 0;
    if(c->pc==0x923d || c->pc==0x92dc || c->pc==0x9388) {
        unsigned entry=c->pc,tile=word(ram,0xb89),cycles=0,helper=0,return_pc=0;
        if(entry==0x923d) {helper=0x847a;return_pc=0x923f;}
        else if(entry==0x92dc) {
            c->a=tile;c->c=tile>=0x39a;c->z=tile==0x39a;c->n=((tile-0x39a)&32768)!=0;
            cycles=5+3+(tile<0x39a?3:2);
            if(tile>=0x39a) {c->a=6;c->z=c->n=false;cycles+=3+3;}
            else {helper=0x8456;return_pc=0x92eb;}
        } else {
            /* The world-aware free-house probe is handled by NativeBatch. */
            if(tile==0x84) return 0;
            c->a=tile;c->c=tile>=0x376;c->z=tile==0x376;c->n=((tile-0x376)&32768)!=0;
            cycles=5+3+(tile<0x376?3:2);
            if(tile>=0x376) {c->a=48;c->z=c->n=false;cycles+=3+3;}
            else {
                c->c=tile>=0x84;c->z=false;c->n=((tile-0x84)&32768)!=0;
                cycles+=3+3;helper=0x842f;return_pc=0x93a1;
            }
        }
        if(helper) {
            put(ram,c->sp-1,return_pc);c->sp-=2;c->pc=helper;
            cycles+=6+ScDevelopmentNativeStep(c,ram)+6;c->sp+=2;
            if(entry==0x9388) c->cyclesUsed=6;
        }
        put(ram,c->dp,c->a);cycles+=4+((c->dp&255)!=0);c->cyclesUsed=4+((c->dp&255)!=0);
        /* Extra attempts reuse the existing transport result at local $04.
         * This is the same redirect as ScDevelopmentStepWorld at skip. */
        c->pc=entry==0x923d?0x926f:entry==0x92dc?0x931b:0x93d1;
        return cycles;
    }
    if(c->pc==0x926f || c->pc==0x931b || c->pc==0x93d1) {
        unsigned entry=c->pc,transport=word(ram,c->dp+4),tile=word(ram,0xb89);
        if(transport==65535) return 0;
        if(entry==0x93d1 && tile==0x84 && free_house_reference()) return 0;
        c->a=transport;c->c=false;c->z=false;c->n=((transport-65535)&32768)!=0;
        unsigned cycles=4+((c->dp&255)!=0)+3+3;
        if(entry==0x93d1) {
            c->a=tile;c->c=tile>=0x84;c->z=tile==0x84;c->n=((tile-0x84)&32768)!=0;
            cycles+=5+3+(tile==0x84?3:2);
            if(tile==0x84) {c->pc=0x93ed;c->cyclesUsed=3;return cycles;}
        }
        c->pc=entry==0x926f?0x927b:entry==0x931b?0x9327:0x93e5;
        unsigned fast=ScDevelopmentNativeStep(c,ram);
        return cycles+fast;
    }
    if(c->pc==0x927b || c->pc==0x9327 || c->pc==0x93e5) {
        if(c->dp<0x20 || c->dp>0x1fc0 || c->sp<0x010a || c->sp>0x1fff) return 0;
        unsigned entry=c->pc;
        put(ram,c->sp-1,entry+2);c->sp-=2;c->pc=0x907e;
        unsigned cycles=ScDevelopmentNativeStep(c,ram);c->sp+=2;
        c->a&=7;c->n=false;c->z=c->a==0;
        c->pc=c->z?(entry==0x927b?0x9283:entry==0x9327?0x932f:0x93ed):
            entry==0x927b?0x92cc:entry==0x9327?0x9378:0x9447;
        c->cyclesUsed=c->z?2:3;return cycles+15+c->cyclesUsed;
    }
    if(c->pc==0x907e) {
        if(c->dp<0x20 || c->dp>0x1fc0 || c->sp<0x0108 || c->sp>0x1fff) return 0;
        /* Six-word additive generator, with carry chaining and descending
         * state shift. PHP/PLP restores every input flag. Scratch and popped
         * stack bytes remain visible to saves and native callers. */
        ram[c->sp]=interp816_getFlags(c);
        put(ram,c->sp-2,c->dp);put(ram,c->sp-4,c->x);
        unsigned sum=0,carry=1;
        put(ram,c->dp-2,0);
        for(unsigned i=12;i;i-=2) {
            unsigned value=word(ram,0xccd+i);
            put(ram,0xccf+i,value);
            sum+=value+carry;carry=sum>65535;sum&=65535;
            put(ram,c->dp-2,sum);
        }
        put(ram,0xccf,sum);c->a=sum;c->pc=0x90a6;c->cyclesUsed=4;
        return 222+13*(((c->dp-2)&255)!=0);
    }
    unsigned origin,period,end,empty;
    switch(c->pc) {
    case 0x842f:origin=0x99;period=36;end=0x8455;empty=0x843b;break;
    case 0x8456:origin=0x144;period=45;end=0x8479;empty=0x8462;break;
    case 0x847a:origin=0x201;period=36;end=0x849d;empty=0x8486;break;
    default:return 0;
    }
    unsigned tile=word(ram,0xb89);
    if(tile<origin) {
        c->a=0;c->c=c->v=c->n=false;c->z=true;c->pc=empty;c->cyclesUsed=3;return 15;
    }
    unsigned remaining=(tile-origin)&255,reductions=remaining/period;
    remaining%=period;
    unsigned steps=remaining/9+1;
    c->y=steps+(origin==0x99);c->a=c->y;c->c=c->v=c->n=false;c->z=false;
    if(origin==0x99) c->a*=8;
    c->pc=end;c->cyclesUsed=2;
    return 30+9*reductions+7*steps+(origin==0x99?6:0);
}

static unsigned free_house_capacity(ScWorld *w,Interp816 *c,uint8_t *ram) {
    if(free_house_reference() || c->pc!=0x9388 || word(ram,0xb89)!=0x84 || c->k!=3 || c->db!=3 ||
       c->mf || c->xf || c->e || c->d || c->waiting || c->stopped ||
       c->nmiWanted || (c->irqWanted && !c->i) || c->dp<0x20 || c->dp>0x1fc0 ||
       c->sp<0x10a || c->sp>0x1fff || (c->sp>=c->dp-6 && c->sp-5<=c->dp+10)) return 0;
    /* Probe with the nested call's CPU frame first. A rejected layout leaves
     * all state untouched; a successful probe supplies the original scratch,
     * popped stack bytes, map anchor and flags. */
    Interp816 next=*c;next.pc=0x9a3e;next.sp-=2;
    unsigned cost=ScWorldGuestHousingStep(w,&next,ram,512);
    if(!cost) return 0;
    put(ram,c->sp-1,0x939c);next.sp+=2;put(ram,c->dp,next.a);
    next.pc=0x93d1;next.cyclesUsed=4+((c->dp&255)!=0);*c=next;
    return cost+35+((c->dp&255)!=0);
}
static void nz(Interp816 *c,unsigned v) {c->z=(uint16_t)v==0;c->n=(v&32768)!=0;}
static void compare(Interp816 *c,unsigned v) {c->c=c->a>=v;nz(c,c->a-v);}
static void add(Interp816 *c,unsigned v) {
    unsigned a=c->a,sum=a+v+c->c;c->a=sum;c->c=sum>65535;
    c->v=((~(a^v))&(a^sum)&32768)!=0;nz(c,sum);
}
static void subtract(Interp816 *c,unsigned v) {
    unsigned a=c->a,result=a-v-!c->c;c->a=result;c->c=result<65536;
    c->v=((a^v)&(a^result)&32768)!=0;nz(c,result);
}
static unsigned random_call(Interp816 *c,uint8_t *r,unsigned return_pc) {
    put(r,c->sp-1,return_pc);c->sp-=2;c->pc=0x907e;
    bool byte=c->mf;uint8_t flags=interp816_getFlags(c);c->mf=false;
    unsigned cost=ScDevelopmentNativeStep(c,r);
    /* The generator selects word A internally but restores its caller's
     * flags. Byte callers retain the whole generated A and their PHP byte. */
    c->mf=byte;r[c->sp]=flags;c->sp+=2;return cost+12;
}
static bool decision_reference(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_ZONE_DECISION_REFERENCE");reference=e && *e=='1';}
    return reference!=0;
}
/* Ordered growth/decline decisions. The random draws and signed overflow
 * branches are part of the simulation, so keep them exactly as the game
 * evaluates them and yield at the selected mutation's call boundary. */
static unsigned zone_decision(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,size_t size) {
    unsigned entry=c->pc,kind=entry==0x9283?0:entry==0x932f?1:2;
    if(decision_reference() || (entry!=0x9283 && entry!=0x932f && entry!=0x93ed) ||
       !w || !w->active || c->mf || c->dp<0x1000 || !rom || size!=0x80000) return 0;
    const unsigned score_pc[]={0x9a31,0x9a0e,0x99d2},demand[]={0xbb1,0xbaf,0xbad};
    const unsigned positive_rng[]={0x929b,0x9347,0x9405},negative_rng[]={0x92b8,0x9364,0x9433};
    const unsigned grow_call[]={0x92ac,0x9358,0x9427},grow[]={0x95fc,0x9567,0x9495};
    const unsigned decline_call[]={0x92c9,0x9375,0x9444},decline[]={0x9794,0x974b,0x9659};
    const unsigned end[]={0x92cc,0x9378,0x9447};
    unsigned dp=(c->dp&255)!=0;
    Interp816 next=*c;next.a=word(r,c->dp+4);nz(&next,next.a);next.sp-=2;next.pc=score_pc[kind];
    unsigned cost=ScWorldGuestKernelStep(w,&next,r,rom,size,512);
    if(!cost) return 0;
    put(r,c->sp-1,entry+4);next.sp+=2;*c=next;
    unsigned cycles=4+dp+6+cost+6;
    c->c=false;add(c,word(r,demand[kind]));cycles+=2+5;
    c->y=word(r,0xb87);nz(c,c->y);cycles+=5;
    if(c->n) cycles+=3;
    else {cycles+=2+3;c->a=0xfe0c;nz(c,c->a);}
    put(r,c->dp+2,c->a);cycles+=4+dp;
    compare(c,0xfea2);cycles+=3;
    bool negative=c->n;
    cycles+=negative?3:2;
    if(!negative) {
        cycles+=random_call(c,r,positive_rng[kind]+2);
        put(r,c->dp+6,c->a);cycles+=4+dp;
        c->a=word(r,c->dp+2);nz(c,c->a);cycles+=4+dp;
        c->c=true;subtract(c,0x670c);cycles+=2+3;
        compare(c,word(r,c->dp+6));cycles+=4+dp;
        negative=c->v;cycles+=negative?3:2;
        if(!negative) {negative=c->n;cycles+=negative?3:2;}
        if(!negative) {
            unsigned call=grow_call[kind],target=grow[kind];
            if(kind==2) {
                c->a=word(r,c->dp);nz(c,c->a);cycles+=4+dp;
                if(c->z) {
                    cycles+=2+random_call(c,r,0x941c);c->a&=3;nz(c,c->a);cycles+=3;
                    if(c->z) {cycles+=2;call=0x9422;target=0x9449;}
                    else cycles+=3;
                } else cycles+=3;
            }
            put(r,c->sp-1,call+2);c->sp-=2;c->pc=target;c->cyclesUsed=6;
            return cycles+6;
        }
    }
    c->a=word(r,c->dp+2);nz(c,c->a);cycles+=4+dp;
    compare(c,350);cycles+=3;
    if(!c->n) {c->pc=end[kind];c->cyclesUsed=3;return cycles+3;}
    cycles+=2+random_call(c,r,negative_rng[kind]+2);
    put(r,c->dp+6,c->a);cycles+=4+dp;
    c->a=word(r,c->dp+2);nz(c,c->a);cycles+=4+dp;
    c->c=false;add(c,0x670c);cycles+=2+3;
    compare(c,word(r,c->dp+6));cycles+=4+dp;
    if(c->v) {c->pc=end[kind];c->cyclesUsed=3;return cycles+3;}
    cycles+=2;
    if(!c->n) {c->pc=end[kind];c->cyclesUsed=3;return cycles+3;}
    cycles+=2;put(r,c->sp-1,decline_call[kind]+2);c->sp-=2;
    c->pc=decline[kind];c->cyclesUsed=6;return cycles+6;
}
static unsigned decline_empty(Interp816 *c,uint8_t *r) {
    if(decision_reference() || c->pc!=0x9659) return 0;
    unsigned tile=word(r,0xb89);
    if(tile==0x37a || tile==0x38c || (tile!=0x383 && tile!=0x395 && r[c->dp])) return 0;
    c->mf=false;c->a=tile;nz(c,tile);unsigned cycles=3+5;
    const unsigned checks[]={0x37a,0x38c,0x383,0x395};
    for(unsigned i=0;i<4;++i) {
        compare(c,checks[i]);cycles+=3;
        if(c->z) {cycles+=3;goto done;}
        cycles+=2;
    }
    c->mf=true;c->a=(c->a&0xff00)|r[c->dp];c->z=true;c->n=false;
    cycles+=3+3+((c->dp&255)!=0)+3;
done:
    c->pc=0x9732;c->cyclesUsed=3;return cycles+3;
}
static unsigned attempt_transport(Interp816 *c,uint8_t *r) {
    unsigned entry=c->pc;
    if(decision_reference() || c->mf || (entry!=0x926f && entry!=0x931b && entry!=0x93d1) ||
       word(r,c->dp+4)!=65535) return 0;
    c->a=65535;c->c=c->z=true;c->n=false;put(r,c->sp-1,entry+9);c->sp-=2;
    c->pc=entry==0x926f?0x9794:entry==0x931b?0x974b:0x9659;c->cyclesUsed=6;
    return 4+((c->dp&255)!=0)+3+2+6;
}
static bool residential_reference(void) {
    static int reference=-1;
    if(reference<0) {
        const char *e=getenv("SC_RESIDENTIAL_REFERENCE"),*site=getenv("SC_HOUSE_SITE_REFERENCE");
        reference=(e && *e=='1') || (site && *site=='1');
    }
    return reference!=0;
}
static void byte_nz(Interp816 *c,unsigned v) {c->z=(v&255)==0;c->n=(v&128)!=0;}
static void byte_compare(Interp816 *c,unsigned v) {unsigned a=c->a&255;c->c=a>=v;byte_nz(c,a-v);}
static unsigned house_builder_frame(Interp816 *c,uint8_t *r) {
    if(residential_reference() || c->pc!=0x97c9 || c->dp<0x100c || c->sp<0x100a ||
       (c->sp>=c->dp-12 && c->sp-7<=c->dp+10)) return 0;
    unsigned old=c->dp,value=old-12;
    put(r,c->sp-1,old);put(r,c->sp-3,c->a);c->sp-=2;c->dp=value;
    put(r,c->dp,0);put(r,c->dp+2,0);c->mf=false;c->y=1;
    c->c=true;c->v=((old^12)&(old^value)&32768)!=0;c->n=c->z=false;
    c->pc=0x97db;c->cyclesUsed=3;return 36+2*((value&255)!=0);
}
/* One complete candidate in the free-house builder. Preserve the eight-lot
 * order, the original neighbour-scoring operands and random tie breaks;
 * leave the selected building's final write at its existing boundary. */
static unsigned house_candidate(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,size_t size) {
    if(residential_reference() || c->pc!=0x97db || !w || !w->active || !rom || size!=0x80000 ||
       c->y<1 || c->y>=9 || c->dp<0x1000 || c->sp<0x1008 ||
       (c->sp>=c->dp && c->sp-7<=c->dp+10)) return 0;
    unsigned dp=(c->dp&255)!=0,lot=c->y;
    unsigned y=r[0xb86]+rom[0x19851+lot],x=r[0xb85]+rom[0x19848+lot];
    c->a=(uint16_t)(((y&255)<<8)|(x&255));c->mf=true;
    c->c=x>255;c->v=((~(r[0xb85]^rom[0x19848+lot]))&(r[0xb85]^x)&128)!=0;
    put(r,c->sp-1,lot);c->sp-=2;put(r,c->sp-1,0x97ef);c->sp-=2;
    unsigned cycles=36;
    c->mf=false;put(r,c->dp+6,c->a);cycles+=3+4+dp;
    put(r,c->sp-1,0x9860);c->sp-=2;c->pc=0x849e;
    ScWorldGuestStepPrepared(w,c,r);c->sp+=2;cycles+=12;
    c->a&=1023;nz(c,c->a);cycles+=3;
    bool valid=c->a==0;
    cycles+=valid?3:2;
    if(!valid) {
        compare(c,0x80);cycles+=3;
        if(!c->c) cycles+=3;
        else {cycles+=2;compare(c,0x89);cycles+=3;valid=!c->c;cycles+=valid?3:2;}
    }
    c->mf=true;
    if(!valid) {c->a=(c->a&0xff00)|255;byte_nz(c,255);cycles+=3+2+3;}
    else {
        c->a=(c->a&0xff00)|1;r[c->dp+10]=1;c->x=0;c->n=false;c->z=true;
        c->pc=0x987f;cycles+=3+2+3+dp+3;
        unsigned cost=ScWorldGuestHouseSiteStep(w,c,r,rom,size,512);
        cycles+=cost;c->a=(c->a&0xff00)|r[c->dp+10];byte_nz(c,c->a);cycles+=3+dp;
    }
    c->sp+=2;c->y=(uint16_t)word(r,c->sp+1);c->sp+=2;cycles+=6+5;
    byte_compare(c,0);cycles+=2;
    bool selected=false;
    if(c->n) cycles+=3;
    else {
        cycles+=2;byte_compare(c,r[c->dp]);cycles+=3+dp;
        if(c->z) {
            cycles+=2+random_call(c,r,0x97fb);c->a=(c->a&0xff00)|(c->a&7);byte_nz(c,c->a);cycles+=2;
            if(!c->z) cycles+=3;
            else {cycles+=2+3;selected=true;}
        } else {
            cycles+=3;
            if(!c->c) cycles+=3;
            else {cycles+=2;r[c->dp]=(uint8_t)c->a;cycles+=3+dp;selected=true;}
        }
    }
    if(selected) {put(r,c->dp+2,c->y);cycles+=4+dp;}
    ++c->y;c->c=c->y>=9;nz(c,c->y-9);cycles+=2+3;
    c->pc=c->y<9?0x97db:0x980e;c->cyclesUsed=c->y<9?3:2;
    return cycles+c->cyclesUsed;
}
static unsigned density_delta(ScWorld *w,Interp816 *c,uint8_t *r) {
    if(residential_reference() || c->pc!=0x961d || !w || !w->active || c->dp<0x1000 || c->sp<0x1004 ||
       (w->huge && ((uint8_t)w->coord[2][0]!=r[0xb85] || (uint8_t)w->coord[2][1]!=r[0xb86]))) return 0;
    unsigned delta=c->a&255,x=(w->huge?w->coord[2][0]:r[0xb85])/8,y=(w->huge?w->coord[2][1]:r[0xb86])/8;
    if(x>=ScWorldWidth(w)/8 || y>=ScWorldHeight(w)/8) return 0;
    unsigned index=y*(ScWorldWidth(w)/8)+x,at=2*index;
    unsigned value=(uint16_t)((delta>=128?delta|0xff00:delta)<<2),old=word(w->fields[7],at),sum=old+value;
    r[c->sp]=(uint8_t)delta;put(r,c->sp-2,0x962f);
    w->field_anchor[2]=index;put(r,0xb3f,x);put(r,0xb3d,y);put(w->fields[7],at,sum);
    c->a=(uint16_t)sum;c->x=(uint16_t)at;c->mf=true;c->c=sum>65535;
    c->v=((~(old^value))&(old^sum)&32768)!=0;nz(c,sum);
    c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;
    return delta>=128?93:91;
}
static unsigned house_remove(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,size_t size) {
    if(residential_reference() || c->pc!=0x970b || !w || !w->active || !rom || size!=0x80000 ||
       c->mf || (c->y&1) || c->y>=18 || c->dp<0x1000) return 0;
    unsigned cycles=0,dp=(c->dp&255)!=0;
    while(c->y<18) {
        unsigned base=word(r,c->dp+8),offset=word(rom,0x19733+c->y);
        offset=(offset/240)*(2*ScWorldWidth(w))+offset%240;
        unsigned sum=base+offset;c->x=(uint16_t)sum;
        c->v=((~(base^offset))&(base^sum)&32768)!=0;
        int logical=(int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor);
        bool valid=w->map_anchor!=UINT32_MAX && logical>=0 && (unsigned)logical+2<=2*ScWorldCells(w);
        c->a=valid?word(w->tiles,logical):0;
        cycles+=4+dp+2+5+2+6+3;
        if(c->a<0x89) cycles+=3;
        else {
            cycles+=2+3;
            if(c->a>=0x95) cycles+=3;
            else {
                cycles+=2;c->a=0x80+c->y/2;c->c=c->v=c->n=c->z=false;
                if(valid) {put(w->tiles,logical,c->a);ScWorldTilesTouch(w,(unsigned)logical,2);}
                c->pc=0x9732;c->cyclesUsed=3;return cycles+2+2+3+6+3;
            }
        }
        c->y+=2;c->c=c->y>=18;nz(c,c->y-18);
        c->cyclesUsed=c->y==18?2:3;cycles+=4+3+c->cyclesUsed;
    }
    c->pc=0x9732;return cycles;
}
static unsigned free_house_decline(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,size_t size) {
    unsigned tile=word(r,0xb89),capacity=r[c->dp];
    if(residential_reference() || c->pc!=0x9659 || !w || !w->active || !rom || size!=0x80000 ||
       tile==0x37a || tile==0x38c || tile==0x383 || tile==0x395 || !capacity || capacity>=16 ||
       c->dp<0x1000 || c->sp<0x1006 ||
       !ScWorldContains(w,w->huge?w->coord[2][0]:r[0xb85],w->huge?w->coord[2][1]:r[0xb86]) ||
       (w->huge && ((uint8_t)w->coord[2][0]!=r[0xb85] || (uint8_t)w->coord[2][1]!=r[0xb86]))) return 0;
    c->a=(tile&0xff00)|255;c->mf=true;c->c=false;c->z=false;c->n=false;
    put(r,c->sp-1,0x96f7);c->sp-=2;c->pc=0x961d;
    unsigned cycles=52+((c->dp&255)!=0)+density_delta(w,c,r);
    c->a=(uint16_t)(((uint8_t)(r[0xb86]-1)<<8)|(uint8_t)(r[0xb85]-1));c->mf=true;
    put(r,c->sp-1,0x9705);c->sp-=2;c->pc=0x849e;
    ScWorldGuestStepPrepared(w,c,r);c->sp+=2;put(r,c->dp+8,c->x);
    c->y=0;c->z=true;c->n=false;c->pc=0x970b;
    cycles+=30+4+((c->dp&255)!=0)+3;
    return cycles+house_remove(w,c,r,rom,size);
}
unsigned ScDevelopmentNativeBatch(ScDevelopment *s,ScWorld *w,Interp816 *c,uint8_t *ram,
                                 const uint8_t *rom,size_t rom_size) {
    if(!s->repeating || c->dp<0x20 || c->dp>0x1fc0 || c->sp<0x010c || c->sp>0x1fff)
        return 0;
    unsigned cycles=0;
    static int driver_reference=-1;
    if(driver_reference<0) {const char *e=getenv("SC_DEVELOPMENT_DRIVER_REFERENCE");driver_reference=e && *e=='1';}
    /* Accelerated attempts consume no guest beam/calendar time. Keep their
     * ordered RNG, capacity, housing and mutation operations in C. Unknown
     * continuations return to the caller. The final PLD/RTS stays with the
     * scheduler and receives its ordinary timing. */
    if(c->k!=3 || c->db!=3 || c->xf || c->e || c->d || c->waiting || c->stopped ||
       c->nmiWanted || (c->irqWanted && !c->i) ||
       (c->sp>=c->dp-6 && c->sp-7<=c->dp+10)) return 0;
    static int control_enabled=-1;
    if(control_enabled<0) {const char *e=getenv("SC_ZONE_CONTROL_REFERENCE");control_enabled=!e || *e!='1';}
    for(unsigned segments=0;segments<500 && s->repeating;++segments) {
        unsigned profile_pc=c->pc;
        uint64_t profile_start=profile_clock?profile_clock():0;
        /* Select the owning operation once. Rejected operations have no
         * writes, so probing every unrelated helper adds no behavior. */
        unsigned cost=0;
        switch(c->pc) {
        case 0x9388:
            cost=free_house_capacity(w,c,ram);
            if(!cost)cost=ScDevelopmentNativeStep(c,ram);break;
        case 0x842f:case 0x8456:case 0x847a:case 0x907e:
        case 0x923d:case 0x92dc:case 0x927b:case 0x9327:case 0x93e5:
            cost=ScDevelopmentNativeStep(c,ram);break;
        case 0x926f:case 0x931b:case 0x93d1:
            cost=ScDevelopmentNativeStep(c,ram);
            if(!cost)cost=attempt_transport(c,ram);break;
        case 0x9283:case 0x932f:case 0x93ed:
            cost=zone_decision(w,c,ram,rom,rom_size);break;
        case 0x9659:
            cost=decline_empty(c,ram);
            if(!cost)cost=free_house_decline(w,c,ram,rom,rom_size);break;
        case 0x97c9:cost=house_builder_frame(c,ram);break;
        case 0x97db:cost=house_candidate(w,c,ram,rom,rom_size);break;
        case 0x961d:cost=density_delta(w,c,ram);break;
        case 0x970b:cost=house_remove(w,c,ram,rom,rom_size);break;
        default:break;
        }
        if(!cost && control_enabled && ScZoningOwns(c->pc))
            cost=ScZoningControlStep(w,c,ram,rom,4096);
        if(!cost && control_enabled && c->pc==0x9468) cost=ScZoningQualityStep(w,c,ram,4096);
        if(!cost && control_enabled && c->pc>=0x98b8 && c->pc<=0x99bf)
            cost=ScWorldGuestZoneArtStep(w,c,ram,rom,4096);
        if(!cost && control_enabled && (c->pc==0x9940 || c->pc==0x9952 || c->pc==0x998b))
            cost=ScWorldGuestZoneReplacementStep(w,c,ram,rom,4096);
        if(!cost && control_enabled && (c->pc==0x99be || c->pc==0x99bf)) {
            if(c->pc==0x99be) {
                c->dp=(uint16_t)word(ram,c->sp+1);c->sp+=2;nz(c,c->dp);cost=5;
            }
            c->pc=(uint16_t)(word(ram,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cost+=6;
        }
        if(!cost && control_enabled && c->pc==0x90a6) {
            c->pc=(uint16_t)(word(ram,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cost=6;
        }
        if(!cost && !decision_reference()) {
            unsigned target=0;
            if(c->pc==0x9732) target=word(ram,c->sp+1)+1;
            if(target==0x93db || target==0x9447) {
                c->sp+=2;c->pc=target;c->cyclesUsed=6;cost=6;
            } else if(c->pc==0x93db) {c->pc=0x9447;c->cyclesUsed=3;cost=3;}
        }
        /* These shared helpers have no beam/calendar observers during an
         * extra attempt. Keep their original CPU/RAM/world publications and
         * RNG order in this driver instead of returning through main. */
        if(!cost && !driver_reference) {
            if(ScMathRngOwns(c->pc) || ScMathDivideOwns(c->pc))
                cost=ScMathBatchStep(c,ram,4096);
            if(!cost && c->pc==0x9a3e)cost=ScWorldGuestHousingStep(w,c,ram,512);
            if(!cost && c->pc==0x987f)cost=ScWorldGuestHouseSiteStep(w,c,ram,rom,rom_size,512);
            if(!cost && ScZoningOwns(c->pc))cost=ScZoningAcceleratedStep(w,c,ram,rom,4096);
        }
        if(profile_clock) {
            uint64_t elapsed=profile_clock()-profile_start;
            ++profile_calls[profile_pc];profile_ticks[profile_pc]+=elapsed;
            if(elapsed>profile_max[profile_pc])profile_max[profile_pc]=elapsed;
        }
        if(!cost) break;
        cycles+=cost;
        if(c->pc==s->attempt) {
            cost=ScDevelopmentNativeStep(c,ram);
            if(!cost) break;
            cycles+=cost;
        }
        if(c->pc!=s->end) continue;
        uint16_t next=ScDevelopmentStepWorld(s,w,ram,c->pc,c->dp,c->sp,1);
        if(next==c->pc) break;
        c->pc=next;c->mf=c->xf=false;
    }
    return cycles;
}

static uint16_t zone(unsigned tile) {
    if ((tile>=0x80 && tile<0x129) || (tile>=0x376 && tile<0x39a)) return 0x937a;
    if ((tile>=0x137 && tile<0x1f4) || tile>=0x39a) return 0x92ce;
    if (tile>=0x1f4 && tile<0x249) return 0x922f;
    return 0;
}
uint16_t ScDevelopmentStep(ScDevelopment *s, uint8_t *ram,
                           uint16_t pc, uint16_t dp, uint16_t sp, int speed) {
    return ScDevelopmentStepWorld(s,NULL,ram,pc,dp,sp,speed);
}
uint16_t ScDevelopmentStepWorld(ScDevelopment *s, const ScWorld *w, uint8_t *ram,
                           uint16_t pc, uint16_t dp, uint16_t sp, int speed) {
    bool large=w && w->active;
    if (speed<=1 && !s->remaining) return pc;
    if (!s->remaining && (pc==0x922f || pc==0x92ce || pc==0x937a)) {
        s->entry=pc; s->dp=dp; s->sp=sp;
        s->cell=(uint16_t)word(ram,0x0b49);
        if ((s->cell&1) || (large?!ScWorldContains(w,w->huge?w->coord[2][0]:ram[0xb85],w->huge?w->coord[2][1]:ram[0xb86]):s->cell>=24000)) return pc;
        s->remaining=speed;
        s->repeating=false;
        if (pc==0x922f) {
            s->end=0x92cc; s->capacity=0x923d; s->skip=0x9242; s->attempt=0x926f;
        } else if (pc==0x92ce) {
            s->end=0x9378; s->capacity=0x92dc; s->skip=0x92ee; s->attempt=0x931b;
        } else {
            s->end=0x9447; s->capacity=0x9388; s->skip=0x93a4; s->attempt=0x93d1;
        }
        ++s->attempts;
        return pc;
    }
    /* Skip the object/population tally and transport probe on extra attempts.
     * Keep their result at local $04, but recalculate local $00 (capacity)
     * through the ROM helper after each change of the zone's tile. */
    if (s->remaining && s->repeating && pc==s->skip) return s->attempt;
    if (s->remaining && pc==s->end) {
        /* Only redirect the live zone frame, never a nested/interrupt frame. */
        unsigned locals=s->entry==0x937a?10:8;
        if (dp!=(uint16_t)(s->dp-locals) || sp!=(uint16_t)(s->sp-2)) {
            s->remaining=0; s->repeating=false; return pc;
        }
        if (--s->remaining>0) {
            unsigned raw=large?ScWorldCell(w,w->huge?w->coord[2][0]:ram[0xb85],w->huge?w->coord[2][1]:ram[0xb86]):word(ram,0x10200+s->cell), tile=raw&0x3ff;
            if (zone(tile)!=s->entry) {
                s->remaining=0; s->repeating=false; return pc;
            }
            put(ram,0x0b87,raw); put(ram,0x0b89,tile);
            s->repeating=true;
            ++s->attempts; ++s->extra_attempts;
            return s->capacity;
        }
        s->repeating=false;
    }
    return pc;
}
