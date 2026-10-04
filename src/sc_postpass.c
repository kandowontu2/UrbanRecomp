#include "sc_postpass.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <stdlib.h>

static unsigned word(const uint8_t *r,unsigned at) {return r[at]|(unsigned)r[at+1]<<8;}
static void put(uint8_t *r,unsigned at,unsigned v) {r[at]=(uint8_t)v;r[at+1]=(uint8_t)(v>>8);}
static uint8_t *field(ScWorld *w,uint8_t *r,unsigned f) {return w->active?w->fields[f]:r+0x10000+ScWorldFields[f].base;}
static unsigned field_size(const ScWorld *w,unsigned f) {
    return w->active?ScWorldFieldSizeWorld(w,f):ScWorldFields[f].stock_width*ScWorldFields[f].stock_height*ScWorldFields[f].element_bytes;
}
static void nz(Interp816 *c,unsigned v,bool byte) {c->n=(v&(byte?128:32768))!=0;c->z=(v&(byte?255:65535))==0;}
static void load(Interp816 *c,unsigned v) {c->a=c->mf?(c->a&0xff00)|(v&255):(uint16_t)v;nz(c,c->a,c->mf);}
static void compare(Interp816 *c,unsigned v) {
    unsigned old=c->a&(c->mf?255:65535);c->c=old>=v;nz(c,old-v,c->mf);
}
static void add(Interp816 *c,unsigned v,bool subtract) {
    unsigned mask=c->mf?255:65535,sign=c->mf?128:32768,old=c->a&mask;
    unsigned sum=old+(subtract?(v^mask):v)+c->c;
    c->v=((subtract?(old^v):~(old^v))&(old^sum)&sign)!=0;c->c=sum>mask;load(c,sum);
}
static bool enabled(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_POSTPASS_REFERENCE");reference=e && *e=='1';}
    return !reference;
}
static unsigned execute(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget,bool single) {
    if(!enabled() || !w || !c || !r || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted ||
       (c->irqWanted && !c->i) || c->dp<0x1000 || c->dp>0x1ff0 ||
       c->sp<0x1008 || c->sp>0x1ffb || (c->sp+4>=c->dp && c->sp-1<=c->dp+6)) return 0;
    unsigned cycles=0,cost,value,old,index,dp=(c->dp&255)!=0;
    bool wide=w->active && w->huge,word_wide=w->active && w->colossal;
#define EDGE(n) do {cost=(n);if(cost>budget-cycles) return cycles;} while(0)
#define NEXT(pc_,label) do {c->pc=(pc_);c->cyclesUsed=(uint8_t)cost;cycles+=cost;if(single) return cycles;goto label;} while(0)
    switch(c->pc) {
    case 0x88f3: goto p88f3;case 0x88f5: goto p88f5;case 0x88f7: goto p88f7;
    case 0x88fa: goto p88fa;case 0x88fe: goto p88fe;case 0x8900: goto p8900;
    case 0x8902: goto p8902;case 0x8904: goto p8904;case 0x8906: goto p8906;
    case 0x8908: goto p8908;case 0x8909: goto p8909;case 0x890b: goto p890b;
    case 0x890d: goto p890d;case 0x890e: goto p890e;case 0x8910: goto p8910;
    case 0x8912: goto p8912;case 0x8914: goto p8914;case 0x8918: goto p8918;
    case 0x8919: goto p8919;case 0x891c: goto p891c;case 0x891e: goto ret;
    case 0x891f: goto p891f;case 0x8921: goto p8921;case 0x8924: goto p8924;
    case 0x8928: goto p8928;case 0x892a: goto p892a;case 0x892c: goto p892c;
    case 0x892d: goto p892d;case 0x8930: goto p8930;case 0x8932: goto p8932;
    case 0x8935: goto p8935;case 0x8937: goto p8937;case 0x8938: goto p8938;
    case 0x893b: goto p893b;case 0x893d: goto p893d;case 0x8940: goto p8940;
    case 0x8944: goto p8944;case 0x8945: goto p8945;case 0x8946: goto p8946;
    case 0x8949: goto p8949;case 0x894b: goto ret;
    case 0xb66a: goto pb66a;case 0xb66c: goto pb66c;case 0xb66e: goto pb66e;
    case 0xb670: goto pb670;case 0xb673: goto pb673;case 0xb674: goto pb674;
    case 0xb676: goto pb676;case 0xb67a: goto pb67a;case 0xb67c: goto pb67c;
    case 0xb680: goto pb680;case 0xb682: goto pb682;case 0xb685: goto pb685;
    case 0xb686: goto pb686;case 0xb688: goto pb688;case 0xb68a: goto pb68a;
    case 0xb68c: goto pb68c;case 0xb68e: goto pb68e;case 0xb690: goto pb690;
    case 0xb691: goto pb691;case 0xb692: goto pb692;case 0xb695: goto pb695;
    case 0xb697: goto pb697;case 0xb699: goto pb699;case 0xb69c: goto pb69c;
    case 0xb69e: goto pb69e;case 0xb6a1: goto ret;case 0xb6a2: goto pb6a2;
    case 0xb6a4: goto pb6a4;case 0xb6a6: goto pb6a6;case 0xb6ac: goto pb6ac;
    case 0xb6af: goto pb6af;case 0xb6b1: goto pb6b1;case 0xb6b7: goto pb6b7;
    case 0xb6b9: goto pb6b9;case 0xb6bc: goto ret;default:return 0;
    }
p88f3: EDGE(3);c->mf=true;if(wide) w->field_scan=0;NEXT(0x88f5,p88f5);
p88f5: EDGE(3);c->xf=false;NEXT(0x88f7,p88f7);
p88f7: EDGE(3);c->x=0;nz(c,0,false);NEXT(0x88fa,p88fa);
p88fa:
    if(!c->mf) return cycles;
    if(!single && w->active) {unsigned span=ScWorldGuestFieldStageStep(w,c,r,budget-cycles);if(span) {cycles+=span;if(c->pc==0x891e) goto ret;goto p88fa;}}
    index=wide?w->field_scan:c->x;if(index>=field_size(w,4)) return cycles;
    EDGE(5);load(c,field(w,r,4)[index]);NEXT(0x88fe,p88fe);
p88fe: EDGE(c->z?3:2);if(c->z) {NEXT(0x8918,p8918);}NEXT(0x8900,p8900);
p8900: if(!c->mf) return cycles;EDGE(2);compare(c,24);NEXT(0x8902,p8902);
p8902: EDGE(c->c?2:3);if(!c->c) {NEXT(0x8912,p8912);}NEXT(0x8904,p8904);
p8904: if(!c->mf) return cycles;EDGE(2);compare(c,200);NEXT(0x8906,p8906);
p8906: EDGE(c->c?2:3);if(!c->c) {NEXT(0x890d,p890d);}NEXT(0x8908,p8908);
p8908: EDGE(2);c->c=true;NEXT(0x8909,p8909);
p8909: if(!c->mf) return cycles;EDGE(2);add(c,34,true);NEXT(0x890b,p890b);
p890b: EDGE(3);NEXT(0x8914,p8914);
p890d: EDGE(2);c->c=true;NEXT(0x890e,p890e);
p890e: if(!c->mf) return cycles;EDGE(2);add(c,24,true);NEXT(0x8910,p8910);
p8910: EDGE(3);NEXT(0x8914,p8914);
p8912: if(!c->mf) return cycles;EDGE(2);load(c,0);NEXT(0x8914,p8914);
p8914:
    if(!c->mf) return cycles;index=wide?w->field_scan:c->x;if(index>=field_size(w,4)) return cycles;
    EDGE(5);field(w,r,4)[index]=(uint8_t)c->a;NEXT(0x8918,p8918);
p8918: EDGE(2);++c->x;nz(c,c->x,false);NEXT(0x8919,p8919);
p8919:
    if(wide) {EDGE(w->field_scan+1==field_size(w,0)?2:3);ScWorldGuestStepPrepared(w,c,r);goto p891c;}
    EDGE(3);value=field_size(w,0);c->c=c->x>=value;nz(c,c->x-value,false);NEXT(0x891c,p891c);
p891c: EDGE(c->z?2:3);if(!c->z) {NEXT(0x88fa,p88fa);}NEXT(0x891e,ret);
p891f: EDGE(3);c->mf=c->xf=false;NEXT(0x8921,p8921);
p8921: EDGE(3);if(word_wide) w->field_scan=0;c->x=0;nz(c,0,false);NEXT(0x8924,p8924);
p8924:
    if(c->mf) return cycles;
    if(!single && w->active) {unsigned span=ScWorldGuestFieldStageStep(w,c,r,budget-cycles);if(span) {cycles+=span;if(c->pc==0x894b) goto ret;goto p8924;}}
    index=word_wide?w->field_scan:c->x;if(index+1>=field_size(w,7)) return cycles;
    EDGE(6);load(c,word(field(w,r,7),index));NEXT(0x8928,p8928);
p8928: EDGE(c->z?3:2);if(c->z) {NEXT(0x8944,p8944);}NEXT(0x892a,p892a);
p892a: EDGE(c->n?3:2);if(c->n) {NEXT(0x8937,p8937);}NEXT(0x892c,p892c);
p892c: if(c->mf) return cycles;EDGE(2);load(c,c->a-1);NEXT(0x892d,p892d);
p892d: if(c->mf) return cycles;EDGE(3);compare(c,200);NEXT(0x8930,p8930);
p8930: EDGE(c->c?2:3);if(!c->c) {NEXT(0x8940,p8940);}NEXT(0x8932,p8932);
p8932: if(c->mf) return cycles;EDGE(3);load(c,200);NEXT(0x8935,p8935);
p8935: EDGE(3);NEXT(0x8940,p8940);
p8937: if(c->mf) return cycles;EDGE(2);load(c,c->a+1);NEXT(0x8938,p8938);
p8938: if(c->mf) return cycles;EDGE(3);compare(c,0xff38);NEXT(0x893b,p893b);
p893b: EDGE(c->c?3:2);if(c->c) {NEXT(0x8940,p8940);}NEXT(0x893d,p893d);
p893d: if(c->mf) return cycles;EDGE(3);load(c,0xff38);NEXT(0x8940,p8940);
p8940:
    if(c->mf) return cycles;index=word_wide?w->field_scan:c->x;if(index+1>=field_size(w,7)) return cycles;
    EDGE(6);put(field(w,r,7),index,c->a);NEXT(0x8944,p8944);
p8944: EDGE(2);++c->x;nz(c,c->x,false);NEXT(0x8945,p8945);
p8945: EDGE(2);++c->x;nz(c,c->x,false);NEXT(0x8946,p8946);
p8946:
    if(word_wide) {EDGE(w->field_scan+2==field_size(w,7)?2:3);ScWorldGuestStepPrepared(w,c,r);goto p8949;}
    EDGE(3);value=field_size(w,7);c->c=c->x>=value;nz(c,c->x-value,false);NEXT(0x8949,p8949);
p8949: EDGE(c->z?2:3);if(!c->z) {NEXT(0x8924,p8924);}NEXT(0x894b,ret);
pb66a: EDGE(3);c->mf=c->xf=false;if(wide) w->field_scan=0;NEXT(0xb66c,pb66c);
pb66c: if(c->mf) return cycles;EDGE(4+dp);put(r,c->dp,0);NEXT(0xb66e,pb66e);
pb66e: if(c->mf) return cycles;EDGE(4+dp);put(r,c->dp+2,0);NEXT(0xb670,pb670);
pb670: EDGE(3);c->y=0;nz(c,0,false);NEXT(0xb673,pb673);
pb673: EDGE(2);c->x=c->y;nz(c,c->x,false);NEXT(0xb674,pb674);
pb674: EDGE(3);c->mf=true;NEXT(0xb676,pb676);
pb676:
    if(!c->mf) return cycles;
    if(!single && w->active) {unsigned span=ScWorldGuestFieldStageStep(w,c,r,budget-cycles);if(span) {cycles+=span;if(c->pc==0xb697) goto pb697;goto pb676;}}
    index=wide?w->field_scan:c->x;if(index>=field_size(w,0)) return cycles;
    EDGE(5);load(c,field(w,r,0)[index]);NEXT(0xb67a,pb67a);
pb67a: EDGE(c->z?3:2);if(c->z) {NEXT(0xb691,pb691);}NEXT(0xb67c,pb67c);
pb67c:
    if(!c->mf) return cycles;index=wide?w->field_scan:c->x;if(index>=field_size(w,4)) return cycles;
    EDGE(5);load(c,field(w,r,4)[index]);NEXT(0xb680,pb680);
pb680: EDGE(3);c->mf=false;NEXT(0xb682,pb682);
pb682: if(c->mf) return cycles;EDGE(3);load(c,c->a&255);NEXT(0xb685,pb685);
pb685: EDGE(2);c->c=false;NEXT(0xb686,pb686);
pb686: if(c->mf) return cycles;EDGE(4+dp);add(c,word(r,c->dp),false);NEXT(0xb688,pb688);
pb688: if(c->mf) return cycles;EDGE(4+dp);put(r,c->dp,c->a);NEXT(0xb68a,pb68a);
pb68a: EDGE(c->c?2:3);if(!c->c) {NEXT(0xb68e,pb68e);}NEXT(0xb68c,pb68c);
pb68c: if(c->mf) return cycles;EDGE(7+dp);value=(word(r,c->dp+2)+1)&65535;put(r,c->dp+2,value);nz(c,value,false);NEXT(0xb68e,pb68e);
pb68e: EDGE(3);c->mf=true;NEXT(0xb690,pb690);
pb690: EDGE(2);++c->y;nz(c,c->y,false);NEXT(0xb691,pb691);
pb691: EDGE(2);++c->x;nz(c,c->x,false);NEXT(0xb692,pb692);
pb692:
    if(wide) {EDGE(w->field_scan+1==field_size(w,0)?2:3);ScWorldGuestStepPrepared(w,c,r);goto pb695;}
    EDGE(3);value=field_size(w,0);c->c=c->x>=value;nz(c,c->x-value,false);NEXT(0xb695,pb695);
pb695: EDGE(c->z?2:3);if(!c->z) {NEXT(0xb676,pb676);}NEXT(0xb697,pb697);
pb697: EDGE(3);c->mf=false;NEXT(0xb699,pb699);
pb699: EDGE(3);c->c=true;nz(c,c->y,false);NEXT(0xb69c,pb69c);
pb69c: EDGE(c->z?2:3);if(!c->z) {NEXT(0xb6a2,pb6a2);}NEXT(0xb69e,pb69e);
pb69e: EDGE(5);put(r,0xc05,c->y);NEXT(0xb6a1,ret);
pb6a2: EDGE(4+dp);put(r,c->dp+4,c->y);NEXT(0xb6a4,pb6a4);
pb6a4: if(c->mf) return cycles;EDGE(4+dp);put(r,c->dp+6,0);NEXT(0xb6a6,pb6a6);
pb6a6: EDGE(6);put(r,c->sp-1,0xb6a8);c->sp-=2;c->pc=0xa421;c->cyclesUsed=6;return cycles+cost;
pb6ac: if(c->mf) return cycles;EDGE(3);load(c,0x266);NEXT(0xb6af,pb6af);
pb6af: if(c->mf) return cycles;EDGE(4+dp);put(r,c->dp+4,c->a);NEXT(0xb6b1,pb6b1);
pb6b1: EDGE(6);put(r,c->sp-1,0xb6b3);c->sp-=2;c->pc=0xa2f5;c->cyclesUsed=6;return cycles+cost;
pb6b7: if(c->mf) return cycles;EDGE(4+dp);load(c,word(r,c->dp+1));NEXT(0xb6b9,pb6b9);
pb6b9: if(c->mf) return cycles;EDGE(5);put(r,0xc05,c->a);NEXT(0xb6bc,ret);
ret: EDGE(6);old=word(r,c->sp+1);c->sp+=2;c->pc=(uint16_t)(old+1);c->cyclesUsed=6;return cycles+cost;
#undef EDGE
#undef NEXT
}
unsigned ScPostpassStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {return execute(w,c,r,budget,false);}
unsigned ScPostpassInstructionStep(ScWorld *w,Interp816 *c,uint8_t *r) {return execute(w,c,r,UINT32_MAX,true);}
