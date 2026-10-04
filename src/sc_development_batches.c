#include "sc_development_batches.h"
#include "sc_world_guest.h"
#include "sc_math.h"
#include "snes/interp816.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct ScDevelopmentBatches {
    uint32_t cells[SC_WORLD_MAX_CELLS],slots[SC_WORLD_MAX_CELLS];
    uint32_t route_frame[SC_WORLD_MAX_CELLS];
    uint32_t transport[SC_WORLD_MAX_CELLS]; /* zero: unknown; native result + 2 */
    uint16_t ids[SC_WORLD_MAX_CELLS];
    uint64_t revisions[SC_WORLD_TILE_CHUNKS],epoch,frame,credit,remainder;
    uint64_t attempts,passes;
    unsigned count,cursor,stride,visited,width,height,period,quadrants;
    int speed,game_speed;
    bool ready;
    uint8_t ram[0x20000],traffic[SC_WORLD_FIELD_BYTES];
    const uint8_t *rom;
    ScWorld *world;
    ScWorldGuest guest;
    Interp816 cpu;
    uint16_t product,quotient,dividend;
    uint8_t multiplicand;
};
static unsigned word(const uint8_t *r,unsigned p) {return r[p]|(unsigned)r[p+1]<<8;}
static void put(uint8_t *r,unsigned p,unsigned v) {r[p]=(uint8_t)v;r[p+1]=(uint8_t)(v>>8);}
static unsigned entry(unsigned tile,const uint8_t *rom) {
    if(tile>=958 || !(rom[0x184eb+tile]&1))return 0;
    if((tile>=0x80 && tile<0x129) || (tile>=0x376 && tile<0x39a))return 0x937a;
    if((tile>=0x137 && tile<0x1f4) || tile>=0x39a)return 0x92ce;
    if(tile>=0x1f4 && tile<0x249)return 0x922f;
    return 0;
}
static bool empty(unsigned tile) {return tile==0x84 || tile==0x13b || tile==0x1f8;}
static bool house(unsigned tile) {return tile>=0x89 && tile<0x95;}
static uint8_t read_bus(void *context,uint32_t a) {
    ScDevelopmentBatches *b=context;uint8_t v;
    if(ScWorldGuestRead(&b->guest,a,&v))return v;
    unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f)return b->ram[a-0x7e0000];
    if(p<0x2000)return b->ram[p];
    if(p==0x4214)return b->quotient;
    if(p==0x4215)return b->quotient>>8;
    if(p==0x4216)return b->product;
    if(p==0x4217)return b->product>>8;
    if(p>=0x8000)return b->rom[((bank&15)*0x8000)+p-0x8000];
    return 0;
}
static void write_bus(void *context,uint32_t a,uint8_t v) {
    ScDevelopmentBatches *b=context;
    if(ScWorldGuestWrite(&b->guest,a,v))return;
    unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f) {b->ram[a-0x7e0000]=v;return;}
    if(p==0x4202)b->multiplicand=v;
    else if(p==0x4203)b->product=b->multiplicand*v;
    else if(p==0x4204)b->dividend=(b->dividend&0xff00)|v;
    else if(p==0x4205)b->dividend=(b->dividend&255)|((unsigned)v<<8);
    else if(p==0x4206) {
        b->quotient=v?b->dividend/v:65535;b->product=v?b->dividend%v:b->dividend;
    } else if(p<0x2000)b->ram[p]=v;
}
ScDevelopmentBatches *ScDevelopmentBatchesCreate(void) {
    ScDevelopmentBatches *b=calloc(1,sizeof(ScDevelopmentBatches));
    if(b)b->game_speed=-1;
    return b;
}
void ScDevelopmentBatchesDestroy(ScDevelopmentBatches *b) {free(b);}
void ScDevelopmentBatchesReset(ScDevelopmentBatches *b) {
    if(!b)return;
    b->ready=false;b->credit=b->remainder=0;b->game_speed=-1;
}
unsigned ScDevelopmentBatchesZones(const ScDevelopmentBatches *b) {return b?b->count:0;}
uint64_t ScDevelopmentBatchesAttempts(const ScDevelopmentBatches *b) {return b?b->attempts:0;}
uint64_t ScDevelopmentBatchesPasses(const ScDevelopmentBatches *b) {return b?b->passes:0;}
unsigned ScDevelopmentBatchesLastQuadrants(const ScDevelopmentBatches *b) {return b?b->quadrants:0;}
void ScDevelopmentBatchesObserve(ScDevelopmentBatches *b,const ScWorld *w,
    const Interp816 *c,const uint8_t *r) {
    if(!b || c->k!=3)return;
    if(!b->ready || (c->pc!=0x926f && c->pc!=0x931b && c->pc!=0x93d1))return;
    int x=w->active && w->huge?w->coord[2][0]:r[0xb85];
    int y=w->active && w->huge?w->coord[2][1]:r[0xb86];
    if(x>=0 && y>=0 && (unsigned)x<b->width && (unsigned)y<b->height)
        {unsigned cell=y*b->width+x;b->transport[cell]=word(r,c->dp+4)+2;b->route_frame[cell]=(uint32_t)b->frame;}
}
static unsigned gcd(unsigned a,unsigned b) {while(b) {unsigned v=a%b;a=b;b=v;}return a;}
static void index_map(ScDevelopmentBatches *b,ScWorld *w,const uint8_t *r,const uint8_t *rom) {
    unsigned width=w->active?ScWorldWidth(w):120,height=w->active?ScWorldHeight(w):100;
    bool reset=!b->ready || b->epoch!=ScWorldStateEpoch() || b->width!=width || b->height!=height;
    if(reset) {
        memset(b->slots,0,sizeof b->slots);memset(b->transport,0,sizeof b->transport);
        memset(b->ids,0,sizeof b->ids);memset(b->revisions,0,sizeof b->revisions);
        b->count=b->cursor=b->visited=0;b->credit=b->remainder=0;b->speed=0;
        b->width=width;b->height=height;b->epoch=ScWorldStateEpoch();b->ready=true;
    }
    const uint8_t *map=w->active?w->tiles:r+0x10200;
    const uint64_t *revisions=w->active?ScWorldTileRevisions(w):NULL;
    unsigned cells=width*height,old_count=b->count;
    bool routes_changed=false;
    for(unsigned chunk=0;chunk<(cells+255)/256;++chunk) {
        if(!reset && revisions && revisions[chunk]==b->revisions[chunk])continue;
        if(revisions)b->revisions[chunk]=revisions[chunk];
        unsigned end=(chunk+1)*256;if(end>cells)end=cells;
        for(unsigned cell=chunk*256;cell<end;++cell) {
            unsigned tile=word(map,2*cell)&1023,old=b->ids[cell];
            if(tile==old && !reset)continue;
            /* Roads/rails affect routes beyond the edited chunk. Power flags,
             * animation and ordinary zone growth do not invalidate routes. */
            if(!reset && ((rom[0x184eb+tile]^rom[0x184eb+old])&0x70))routes_changed=true;
            if(house(old)!=house(tile)) {
                int x=cell%width,y=cell/width;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    int nx=x+dx,ny=y+dy;
                    if(nx>=0 && ny>=0 && (unsigned)nx<width && (unsigned)ny<height) {
                        unsigned neighbor=(unsigned)ny*width+(unsigned)nx;
                        if((word(map,2*neighbor)&1023)==0x84)b->transport[neighbor]=0;
                    }
                }
            }
            b->ids[cell]=tile;
            if(entry(tile,rom)) {
                if(!b->slots[cell]) {b->slots[cell]=++b->count;b->cells[b->count-1]=cell;}
                if(entry(old,rom)!=entry(tile,rom) || empty(old)!=empty(tile))b->transport[cell]=0;
            } else if(b->slots[cell]) {
                unsigned slot=b->slots[cell]-1,last=b->cells[--b->count];
                b->cells[slot]=last;b->slots[last]=slot+1;b->slots[cell]=0;b->transport[cell]=0;
            }
        }
    }
    if(routes_changed)memset(b->transport,0,sizeof b->transport);
    if(reset || old_count!=b->count) {
        /* A coprime jump distributes even short batches over the entire dense
         * zone index. Every zone is visited before any zone gets a second turn. */
        b->stride=b->count?b->count*5/8+1:1;
        while(b->count && gcd(b->stride,b->count)!=1)++b->stride;
        if(b->count)b->cursor%=b->count;else b->cursor=0;
        b->visited=0;
    }
}
static bool attempt(ScDevelopmentBatches *b,unsigned cell,unsigned (*fallback)(Interp816 *)) {
    ScWorld *w=b->world;uint8_t *r=b->ram;
    unsigned raw=w->active?ScWorldCell(w,cell%b->width,cell/b->width):word(r,0x10200+2*cell);
    unsigned start=entry(raw&1023,b->rom);if(!start)return true;
    unsigned locals=start==0x937a?10:8;
    ScDevelopment s={.entry=start,.end=start==0x937a?0x9447:start==0x92ce?0x9378:0x92cc,
        .capacity=start==0x937a?0x9388:start==0x92ce?0x92dc:0x923d,
        .skip=start==0x937a?0x93a4:start==0x92ce?0x92ee:0x9242,
        .attempt=start==0x937a?0x93d1:start==0x92ce?0x931b:0x926f};
    Interp816 *c=&b->cpu;memset(c,0,sizeof *c);
    c->mem=b;c->read=read_bus;c->write=write_bus;c->k=c->db=3;c->i=true;
    c->dp=0x1e00;c->sp=0x1ffd;c->pc=start;
    unsigned x=cell%b->width,y=cell/b->width;
    r[0xb85]=x;r[0xb86]=y;put(r,0xb49,2*cell);put(r,0xb87,raw);put(r,0xb89,raw&1023);
    if(w->active) {w->coord[2][0]=x;w->coord[2][1]=y;w->map_anchor=2*cell;}
    /* Age each route from its own probe, rather than flushing a city-wide
     * cache before a large cold pass can finish. Edits/capacity transitions
     * invalidate sooner; ordinary routes expire after eight Normal intervals. */
    if((uint32_t)((uint32_t)b->frame-b->route_frame[cell])>=8*b->period)b->transport[cell]=0;
    bool cached=b->transport[cell]!=0;
    if(cached) {
        c->dp-=locals;c->sp-=2;c->pc=s.capacity;s.repeating=true;
        put(r,c->dp+4,b->transport[cell]-2);
    }
    memset(&b->guest,0,sizeof b->guest);
    for(unsigned steps=0;steps<100000;++steps) {
        if(c->pc==s.end && c->dp==0x1e00-locals && c->sp==0x1ffb)return true;
        if(c->k!=3 || c->stopped || c->waiting)return false;
        ScWorldGuestStepPrepared(w,c,r);
        if(c->pc==s.attempt && !cached) {
            b->transport[cell]=word(r,c->dp+4)+2;b->route_frame[cell]=(uint32_t)b->frame;
            s.repeating=true;cached=true;
        }
        if(s.repeating && c->pc==s.skip) {c->pc=s.attempt;c->mf=c->xf=false;}
        unsigned cost=s.repeating?ScDevelopmentNativeBatch(&s,w,c,r,b->rom,0x80000):0;
        if(!cost)cost=ScMathBatchStep(c,r,4096);
        if(!cost && w->active)cost=ScWorldGuestBatchStep(w,c,r,b->rom,0x80000,4096);
        if(!cost)cost=ScWorldGuestFastStep(w,c,r,b->rom,0x80000);
        if(!cost)cost=ScWorldGuestInstructionStep(w,c,r,b->rom,0x80000);
        if(!cost) {
            ScWorldGuestBeginPrepared(&b->guest,w,c,b->rom,0x80000);
            if(fallback)cost=fallback(c);
            if(!cost)cost=interp816_runOpcode(c);
        }
    }
    return false;
}
unsigned ScDevelopmentBatchesRun(ScDevelopmentBatches *b,ScWorld *w,uint8_t *r,
    const uint8_t *rom,size_t size,uint64_t frame,int speed,unsigned maximum,
    uint64_t (*clock)(void),uint64_t frequency,double ms,unsigned (*fallback)(Interp816 *)) {
    if(!b || !w || !r || !rom || size!=0x80000 || !maximum)return 0;
    if(speed<1 || r[0x193]>=3) {b->speed=1;b->frame=frame;b->credit=b->remainder=0;return 0;}
    b->quadrants=0;
    uint64_t deadline=clock?clock()+(uint64_t)(frequency*ms/1000):0;
    index_map(b,w,r,rom);
    if(!b->count) {b->frame=frame;b->speed=speed;return 0;}
    if(b->game_speed!=r[0x193]) {
        b->game_speed=r[0x193];b->period=b->game_speed==0?800:b->game_speed==1?400:200;
        /* Match the original initial Normal cadence at each game speed,
         * independent of world area and the length of the native sweep. */
    }
    unsigned period=b->period?b->period:200;
    if(b->speed!=speed || frame<b->frame) {
        b->speed=speed;b->frame=frame;b->credit=b->count;b->remainder=0;
    }
    uint64_t delta=frame-b->frame;b->frame=frame;
    /* Never burst missed work after a popup, pause, load or speed change. */
    if(delta>8)delta=1;
    uint64_t earned=b->remainder+delta*b->count*(unsigned)speed;
    b->credit+=earned/period;b->remainder=earned%period;
    if(b->credit>b->count*2u)b->credit=b->count*2u;
    if(!b->credit || (clock && clock()>=deadline))return 0;
    b->rom=rom;b->world=w;memcpy(b->ram,r,sizeof b->ram);
    uint8_t *traffic=w->active?w->fields[4]:r+0x199e0;
    unsigned traffic_size=w->active?ScWorldFieldSizeWorld(w,4):3000;
    memcpy(b->traffic,traffic,traffic_size);
    /* A private zone call must never overwrite a suspended native iterator,
     * census, stack, UI registers or sprite buffer. Restore all world cursors. */
    int16_t coord[3][2];uint32_t bank[3],field[3],anchor=w->map_anchor,scan=w->field_scan;
    memcpy(coord,w->coord,sizeof coord);memcpy(bank,w->bank_anchor,sizeof bank);memcpy(field,w->field_anchor,sizeof field);
    unsigned done=0;
    while(b->credit && done<maximum) {
        unsigned cell=b->cells[b->cursor];
        b->cursor=(b->cursor+b->stride)%b->count;
        if(!attempt(b,cell,fallback)) {
            fprintf(stderr,"[development batches] unsupported zone call %02x:%04x; resetting route cache\n",b->cpu.k,b->cpu.pc);
            b->transport[cell]=0;break;
        }
        b->quadrants|=1u<<((cell%b->width>=b->width/2)+2*(cell/b->width>=b->height/2));
        --b->credit;++done;++b->attempts;
        if(++b->visited==b->count) {b->visited=0;++b->passes;}
        if(clock && !(done&7) && clock()>=deadline)break;
    }
    if(!w->active) {
        memcpy(r+0x10200,b->ram+0x10200,24000);
        memcpy(r+0x1ae62,b->ram+0x1ae62,390); /* native growth/density deltas */
    }
    /* Probes establish accessibility, but extra attempts must not inflate
     * native traffic/census totals. Only native attempts publish route traffic. */
    memcpy(traffic,b->traffic,traffic_size);
    memcpy(r+0xccf,b->ram+0xccf,14); /* Consume the native RNG, in batch order. */
    memcpy(w->coord,coord,sizeof coord);memcpy(w->bank_anchor,bank,sizeof bank);memcpy(w->field_anchor,field,sizeof field);
    w->map_anchor=anchor;w->field_scan=scan;
    return done;
}
