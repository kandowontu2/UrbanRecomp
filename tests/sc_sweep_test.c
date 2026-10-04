#include "sc_sweep.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t ram[0x20000],rom[0x80000],before_ram[0x20000],actual_ram[0x20000];
static ScWorld world,before_world,actual_world;
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
static void check(const Interp816 *actual,const Interp816 *expected,unsigned cost,unsigned elapsed,unsigned start) {
    if(cost!=elapsed || memcmp(actual,expected,sizeof *actual) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,&actual_world,sizeof world)) {
        fprintf(stderr,"sweep start=%x clocks=%u/%u pc=%x/%x A=%x/%x X=%x/%x Y=%x/%x flags=%x/%x scan=%u/%u\n",
            start,cost,elapsed,actual->pc,expected->pc,actual->a,expected->a,actual->x,expected->x,actual->y,expected->y,
            interp816_getFlags((Interp816 *)actual),interp816_getFlags((Interp816 *)expected),actual_world.field_scan,world.field_scan);
        abort();
    }
}
int main(int argc,char **argv) {
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
 Interp816 *c=interp816_init(NULL,read_bus,write_bus);assert(c);
 const unsigned entries[]={0x8297,0x82ac,0x82b4,0x82e5,0x82fd,0x8309,0x831a,0x832b,0x835d,0x83f9,0x84c3,0x84ea};
 const unsigned tiles[]={0,0x15,0x26,0x80,0x84,0x144,0x201,0x30,0x40,0x377,0x364,0x365,0x7f};
 const unsigned budgets[]={0,1,2,3,5,6,12,31,128,256,1024};
 unsigned spans=0,edges=0,rejects=0;uint8_t seen[65536]={0};
 for(unsigned map=0;map<5;++map) for(unsigned n=0;n<78;++n) {
  ScWorldReset(&world);world.active=map!=0;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
  world.scan_spread=map>=2 && n>=39;
  memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(c);
  unsigned width=world.active?ScWorldWidth(&world):120,height=world.active?ScWorldHeight(&world):100;
  unsigned x=n&1?width-1:width/2,y=n&2?height-1:height/2,offset=2*(y*width+x),tile=tiles[n%13];
  if(world.active) {ScWorldPutCell(&world,x,y,tile|0xc000);if(y+1<height && x+1<width)ScWorldPutCell(&world,x+1,y+1,0x15);}
  else {put(0x10200+offset,tile|0xc000);if(offset+242<24000)put(0x10200+offset+242,0x15);}
  world.scan_x=x;world.scan_y=y;world.coord[2][0]=x;world.coord[2][1]=y;world.map_anchor=offset;
  ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;put(0xb89,tile);
  c->k=c->db=3;c->pc=(uint16_t)entries[n%12];c->dp=n&1?0x1eef:0x1e00;c->sp=0x1f75;
  c->e=c->xf=c->d=false;c->mf=c->pc!=0x82b4 && c->pc!=0x82e5;c->i=true;c->c=c->v=true;c->a=0xab12;c->x=(uint16_t)offset;c->y=tile;
  put(c->dp,offset);put(c->sp+1,c->pc==0x8297 || c->pc>=0x83f9?0x6fff:0x1e00);put(c->sp+3,0x6fff);
  for(unsigned p=0xe01;p<=0xe29;p+=2)put(p,(p+n)%4==0?65535:(p+n)%4==1?32767:32768+n);
  ScWorldGuestStep(&world,c,ram);Interp816 initial=*c;before_world=world;memcpy(before_ram,ram,sizeof ram);
  for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
   *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);memset(&guest,0,sizeof guest);
   unsigned cost=ScSweepStep(&world,c,ram,rom,budgets[b]);Interp816 actual=*c;actual_world=world;memcpy(actual_ram,ram,sizeof ram);
   *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);unsigned elapsed=0,guard=0;
   while(elapsed<cost) {assert(++guard<3000);unsigned pc=c->pc;ScWorldGuestStep(&world,c,ram);if(c->pc!=pc)continue;ScWorldGuestBegin(&guest,&world,c,rom,sizeof rom);elapsed+=interp816_runOpcode(c);}
   if(world.huge && c->pc==0x8343 && actual.pc!=c->pc)ScWorldGuestStep(&world,c,ram);
   check(&actual,c,cost,elapsed,initial.pc);assert(cost<=budgets[b]);++spans;
  }
  *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);
  for(unsigned guard=0;guard<300 && ScSweepOwns(c->pc);++guard) {
   ScWorldGuestStep(&world,c,ram);
   if(!ScSweepOwns(c->pc))break;
   if(!(seen[c->pc]&(1u<<map))) {
    seen[c->pc]|=1u<<map;Interp816 old=*c;before_world=world;memcpy(before_ram,ram,sizeof ram);
    unsigned cost=ScSweepInstructionStep(&world,c,ram,rom);if(!cost)fprintf(stderr,"sweep atomic rejected pc=%x map=%u case=%u dp=%x sp=%x mf=%d xf=%d y=%x\n",old.pc,map,n,c->dp,c->sp,c->mf,c->xf,c->y);assert(cost);Interp816 actual=*c;actual_world=world;memcpy(actual_ram,ram,sizeof ram);
    *c=old;world=before_world;memcpy(ram,before_ram,sizeof ram);original(c);check(&actual,c,cost,c->cyclesUsed,old.pc);++edges;
   } else original(c);
  }
 }
 for(unsigned n=0;n<8;++n) {
  interp816_reset(c);c->k=c->db=3;c->pc=0x82ac;c->dp=0x1eef;c->sp=0x1f75;c->e=c->xf=c->d=false;c->i=false;
  switch(n) {case 0:c->nmiWanted=true;break;case 1:c->irqWanted=true;break;case 2:c->waiting=true;break;case 3:c->stopped=true;break;case 4:c->db=0;break;case 5:c->sp=c->dp;break;case 6:c->xf=true;break;case 7:c->d=true;break;}
  Interp816 before=*c;before_world=world;memcpy(before_ram,ram,sizeof ram);
  assert(!ScSweepStep(&world,c,ram,rom,1024) && !ScSweepInstructionStep(&world,c,ram,rom));
  assert(!memcmp(c,&before,sizeof before) && !memcmp(&world,&before_world,sizeof world) && !memcmp(ram,before_ram,sizeof ram));++rejects;
 }
 assert(edges>600);printf("PASS: %u bounded connected sweep spans, %u independent instruction edges, %u immutable fallbacks; CPU/RAM/full-world/clocks exact on all five sizes\n",spans,edges,rejects);interp816_free(c);
}
