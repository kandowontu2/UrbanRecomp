#include "sc_sprite.h"
#include "snes/interp816.h"
#include <stdlib.h>

static unsigned word(const uint8_t *r,unsigned p) {return r[p]|(unsigned)r[p+1]<<8;}
static void put(uint8_t *r,unsigned p,unsigned v) {r[p]=(uint8_t)v;r[p+1]=(uint8_t)(v>>8);}
static void nz(Interp816 *c,unsigned v) {c->n=(v&0x8000)!=0;c->z=(uint16_t)v==0;}
static unsigned read_word(Interp816 *c,unsigned p) {
    return c->read(c->mem,p)|(unsigned)c->read(c->mem,p+1)<<8;
}
static bool record(Interp816 *c,const uint8_t *r,unsigned bytes,unsigned *address) {
    unsigned p=word(r,c->sp+3),end=p+c->y+bytes;
    /* Records are cartridge data (including the virtual menu/message bus).
     * Decline unusual bank crossings or device/RAM sources before reading. */
    if(p<0x8000 || end>0x10000) return false;
    *address=((unsigned)c->db<<16)+p+c->y;return true;
}
static void coordinate(Interp816 *c,uint8_t *r,unsigned p,unsigned offset) {
    unsigned value=c->read(c->mem,p),add=r[offset],sum=value+add;
    c->a=(c->a&0xff00)|(sum&255);
    c->c=sum>255;c->v=((value^add)&128)==0 && ((value^sum)&128)!=0;
    r[0x2000+c->x]=(uint8_t)sum;++c->x;++c->y;nz(c,c->x);
}
static unsigned coordinate_tail(Interp816 *c,uint8_t *r,unsigned budget) {
    bool vertical=c->pc>=0x908e;unsigned p=c->pc-(vertical?0x908e:0x9080),cost=0,address;
    bool fetch=p==(vertical?0:2),advance=p<=(vertical?2:4),clear=p<=(vertical?3:5);
    bool add=p<=(vertical?4:6),store=p<=(vertical?7:9);
    if(c->xf || !c->mf || c->x>511 ||
       !(p==(vertical?0:2) || p==(vertical?2:4) || p==(vertical?3:5) ||
         p==(vertical?4:6) || p==(vertical?7:9) || p==(vertical?11:13))) return 0;
    cost=(fetch?7:0)+(advance?2:0)+(clear?2:0)+(add?4:0)+(store?5:0)+2;
    if(cost>budget || (fetch && !record(c,r,1,&address))) return 0;
    if(fetch) c->a=(c->a&0xff00)|c->read(c->mem,address);
    if(advance) ++c->y;
    if(clear) c->c=false;
    if(add) {
        unsigned value=c->a&255,operand=r[vertical?0x25f:0x25d],sum=value+operand+c->c;
        c->a=(c->a&0xff00)|(sum&255);c->c=sum>255;
        c->v=((value^operand)&128)==0 && ((value^sum)&128)!=0;
    }
    if(store) r[0x2000+c->x]=(uint8_t)c->a;
    ++c->x;nz(c,c->x);c->pc=vertical?0x909a:0x908e;c->cyclesUsed=2;return cost;
}
static unsigned tile_tail(Interp816 *c,uint8_t *r,unsigned budget) {
    unsigned p=c->pc,address=0;
    if(c->mf || c->xf || c->x>512 ||
       !(p==0x909c || p==0x909e || p==0x909f || p==0x90a0 ||
         p==0x90a4 || p==0x90a5 || p==0x90a6 || p==0x90a9)) return 0;
    bool fetch=p==0x909c,store=p<=0x90a0,decrement=p<=0x90a6;
    unsigned dy=p<=0x909e?2:p==0x909f?1:0,dx=p<=0x90a4?2:p==0x90a5?1:0;
    unsigned count=(uint16_t)(word(r,0x287)-1);
    bool zero=decrement?count==0:c->z;
    unsigned cost=(fetch?8:0)+2*dy+(store?6:0)+2*dx+(decrement?8:0)+(zero?2:3);
    if(cost>budget || (store && c->x>510) || (fetch && !record(c,r,2,&address))) return 0;
    if(fetch) c->a=(uint16_t)read_word(c,address);
    if(store) put(r,0x2000+c->x,c->a);
    c->x+=dx;c->y+=dy;
    if(decrement) {put(r,0x287,count);nz(c,count);}
    c->pc=zero?0x90ab:0x9080;c->cyclesUsed=zero?2:3;return cost;
}
/* Short deadline tails use fixed C edges, not fetched/decoded opcodes. The
 * ordinary path above them still assembles complete records/groups at once.
 * This lets an IRQ/HDMA boundary resume anywhere in the original routine. */
static unsigned edge(Interp816 *c,uint8_t *r,unsigned budget) {
    unsigned p=c->pc,cost=0,next=0,value=0,address=0;
    if(p!=0x905c && (c->xf || (p<0x9080 && c->mf) ||
       (p>=0x9082 && p<=0x9099 && !c->mf) || (p>=0x909c && c->mf))) return 0;
    switch(p) {
    case 0x905c:cost=3;next=0x905e;break;
    case 0x905e:case 0x9068:case 0x9075:case 0x90bd:case 0x90d4:cost=5;next=p+3;break;
    case 0x9061:case 0x9062:case 0x9063:case 0x9064:
    case 0x906b:case 0x906c:case 0x907a:case 0x907b:
    case 0x9084:case 0x9085:case 0x908d:case 0x9090:case 0x9091:case 0x9099:
    case 0x909e:case 0x909f:case 0x90a4:case 0x90a5:
    case 0x90af:case 0x90b0:case 0x90b1:case 0x90b2:case 0x90b3:case 0x90b4:
    case 0x90b8:case 0x90b9:case 0x90cf:case 0x90d0:case 0x90d1:cost=2;next=p+1;break;
    case 0x9065:case 0x907c:case 0x90ab:case 0x90c0:cost=5;next=p+3;break;
    case 0x906d:cost=6;next=0x9071;if(c->x>0x2558) return 0;break;
    case 0x9071:case 0x907f:cost=4;next=p+1;break;
    case 0x9072:case 0x90b5:case 0x90c3:case 0x90c8:cost=3;next=p+3;break;
    case 0x9078: {
        unsigned pointer=word(r,c->sp+1);
        if(pointer<0x8000 || pointer+c->y+2>0x10000) return 0;
        address=((unsigned)c->db<<16)+pointer+c->y;cost=8;next=0x907a;break;
    }
    case 0x9080:case 0x909a:cost=3;next=p+2;break;
    case 0x9082:case 0x908e:case 0x909c:
        cost=p==0x909c?8:7;next=p+2;
        if(!record(c,r,p==0x909c?2:1,&address)) return 0;break;
    case 0x9086:case 0x9092:cost=4;next=p+3;break;
    case 0x9089:case 0x9095:cost=5;next=p+4;if(c->x>511) return 0;break;
    case 0x90a0:case 0x90cb:case 0x90d7:
        cost=6;next=p+4;if(c->x>(p==0x90a0?510:96)) return 0;break;
    case 0x90a6:cost=8;next=0x90a9;break;
    case 0x90a9:case 0x90d2:cost=c->z?2:3;next=c->z?p+2:p==0x90a9?0x9080:0x90cb;break;
    case 0x90ae:case 0x90db:cost=5;next=p+1;break;
    case 0x90ba:cost=5;next=0x90bd;if(c->x>14) return 0;break;
    case 0x90c6:cost=c->z?3:2;next=c->z?0x90d4:0x90c8;break;
    case 0x90dc:cost=6;next=(uint16_t)(word(r,c->sp+1)+1);break;
    default:return 0;
    }
    if(cost>budget) return 0;
    switch(p) {
    case 0x905c:c->mf=false;c->xf=false;break;
    case 0x905e:c->a=(uint16_t)word(r,0x253);nz(c,c->a);break;
    case 0x9068:c->a=(uint16_t)word(r,0x261);nz(c,c->a);break;
    case 0x9075:c->x=(uint16_t)word(r,0x253);nz(c,c->x);break;
    case 0x90bd:c->x=(uint16_t)word(r,0x289);nz(c,c->x);break;
    case 0x90d4:c->a=(uint16_t)word(r,0x289);nz(c,c->a);break;
    case 0x9061:case 0x9062:case 0x9063:case 0x9064:case 0x90b0:case 0x90b1:case 0x90b2:
        c->c=(c->a&1)!=0;c->a>>=1;nz(c,c->a);break;
    case 0x906b:case 0x90b8:c->c=(c->a&0x8000)!=0;c->a<<=1;nz(c,c->a);break;
    case 0x906c:case 0x90af:case 0x90b9:c->x=c->a;nz(c,c->x);break;
    case 0x907a:case 0x907b:case 0x9084:case 0x9090:case 0x909e:case 0x909f:
        ++c->y;nz(c,c->y);break;
    case 0x9085:case 0x9091:c->c=false;break;
    case 0x908d:case 0x9099:case 0x90a4:case 0x90a5:case 0x90cf:case 0x90d0:
        ++c->x;nz(c,c->x);break;
    case 0x90b3:c->y=c->a;nz(c,c->y);break;
    case 0x90b4:c->a=c->x;nz(c,c->a);break;
    case 0x90d1:--c->y;nz(c,c->y);break;
    case 0x9065:put(r,0x289,c->a);break;
    case 0x907c:put(r,0x287,c->a);break;
    case 0x90ab:put(r,0x253,c->x);break;
    case 0x90c0:put(r,0x289,c->a);break;
    case 0x906d:c->a=(uint16_t)read_word(c,0xdaa6+c->x);nz(c,c->a);break;
    case 0x9071:case 0x907f:put(r,c->sp-1,c->a);c->sp-=2;break;
    case 0x9072:c->y=0;nz(c,c->y);break;
    case 0x90b5:c->a&=7;nz(c,c->a);break;
    case 0x90c3:c->c=true;nz(c,c->y);break;
    case 0x90c8:c->a=0;nz(c,c->a);break;
    case 0x9078:case 0x909c:c->a=(uint16_t)read_word(c,address);nz(c,c->a);break;
    case 0x9080:c->mf=true;break;
    case 0x909a:c->mf=false;break;
    case 0x9082:case 0x908e:
        c->a=(c->a&0xff00)|c->read(c->mem,address);c->n=(c->a&128)!=0;c->z=(c->a&255)==0;break;
    case 0x9086:case 0x9092: {
        unsigned operand=r[p==0x9086?0x25d:0x25f],old=c->a&255,sum=old+operand+c->c;
        c->a=(c->a&0xff00)|(sum&255);c->c=sum>255;
        c->v=((old^operand)&128)==0 && ((old^sum)&128)!=0;c->n=(sum&128)!=0;c->z=(sum&255)==0;break;
    }
    case 0x9089:case 0x9095:r[0x2000+c->x]=(uint8_t)c->a;break;
    case 0x90a0:put(r,0x2000+c->x,c->a);break;
    case 0x90cb:case 0x90d7:put(r,0x2200+c->x,c->a);break;
    case 0x90a6:value=(uint16_t)(word(r,0x287)-1);put(r,0x287,value);nz(c,value);break;
    case 0x90ae:case 0x90db:c->a=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->a);break;
    case 0x90ba:c->a=(uint16_t)read_word(c,((unsigned)c->db<<16)+0x9048+c->x);nz(c,c->a);break;
    case 0x90dc:c->sp+=2;break;
    }
    c->pc=(uint16_t)next;c->cyclesUsed=(uint8_t)cost;return cost;
}
static bool eligible(Interp816 *c,uint8_t *r) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_SPRITE_REFERENCE");reference=e && *e=='1';}
    return !(reference || !c || !r || c->k || c->e || c->d ||
       ((c->db&0x7f)>=0x40) || c->sp<0x304 || c->sp>0x1ff9 ||
       !c->read || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i));
}
unsigned ScSpriteInstructionStep(Interp816 *c,uint8_t *r) {
    return eligible(c,r)?edge(c,r,8):0;
}
unsigned ScSpriteStep(Interp816 *c,uint8_t *r,unsigned budget) {
    if(!eligible(c,r)) return 0;
    unsigned cycles=0;
    for(;;) {
        unsigned remaining=budget-cycles,cost=0,address=0,count;
        if(c->pc==0x905c) {
            if(remaining<69) goto residual;
            unsigned index=word(r,0x261),start=word(r,0x253);
            if(index>0x12ac) break;
            unsigned pointer=read_word(c,0xdaa6+index*2);
            if(pointer<0x8000 || pointer>0xfffd) break;
            count=read_word(c,((unsigned)c->db<<16)+pointer);
            if(!count || count>128 || start+count*4>512 || pointer+2+count*4>0x10000) break;
            c->xf=false;c->mf=false;c->c=(index&0x8000)!=0;
            put(r,0x289,start>>4);
            put(r,c->sp-1,pointer);c->sp-=2;
            put(r,0x287,count);put(r,c->sp-1,count);c->sp-=2;
            c->a=(uint16_t)count;c->x=(uint16_t)start;c->y=2;c->n=false;c->z=false;
            c->pc=0x9080;c->cyclesUsed=4;cost=69;
        } else if(c->pc==0x9080 || c->pc==0x908e || c->pc==0x909a) {
            if(c->xf || c->x>0x1fc+(c->pc==0x909a?2:c->pc==0x908e?1:0) ||
               (c->pc==0x908e && !c->mf)) break;
            /* The full record is the hot path: four cartridge bytes, four
             * OAM bytes, one remaining-count update. No opcode decoder or
             * intermediate register/flag simulation is needed here. */
            count=word(r,0x287);
            unsigned tail=count==1?35:36;
            if(c->pc==0x9080 && remaining>=47+tail && record(c,r,4,&address)) {
                c->mf=true;coordinate(c,r,address,0x25d);coordinate(c,r,address+1,0x25f);
                c->mf=false;c->a=(uint16_t)read_word(c,address+2);
                put(r,0x2000+c->x,c->a);c->x+=2;c->y+=2;
                put(r,0x287,(uint16_t)(count-1));nz(c,(uint16_t)(count-1));
                c->pc=c->z?0x90ab:0x9080;c->cyclesUsed=c->z?2:3;cost=47+tail;
            } else if(c->pc==0x9080 || c->pc==0x908e) {
                cost=c->pc==0x9080?25:22;
                if(cost>remaining || !record(c,r,1,&address)) goto residual;
                c->mf=true;coordinate(c,r,address,c->pc==0x9080?0x25d:0x25f);
                c->pc=c->pc==0x9080?0x908e:0x909a;c->cyclesUsed=2;
            } else {
                cost=tail;
                if(cost>remaining || !record(c,r,2,&address)) goto residual;
                c->mf=false;c->a=(uint16_t)read_word(c,address);
                put(r,0x2000+c->x,c->a);c->x+=2;c->y+=2;
                put(r,0x287,(uint16_t)(count-1));nz(c,(uint16_t)(count-1));
                c->pc=c->z?0x90ab:0x9080;c->cyclesUsed=c->z?2:3;
            }
        } else if(c->pc>=0x9082 && c->pc<=0x9099) {
            cost=coordinate_tail(c,r,remaining);if(!cost) goto residual;
        } else if(c->pc>=0x909c && c->pc<=0x90a9) {
            cost=tile_tail(c,r,remaining);if(!cost) goto residual;
        } else if(c->pc==0x90ab) {
            if(c->mf || c->xf || c->x>512) break;
            count=word(r,c->sp+1);
            if(count>128 || word(r,0x289)>32) break;
            unsigned groups=count>>3;cost=groups?52:50;
            if(cost>remaining) goto residual;
            unsigned mask=read_word(c,((unsigned)c->db<<16)+0x9048+(count&7)*2),high=word(r,0x289);
            put(r,0x253,c->x);c->sp+=2;c->x=(uint16_t)high;c->y=(uint16_t)groups;
            put(r,0x289,mask);c->a=groups?0:(uint16_t)mask;c->c=true;c->n=false;c->z=true;
            c->pc=groups?0x90cb:0x90d4;c->cyclesUsed=3;
        } else if(c->pc==0x90cb) {
            if(c->mf || c->xf || c->x>64 || !c->y || c->y>16) break;
            unsigned groups=remaining/15;
            if(groups>c->y) groups=c->y;
            if(!groups && c->y==1 && remaining>=14) groups=1;
            if(!groups) goto residual;
            /* Each complete group writes one high-bit word, then advances
             * X and the original loop counter. Preserve the final branch. */
            cost=15*groups-(groups==c->y);
            for(unsigned i=0;i<groups;++i) put(r,0x2200+c->x+2*i,c->a);
            c->x+=2*groups;c->y-=groups;nz(c,c->y);
            c->pc=c->y?0x90cb:0x90d4;c->cyclesUsed=c->y?3:2;
        } else if(c->pc==0x90d4) {
            cost=22;
            if(c->mf || c->xf || c->x>96) break;
            if(cost>remaining) goto residual;
            put(r,0x2200+c->x,word(r,0x289));c->a=(uint16_t)word(r,c->sp+1);nz(c,c->a);
            c->pc=(uint16_t)(word(r,c->sp+3)+1);c->sp+=4;c->cyclesUsed=6;
        } else goto residual;
        cycles+=cost;continue;
        residual:
        cost=edge(c,r,remaining);if(!cost) break;cycles+=cost;
    }
    return cycles;
}
