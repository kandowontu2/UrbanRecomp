#include "sc_construction.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t rom[0x80000], ram[0x20000], before[0x20000];
static ScBuildPlan plan;
static ScWorld world,world_before;
static unsigned word(unsigned a) { return ram[a] | (ram[a+1]<<8); }
static void put(unsigned a,unsigned v) { ram[a]=(uint8_t)v; ram[a+1]=(uint8_t)(v>>8); }
static unsigned tile(int x,int y) { return word(0x10200+(y*120+x)*2)&0x3ff; }
static void reset(unsigned cash) {
  memset(ram,0,sizeof ram); put(0xb9d,cash); ram[0xb9f]=(uint8_t)(cash>>16);
  put(0x01bd,40); put(0x01bf,40);
  memset(ram+0x1c00,0x69,0x400);
}
static unsigned build(unsigned tool,int x0,int y0,int x1,int y1) {
  assert(ScConstructionPlan(&plan,tool,x0,y0,x1,y1));
  unsigned cost=0;
  ScBuildResult r=ScConstructionCommit(ram,rom,sizeof rom,&plan,&cost);
  if (r!=SC_BUILD_OK) fprintf(stderr,"tool %u result %d cost %u\n",tool,r,cost);
  assert(r==SC_BUILD_OK); return cost;
}
int main(int argc,char **argv) {
  assert(argc==2); FILE *f=fopen(argv[1],"rb"); assert(f);
  assert(fread(rom,1,sizeof rom,f)==sizeof rom); fclose(f);
  reset(1000);
  assert(build(1,45,45,52,47)==80); /* Dominant axis: eight road cells. */
  for (int x=45;x<=52;++x) assert(tile(x,45)>=0x30 && tile(x,45)<0x40);
  assert(!tile(45,46)); assert(word(0xb9d)==920);
  for (unsigned i=0x1c00;i<0x2000;++i) assert(ram[i]==0x69);
  assert(build(1,45,45,52,45)==0); /* Existing road costs nothing. */
  assert(build(1,48,42,48,48)==60); /* Joins the existing road once. */
  unsigned junction=tile(48,45);
  assert(junction>=0x30 && junction<0x40 && junction!=tile(47,45));
  reset(79); memcpy(before,ram,sizeof ram);
  assert(ScConstructionPlan(&plan,1,45,45,52,45));
  unsigned cost=0;
  assert(ScConstructionCommit(ram,rom,sizeof rom,&plan,&cost)==SC_BUILD_FUNDS);
  assert(cost==80 && !memcmp(ram,before,sizeof ram));
  reset(1000);
  assert(build(5,44,44,50,47)==600); /* Six non-overlapping 3x3 zones. */
  assert(plan.count==6);
  for (int y=44;y<50;++y) for (int x=44;x<53;++x) assert(tile(x,y)>=0x80);
  reset(1000);
  put(0x10200+(45*120+48)*2,0x99); /* Obstructed cell is skipped. */
  assert(build(1,45,45,52,45)==70); assert(tile(48,45)==0x99);
  reset(1000); assert(build(2,45,45,47,49)==100);
  reset(1000); assert(build(3,45,45,49,47)==25);
  reset(1000); assert(build(4,45,45,46,46)==40);
  assert(build(0,45,45,46,46)==4);
  for (int y=45;y<47;++y) for(int x=45;x<47;++x) assert(tile(x,y)<0x30);
  /* Exercise the original placement code beyond both old map boundaries and
   * the native 16-bit tile-index limit, including atomic world rollback. */
  for (unsigned tool=0;tool<=14;++tool) {
    reset(100000); put(0x1bd,180); put(0x1bf,160); ScWorldReset(&world); world.active=true;
    if (!tool) ScWorldPutCell(&world,200,180,0x30);
    assert(ScConstructionPlanWorld(&plan,&world,tool,200,180,200,180));
    ScBuildResult r=ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost);
    if (r!=SC_BUILD_OK || !cost) fprintf(stderr,"large tool %u result %d cost %u cell %x\n",tool,r,cost,ScWorldCell(&world,200,180));
    assert(r==SC_BUILD_OK && cost>0);
    assert(!ScWorldCell(&world,80,80));
    assert(!tile(80,80));
    if (tool>=5 && tool<=9) for (int y=180;y<183;++y) for(int x=200;x<203;++x)
      assert((ScWorldCell(&world,x,y)&1023)>=0x80);
  }
  reset(79); put(0x1bd,180); put(0x1bf,160); ScWorldReset(&world); world.active=true;
  memcpy(&world_before,&world,sizeof world); memcpy(before,ram,sizeof ram);
  assert(ScConstructionPlanWorld(&plan,&world,1,200,180,207,180));
  assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_FUNDS);
  assert(cost==80 && !memcmp(&world,&world_before,sizeof world) && !memcmp(ram,before,sizeof ram));
  reset(1000); put(0x1bd,180); put(0x1bf,160);
  assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
  for (int x=200;x<=207;++x) assert((ScWorldCell(&world,x,180)&1023)>=0x30);
  /* Reconnect an actual coal plant, wire and zone through the original power
   * routine, without waiting for or running a city/calendar tick. */
  reset(100000);
  build(13,40,40,40,40);
  build(5,50,40,50,40);
  build(3,44,41,46,41); build(3,48,41,49,41);
  assert(ScConstructionRefreshPower(ram,NULL,rom,sizeof rom));
  assert(!(word(0x10200+(41*120+51)*2)&0x8000));
  build(3,47,41,47,41);
  memcpy(before,ram,sizeof ram);
  assert(ScConstructionRefreshPower(ram,NULL,rom,sizeof rom));
  assert(word(0x10200+(41*120+51)*2)&0x8000);
  for (unsigned a=0;a<sizeof ram;++a)
    if (!(a>=0x10200 && a<0x15fc0) && !(a>=0x1a598 && a<0x1ab74)) assert(ram[a]==before[a]);
  build(0,47,41,47,41);
  assert(ScConstructionRefreshPower(ram,NULL,rom,sizeof rom));
  assert(!(word(0x10200+(41*120+51)*2)&0x8000));
  /* Full-size traversal crosses both the old bounds and the 16-bit offset. */
  reset(100000); ScWorldReset(&world); world.active=true;
  assert(ScConstructionPlanWorld(&plan,&world,13,190,175,190,175));
  assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
  assert(ScConstructionPlanWorld(&plan,&world,3,194,176,210,176));
  assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
  assert(ScConstructionRefreshPower(ram,&world,rom,sizeof rom));
  assert(ScWorldCell(&world,210,176)&0x8000);
  unsigned wire=ScWorldCell(&world,210,176)&1023;
  assert(!ScWorldCell(&world,90,76));
  /* A plant's original capacity still limits a connected network. */
  reset(100000); ScWorldReset(&world); world.active=true;
  ScWorldPutCell(&world,10,10,0x28c);
  for (int y=10;y<20;++y) for (int x=10;x<100;++x)
    if (x!=10 || y!=10) ScWorldPutCell(&world,x,y,wire);
  assert(ScConstructionRefreshPower(ram,&world,rom,sizeof rom));
  unsigned powered=0;
  for (int y=10;y<20;++y) for (int x=10;x<100;++x)
    powered+=(ScWorldCell(&world,x,y)&0x8000)!=0;
  if (powered!=700) fprintf(stderr,"power capacity: %u\n",powered);
  assert(powered==700);
  for(unsigned tool=0;tool<=14;++tool) {
    reset(100000);ScWorldReset(&world);world.active=world.huge=true;
    if(!tool) ScWorldPutCell(&world,460,380,0x30);
    assert(ScConstructionPlanWorld(&plan,&world,tool,460,380,460,380));
    assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK && cost);
    if(ScWorldCell(&world,204,124)) fprintf(stderr,"huge alias tool %u tile %x\n",tool,ScWorldCell(&world,204,124));
    assert(!ScWorldCell(&world,204,124));
    if(tool>=5 && tool<=9) for(int y=380;y<383;++y) for(int x=460;x<463;++x)
      assert((ScWorldCell(&world,x,y)&1023)>=0x80);
    for(int y=0;y<400;++y) for(int x=0;x<480;++x)
      if(x<458 || x>468 || y<378 || y>388) {
        unsigned unexpected=ScWorldCell(&world,x,y);
        if(unexpected) fprintf(stderr,"huge unexpected tool %u at %d,%d: %x\n",tool,x,y,unexpected);
        assert(!unexpected);
      }
    if(tool==14) {
      assert(ScConstructionPlanWorld(&plan,&world,3,464,381,470,381));
      assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
      assert(ScConstructionRefreshPower(ram,&world,rom,sizeof rom));
      assert(ScWorldCell(&world,461,381)&0x8000);
      assert(ScWorldCell(&world,470,381)&0x8000);
    }
  }
  /* A continuous power line crosses both byte-coordinate seams. It must
   * reach the far plant/zone instead of reconnecting their modulo proxies. */
  reset(100000);ScWorldReset(&world);world.active=world.huge=true;
  ScWorldPutCell(&world,253,253,0x27c);
  for(int y=253;y<=260;++y) ScWorldPutCell(&world,253,y,y==253?0x27c:wire);
  for(int x=254;x<=270;++x) ScWorldPutCell(&world,x,260,wire);
  assert(ScConstructionRefreshPower(ram,&world,rom,sizeof rom));
  assert(ScWorldCell(&world,270,260)&0x8000);
  ScWorldPutCell(&world,253,256,0);
  assert(ScConstructionRefreshPower(ram,&world,rom,sizeof rom));
  assert(!(ScWorldCell(&world,270,260)&0x8000));
  ScWorldReset(&world);world.active=world.huge=true;ScWorldPutCell(&world,300,300,0x27c);
  for(int y=300;y<325;++y) for(int x=300;x<400;++x)
    if(x!=300 || y!=300) ScWorldPutCell(&world,x,y,wire);
  assert(ScConstructionRefreshPower(ram,&world,rom,sizeof rom));powered=0;
  for(int y=300;y<325;++y) for(int x=300;x<400;++x) powered+=(ScWorldCell(&world,x,y)&0x8000)!=0;
  assert(powered==2000);
  for(unsigned huge=0;huge<2;++huge) for(unsigned gift=1;gift<=14;++gift) {
    reset(100000);ScWorldReset(&world);world.active=true;world.huge=huge;
    ram[0x3f5]=gift;put(0x3f3,0);
    int x=huge?460:200,y=huge?380:180;
    world.coord[1][0]=40;world.coord[1][1]=50;world.coord[2][0]=30;world.coord[2][1]=60;
    world_before=world;
    assert(ScConstructionPlanWorld(&plan,&world,15,x,y,x,y));
    ScBuildResult result=ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost);
    fprintf(stderr,"gift %u huge %u result %d cost %u inventory %u tile %x\n",gift,huge,result,cost,ram[0x3f5],ScWorldCell(&world,x,y));
    assert(result==SC_BUILD_OK);
    if(gift==6) { /* Landfill requires water within the footprint. */
      assert(!cost && ram[0x3f5]==6);
      for(int dy=0;dy<3;++dy) for(int dx=0;dx<3;++dx) ScWorldPutCell(&world,x+dx,y+dy,1);
      assert(ScConstructionPlanWorld(&plan,&world,15,x,y,x,y));
      assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
    }
    assert(cost==100 && !ram[0x3f5]);
    assert(!memcmp(world.coord,world_before.coord,sizeof world.coord));
    for(int yy=0;yy<ScWorldHeight(&world);++yy) for(int xx=0;xx<ScWorldWidth(&world);++xx)
      if(xx<x || xx>=x+3 || yy<y || yy>=y+3) assert(!ScWorldCell(&world,xx,yy));
  }
  puts("PASS: native construction costs, locality, rollback, power, gifts and preserved simulation coordinates");
}
