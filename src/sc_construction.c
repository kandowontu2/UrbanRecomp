#include "sc_construction.h"
#include "snes/interp816.h"
#include "sc_world_guest.h"
#include <stdlib.h>
#include <string.h>

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
  memset(p,0,sizeof *p);
  int width=w && w->active?ScWorldWidth(w):120,height=w && w->active?ScWorldHeight(w):100;
  if (tool>15 || x0<0 || x0>=width || x1<0 || x1>=width ||
      y0<0 || y0>=height || y1<0 || y1>=height) return false;
  p->tool=tool;
  if (tool>=10) { p->cells[0]=(ScBuildCell){x0,y0}; p->count=1; return true; }
  int dx=x1>=x0?1:-1, dy=y1>=y0?1:-1;
  if (tool>=1 && tool<=3) {
    if (abs(x1-x0)>=abs(y1-y0)) y1=y0; else x1=x0;
  }
  int step=tool>=5?3:1;
  int nx=abs(x1-x0)/step+1, ny=abs(y1-y0)/step+1;
  for (int y=0;y<ny;++y) for (int x=0;x<nx;++x)
    p->cells[p->count++]=(ScBuildCell){x0+x*step*dx,y0+y*step*dy};
  return true;
}
ScBuildResult ScConstructionCommit(uint8_t *ram,const uint8_t *rom,size_t size,
                                  const ScBuildPlan *p,unsigned *cost) {
  return ScConstructionCommitWorld(ram,NULL,rom,size,p,cost);
}
ScBuildResult ScConstructionCommitWorld(uint8_t *ram,ScWorld *w,const uint8_t *rom,size_t size,
                                  const ScBuildPlan *p,unsigned *cost) {
  if (cost) *cost=0;
  if (!rom || size!=0x80000 || p->tool>15 || !p->count || p->count>SC_BUILD_MAX ||
      (p->tool>=10 && p->count!=1))
    return SC_BUILD_INVALID;
  int width=w && w->active?ScWorldWidth(w):120,height=w && w->active?ScWorldHeight(w):100;
  for (unsigned i=0;i<p->count;++i)
    if (p->cells[i].x<0 || p->cells[i].x>=width || p->cells[i].y<0 || p->cells[i].y>=height)
      return SC_BUILD_INVALID;
  BuildBus *b=calloc(1,sizeof *b);
  if (!b) return SC_BUILD_FAULT;
  b->rom=rom; b->size=size; memcpy(b->ram,ram,sizeof b->ram);
  if (w && w->active) {
    b->world=malloc(sizeof *w);
    if (!b->world) { free(b); return SC_BUILD_FAULT; }
    memcpy(b->world,w,sizeof *w);
  }
  unsigned available=funds(ram);
  /* Preflight all eligible cells even if the actual treasury is too small. */
  money(b->ram,0xffffff);
  Interp816 *cpu=interp816_init(b,read_bus,write_bus);
  if (!cpu) { free(b->world); free(b); return SC_BUILD_FAULT; }
  bool fault=false;
  for (unsigned i=0;i<p->count && !fault;++i) {
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
    unsigned steps=0;
    memset(&b->guest,0,sizeof b->guest);
    while (!(cpu->k==1 && cpu->pc==0x7000)) {
      if (++steps>200000 || b->fault || cpu->stopped || cpu->waiting) { fault=true; break; }
      if (b->world) {
        ScWorldGuestStep(b->world,cpu,b->ram);
        ScWorldGuestBegin(&b->guest,b->world,cpu,rom,size);
      }
      /* COP calls here emit sprite records. Map tiles are regular WRAM writes;
       * the live renderer emits the cursor/HUD on its next normal pass. */
      if (read_bus(b,((uint32_t)cpu->k<<16)|cpu->pc)==0x02) cpu->pc+=2;
      else interp816_runOpcode(cpu);
    }
    if (b->fault || cpu->sp!=0x1fff || cpu->dp!=0) fault=true;
  }
  unsigned price=0xffffff-funds(b->ram);
  if (cost) *cost=price;
  ScBuildResult result=fault?SC_BUILD_FAULT:price>available?SC_BUILD_FUNDS:SC_BUILD_OK;
  if (result==SC_BUILD_OK) {
    money(b->ram,available-price);
    /* Preserve the live CPU's stacks, scheduler context, pointer and tool.
     * Commit all original placement side effects, including joins and DMA
     * staging, rather than reimplementing the ROM's tile classifications. */
    memcpy(b->ram+0x1c00,ram+0x1c00,0x400);
    memcpy(b->ram+0x01eb,ram+0x01eb,4);
    memcpy(b->ram+0x01bd,ram+0x01bd,4);
    memcpy(b->ram+0x0205,ram+0x0205,10);
    memcpy(ram,b->ram,sizeof b->ram);
    if (b->world) {
      b->world->map_anchor=w->map_anchor;
      memcpy(b->world->bank_anchor,w->bank_anchor,sizeof w->bank_anchor);
      memcpy(b->world->coord,w->coord,sizeof w->coord);
      memcpy(w,b->world,sizeof *w);
    }
  }
  interp816_free(cpu); free(b->world); free(b); return result;
}
bool ScConstructionPowerBitmap(const uint8_t *ram,const ScWorld *w,const uint8_t *rom,size_t size,
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
    if (plants>=(b->world?20000u:5000u)) { free(b->world); free(b); return false; }
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
  if (ok) {
    const uint8_t *power=b->world?b->world->fields[5]:b->ram+0x1a598;
    memcpy(bitmap,power,(size_t)width*height/8);
  }
  interp816_free(cpu); free(b->world); free(b); return ok;
}
bool ScConstructionRefreshPower(uint8_t *ram,ScWorld *w,const uint8_t *rom,size_t size) {
  uint8_t power[SC_WORLD_MAX_CELLS/8];
  if (!ScConstructionPowerBitmap(ram,w,rom,size,power,sizeof power)) return false;
  int width=w && w->active?ScWorldWidth(w):120,height=w && w->active?ScWorldHeight(w):100;
  for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
      unsigned i=y*width+x;
      unsigned tile=(w && w->active?ScWorldCell(w,x,y):word(ram,0x10200+2*i))&0x7fff;
      if (power[i/8]&(128>>(i&7))) tile|=0x8000;
      if (w && w->active) ScWorldPutCell(w,x,y,tile); else put(ram,0x10200+2*i,tile);
  }
  if (w && w->active) memcpy(w->fields[5],power,ScWorldCells(w)/8);
  else memcpy(ram+0x1a598,power,1500);
  return true;
}
