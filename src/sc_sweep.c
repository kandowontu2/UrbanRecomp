#include "sc_sweep.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <stdlib.h>
static unsigned word(const uint8_t *r,unsigned a) {return r[a]|(unsigned)r[a+1]<<8;}
static void put(uint8_t *r,unsigned a,unsigned v) {r[a]=(uint8_t)v;r[a+1]=(uint8_t)(v>>8);}
static void nz(Interp816 *c,unsigned v,bool byte) {c->n=(v&(byte?128:32768))!=0;c->z=(v&(byte?255:65535))==0;}
static void load(Interp816 *c,unsigned v) {c->a=c->mf?(c->a&0xff00)|(v&255):(uint16_t)v;nz(c,c->a,c->mf);}
static unsigned value(const uint8_t *r,unsigned at,bool byte) {return byte?r[at]:word(r,at);}
static void store(uint8_t *r,unsigned at,unsigned v,bool byte) {if(byte)r[at]=(uint8_t)v;else put(r,at,v);}
static void compare(Interp816 *c,unsigned v,bool byte,unsigned reg) {unsigned old=reg&(byte?255:65535);c->c=old>=v;nz(c,old-v,byte);}
static void add(Interp816 *c,unsigned v,bool subtract) {
 unsigned mask=c->mf?255:65535,old=c->a&mask,sum=old+(subtract?(v^mask):v)+c->c;
 c->v=((subtract?(old^v):~(old^v))&(old^sum)&(c->mf?128:32768))!=0;c->c=sum>mask;load(c,sum);
}
static bool enabled(void) {
 static int reference=-1;if(reference<0) {const char *e=getenv("SC_SWEEP_DRIVER_REFERENCE");reference=e && *e=='1';}return !reference;
}
bool ScSweepOwns(unsigned pc) {return (pc>=0x8297 && pc<=0x842e) || pc==0x84c3 || pc==0x84ea;}
static int tile_offset(const ScWorld *w,const Interp816 *c,unsigned below) {
 return (int)w->map_anchor+(int16_t)(c->x-(uint16_t)w->map_anchor)+(int)(below?2*ScWorldWidth(w):0);
}
static unsigned tile_read(const ScWorld *w,const Interp816 *c,const uint8_t *r,unsigned below) {
 if(!w->active)return value(r,0x10200+c->x+(below?240:0),c->mf);
 int at=tile_offset(w,c,below);unsigned bytes=c->mf?1:2;
 return w->map_anchor!=UINT32_MAX && at>=0 && (unsigned)at+bytes<=ScWorldCells(w)*2?value(w->tiles,(unsigned)at,c->mf):0;
}
static void tile_write(ScWorld *w,const Interp816 *c,uint8_t *r,unsigned below) {
 if(!w->active) {store(r,0x10200+c->x+(below?240:0),c->a,c->mf);return;}
 int at=tile_offset(w,c,below);unsigned bytes=c->mf?1:2;
 if(w->map_anchor==UINT32_MAX || at<0 || (unsigned)at+bytes>ScWorldCells(w)*2)return;
 for(unsigned i=0;i<bytes;++i) {uint8_t v=(uint8_t)(c->a>>(8*i));if(w->tiles[at+i]!=v) {w->tiles[at+i]=v;ScWorldTilesTouch(w,(unsigned)at+i,1);}}
}
/* Fixed US city-sweep C control flow. Each edge retains its original clock;
 * callee boundaries and full-coordinate hooks remain observable by main. */
static unsigned execute(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned budget,bool single) {
 if(!enabled() || !w || !c || !r || !rom || c->k!=3 || c->db!=3 || c->e || c->d || c->xf ||
    c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) || c->dp<0x20 || c->dp>0x1ff0 ||
    c->sp<0x108 || c->sp>0x1ffd || (c->sp+2>=c->dp-2 && c->sp-3<=c->dp+1))return 0;
 unsigned cycles=0,cost,v,old;
#define EDGE(n) do {cost=(n);if(cost>budget-cycles)return cycles;} while(0)
#define NEXT(pc_,label) do {c->pc=(pc_);c->cyclesUsed=(uint8_t)cost;cycles+=cost;if(single)return cycles;goto label;} while(0)
 switch(c->pc) {
 case 0x84c3:case 0x84ea:goto coordinate_return;
 case 0x8297:goto p8297;
 case 0x8299:goto p8299;
 case 0x829a:goto p829a;
 case 0x829b:goto p829b;
 case 0x829c:goto p829c;
 case 0x829d:goto p829d;
 case 0x82a0:goto p82a0;
 case 0x82a1:goto p82a1;
 case 0x82a2:goto p82a2;
 case 0x82a4:goto p82a4;
 case 0x82a6:goto p82a6;
 case 0x82a9:goto p82a9;
 case 0x82ac:goto p82ac;
 case 0x82ae:goto p82ae;
 case 0x82b1:goto p82b1;
 case 0x82b4:goto p82b4;
 case 0x82b6:goto p82b6;
 case 0x82b8:goto p82b8;
 case 0x82b9:goto p82b9;
 case 0x82bb:goto p82bb;
 case 0x82be:goto p82be;
 case 0x82c2:goto p82c2;
 case 0x82c3:goto p82c3;
 case 0x82c4:goto p82c4;
 case 0x82c6:goto p82c6;
 case 0x82c9:goto p82c9;
 case 0x82cc:goto p82cc;
 case 0x82cf:goto p82cf;
 case 0x82d2:goto p82d2;
 case 0x82d4:goto p82d4;
 case 0x82d7:goto p82d7;
 case 0x82d9:goto p82d9;
 case 0x82da:goto p82da;
 case 0x82dc:goto p82dc;
 case 0x82df:goto p82df;
 case 0x82e1:goto p82e1;
 case 0x82e3:goto p82e3;
 case 0x82e5:goto p82e5;
 case 0x82e8:goto p82e8;
 case 0x82ec:goto p82ec;
 case 0x82ef:goto p82ef;
 case 0x82f3:goto p82f3;
 case 0x82f6:goto p82f6;
 case 0x82f8:goto p82f8;
 case 0x82fa:goto p82fa;
 case 0x82fd:goto p82fd;
 case 0x82ff:goto p82ff;
 case 0x8302:goto p8302;
 case 0x8304:goto p8304;
 case 0x8306:goto p8306;
 case 0x8309:goto p8309;
 case 0x830b:goto p830b;
 case 0x830d:goto p830d;
 case 0x8310:goto p8310;
 case 0x8313:goto p8313;
 case 0x8315:goto p8315;
 case 0x8317:goto p8317;
 case 0x831a:goto p831a;
 case 0x831c:goto p831c;
 case 0x831f:goto p831f;
 case 0x8322:goto p8322;
 case 0x8324:goto p8324;
 case 0x8326:goto p8326;
 case 0x8329:goto p8329;
 case 0x832b:goto p832b;
 case 0x832e:goto p832e;
 case 0x8331:goto p8331;
 case 0x8333:goto p8333;
 case 0x8336:goto p8336;
 case 0x8339:goto p8339;
 case 0x833b:goto p833b;
 case 0x833e:goto p833e;
 case 0x8341:goto p8341;
 case 0x8343:goto p8343;
 case 0x8346:goto p8346;
 case 0x8349:goto p8349;
 case 0x834b:goto p834b;
 case 0x834d:goto p834d;
 case 0x8350:goto p8350;
 case 0x8353:goto p8353;
 case 0x8356:goto p8356;
 case 0x8358:goto p8358;
 case 0x835a:goto p835a;
 case 0x835d:goto p835d;
 case 0x835f:goto p835f;
 case 0x8362:goto p8362;
 case 0x8365:goto p8365;
 case 0x8368:goto p8368;
 case 0x836b:goto p836b;
 case 0x836e:goto p836e;
 case 0x8371:goto p8371;
 case 0x8374:goto p8374;
 case 0x8377:goto p8377;
 case 0x837a:goto p837a;
 case 0x837d:goto p837d;
 case 0x8380:goto p8380;
 case 0x8383:goto p8383;
 case 0x8386:goto p8386;
 case 0x8389:goto p8389;
 case 0x838c:goto p838c;
 case 0x838f:goto p838f;
 case 0x8392:goto p8392;
 case 0x8395:goto p8395;
 case 0x8396:goto p8396;
 case 0x8399:goto p8399;
 case 0x839c:goto p839c;
 case 0x839f:goto p839f;
 case 0x83a2:goto p83a2;
 case 0x83a3:goto p83a3;
 case 0x83a6:goto p83a6;
 case 0x83a9:goto p83a9;
 case 0x83ac:goto p83ac;
 case 0x83af:goto p83af;
 case 0x83b2:goto p83b2;
 case 0x83b5:goto p83b5;
 case 0x83b8:goto p83b8;
 case 0x83bb:goto p83bb;
 case 0x83be:goto p83be;
 case 0x83c1:goto p83c1;
 case 0x83c4:goto p83c4;
 case 0x83c7:goto p83c7;
 case 0x83ca:goto p83ca;
 case 0x83cd:goto p83cd;
 case 0x83d0:goto p83d0;
 case 0x83d3:goto p83d3;
 case 0x83d6:goto p83d6;
 case 0x83d9:goto p83d9;
 case 0x83dc:goto p83dc;
 case 0x83df:goto p83df;
 case 0x83e2:goto p83e2;
 case 0x83e5:goto p83e5;
 case 0x83e8:goto p83e8;
 case 0x83eb:goto p83eb;
 case 0x83ee:goto p83ee;
 case 0x83f1:goto p83f1;
 case 0x83f4:goto p83f4;
 case 0x83f7:goto p83f7;
 case 0x83f8:goto p83f8;
 case 0x83f9:goto p83f9;
 case 0x83fb:goto p83fb;
 case 0x83fe:goto p83fe;
 case 0x8400:goto p8400;
 case 0x8403:goto p8403;
 case 0x8405:goto p8405;
 case 0x8408:goto p8408;
 case 0x840a:goto p840a;
 case 0x840d:goto p840d;
 case 0x840f:goto p840f;
 case 0x8412:goto p8412;
 case 0x8414:goto p8414;
 case 0x8417:goto p8417;
 case 0x8419:goto p8419;
 case 0x841c:goto p841c;
 case 0x841e:goto p841e;
 case 0x8421:goto p8421;
 case 0x8423:goto p8423;
 case 0x8426:goto p8426;
 case 0x8429:goto p8429;
 case 0x842b:goto p842b;
 case 0x842e:goto p842e;
 default:return 0;
 }
p8297:
 EDGE(3);ScWorldGuestStepPrepared(w,c,r);c->mf=false;NEXT(0x8299,p8299);
p8299:
 EDGE(4);put(r,c->sp-1,c->dp);c->sp-=2;NEXT(0x829a,p829a);
p829a:
 if(c->mf)return cycles;
 EDGE(4);put(r,c->sp-1,c->a);c->sp-=2;NEXT(0x829b,p829b);
p829b:
 EDGE(2);c->a=c->dp;nz(c,c->a,false);NEXT(0x829c,p829c);
p829c:
 EDGE(2);c->c=true;NEXT(0x829d,p829d);
p829d:
 if(c->mf!=false)return cycles;
 EDGE(2+!c->mf);add(c,2,true);NEXT(0x82a0,p82a0);
p82a0:
 if(c->a<0x20 || c->a>0x1ff0)return cycles;
 EDGE(2);c->dp=c->a;nz(c,c->dp,false);NEXT(0x82a1,p82a1);
p82a1:
 if(c->mf || c->sp>0x1ffd)return cycles;
 EDGE(5);c->a=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->a,false);NEXT(0x82a2,p82a2);
p82a2:
 EDGE(3+!c->mf+((c->dp&255)!=0));store(r,c->dp+0x0,0,c->mf);NEXT(0x82a4,p82a4);
p82a4:
 EDGE(3);c->mf=true;NEXT(0x82a6,p82a6);
p82a6:
 EDGE(4+!c->mf);store(r,0xb86,0,c->mf);NEXT(0x82a9,p82a9);
p82a9:
 EDGE(4+!c->mf);store(r,0xb85,0,c->mf);NEXT(0x82ac,p82ac);
p82ac:
 if(!single && w->active) {unsigned fused=ScWorldGuestSweepStageStep(w,c,r,rom,budget-cycles);if(fused)return cycles+fused;}
 EDGE(3);c->mf=false;NEXT(0x82ae,p82ae);
p82ae:
 EDGE(4+!c->mf);ScWorldGuestStepPrepared(w,c,r);load(c,value(r,0xb85,c->mf));NEXT(0x82b1,p82b1);
p82b1:
 EDGE(6);put(r,c->sp-1,0x82b3);c->sp-=2;c->pc=0x849e;c->cyclesUsed=6;cycles+=6;
 /* Expanded geometry is already a zero-clock native hook. Join its return
  * when both original call/return edges fit; stock lookup keeps its original
  * arithmetic boundary. Single-edge entry never fuses the call. */
 if(!single && w->active && budget-cycles>=6) {ScWorldGuestStepPrepared(w,c,r);goto coordinate_return;}
 return cycles;
p82b4:
 EDGE(4+((c->dp&255)!=0));compare(c,word(r,c->dp),false,c->x);NEXT(0x82b6,p82b6);
p82b6:
 EDGE(c->z?3:2);if(c->z) {NEXT(0x82b9,p82b9);} NEXT(0x82b8,p82b8);
p82b8:
 EDGE(2);NEXT(0x82b9,p82b9);
p82b9:
 EDGE(4+((c->dp&255)!=0));ScWorldGuestStepPrepared(w,c,r);c->x=(uint16_t)word(r,c->dp+0x0);nz(c,c->x,false);NEXT(0x82bb,p82bb);
p82bb:
 EDGE(5);put(r,0xb49,c->x);NEXT(0x82be,p82be);
p82be:
 if(!w->active && 0x10200+c->x+0+2>0x20000)return cycles;
 EDGE(5+!c->mf);load(c,tile_read(w,c,r,0));NEXT(0x82c2,p82c2);
p82c2:
 EDGE(2);++c->x;nz(c,c->x,false);NEXT(0x82c3,p82c3);
p82c3:
 EDGE(2);++c->x;nz(c,c->x,false);NEXT(0x82c4,p82c4);
p82c4:
 EDGE(4+((c->dp&255)!=0));put(r,c->dp+0x0,c->x);NEXT(0x82c6,p82c6);
p82c6:
 EDGE(4+!c->mf);store(r,0xb87,c->a,c->mf);NEXT(0x82c9,p82c9);
p82c9:
 if(c->mf!=false)return cycles;
 EDGE(2+!c->mf);load(c,c->a&0x3ff);NEXT(0x82cc,p82cc);
p82cc:
 EDGE(4+!c->mf);store(r,0xb89,c->a,c->mf);NEXT(0x82cf,p82cf);
p82cf:
 if(c->mf!=false)return cycles;
 EDGE(2+!c->mf);compare(c,0x7f,c->mf,c->a);NEXT(0x82d2,p82d2);
p82d2:
 EDGE(!c->z?3:2);if(!c->z) {NEXT(0x82d9,p82d9);} NEXT(0x82d4,p82d4);
p82d4:
 EDGE(6);put(r,c->sp-1,0x82d6);c->sp-=2;c->pc=0xa7e0;c->cyclesUsed=6;cycles+=6;return cycles;
p82d7:
 EDGE(3);NEXT(0x8341,p8341);
p82d9:
 EDGE(2);c->y=c->a;nz(c,c->y,false);NEXT(0x82da,p82da);
p82da:
 EDGE(3);c->mf=true;NEXT(0x82dc,p82dc);
p82dc:
 if(c->y>1023)return cycles;
 EDGE(4+!c->mf+(c->y>=21));v=rom[0x184eb+c->y];if(!c->mf)v|=(unsigned)rom[0x184ec+c->y]<<8;load(c,v);NEXT(0x82df,p82df);
p82df:
 if(c->mf!=true)return cycles;
 EDGE(2+!c->mf);load(c,c->a&0x1);NEXT(0x82e1,p82e1);
p82e1:
 EDGE(c->z?3:2);if(c->z) {NEXT(0x82ff,p82ff);} NEXT(0x82e3,p82e3);
p82e3:
 EDGE(3);c->mf=false;NEXT(0x82e5,p82e5);
p82e5:
 EDGE(4+!c->mf);store(r,0x23f,0,c->mf);NEXT(0x82e8,p82e8);
p82e8:
 if(!w->active && 0x10200+c->x+240+2>0x20000)return cycles;
 EDGE(5+!c->mf);load(c,tile_read(w,c,r,1));NEXT(0x82ec,p82ec);
p82ec:
 if(c->mf!=false)return cycles;
 EDGE(2+!c->mf);load(c,c->a|0x4000);NEXT(0x82ef,p82ef);
p82ef:
 if(!w->active && 0x10200+c->x+240+2>0x20000)return cycles;
 EDGE(5+!c->mf);tile_write(w,c,r,1);NEXT(0x82f3,p82f3);
p82f3:
 EDGE(4+!c->mf);load(c,value(r,0x23f,c->mf));NEXT(0x82f6,p82f6);
p82f6:
 EDGE(!c->z?3:2);if(!c->z) {NEXT(0x82e5,p82e5);} NEXT(0x82f8,p82f8);
p82f8:
 EDGE(3);c->mf=true;NEXT(0x82fa,p82fa);
p82fa:
 EDGE(6);put(r,c->sp-1,0x82fc);c->sp-=2;c->pc=0x90c5;c->cyclesUsed=6;cycles+=6;return cycles;
p82fd:
 EDGE(3);NEXT(0x8341,p8341);
p82ff:
 if(c->y>1023)return cycles;
 EDGE(4+!c->mf+(c->y>=21));v=rom[0x184eb+c->y];if(!c->mf)v|=(unsigned)rom[0x184ec+c->y]<<8;load(c,v);NEXT(0x8302,p8302);
p8302:
 if(c->mf!=true)return cycles;
 EDGE(2+!c->mf);load(c,c->a&0x20);NEXT(0x8304,p8304);
p8304:
 EDGE(c->z?3:2);if(c->z) {NEXT(0x830b,p830b);} NEXT(0x8306,p8306);
p8306:
 EDGE(6);put(r,c->sp-1,0x8308);c->sp-=2;c->pc=0xa73d;c->cyclesUsed=6;cycles+=6;return cycles;
p8309:
 EDGE(3);NEXT(0x831a,p831a);
p830b:
 EDGE(3);c->mf=true;NEXT(0x830d,p830d);
p830d:
 EDGE(5);c->y=(uint16_t)word(r,0xb89);nz(c,c->y,false);NEXT(0x8310,p8310);
p8310:
 if(c->y>1023)return cycles;
 EDGE(4+!c->mf+(c->y>=21));v=rom[0x184eb+c->y];if(!c->mf)v|=(unsigned)rom[0x184ec+c->y]<<8;load(c,v);NEXT(0x8313,p8313);
p8313:
 if(c->mf!=true)return cycles;
 EDGE(2+!c->mf);load(c,c->a&0x40);NEXT(0x8315,p8315);
p8315:
 EDGE(c->z?3:2);if(c->z) {NEXT(0x831a,p831a);} NEXT(0x8317,p8317);
p8317:
 EDGE(6);put(r,c->sp-1,0x8319);c->sp-=2;c->pc=0xa493;c->cyclesUsed=6;cycles+=6;return cycles;
p831a:
 EDGE(3);c->mf=true;NEXT(0x831c,p831c);
p831c:
 EDGE(5);c->y=(uint16_t)word(r,0xb89);nz(c,c->y,false);NEXT(0x831f,p831f);
p831f:
 if(c->y>1023)return cycles;
 EDGE(4+!c->mf+(c->y>=21));v=rom[0x184eb+c->y];if(!c->mf)v|=(unsigned)rom[0x184ec+c->y]<<8;load(c,v);NEXT(0x8322,p8322);
p8322:
 if(c->mf!=true)return cycles;
 EDGE(2+!c->mf);load(c,c->a&0x10);NEXT(0x8324,p8324);
p8324:
 EDGE(c->z?3:2);if(c->z) {NEXT(0x832b,p832b);} NEXT(0x8326,p8326);
p8326:
 EDGE(6);put(r,c->sp-1,0x8328);c->sp-=2;c->pc=0xa7da;c->cyclesUsed=6;cycles+=6;return cycles;
p8329:
 EDGE(3);NEXT(0x833e,p833e);
p832b:
 EDGE(5);c->y=(uint16_t)word(r,0xb89);nz(c,c->y,false);NEXT(0x832e,p832e);
p832e:
 EDGE(3);compare(c,0x365,false,c->y);NEXT(0x8331,p8331);
p8331:
 EDGE(!c->z?3:2);if(!c->z) {NEXT(0x8336,p8336);} NEXT(0x8333,p8333);
p8333:
 EDGE(6);put(r,c->sp-1,0x8335);c->sp-=2;c->pc=0xad3f;c->cyclesUsed=6;cycles+=6;return cycles;
p8336:
 EDGE(3);compare(c,0x364,false,c->y);NEXT(0x8339,p8339);
p8339:
 EDGE(!c->z?3:2);if(!c->z) {NEXT(0x833e,p833e);} NEXT(0x833b,p833b);
p833b:
 EDGE(6);put(r,c->sp-1,0x833d);c->sp-=2;c->pc=0xaa8b;c->cyclesUsed=6;cycles+=6;return cycles;
p833e:
 EDGE(6);put(r,c->sp-1,0x8340);c->sp-=2;c->pc=0x83f9;c->cyclesUsed=6;cycles+=6;if(!single)goto p83f9;return cycles;
p8341:
 EDGE(3);c->mf=true;NEXT(0x8343,p8343);
p8343:
 if(w->active && w->huge) {if(3>budget-cycles)return cycles;ScWorldGuestStepPrepared(w,c,r);if(c->pc==0x82ac)goto p82ac;goto p835d;}
 EDGE(6+2*!c->mf);v=value(r,0xb85,c->mf)+1;store(r,0xb85,v,c->mf);nz(c,v,c->mf);NEXT(0x8346,p8346);
p8346:
 EDGE(4+!c->mf);load(c,value(r,0xb85,c->mf));NEXT(0x8349,p8349);
p8349:
 if(w->active && w->huge)return cycles; /* main owns resumed wide byte-geometry hooks */
 if(c->mf!=true)return cycles;
 EDGE(2+!c->mf);compare(c,w->active?ScWorldWidth(w):120,c->mf,c->a);NEXT(0x834b,p834b);
p834b:
 EDGE(c->z?3:2);if(c->z) {NEXT(0x8350,p8350);} NEXT(0x834d,p834d);
p834d:
 EDGE(3);NEXT(0x82ac,p82ac);
p8350:
 EDGE(6+2*!c->mf);v=value(r,0xb86,c->mf)+1;store(r,0xb86,v,c->mf);nz(c,v,c->mf);NEXT(0x8353,p8353);
p8353:
 EDGE(4+!c->mf);load(c,value(r,0xb86,c->mf));NEXT(0x8356,p8356);
p8356:
 if(w->active && w->huge)return cycles; /* main owns resumed wide byte-geometry hooks */
 if(c->mf!=true)return cycles;
 EDGE(2+!c->mf);compare(c,w->active?ScWorldHeight(w):100,c->mf,c->a);NEXT(0x8358,p8358);
p8358:
 EDGE(c->z?3:2);if(c->z) {NEXT(0x835d,p835d);} NEXT(0x835a,p835a);
p835a:
 EDGE(3);NEXT(0x82a9,p82a9);
p835d:
 EDGE(3);c->mf=false;NEXT(0x835f,p835f);
p835f:
 EDGE(4+!c->mf);load(c,value(r,0xb8d,c->mf));NEXT(0x8362,p8362);
p8362:
 EDGE(4+!c->mf);store(r,0xc73,c->a,c->mf);NEXT(0x8365,p8365);
p8365:
 EDGE(4+!c->mf);load(c,value(r,0xb91,c->mf));NEXT(0x8368,p8368);
p8368:
 EDGE(4+!c->mf);store(r,0xc75,c->a,c->mf);NEXT(0x836b,p836b);
p836b:
 EDGE(4+!c->mf);load(c,value(r,0xb95,c->mf));NEXT(0x836e,p836e);
p836e:
 EDGE(4+!c->mf);store(r,0xc77,c->a,c->mf);NEXT(0x8371,p8371);
p8371:
 EDGE(4+!c->mf);load(c,value(r,0xe15,c->mf));NEXT(0x8374,p8374);
p8374:
 EDGE(4+!c->mf);store(r,0xc7f,c->a,c->mf);NEXT(0x8377,p8377);
p8377:
 EDGE(4+!c->mf);load(c,value(r,0xe17,c->mf));NEXT(0x837a,p837a);
p837a:
 EDGE(4+!c->mf);store(r,0xc81,c->a,c->mf);NEXT(0x837d,p837d);
p837d:
 EDGE(4+!c->mf);load(c,value(r,0xe19,c->mf));NEXT(0x8380,p8380);
p8380:
 EDGE(4+!c->mf);store(r,0xc83,c->a,c->mf);NEXT(0x8383,p8383);
p8383:
 EDGE(4+!c->mf);load(c,value(r,0xe1b,c->mf));NEXT(0x8386,p8386);
p8386:
 EDGE(4+!c->mf);store(r,0xc79,c->a,c->mf);NEXT(0x8389,p8389);
p8389:
 EDGE(4+!c->mf);load(c,value(r,0xe1d,c->mf));NEXT(0x838c,p838c);
p838c:
 EDGE(4+!c->mf);store(r,0xc7b,c->a,c->mf);NEXT(0x838f,p838f);
p838f:
 EDGE(4+!c->mf);load(c,value(r,0xe1f,c->mf));NEXT(0x8392,p8392);
p8392:
 EDGE(4+!c->mf);store(r,0xc7d,c->a,c->mf);NEXT(0x8395,p8395);
p8395:
 EDGE(2);c->c=false;NEXT(0x8396,p8396);
p8396:
 EDGE(4+!c->mf);add(c,value(r,0x0e1b,c->mf),false);NEXT(0x8399,p8399);
p8399:
 EDGE(4+!c->mf);add(c,value(r,0x0e1d,c->mf),false);NEXT(0x839c,p839c);
p839c:
 EDGE(4+!c->mf);store(r,0xe21,c->a,c->mf);NEXT(0x839f,p839f);
p839f:
 EDGE(4+!c->mf);load(c,value(r,0xe0d,c->mf));NEXT(0x83a2,p83a2);
p83a2:
 EDGE(2);c->c=false;NEXT(0x83a3,p83a3);
p83a3:
 EDGE(4+!c->mf);add(c,value(r,0x0e0f,c->mf),false);NEXT(0x83a6,p83a6);
p83a6:
 EDGE(4+!c->mf);store(r,0xc85,c->a,c->mf);NEXT(0x83a9,p83a9);
p83a9:
 EDGE(4+!c->mf);load(c,value(r,0xe0b,c->mf));NEXT(0x83ac,p83ac);
p83ac:
 EDGE(4+!c->mf);store(r,0xc87,c->a,c->mf);NEXT(0x83af,p83af);
p83af:
 EDGE(4+!c->mf);load(c,value(r,0xe09,c->mf));NEXT(0x83b2,p83b2);
p83b2:
 EDGE(4+!c->mf);store(r,0xc89,c->a,c->mf);NEXT(0x83b5,p83b5);
p83b5:
 EDGE(4+!c->mf);load(c,value(r,0xe07,c->mf));NEXT(0x83b8,p83b8);
p83b8:
 EDGE(4+!c->mf);store(r,0xc8b,c->a,c->mf);NEXT(0x83bb,p83bb);
p83bb:
 EDGE(4+!c->mf);load(c,value(r,0xca1,c->mf));NEXT(0x83be,p83be);
p83be:
 EDGE(4+!c->mf);store(r,0xc8d,c->a,c->mf);NEXT(0x83c1,p83c1);
p83c1:
 EDGE(4+!c->mf);load(c,value(r,0xe13,c->mf));NEXT(0x83c4,p83c4);
p83c4:
 EDGE(4+!c->mf);store(r,0xc8f,c->a,c->mf);NEXT(0x83c7,p83c7);
p83c7:
 EDGE(4+!c->mf);load(c,value(r,0xe11,c->mf));NEXT(0x83ca,p83ca);
p83ca:
 EDGE(4+!c->mf);store(r,0xc91,c->a,c->mf);NEXT(0x83cd,p83cd);
p83cd:
 EDGE(4+!c->mf);load(c,value(r,0xe03,c->mf));NEXT(0x83d0,p83d0);
p83d0:
 EDGE(4+!c->mf);store(r,0xc93,c->a,c->mf);NEXT(0x83d3,p83d3);
p83d3:
 EDGE(4+!c->mf);load(c,value(r,0xe05,c->mf));NEXT(0x83d6,p83d6);
p83d6:
 EDGE(4+!c->mf);store(r,0xc95,c->a,c->mf);NEXT(0x83d9,p83d9);
p83d9:
 EDGE(4+!c->mf);load(c,value(r,0xe23,c->mf));NEXT(0x83dc,p83dc);
p83dc:
 EDGE(4+!c->mf);store(r,0xc97,c->a,c->mf);NEXT(0x83df,p83df);
p83df:
 EDGE(4+!c->mf);load(c,value(r,0xe25,c->mf));NEXT(0x83e2,p83e2);
p83e2:
 EDGE(4+!c->mf);store(r,0xc99,c->a,c->mf);NEXT(0x83e5,p83e5);
p83e5:
 EDGE(4+!c->mf);load(c,value(r,0xe27,c->mf));NEXT(0x83e8,p83e8);
p83e8:
 EDGE(4+!c->mf);store(r,0xc9b,c->a,c->mf);NEXT(0x83eb,p83eb);
p83eb:
 EDGE(4+!c->mf);load(c,value(r,0xe29,c->mf));NEXT(0x83ee,p83ee);
p83ee:
 EDGE(4+!c->mf);store(r,0xc9d,c->a,c->mf);NEXT(0x83f1,p83f1);
p83f1:
 EDGE(4+!c->mf);load(c,value(r,0xe01,c->mf));NEXT(0x83f4,p83f4);
p83f4:
 EDGE(4+!c->mf);store(r,0xc9f,c->a,c->mf);NEXT(0x83f7,p83f7);
p83f7:
 if(c->sp>0x1ffd)return cycles;
 EDGE(5);c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);NEXT(0x83f8,p83f8);
p83f8:
 if(c->sp>0x1ffd)return cycles;EDGE(6);c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(!single && c->pc==0x8341)goto p8341;return cycles;
p83f9:
 EDGE(3);c->mf=false;NEXT(0x83fb,p83fb);
p83fb:
 EDGE(5);c->y=(uint16_t)word(r,0xb89);nz(c,c->y,false);NEXT(0x83fe,p83fe);
p83fe:
 EDGE(!c->z?3:2);if(!c->z) {NEXT(0x8405,p8405);} NEXT(0x8400,p8400);
p8400:
 EDGE(6+2*!c->mf);v=value(r,0xe27,c->mf)+1;store(r,0xe27,v,c->mf);nz(c,v,c->mf);NEXT(0x8403,p8403);
p8403:
 EDGE(3);NEXT(0x842e,p842e);
p8405:
 EDGE(3);compare(c,0x26,false,c->y);NEXT(0x8408,p8408);
p8408:
 EDGE(!c->c?3:2);if(!c->c) {NEXT(0x8414,p8414);} NEXT(0x840a,p840a);
p840a:
 EDGE(3);compare(c,0x28,false,c->y);NEXT(0x840d,p840d);
p840d:
 EDGE(c->c?3:2);if(c->c) {NEXT(0x8414,p8414);} NEXT(0x840f,p840f);
p840f:
 EDGE(6+2*!c->mf);v=value(r,0xe23,c->mf)+1;store(r,0xe23,v,c->mf);nz(c,v,c->mf);NEXT(0x8412,p8412);
p8412:
 EDGE(3);NEXT(0x842e,p842e);
p8414:
 EDGE(3);compare(c,0x14,false,c->y);NEXT(0x8417,p8417);
p8417:
 EDGE(!c->c?3:2);if(!c->c) {NEXT(0x8423,p8423);} NEXT(0x8419,p8419);
p8419:
 EDGE(3);compare(c,0x26,false,c->y);NEXT(0x841c,p841c);
p841c:
 EDGE(c->c?3:2);if(c->c) {NEXT(0x8423,p8423);} NEXT(0x841e,p841e);
p841e:
 EDGE(6+2*!c->mf);v=value(r,0xe25,c->mf)+1;store(r,0xe25,v,c->mf);nz(c,v,c->mf);NEXT(0x8421,p8421);
p8421:
 EDGE(3);NEXT(0x842e,p842e);
p8423:
 if(c->y>1023)return cycles;
 EDGE(4+!c->mf+(c->y>=21));v=rom[0x184eb+c->y];if(!c->mf)v|=(unsigned)rom[0x184ec+c->y]<<8;load(c,v);NEXT(0x8426,p8426);
p8426:
 if(c->mf!=false)return cycles;
 EDGE(2+!c->mf);load(c,c->a&0x8);NEXT(0x8429,p8429);
p8429:
 EDGE(c->z?3:2);if(c->z) {NEXT(0x842e,p842e);} NEXT(0x842b,p842b);
p842b:
 EDGE(6+2*!c->mf);v=value(r,0xe29,c->mf)+1;store(r,0xe29,v,c->mf);nz(c,v,c->mf);NEXT(0x842e,p842e);
p842e:
 if(c->sp>0x1ffd)return cycles;EDGE(6);c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;if(!single && c->pc==0x8341)goto p8341;return cycles;
coordinate_return:
 if(c->sp>0x1ffd)return cycles;EDGE(6);c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cycles+=6;
 if(!single && c->pc==0x82b4)goto p82b4;
 return cycles;
#undef EDGE
#undef NEXT
}
unsigned ScSweepStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned budget) {return execute(w,c,r,rom,budget,false);}
unsigned ScSweepInstructionStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom) {return execute(w,c,r,rom,UINT32_MAX,true);}
