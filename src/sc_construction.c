#include "sc_construction.h"
#include "snes/interp816.h"
#include "sc_world_guest.h"
#include "sc_tile_lookup.h"
#include "sc_program.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

typedef struct {
  uint8_t ram[0x20000];
  const uint8_t *rom;
  size_t size;
  uint16_t product, quotient, dividend;
  uint8_t multiplicand;
  bool fault;
  ScWorld *world;
  ScWorldGuest guest;
} BuildBus;
static unsigned word(const uint8_t *r, unsigned a) { return r[a] | (r[a+1] << 8); }
static void put(uint8_t *r, unsigned a, unsigned v) { r[a]=(uint8_t)v; r[a+1]=(uint8_t)(v>>8); }
static unsigned funds(const uint8_t *r) { return word(r,0xb9d) | ((unsigned)r[0xb9f]<<16); }
static void money(uint8_t *r, unsigned v) { put(r,0xb9d,v); r[0xb9f]=(uint8_t)(v>>16); }
static uint8_t read_bus(void *ctx, uint32_t a) {
  BuildBus *b=ctx; unsigned bank=a>>16, p=a&0xffff;
  uint8_t value;
  if (ScWorldGuestRead(&b->guest,a,&value)) return value;
  if (bank==0x7e || bank==0x7f) return b->ram[a-0x7e0000];
  if ((bank&0x7f)<0x40 && p<0x2000) return b->ram[p];
  if (p==0x4214) return (uint8_t)b->quotient;
  if (p==0x4215) return (uint8_t)(b->quotient>>8);
  if (p==0x4216) return (uint8_t)b->product;
  if (p==0x4217) return (uint8_t)(b->product>>8);
  /* Let the live game's money refresh draw the real post-transaction funds. */
  if (a==0x00842e) return 0x6b; /* RTL */
  if (p>=0x8000) {
    size_t off=(size_t)(bank&0x7f)*0x8000 + p-0x8000;
    if (off<b->size) return b->rom[off];
  }
  b->fault=true; return 0;
}
static void write_bus(void *ctx, uint32_t a, uint8_t v) {
  BuildBus *b=ctx; unsigned bank=a>>16, p=a&0xffff;
  if (ScWorldGuestWrite(&b->guest,a,v)) return;
  if (bank==0x7e || bank==0x7f) { b->ram[a-0x7e0000]=v; return; }
  if ((bank&0x7f)<0x40 && p<0x2000) { b->ram[p]=v; return; }
  switch (p) {
  case 0x4202: b->multiplicand=v; break;
  case 0x4203: b->product=b->multiplicand*v; break;
  case 0x4204: b->dividend=(b->dividend&0xff00)|v; break;
  case 0x4205: b->dividend=(b->dividend&0xff)|(v<<8); break;
  case 0x4206:
    b->quotient=v?b->dividend/v:0xffff;
    b->product=v?b->dividend%v:b->dividend; break;
  default: b->fault=true; break;
  }
}
bool ScConstructionPlan(ScBuildPlan *p, unsigned tool, int x0, int y0, int x1, int y1) {
  return ScConstructionPlanWorld(p,NULL,tool,x0,y0,x1,y1);
}
bool ScConstructionPlanWorld(ScBuildPlan *p, const ScWorld *w, unsigned tool,
                             int x0, int y0, int x1, int y1) {
  p->count=0; p->tool=0;
  int width=w && w->active?ScWorldWidth(w):120,height=w && w->active?ScWorldHeight(w):100;
  if (tool>15 || x0<0 || x0>=width || x1<0 || x1>=width ||
      y0<0 || y0>=height || y1<0 || y1>=height) return false;
  p->tool=tool;
  if (tool>=10 && tool!=13 && tool!=14) { p->cells[0]=(ScBuildCell){x0,y0}; p->count=1; return true; }
  int dx=x1>=x0?1:-1, dy=y1>=y0?1:-1;
  if (tool>=1 && tool<=3) {
    if (abs(x1-x0)>=abs(y1-y0)) y1=y0; else x1=x0;
  }
  int step=tool==13 || tool==14?4:tool>=5?3:1;
  int nx=abs(x1-x0)/step+1, ny=abs(y1-y0)/step+1;
  if ((unsigned)nx*(unsigned)ny>SC_BUILD_MAX) return false;
  for (int y=0;y<ny;++y) for (int x=0;x<nx;++x)
    p->cells[p->count++]=(ScBuildCell){x0+x*step*dx,y0+y*step*dy};
  return true;
}
ScBuildResult ScConstructionCommit(uint8_t *ram,const uint8_t *rom,size_t size,
                                  const ScBuildPlan *p,unsigned *cost) {
  return ScConstructionCommitWorld(ram,NULL,rom,size,p,cost);
}
struct ScBuildWork {
  BuildBus *bus;
  Interp816 *cpu;
  const ScBuildPlan *plan;
  unsigned available,index,steps;
  bool placing,done,fault,reference;
  unsigned batch_operations,batch_limit;
};
/* 01:bcaf..bcc2 writes one already-validated footprint cell. Fuse its three
 * helpers instead of dispatching each 65816 instruction for every cell in a
 * large fill. Site validation, footprint patterns, joins and charging still
 * run through the cartridge. The private stack's popped bytes are discarded
 * at Finish; its live pattern pointer and stack depth remain untouched. */
static bool build_footprint(ScBuildWork *job) {
  BuildBus *b=job->bus;Interp816 *c=job->cpu;uint8_t *r=b->ram;
  if(!b->world || c->k!=1 || c->pc!=0xbcaf || c->mf || c->xf || c->dp || c->d)
    return false;
  unsigned i=word(r,0x217),pointer=word(r,(uint16_t)(c->sp+1));
  unsigned address=(((unsigned)c->db<<16)+pointer+i*2)&0xffffff;
  unsigned tile=read_bus(b,address)|(read_bus(b,(address+1)&0xffffff)<<8);
  unsigned dx=read_bus(b,0x018072+i),dy=read_bus(b,0x018096+i);
  unsigned x=(word(r,0x205)+dx)&65535,y=(word(r,0x207)+dy)&65535;
  unsigned sx=(word(r,0x211)+dx)&65535,sy=(word(r,0x213)+dy)&65535;
  put(r,0x215,tile);put(r,0x219,x);put(r,0x21b,y);put(r,0x21d,sx);put(r,0x21f,sy);
  unsigned at=2*(y*ScWorldWidth(b->world)+x);
  b->world->bank_anchor[1]=ScWorldContains(b->world,x,y)?at:UINT32_MAX;
  ScWorldPutCell(b->world,x,y,tile);
  unsigned column=((word(r,0x139)>>3)+sx)&65535;
  if(column>=32)column-=32;
  put(r,0x79,column);
  unsigned row=((word(r,0x137)>>3)+sy)&65535;
  if(row>=32)row-=32;
  b->multiplicand=(uint8_t)row;b->product=b->multiplicand*32;
  r[0xb1]=r[0xb3];
  unsigned sum=b->product+column;
  unsigned offset=(sum*2)&65535,lookup=0x02cf2d+((tile*2)&65535);
  unsigned graphic=read_bus(b,lookup)|(read_bus(b,lookup+1)<<8);
  put(r,0x2840+offset,graphic);
  c->a=graphic;c->x=offset;c->y=(uint16_t)(i*2);
  c->c=(tile&0x8000)!=0;c->v=((~(b->product^column)&(b->product^sum))&0x8000)!=0;
  c->n=(offset&0x8000)!=0;c->z=!offset;c->pc=0xbcc2;
  memset(&b->guest,0,sizeof b->guest);
  return true;
}
/* Common R/C/I sites contain only land, trees or removable wire. Collapse
 * the successful scan, leaving the final cell to the original classifier so
 * its scratch registers/flags and any failure path retain native semantics. */
static bool build_zone_scan(ScBuildWork *job) {
  BuildBus *b=job->bus;Interp816 *c=job->cpu;uint8_t *r=b->ram;
  unsigned tool=word(r,0x20d),last=word(r,0x20f),trees=0,wires=0;
  if(!b->world || c->k!=1 || c->pc!=0xb928 || c->mf || c->xf || c->dp || c->d ||
      tool<5 || tool>7 || last!=8)return false;
  int x0=word(r,0x205),y0=word(r,0x207);
  for(unsigned i=0;i<=last;++i) {
    int x=x0+b->rom[0x8072+i],y=y0+b->rom[0x8096+i];
    if(!ScWorldContains(b->world,x,y))return false;
    unsigned tile=ScWorldCell(b->world,x,y)&1023;
    if(tile && (!(word(r,0x195)&1) ||
        !((tile>=4 && tile<0x2e) || (tile>=0x62 && tile<0x6d))))return false;
    if(i) {trees+=tile!=0;wires+=tile>=0x62;}
  }
  put(r,0x227,word(r,0x227)+trees);put(r,0x225,word(r,0x225)+wires);put(r,0x20f,0);
  int x=x0+b->rom[0x8072],y=y0+b->rom[0x8096];
  unsigned offset=2*(y*ScWorldWidth(b->world)+x);
  b->world->bank_anchor[1]=offset;put(r,0x79,y*ScWorldWidth(b->world));
  c->a=ScWorldCell(b->world,x,y)&1023;c->x=(uint16_t)offset;
  unsigned low_y=y0&255,dy=b->rom[0x8096];
  c->v=((~(low_y^dy)&(low_y^(low_y+dy)))&128)!=0;
  c->z=!c->a;c->n=false;c->c=offset>65535;c->pc=0xb943;
  memset(&b->guest,0,sizeof b->guest);return true;
}
/* Most neighbours of a zone are terrain or another zone, so there is no
 * transport junction to rebuild. Keep the native join handler for roads,
 * rails and wires; fuse only iterations it would immediately skip. */
static bool build_no_join(ScBuildWork *job) {
  BuildBus *b=job->bus;Interp816 *c=job->cpu;uint8_t *r=b->ram;
  if(!b->world || c->k!=1 || c->pc!=0xb7b9 || c->mf || c->xf || c->dp || c->d)return false;
  unsigned xp=word(r,(uint16_t)(c->sp+3)),yp=word(r,(uint16_t)(c->sp+1));
  int dx=(int8_t)read_bus(b,(((unsigned)c->db<<16)+xp+c->y)&0xffffff);
  int dy=(int8_t)read_bus(b,(((unsigned)c->db<<16)+yp+c->y)&0xffffff);
  int x=word(r,0x205)+dx,y=word(r,0x207)+dy;
  if(!ScWorldContains(b->world,x,y))return false;
  unsigned tile=ScWorldCell(b->world,x,y)&1023;
  if(tile>=0x30 && tile<0x80 && (tile&15)>=2 && (tile&15)<13)return false;
  put(r,0x209,x);put(r,0x20b,y);put(r,0x243,0);put(r,0x245,0);put(r,0x247,0);
  unsigned offset=2*(y*ScWorldWidth(b->world)+x);
  put(r,0x79,y*ScWorldWidth(b->world));b->world->bank_anchor[1]=offset;
  c->a=tile;c->x=(uint16_t)offset;c->c=true;c->n=(c->y&0x8000)!=0;c->z=!c->y;
  c->v=((~((uint16_t)dy^word(r,0x207))&((uint16_t)dy^y))&0x8000)!=0;
  c->pc=0xb821;memset(&b->guest,0,sizeof b->guest);return true;
}
ScBuildWork *ScConstructionBegin(const uint8_t *ram,const ScWorld *w,const uint8_t *rom,size_t size,const ScBuildPlan *p) {
  if (!rom || size!=0x80000 || p->tool>15 || !p->count || p->count>SC_BUILD_MAX ||
      (p->tool>=10 && p->tool!=13 && p->tool!=14 && p->count!=1))
    return NULL;
  int width=w && w->active?ScWorldWidth(w):120,height=w && w->active?ScWorldHeight(w):100;
  for (unsigned i=0;i<p->count;++i)
    if (p->cells[i].x<0 || p->cells[i].x>=width || p->cells[i].y<0 || p->cells[i].y>=height)
      return NULL;
  BuildBus *b=calloc(1,sizeof *b);
  if (!b) return NULL;
  b->rom=rom; b->size=size; memcpy(b->ram,ram,sizeof b->ram);
  if (w && w->active) {
    b->world=malloc(sizeof *w);
    if (!b->world) { free(b); return NULL; }
    memcpy(b->world,w,sizeof *w);
  }
  unsigned available=funds(ram);
  /* Private preflight uses a full treasury; bounded interactive steps stop
   * as soon as the real treasury cannot afford the selection. */
  money(b->ram,0xffffff);
  Interp816 *cpu=interp816_init(b,read_bus,write_bus);
  if (!cpu) { free(b->world); free(b); return NULL; }
  ScBuildWork *job=calloc(1,sizeof *job);
  if(!job) {interp816_free(cpu);free(b->world);free(b);return NULL;}
  job->bus=b;job->cpu=cpu;job->plan=p;job->available=available;
  const char *reference=getenv("SC_CONSTRUCTION_REFERENCE");
  job->reference=reference && *reference=='1';return job;
}

/* Connected native C placement uses the same mapped world hooks as the
 * interpreter oracle, yielding on COP, return, fault or the private budget. */
static bool build_before(void *context,Interp816 *cpu) {
  ScBuildWork *job=context;BuildBus *b=job->bus;
  if(job->batch_operations>=job->batch_limit || b->fault || cpu->stopped || cpu->waiting ||
      (cpu->k==1 && (cpu->pc==0x7000 || (b->world && (cpu->pc==0xbcaf || cpu->pc==0xb7b9 ||
          (cpu->pc==0xb928 && word(b->ram,0x20d)>=5 && word(b->ram,0x20d)<=7 && word(b->ram,0x20f)==8))) || ScTileLookupOwns(cpu->pc))))return false;
  if(++job->steps>200000) {job->fault=true;return false;}
  if(b->world) {
    ScWorldGuestStepPrepared(b->world,cpu,b->ram);
    ScWorldGuestBeginPrepared(&b->guest,b->world,cpu,b->rom,b->size);
  }
  size_t at=(size_t)cpu->k*32768+cpu->pc-0x8000;
  return at<b->size && b->rom[at]!=0x02;
}
static bool build_after(void *context,Interp816 *cpu,unsigned clocks) {
  ScBuildWork *job=context;(void)cpu;(void)clocks;
  return ++job->batch_operations<job->batch_limit;
}
static unsigned build_fallback(void *context,Interp816 *cpu) {
  (void)context;return interp816_runOpcode(cpu);
}
bool ScConstructionStep(ScBuildWork *job,unsigned max_operations) {
  if(!job || job->done)return true;
  if(!max_operations)return false;
  BuildBus *b=job->bus;Interp816 *cpu=job->cpu;
  const ScBuildPlan *p=job->plan;const uint8_t *rom=b->rom;size_t size=b->size;
  unsigned operations=0;
  while(job->index<p->count && !job->fault) {
    if(!job->placing) {
      unsigned i=job->index;
      int x=p->cells[i].x, y=p->cells[i].y;
      /* Native input derives coordinates from an 8-bit on-screen pointer.
       * Give each private placement a camera containing its cell, so a long
       * drag cannot wrap the cursor and build elsewhere in the world. */
      put(b->ram,0x01bd,x-16); put(b->ram,0x01bf,y-16);
      if(b->world) {b->world->coord[1][0]=x;b->world->coord[1][1]=y;}
      put(b->ram,0x0205,x); put(b->ram,0x0207,y); put(b->ram,0x020d,p->tool);
      put(b->ram,0x01eb,128); put(b->ram,0x01ed,128);
      put(b->ram,0x1ffe,0x6fff);
      interp816_reset(cpu);
      cpu->k=1; cpu->db=0; cpu->pc=0x8e28; cpu->sp=0x1ffd; cpu->dp=0;
      cpu->mf=false; cpu->xf=false; cpu->e=false; cpu->i=true;
      job->steps=0;memset(&b->guest,0,sizeof b->guest);job->placing=true;
    }
    while (!(cpu->k==1 && cpu->pc==0x7000)) {
      if(!job->reference && (build_footprint(job) || build_zone_scan(job) || build_no_join(job))) {
        if(++operations>=max_operations)return false;
        continue;
      }
      if(!job->reference) {
        job->batch_operations=0;job->batch_limit=max_operations-operations;
        unsigned edges=ScProgramRun(cpu,job,build_before,build_after,build_fallback);
        operations+=edges;
        if(operations>=max_operations)return false;
        if(job->fault)break;
        if(edges)continue;
      }
      if (++job->steps>200000 || b->fault || cpu->stopped || cpu->waiting) { job->fault=true; break; }
      if (b->world) {
        if(job->reference) {ScWorldGuestStep(b->world,cpu,b->ram);ScWorldGuestBegin(&b->guest,b->world,cpu,rom,size);}
        else {ScWorldGuestStepPrepared(b->world,cpu,b->ram);ScWorldGuestBeginPrepared(&b->guest,b->world,cpu,rom,size);}
      }
      /* COP calls here emit sprite records. Map tiles are regular WRAM writes;
       * the live renderer emits the cursor/HUD on its next normal pass. */
      if (read_bus(b,((uint32_t)cpu->k<<16)|cpu->pc)==0x02) cpu->pc+=2;
      else {
        unsigned remaining=max_operations-operations,budget=remaining>2048?4096:remaining*2;
        unsigned fast=job->reference || !b->world?0:ScTileLookupStep(b->world,cpu,b->ram,rom,size,budget);
        if(!fast && !job->reference && b->world && cpu->k==3)fast=ScWorldGuestBatchStep(b->world,cpu,b->ram,rom,size,budget);
        if(fast) operations+=fast/2;
        else interp816_runOpcode(cpu);
      }
      if(++operations>=max_operations) return false;
    }

    if (b->fault || cpu->sp!=0x1fff || cpu->dp!=0)job->fault=true;
    job->placing=false;++job->index;
    /* Interactive preflight can reject as soon as its cost exceeds funds.
     * No live tile or money has changed; the synchronous oracle totals all. */
    if(max_operations!=UINT_MAX && 0xffffff-funds(b->ram)>job->available)break;
  }
  job->done=true;return true;
}
unsigned ScConstructionCompleted(const ScBuildWork *job) {return job?job->index:0;}
ScBuildResult ScConstructionFinish(ScBuildWork *job,uint8_t *ram,ScWorld *w,unsigned *cost) {
  if(cost)*cost=0;
  if(!job || !job->done)return SC_BUILD_INVALID;
  BuildBus *b=job->bus;
  unsigned price=0xffffff-funds(b->ram);
  if (cost) *cost=price;
  ScBuildResult result=job->fault?SC_BUILD_FAULT:price>job->available?SC_BUILD_FUNDS:SC_BUILD_OK;
  if (result==SC_BUILD_OK) {
    money(b->ram,job->available-price);
    /* Preserve the live CPU's stacks, scheduler context, pointer and tool.
     * Commit all original placement side effects, including joins and DMA
     * staging, rather than reimplementing the ROM's tile classifications. */
    memcpy(b->ram+0x1c00,ram+0x1c00,0x400);
    memcpy(b->ram+0x01eb,ram+0x01eb,4);
    memcpy(b->ram+0x01bd,ram+0x01bd,4);
    memcpy(b->ram+0x0205,ram+0x0205,10);
    /* F12 remains usable during a long job. Publish its current cheat flags,
     * while the transaction itself uses the flags captured at Begin. */
    memcpy(b->ram+0x425,ram+0x425,2);
    memcpy(ram,b->ram,sizeof b->ram);
    if (b->world) {
      b->world->map_anchor=w->map_anchor;
      memcpy(b->world->bank_anchor,w->bank_anchor,sizeof w->bank_anchor);
      memcpy(b->world->coord,w->coord,sizeof w->coord);
      /* Placements run against a private world for atomic rollback. Its
       * mapped writes do not invalidate the live world's renderer or power
       * cache; publish only the tile regions changed by a successful commit. */
      unsigned bytes=ScWorldCells(w)*2;
      for(unsigned at=0;at<bytes;at+=SC_WORLD_TILE_CHUNK_BYTES) {
        unsigned n=bytes-at<SC_WORLD_TILE_CHUNK_BYTES?bytes-at:SC_WORLD_TILE_CHUNK_BYTES;
        if(memcmp(w->tiles+at,b->world->tiles+at,n)) ScWorldTilesTouch(w,at,n);
      }
      memcpy(w,b->world,sizeof *w);
    }
  }
  return result;
}

void ScConstructionFree(ScBuildWork *job) {
  if(!job)return;
  interp816_free(job->cpu);free(job->bus->world);free(job->bus);free(job);
}
ScBuildResult ScConstructionCommitWorld(uint8_t *ram,ScWorld *w,const uint8_t *rom,size_t size,const ScBuildPlan *p,unsigned *cost) {
  if(cost)*cost=0;
  /* Retain the existing invalid-plan result for synchronous callers. */
  int width=w && w->active?ScWorldWidth(w):120,height=w && w->active?ScWorldHeight(w):100;
  if(!rom || size!=0x80000 || p->tool>15 || !p->count || p->count>SC_BUILD_MAX ||
      (p->tool>=10 && p->tool!=13 && p->tool!=14 && p->count!=1))return SC_BUILD_INVALID;
  for(unsigned i=0;i<p->count;++i)if(p->cells[i].x<0 || p->cells[i].x>=width || p->cells[i].y<0 || p->cells[i].y>=height)return SC_BUILD_INVALID;
  ScBuildWork *job=ScConstructionBegin(ram,w,rom,size,p);if(!job)return SC_BUILD_FAULT;
  while(!ScConstructionStep(job,UINT_MAX)) {}
  ScBuildResult result=ScConstructionFinish(job,ram,w,cost);ScConstructionFree(job);return result;
}
bool ScConstructionPowerBitmapReference(const uint8_t *ram,const ScWorld *w,const uint8_t *rom,size_t size,
                               uint8_t *bitmap,size_t bitmap_size) {
  if (!rom || size!=0x80000 || !bitmap || bitmap_size<(w && w->active?ScWorldCells(w)/8:1500u)) return false;
  BuildBus *b=calloc(1,sizeof *b);
  if (!b) return false;
  b->rom=rom; b->size=size; memcpy(b->ram,ram,sizeof b->ram);
  if (w && w->active) {
    b->world=malloc(sizeof *w);
    if (!b->world) { free(b); return false; }
    memcpy(b->world,w,sizeof *w);
  }
  int width=b->world?ScWorldWidth(b->world):120,height=b->world?ScWorldHeight(b->world):100;
  unsigned plants=0,coal=0,nuclear=0;
  /* These are the original plant centre tiles and the same one-based seed
   * stack produced by 03:aa9f. Do not rerun zone development or nuclear
   * accident checks just to reconnect electricity. */
  for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
    unsigned tile=(b->world?ScWorldCell(b->world,x,y):word(ram,0x10200+2*(y*width+x)))&1023;
    if (tile!=0x28c && tile!=0x27c) continue;
    if (plants>=(b->world?ScWorldFieldWidth(b->world,17)-1:5000u)) { free(b->world); free(b); return false; }
    if (tile==0x28c) ++coal; else ++nuclear;
    ++plants;
    if (b->world) {
      if(b->world->huge) {put(b->world->fields[17],2*plants,x);put(b->world->fields[18],2*plants,y);}
      else {b->world->fields[17][plants]=(uint8_t)x;b->world->fields[18][plants]=(uint8_t)y;}
    }
    else { b->ram[0x1d1e4+plants]=(uint8_t)x; b->ram[0x1e56c+plants]=(uint8_t)y; }
  }
  put(b->ram,0xe0d,coal); put(b->ram,0xe0f,nuclear);
  put(b->ram,0xc57,plants); put(b->ram,0xc59,0);
  put(b->ram,0x1ffe,0x6fff);
  Interp816 *cpu=interp816_init(b,read_bus,write_bus);
  if (!cpu) { free(b->world); free(b); return false; }
  interp816_reset(cpu);
  cpu->k=3; cpu->db=3; cpu->pc=0xafb0; cpu->sp=0x1ffd; cpu->dp=0x1e00;
  cpu->mf=false; cpu->xf=false; cpu->e=false; cpu->i=true;
  unsigned steps=0;
  while (!(cpu->k==3 && cpu->pc==0x7000)) {
    if (++steps>20000000 || b->fault || cpu->stopped || cpu->waiting) { b->fault=true; break; }
    if (b->world) {
      ScWorldGuestStep(b->world,cpu,b->ram);
      ScWorldGuestBegin(&b->guest,b->world,cpu,rom,size);
    }
    interp816_runOpcode(cpu);
  }
  bool ok=!b->fault && cpu->sp==0x1fff && cpu->dp==0x1e00;
  if(ok && getenv("SC_POWER_DIAG")) {
    unsigned capacity=word(b->ram,0x1df6)|((unsigned)word(b->ram,0x1df8)<<16);
    unsigned used=word(b->ram,0x1dfa)|((unsigned)word(b->ram,0x1dfc)<<16);
    fprintf(stderr,"[power network] coal %u nuclear %u capacity %u visited %u%s\n",
        coal,nuclear,capacity,used,used>capacity?" exhausted":"");
  }
  if (ok) {
    const uint8_t *power=b->world?b->world->fields[5]:b->ram+0x1a598;
    memcpy(bitmap,power,(size_t)width*height/8);
  }
  interp816_free(cpu); free(b->world); free(b); return ok;
}
bool ScConstructionPowerBitmap(const uint8_t *ram,const ScWorld *w,const uint8_t *rom,size_t size,
                               uint8_t *bitmap,size_t bitmap_size) {
  if (getenv("SC_POWER_REFERENCE"))
    return ScConstructionPowerBitmapReference(ram,w,rom,size,bitmap,bitmap_size);
  bool large=w && w->active;
  unsigned width=large?ScWorldWidth(w):120,height=large?ScWorldHeight(w):100;
  unsigned cells=width*height,limit=large?cells:5000;
  if (!ram || !rom || size!=0x80000 || !bitmap || bitmap_size<cells/8) return false;
  uint32_t *stack=malloc(limit*sizeof *stack);
  if (!stack) return false;
  const uint8_t *tiles=large?w->tiles:ram+0x10200;
  unsigned count=0,coal=0,nuclear=0;uint64_t used=0;
  for (unsigned i=0;i<cells;++i) {
    unsigned tile=word(tiles,2*i)&1023;
    if (tile!=0x28c && tile!=0x27c) continue;
    if (count>=limit) {free(stack);return false;}
    stack[count++]=i;
    if (tile==0x28c) ++coal; else ++nuclear;
  }
  memset(bitmap,0,cells/8);
  uint64_t capacity=(uint64_t)coal*700+(uint64_t)nuclear*2000;
  /* Ordered translation of 03:afb0..b151. Branches retain the ROM's
   * second-neighbour-first walk and LIFO stack, including repeated
   * seed/branch visits in the capacity count. A conventional parallel flood
   * fill changes which districts brown out when capacity is exhausted.
   * $b89 is a read-only input to b0f8, not the candidate tile register.
   * Enlarged maps use a full-width cell stack: the old guest word counter
   * must not truncate their power seeds or deferred network branches. */
  unsigned last_tile=word(ram,0xb89);
  bool traverse=last_tile!=0x27c && last_tile!=0x28c;
  bool exhausted=false;
  while (count && !exhausted) {
    unsigned current=stack[--count];
    for (;;) {
      if (++used>capacity) {exhausted=true;break;}
      bitmap[current/8]|=128>>(current&7);
      unsigned x=current%width,y=current/width,found=0,next=current;
      const bool valid[]={y>0,x+1<width,y+1<height,x>0};
      const unsigned neighbours[]={current-width,current+1,current+width,current-1};
      for (unsigned direction=0;direction<4 && found<2;++direction) {
        unsigned candidate=neighbours[direction];
        if (!traverse || !valid[direction] || (bitmap[candidate/8]&(128>>(candidate&7)))) continue;
        unsigned tile=word(tiles,2*candidate)&1023;
        if (!(rom[0x184eb+tile]&128)) continue;
        next=candidate;++found;
      }
      if (found==2 && count<limit) stack[count++]=current;
      if (!found) break;
      current=next;
    }
  }
  if (getenv("SC_POWER_DIAG"))
    fprintf(stderr,"[power network] coal %u nuclear %u capacity %llu visited %llu%s\n",
        coal,nuclear,(unsigned long long)capacity,(unsigned long long)used,exhausted?" exhausted":"");
  free(stack);return true;
}
bool ScConstructionRefreshPower(uint8_t *ram,ScWorld *w,const uint8_t *rom,size_t size) {
  unsigned bytes=w && w->active?ScWorldCells(w)/8:1500;
  uint8_t *power=malloc(bytes);if(!power)return false;
  if (!ScConstructionPowerBitmap(ram,w,rom,size,power,bytes)) {free(power);return false;}
  bool large=w && w->active;
  ScWorldPublishPower(large?w:NULL,large?w->tiles:ram+0x10200,power,0,large?ScWorldCells(w):12000);
  if (w && w->active) memcpy(w->fields[5],power,ScWorldCells(w)/8);
  else memcpy(ram+0x1a598,power,1500);
  free(power);return true;
}

/* Generated districts need live density, land value and service fields before
 * their first development pass. Run the original stock scans on private WRAM;
 * the generator tiles the surrounded district's resulting spatial fields. */
bool ScConstructionPrimeFields(uint8_t *ram,const uint8_t *rom,size_t size) {
  if(!ram || !rom || size!=0x80000)return false;
  BuildBus *b=calloc(1,sizeof *b);if(!b)return false;
  b->rom=rom;b->size=size;memcpy(b->ram,ram,sizeof b->ram);
  /* 03:ab0e/ab67 contribute the full 1000-point funded service strength
   * at each powered, transport-connected ordinary fire/police station. */
  for(unsigned y=0;y<100;++y)for(unsigned x=0;x<120;++x) {
    unsigned raw=word(ram,0x10200+2*(y*120+x)),tile=raw&1023;
    if(tile!=0x249 && tile!=0x252)continue;
    unsigned a=0x10000+(tile==0x249?0xb2f4:0xb16e)+2*((y/8)*15+x/8);
    put(b->ram,a,word(b->ram,a)+((raw&0x8000)?1000:500));
  }
  Interp816 *c=interp816_init(b,read_bus,write_bus);if(!c){free(b);return false;}
  const unsigned scans[]={0x9ad7,0x9aa3,0x9c11,0x9e8e};bool ok=true;
  for(unsigned n=0;n<sizeof scans/sizeof *scans && ok;++n) {
    interp816_reset(c);c->k=c->db=3;c->pc=scans[n];c->sp=0x1ffd;c->dp=0x1e00;
    c->mf=c->xf=c->e=false;c->i=true;put(b->ram,0x1ffe,0x6fff);
    unsigned steps=0;
    while(c->pc!=0x7000 || c->k!=3) {
      if(++steps>20000000 || b->fault || c->stopped || c->waiting){ok=false;break;}
      interp816_runOpcode(c);
    }
    ok=ok && c->sp==0x1fff && c->dp==0x1e00;
  }
  if(ok) {
    for(unsigned f=0;f<17;++f) {
      const ScWorldField *field=&ScWorldFields[f];
      memcpy(ram+0x10000+field->base,b->ram+0x10000+field->base,
          field->stock_width*field->stock_height*field->element_bytes);
    }
  }
  interp816_free(c);free(b);return ok;
}
