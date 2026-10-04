#include "sc_world_guest.h"
#include "sc_infrastructure.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t ram[0x20000],rom[0x80000],before_ram[0x20000],actual_ram[0x20000];
static ScWorld world;
static ScWorld before_world,actual_world;
static ScWorldGuest guest;
static uint8_t read_bus(void *ctx,uint32_t at) {
    (void)ctx;uint8_t value;if(ScWorldGuestRead(&guest,at,&value)) return value;
    unsigned bank=at>>16,p=at&65535;
    if(bank==0x7e || bank==0x7f) return ram[at&0x1ffff];
    if((bank&0x7f)<0x40 && p<0x2000) return ram[p];
    if(p>=0x8000) return rom[(bank&15)*0x8000+(p&0x7fff)];
    assert(!"unexpected device read");return 0;
}
static void write_bus(void *ctx,uint32_t at,uint8_t value) {
    (void)ctx;if(ScWorldGuestWrite(&guest,at,value)) return;
    unsigned bank=at>>16,p=at&65535;
    if(bank==0x7e || bank==0x7f) {ram[at&0x1ffff]=value;return;}
    if((bank&0x7f)<0x40 && p<0x2000) {ram[p]=value;return;}
    assert(!"unexpected device write");
}
static void put(unsigned at,unsigned value) {ram[at]=(uint8_t)value;ram[at+1]=(uint8_t)(value>>8);}
static void original(Interp816 *c) {
    ScWorldGuestStep(&world,c,ram);ScWorldGuestBegin(&guest,&world,c,rom,sizeof rom);interp816_runOpcode(c);
}

static void family_check(const Interp816 *actual,const Interp816 *expected,
 unsigned cost,unsigned elapsed,unsigned entry,unsigned map,unsigned n) {
 if(cost!=elapsed || memcmp(actual,expected,sizeof *actual) ||
    memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,&actual_world,sizeof world)) {
  fprintf(stderr,"infrastructure family entry=%x map=%u case=%u clocks=%u/%u pc=%x/%x A=%x/%x X=%x/%x Y=%x/%x flags=%x/%x dp=%x/%x sp=%x/%x\n",
   entry,map,n,cost,elapsed,actual->pc,expected->pc,actual->a,expected->a,
   actual->x,expected->x,actual->y,expected->y,interp816_getFlags((Interp816 *)actual),
   interp816_getFlags((Interp816 *)expected),actual->dp,expected->dp,actual->sp,expected->sp);
  if(memcmp(ram,actual_ram,sizeof ram)) for(unsigned i=0;i<sizeof ram;++i) if(ram[i]!=actual_ram[i]) {fprintf(stderr,"RAM first %x=%x/%x\n",i,actual_ram[i],ram[i]);break;}
  if(memcmp(&world,&actual_world,sizeof world)) for(unsigned i=0;i<sizeof world;++i) if(((uint8_t *)&world)[i]!=((uint8_t *)&actual_world)[i]) {fprintf(stderr,"world first %x=%x/%x\n",i,((uint8_t *)&actual_world)[i],((uint8_t *)&world)[i]);break;}
  abort();
 }
}
static void family_test(Interp816 *c) {
 const unsigned entries[]={0xa493,0xa503,0xa53b,0xa5d6,0xa70c,0xa73d,0xa78b,0xa7da};
 const unsigned budgets[]={0,1,3,12,47,128,1024};
 unsigned spans=0,edges=0,rejects=0,calls=0;uint8_t seen[65536]={0};
 for(unsigned map=1;map<5;++map) for(unsigned e=0;e<sizeof entries/sizeof *entries;++e) for(unsigned n=0;n<48;++n) {
  ScWorldReset(&world);world.active=true;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
  memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(c);
  unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
  unsigned x=n<24?10:n&1?width-1:width/2,y=n<24?12:n&2?height-1:height/2;
  unsigned offset=2*(y*width+x),tile=e==2?(n&1?0x355:0x354):e==3?(n&1?0x31:0x30):e>=5?(n%3==0?0x72:n%3==1?0x73:0x70):n%4==0?0x30:n%4==1?0x31:0x32+(n%14);
  world.coord[2][0]=x;world.coord[2][1]=y;world.map_anchor=offset;
  ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;put(0xb89,tile);put(0xb87,0x8052+(n&15));put(0xb49,offset);
  world.fields[4][(y/2)*(width/2)+x/2]=(uint8_t)(n*7);
  /* Closed/open bridge footprints and deliberately mismatching footprints.
   * Fill from the original ROM row-offset tables rather than the C module. */
  unsigned table=tile&1?0xa6e2:0xa6b8,pattern=table+((tile==0x354 || tile==0x355)?14:28);
  for(unsigned j=0;j<14;j+=2) {
   int at=(int)offset+(int16_t)(rom[0x10000+table+j]|rom[0x10000+table+j+1]<<8);
   unsigned value=rom[0x10000+pattern+j]|rom[0x10000+pattern+j+1]<<8;
   if(at>=0 && at+1<(int)(2*ScWorldCells(&world))) {world.tiles[at]=(uint8_t)value;world.tiles[at+1]=(uint8_t)(value>>8);}
  }
  int neighbor=(int)offset-(tile&1?4:(int)(2*width+2));
  if(e==3 && neighbor>=0) {world.tiles[neighbor]=2;world.tiles[neighbor+1]=0;}
  if(n%4==3) {world.tiles[offset]=0;world.tiles[offset+1]=0;}
  ram[0xa65]=(uint8_t)(x+(e==2 || n%3==0?30:0));ram[0xa63]=(uint8_t)y;
  for(unsigned j=0;j<16;j+=2) put(0xccf+j,n<24?0:n*31+j*17);
  if(n<24) put(0xccf,e==2?3:e==0 || e==5?65535:0);
  put(0xbc5,n%4==0?0:n%4==1?29:n%4==2?30:65535);
  put(0xa93,n&2?0:1);put(0xc0f,n&4?1:0);put(0xe15,65535-n);put(0xe17,65535-n);put(0xe19,65535-n);
  c->k=c->db=3;c->pc=(uint16_t)entries[e];c->dp=n&1?0x1eef:0x1e00;c->sp=0x1f75;
  c->e=c->xf=c->d=false;c->mf=false;c->i=true;c->c=c->v=n&1;c->a=0xab12;c->x=(uint16_t)offset;c->y=tile;
  put(c->dp,offset);put(c->sp+1,0x6fff);
  unsigned guard=0;
  while(c->pc!=0x7000) {
   assert(++guard<2000);ScWorldGuestStep(&world,c,ram);
   if(ScInfrastructureOwns(c->pc) && !(seen[c->pc]&(1u<<map))) {
    seen[c->pc]|=1u<<map;Interp816 initial=*c;before_world=world;memcpy(before_ram,ram,sizeof ram);
    for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
     *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);
     unsigned cost=ScInfrastructureStep(&world,c,ram,rom,budgets[b]);Interp816 actual=*c;actual_world=world;memcpy(actual_ram,ram,sizeof ram);
     *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);unsigned elapsed=0;
     while(elapsed<cost) {original(c);elapsed+=c->cyclesUsed;}
     family_check(&actual,c,cost,elapsed,initial.pc,map,n);assert(cost<=budgets[b]);++spans;
    }
    *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);
    unsigned cost=ScInfrastructureInstructionStep(&world,c,ram,rom);assert(cost);Interp816 actual=*c;actual_world=world;memcpy(actual_ram,ram,sizeof ram);
    *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);original(c);
    family_check(&actual,c,cost,c->cyclesUsed,initial.pc,map,n);++edges;
   } else original(c);
  }
  ++calls;
 }
 for(unsigned n=0;n<10;++n) {
  interp816_reset(c);c->k=c->db=3;c->pc=0xa493;c->dp=0x1eef;c->sp=0x1f75;c->e=c->xf=c->d=false;c->i=false;
  switch(n) {case 0:c->nmiWanted=true;break;case 1:c->irqWanted=true;break;case 2:c->waiting=true;break;case 3:c->stopped=true;break;case 4:c->db=0;break;case 5:c->sp=0;break;case 6:c->xf=true;break;case 7:c->d=true;break;case 8:c->dp=1;break;case 9:world.active=false;break;}
  Interp816 old=*c;before_world=world;memcpy(before_ram,ram,sizeof ram);
  assert(!ScInfrastructureStep(&world,c,ram,rom,1024) && !ScInfrastructureInstructionStep(&world,c,ram,rom));
  assert(!memcmp(c,&old,sizeof old) && !memcmp(&world,&before_world,sizeof world) && !memcmp(ram,before_ram,sizeof ram));++rejects;
 }
 const unsigned mutations[]={0xa4dc,0xa57a,0xa5c5,0xa657,0xa6a5,0xa786};
 for(unsigned i=0;i<sizeof mutations/sizeof *mutations;++i) {
  if(seen[mutations[i]]!=30) fprintf(stderr,"missing ordered mutation coverage pc=%x maps=%x\n",mutations[i],seen[mutations[i]]);
  assert(seen[mutations[i]]==30);
 }
 assert(edges>1000);printf("PASS: %u road/bridge/rail/port calls, %u bounded C spans, %u independent instruction edges and %u immutable fallbacks; CPU/RAM/full world/clocks exact on all expanded sizes\n",calls,spans,edges,rejects);
}

int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
 Interp816 *c=interp816_init(NULL,read_bus,write_bus);assert(c);
 if(getenv("SC_INFRASTRUCTURE_FAMILY_TEST")) {family_test(c);interp816_free(c);return 0;}
 unsigned spans=0,rejects=0;uint32_t rng=1;
 for(unsigned map=1;map<5;++map) for(unsigned n=0;n<400;++n) {
  ScWorldReset(&world);world.active=true;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
  memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(c);
  rng=rng*1664525u+1013904223u;ram[0xa65]=(uint8_t)rng;ram[0xb85]=(uint8_t)(rng>>8);ram[0xa63]=(uint8_t)(rng>>16);ram[0xb86]=(uint8_t)(rng>>24);
  if(n<8) {ram[0xa65]=n&1?255:0;ram[0xb85]=n&2?255:0;ram[0xa63]=n&4?255:0;ram[0xb86]=128;}
  c->k=c->db=3;c->pc=0xa70c;c->dp=n&1?0x1eef:0x1e02;c->sp=0x1f75;
  c->e=c->xf=c->d=false;c->mf=n&2;c->i=true;c->c=c->v=true;c->a=(uint16_t)(rng>>8);
  put(c->sp+1,0xa5e2);Interp816 initial=*c;memcpy(before_ram,ram,sizeof ram);
  unsigned cost=ScWorldGuestInfrastructureStep(&world,c,ram,256);assert(cost && cost<128);
  Interp816 actual=*c;memcpy(actual_ram,ram,sizeof ram);
  *c=initial;memcpy(ram,before_ram,sizeof ram);unsigned elapsed=0,guard=0;
  while(c->pc!=0xa5e3) {assert(++guard<80);original(c);elapsed+=c->cyclesUsed;}
  if(cost!=elapsed || memcmp(&actual,c,sizeof *c) || memcmp(ram,actual_ram,sizeof ram)) {
   fprintf(stderr,"infrastructure distance case=%u map=%u clocks=%u/%u A=%x/%x flags=%x/%x sp=%x/%x\n",n,map,cost,elapsed,actual.a,c->a,interp816_getFlags(&actual),interp816_getFlags(c),actual.sp,c->sp);exit(1);
  }
  ++spans;
  for(unsigned cut=0;cut<cost;++cut) {
   *c=initial;memcpy(ram,before_ram,sizeof ram);
   assert(!ScWorldGuestInfrastructureStep(&world,c,ram,cut));assert(!memcmp(c,&initial,sizeof *c) && !memcmp(ram,before_ram,sizeof ram));++rejects;
  }
 }
 for(unsigned entry=0;entry<3;++entry) for(unsigned n=0;n<9;++n) {
  interp816_reset(c);c->k=c->db=3;c->pc=entry==0?0xa70c:entry==1?0xa503:0xa493;c->dp=0x1eef;c->sp=0x1f75;c->e=c->xf=c->d=false;c->i=false;
  switch(n) {case 0:c->nmiWanted=true;break;case 1:c->irqWanted=true;break;case 2:c->waiting=true;break;case 3:c->stopped=true;break;case 4:c->db=0;break;case 5:c->sp=c->dp;break;case 6:c->xf=true;break;case 7:c->d=true;break;case 8:c->dp=1;break;}
  Interp816 old=*c;memcpy(before_ram,ram,sizeof ram);before_world=world;
  assert(!ScWorldGuestInfrastructureStep(&world,c,ram,256));assert(!memcmp(c,&old,sizeof old) && !memcmp(ram,before_ram,sizeof ram) && !memcmp(&world,&before_world,sizeof world));++rejects;
 }
 unsigned roads=0;
 for(unsigned entry=0;entry<2;++entry) for(unsigned map=1;map<5;++map) for(unsigned n=0;n<256;++n) {
  ScWorldReset(&world);world.active=true;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
  memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(c);
  unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
  unsigned x=(n*127+1)%width,y=(n*131+1)%height;
  if(n<4) {x=n&1?width-1:1;y=n&2?height-1:1;}
  world.coord[2][0]=x;world.coord[2][1]=y;world.map_anchor=2*(y*width+x);
  ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;put(0xb87,0x8052+(n&15));
  world.fields[4][(y/2)*(width/2)+x/2]=(uint8_t)n;
  if(entry) {put(0xb89,0x30+2+n%14);put(0xbc5,n&1?30:65535);put(0xe15,n?65535-n:65535);}
  c->k=c->db=3;c->pc=entry?0xa493:0xa503;c->dp=n&1?0x1eef:0x1e02;c->sp=0x1f75;
  c->e=c->xf=c->d=false;c->mf=n&2;c->i=true;c->c=c->v=n&4;c->a=0xffff;
  put(c->dp,world.map_anchor);put(c->sp+1,0x84c2);
  Interp816 initial=*c;memcpy(before_ram,ram,sizeof ram);before_world=world;
  unsigned cost=ScWorldGuestInfrastructureStep(&world,c,ram,256);assert(cost && cost<192);
  Interp816 actual=*c;memcpy(actual_ram,ram,sizeof ram);actual_world=world;
  *c=initial;memcpy(ram,before_ram,sizeof ram);world=before_world;unsigned elapsed=0,guard=0;
  while(c->pc!=0x84c3) {assert(++guard<80);original(c);elapsed+=c->cyclesUsed;}
  if(cost!=elapsed || memcmp(&actual,c,sizeof *c) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,&actual_world,sizeof world)) {
   fprintf(stderr,"road artwork case=%u map=%u clocks=%u/%u A=%x/%x flags=%x/%x X=%x/%x Y=%x/%x sp=%x/%x\n",n,map,cost,elapsed,actual.a,c->a,interp816_getFlags(&actual),interp816_getFlags(c),actual.x,c->x,actual.y,c->y,actual.sp,c->sp);exit(1);
  }
  ++roads;
  world=before_world;
  for(unsigned cut=0;cut<cost;++cut) {
   *c=initial;memcpy(ram,before_ram,sizeof ram);
   assert(!ScWorldGuestInfrastructureStep(&world,c,ram,cut));
   assert(!memcmp(c,&initial,sizeof *c) && !memcmp(ram,before_ram,sizeof ram));
   ++rejects;
  }
  assert(!memcmp(&world,&before_world,sizeof world));
 }
 /* Funding and bridge/non-road guards must yield without counting upkeep. */
 for(unsigned n=0;n<6;++n) {
  interp816_reset(c);c->k=c->db=3;c->pc=0xa493;c->dp=0x1eef;c->sp=0x1f75;c->e=c->xf=c->d=false;c->i=true;
  put(0xb89,n<2?0x32:n==2?0x30:n==3?0x31:n==4?0x80:0xffff);put(0xbc5,n==0?0:n==1?29:30);
  Interp816 old=*c;memcpy(before_ram,ram,sizeof ram);before_world=world;
  assert(!ScWorldGuestInfrastructureStep(&world,c,ram,256));
  assert(!memcmp(c,&old,sizeof old) && !memcmp(ram,before_ram,sizeof ram) && !memcmp(&world,&before_world,sizeof world));++rejects;
 }
 printf("PASS: %u distance calls, %u road artwork calls and %u immutable deadline/interrupt/eligibility fallbacks; CPU/RAM/world/clocks match the original routines on all expanded sizes\n",spans,roads,rejects);interp816_free(c);
}
