#include "sc_world.h"
#include "sc_world_guest.h"
#include "sc_population.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t ram[0x20000],rom[0x80000];
static ScWorld world,copy;
static ScWorldGuest guest;
static Interp816 *cpu;
static uint16_t product,quotient,dividend;
static uint8_t multiplicand,visits[SC_WORLD_MAX_CELLS];
static uint8_t native_sram[0x8000];
static uint8_t *native_cities;
static uint8_t bitmap_ram[0x20000],bitmap[0x2000];
static uint8_t stencil_expected_ram[0x20000];
static ScPopulation population;
static bool population_enabled;
static uint64_t raw_cycles,spatial_cycles;
static unsigned cycle_remainder;
static unsigned mini_x,mini_y;
static uint8_t read_bus(void *ctx,uint32_t a) {
    (void)ctx; uint8_t v;
    if (ScWorldGuestRead(&guest,a,&v)) return v;
    unsigned bank=a>>16,p=a&65535;
    if (bank==0x70 && p<0x8000) return native_sram[p];
    if (a==0x0194e2 || a==0x0194a3) return 0x6b; /* presentation only, RTL */
    if (bank==0x7e || bank==0x7f) return ram[a-0x7e0000];
    if (p<0x2000) return ram[p];
    if (p==0x4214) return (uint8_t)quotient;
    if (p==0x4215) return (uint8_t)(quotient>>8);
    if (p==0x4216) return (uint8_t)product;
    if (p==0x4217) return (uint8_t)(product>>8);
    if (p>=0x8000 && bank<16) return rom[bank*32768+p-0x8000];
    return 0;
}
static void write_bus(void *ctx,uint32_t a,uint8_t v) {
    (void)ctx;
    if (ScWorldGuestWrite(&guest,a,v)) return;
    unsigned bank=a>>16,p=a&65535;
    if (bank==0x70 && p<0x8000) { native_sram[p]=v; return; }
    if (bank==0x7e || bank==0x7f) ram[a-0x7e0000]=v;
    else if (p<0x2000) ram[p]=v;
    else if (p==0x4202) multiplicand=v;
    else if (p==0x4203) product=multiplicand*v;
    else if (p==0x4204) dividend=(dividend&0xff00)|v;
    else if (p==0x4205) dividend=(dividend&255)|(v<<8);
    else if (p==0x4206) { quotient=v?dividend/v:65535; product=v?dividend%v:dividend; }
}
static void put(unsigned p,unsigned v) { ram[p]=(uint8_t)v; ram[p+1]=(uint8_t)(v>>8); }
static void routine_bank(unsigned bank,unsigned pc,unsigned a,unsigned y) {
    interp816_reset(cpu); memset(&guest,0,sizeof guest);
    cpu->k=cpu->db=(uint8_t)bank; cpu->pc=(uint16_t)pc; cpu->a=(uint16_t)a; cpu->y=(uint16_t)y;
    cpu->sp=0x1ffd; cpu->dp=0x1e00; cpu->e=cpu->mf=cpu->xf=false;
    put(0x1ffe,0x6fff); unsigned steps=0;
    raw_cycles=spatial_cycles=cycle_remainder=0;
    while (cpu->pc!=0x7000) {
        assert(++steps<200000000);
        if (native_cities && cpu->k==3) {
            if (cpu->pc==0xcf89) ScWorldMirror(&world,ram);
            if (cpu->pc==0xcbe2) {
                assert(native_sram[5]==1);
                assert(ScWorldCitySave(native_cities,native_sram,0,&world));
            }
            if (cpu->pc==0xc8c8) ScWorldReset(&world);
            if (cpu->pc==0xc8ce)
                assert(ScWorldCityLoad(&world,native_cities,ScWorldCitiesSize(),native_sram,0));
        }
        ScWorldGuestStep(&world,cpu,ram);
        if (cpu->k==1 && cpu->pc==0x8ad9) {
            mini_x=ram[0x25d]|(ram[0x25e]<<8);
            mini_y=ram[0x25f]|(ram[0x260]<<8);
        }
        if (population_enabled && cpu->k==3) {
            uint16_t next=ScPopulationStep(&population,ram,cpu->pc,cpu->dp);
            if (next!=cpu->pc) {
                cpu->y=(uint16_t)ScPopulationClass(population.value);
                cpu->a=(uint16_t)((ram[0xba7]|(ram[0xba8]<<8))-7);
                cpu->c=population.value>=500000; cpu->mf=cpu->xf=false;
            }
            cpu->pc=next;
        }
        if (cpu->k==3 && cpu->pc==0x82be)
            ++visits[(world.huge?world.scan_y:ram[0xb86])*ScWorldWidth(&world)+(world.huge?world.scan_x:ram[0xb85])];
        ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
        if (read_bus(NULL,((uint32_t)cpu->k<<16)|cpu->pc)==2) cpu->pc+=2;
        else {
            uint32_t executed_pc=((uint32_t)cpu->k<<16)|cpu->pc;
            unsigned master=interp816_runOpcode(cpu)*8;
            raw_cycles+=master;
            spatial_cycles+=ScWorldGuestMasterCycles(&world,executed_pc,master,&cycle_remainder);
        }
    }
    assert(cpu->sp==0x1fff && cpu->dp==0x1e00);
}
static void routine(unsigned pc,unsigned a,unsigned y) { routine_bank(3,pc,a,y); }
static void terrain_bounds(void) {
    const int points[][2]={{127,127},{128,128},{223,143},{224,144},{239,199},
        {255,255},{256,256},{300,300},{450,372},{479,399},{-1,40},{40,-1},{480,40},{40,400}};
    for(unsigned i=0;i<sizeof points/sizeof *points;++i) {
        int x=points[i][0],y=points[i][1];bool valid=ScWorldContains(&world,x,y);
        if(valid) ScWorldPutCell(&world,x,y,0x8015);
        put(0x1d3,x);put(0x1d5,y);
        routine_bank(1,0xc772,0,0);
        assert((ram[0x13b]|ram[0x13c]<<8)==(valid?0x8015:0));
        assert(valid || (ram[0x13d]|ram[0x13e]<<8)==0x300);
    }
}
static void building_repair_bounds(void) {
    const unsigned sizes[]={3,4,6},owners[]={0x144,0x27c,0x2a5};
    for(unsigned huge=0;huge<2;++huge) for(unsigned kind=0;kind<3;++kind) {
        ScWorldReset(&world);world.active=true;world.huge=huge;memset(ram,0,sizeof ram);
        unsigned n=sizes[kind],shift=n==6?2:1;
        unsigned cx=ScWorldWidth(&world)-n+shift,cy=ScWorldHeight(&world)-n+shift;
        unsigned offset=2*(cy*ScWorldWidth(&world)+cx),owner=owners[kind];
        ScWorldPutCell(&world,cx,cy,owner);world.map_anchor=offset;
        world.coord[2][0]=cx;world.coord[2][1]=cy;
        put(0x1e00,offset+2);put(0xb87,owner);ram[0xb85]=(uint8_t)cx;ram[0xb86]=(uint8_t)cy;
        routine(0xaea1,0,n);
        for(unsigned y=0;y<ScWorldHeight(&world);++y) for(unsigned x=0;x<ScWorldWidth(&world);++x) {
            unsigned expected=x>=cx-shift && x<cx-shift+n && y>=cy-shift && y<cy-shift+n
                ?owner-(shift*n+shift)+(y-(cy-shift))*n+x-(cx-shift):0;
            unsigned actual=ScWorldCell(&world,x,y)&1023;
            if(actual!=expected) fprintf(stderr,"repair size=%u huge=%u at %u,%u expected=%x actual=%x\n",n,huge,x,y,expected,actual);
            assert(actual==expected);
        }
    }
}
static void building_update_bounds(void) {
    const unsigned sizes[]={3,4,6},owners[]={0x144,0x27c,0x2a5};
    for(unsigned huge=0;huge<2;++huge) {
      for(unsigned conversion=0;conversion<2;++conversion) {
        ScWorldReset(&world);world.active=true;world.huge=huge;memset(ram,0,sizeof ram);
        unsigned x=ScWorldWidth(&world)-2,y=ScWorldHeight(&world)-2;
        for(unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx)
          ScWorldPutCell(&world,x-1+dx,y-1+dy,conversion?0x80+3*dy+dx:0x89);
        world.coord[2][0]=x;world.coord[2][1]=y;ram[0xb85]=x;ram[0xb86]=y;put(0x1e06,0x89);
        for(unsigned attempt=0;attempt<(conversion?1:9);++attempt) {
          world.coord[2][0]=x;world.coord[2][1]=y;
          routine(conversion?0x969e:0x96f8,0,0);
        }
        for(unsigned yy=0;yy<ScWorldHeight(&world);++yy) for(unsigned xx=0;xx<ScWorldWidth(&world);++xx) {
          unsigned tile=ScWorldCell(&world,xx,yy)&1023;
          if(xx<x-1 || xx>x+1 || yy<y-1 || yy>y+1) assert(!tile);
          else if(!conversion) {
            unsigned expected=0x80+3*(yy-y+1)+xx-x+1;
            if(tile!=expected) fprintf(stderr,"house revert huge=%u x=%u y=%u expected=%x tile=%x anchor=%u\n",huge,xx,yy,expected,tile,world.map_anchor);
            assert(tile==expected);
          }
          else assert((xx==x && yy==y)?tile==0x84:tile>=0x89 && tile<0x8c);
        }
      }
      for(unsigned kind=0;kind<3;++kind) {
        ScWorldReset(&world);world.active=true;world.huge=huge;memset(ram,0,sizeof ram);
        unsigned n=sizes[kind],shift=n==6?2:1;
        unsigned x=ScWorldWidth(&world)-n,y=ScWorldHeight(&world)-n,cx=x+shift,cy=y+shift;
        unsigned first=owners[kind]-shift*n-shift;
        for(unsigned dy=0;dy<n;++dy) for(unsigned dx=0;dx<n;++dx)
          ScWorldPutCell(&world,x+dx,y+dy,first+dy*n+dx);
        copy=world;world.coord[2][0]=cx;world.coord[2][1]=cy;ram[0xb85]=cx;ram[0xb86]=cy;
        routine(0xa89f,0,0);unsigned changed=0;
        for(unsigned yy=0;yy<ScWorldHeight(&world);++yy) for(unsigned xx=0;xx<ScWorldWidth(&world);++xx) {
          unsigned before=ScWorldCell(&copy,xx,yy),after=ScWorldCell(&world,xx,yy);
          if(xx<x || xx>=x+n || yy<y || yy>=y+n) assert(after==before);
          else changed+=after!=before;
        }
        assert(changed);
      }
    }
}
static void stencil_equivalence(void) {
    for(unsigned huge=0;huge<2;++huge) for(unsigned kernel=0;kernel<2;++kernel)
      for(unsigned pattern=0;pattern<3;++pattern) for(unsigned point=0;point<9;++point)
      for(unsigned aligned=0;aligned<2;++aligned) {
        ScWorldReset(&world);world.active=true;world.huge=huge;
        unsigned width=ScWorldFieldWidth(&world,13),height=ScWorldFieldHeight(&world,13);
        unsigned x=point%3==0?0:point%3==1?width/2:width-1;
        unsigned y=point/3==0?0:point/3==1?height/2:height-1;
        unsigned src=kernel?14:13,dst=kernel?13:14,index=y*width+x;
        for(unsigned i=0;i<width*height;++i) world.fields[src][i]=pattern==0?0:pattern==1?255:(i*173+71)&255;
        copy=world;memset(ram,0x5a,sizeof ram);
        interp816_reset(cpu);cpu->k=cpu->db=3;cpu->pc=kernel?0xa0d0:0xa04a;
        cpu->sp=0x1ffd;cpu->dp=aligned?0x1e00:0x1df6;
        cpu->x=index;cpu->e=cpu->xf=false;cpu->mf=cpu->i=true;
        put(cpu->dp,x);put(cpu->dp+2,y);
        Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned target=kernel?0xa125:0xa09f,cycles=0;
        while(cpu->pc!=target) {
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            cycles+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;unsigned expected_pixel=world.fields[dst][index];
        memcpy(stencil_expected_ram,ram,sizeof ram);
        world=copy;*cpu=initial;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned fast=ScWorldGuestFastStep(&world,cpu,ram,rom,sizeof rom);
        if(fast!=cycles) fprintf(stderr,"stencil cycles kernel=%u pattern=%u point=%u fast=%u original=%u\n",kernel,pattern,point,fast,cycles);
        assert(fast==cycles && cpu->pc==expected.pc && cpu->a==expected.a && cpu->x==expected.x && cpu->y==expected.y);
        assert(cpu->sp==expected.sp && cpu->dp==expected.dp && interp816_getFlags(cpu)==interp816_getFlags(&expected));
        assert(!memcmp(ram,stencil_expected_ram,sizeof ram) && world.fields[dst][index]==expected_pixel);
        cpu->pc=initial.pc;cpu->nmiWanted=true;assert(!ScWorldGuestFastStep(&world,cpu,ram,rom,sizeof rom));
    }
}
static void empty_cell_equivalence(void) {
    unsigned cases=0;
    for(unsigned huge=0;huge<2;++huge) for(unsigned sweep=0;sweep<2;++sweep)
      for(unsigned tile=0;tile<1024;++tile) {
        unsigned property=rom[0x184eb+tile];
        if(sweep?(tile>=64 || (property&0x71)):(property&1)) continue;
        for(unsigned point=0;point<4;++point) for(unsigned aligned=0;aligned<2;++aligned) {
            ScWorldReset(&world);world.active=true;world.huge=huge;
            unsigned x=point&1?ScWorldWidth(&world)-1:0;
            unsigned y=point&2?ScWorldHeight(&world)-1:0;
            unsigned offset=2*(y*ScWorldWidth(&world)+x);
            ScWorldPutCell(&world,x,y,tile|0xc000);world.scan_x=x;world.scan_y=y;
            memset(ram,0x5a,sizeof ram);
            interp816_reset(cpu);cpu->k=cpu->db=3;cpu->pc=sweep?0x82ae:0x9af7;
            cpu->sp=0x1ffd;cpu->dp=aligned?0x1e00:0x1df6;
            cpu->e=cpu->xf=false;cpu->mf=!sweep;cpu->i=true;cpu->c=cpu->v=true;
            put(cpu->dp,offset);put(cpu->dp+0x10,x);put(cpu->dp+0x12,y);
            ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;
            ScWorldGuestStep(&world,cpu,ram);
            copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
            unsigned target=sweep?0x8341:0x9b5a,cycles=0,steps=0;
            while(cpu->pc!=target) {
                assert(++steps<500);
                ScWorldGuestStep(&world,cpu,ram);
                ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
                cycles+=interp816_runOpcode(cpu);
            }
            Interp816 expected=*cpu;unsigned anchor=world.map_anchor;
            memcpy(stencil_expected_ram,ram,sizeof ram);
            world=copy;*cpu=initial;memcpy(ram,bitmap_ram,sizeof ram);
            unsigned fast=ScWorldGuestFastStep(&world,cpu,ram,rom,sizeof rom);
            if(fast!=cycles || cpu->a!=expected.a || interp816_getFlags(cpu)!=interp816_getFlags(&expected))
                fprintf(stderr,"empty sweep=%u huge=%u tile=%x point=%u dp=%x fast=%u original=%u A=%x/%x flags=%x/%x\n",sweep,huge,tile,point,cpu->dp,fast,cycles,cpu->a,expected.a,interp816_getFlags(cpu),interp816_getFlags(&expected));
            assert(fast==cycles && cpu->pc==expected.pc && cpu->a==expected.a && cpu->x==expected.x && cpu->y==expected.y);
            assert(cpu->sp==expected.sp && cpu->dp==expected.dp && interp816_getFlags(cpu)==interp816_getFlags(&expected));
            if(memcmp(ram,stencil_expected_ram,sizeof ram) || world.map_anchor!=anchor) {
                fprintf(stderr,"empty memory sweep=%u huge=%u tile=%x point=%u dp=%x anchor=%x/%x\n",sweep,huge,tile,point,cpu->dp,world.map_anchor,anchor);
                for(unsigned p=0;p<sizeof ram;++p) if(ram[p]!=stencil_expected_ram[p]) fprintf(stderr,"RAM %x=%x/%x\n",p,ram[p],stencil_expected_ram[p]);
            }
            assert(!memcmp(ram,stencil_expected_ram,sizeof ram) && world.map_anchor==anchor);
            ++cases;
        }
      }
    printf("Empty spatial cell equivalence: %u cases\n",cases);
}
static void kernel_equivalence(void) {
    const unsigned starts[]={0x9cdf,0x9c77,0x9eb0,0xa040,0xa0c6};
    const unsigned ends[]={0x9dc9,0x9cb0,0x9f47,0xa09f,0xa125};
    for(unsigned map=0;map<3;++map) for(unsigned k=0;k<5;++k)
    for(unsigned point=0;point<3;++point) for(unsigned pattern=0;pattern<3;++pattern) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map==2;
        unsigned width=ScWorldWidth(&world)/2,height=ScWorldHeight(&world)/2;
        unsigned x=point==0?0:point==1?width/2:width-1,y=point==0?0:point==1?height/2:height-1;
        for(unsigned f=0;f<17;++f) for(unsigned i=0;i<ScWorldFieldSizeWorld(&world,f);++i)
            world.fields[f][i]=pattern==0?0:pattern==1?255:(i*197+71)&255;
        for(unsigned i=0;i<ScWorldCells(&world);++i) {
            unsigned tile=pattern==0?0:pattern==1?0x15:(i*31+57)%958;
            world.tiles[2*i]=(uint8_t)tile;world.tiles[2*i+1]=(uint8_t)(tile>>8);
        }
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=starts[k];cpu->dp=0x1df6;cpu->sp=0x1ffc;
        cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=true;cpu->c=cpu->v=true;
        put(cpu->dp+(k>=3?0:8),x);put(cpu->dp+(k>=3?2:10),y);
        ScWorldGuestStep(&world,cpu,ram);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cycles=0,steps=0;
        while(cpu->pc!=ends[k]) {
            assert(++steps<5000);ScWorldGuestStep(&world,cpu,ram);
            if(cpu->pc==ends[k]) break;
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
        *expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned fast=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,20000);
        if(fast!=cycles || memcmp(&world,expected_world,sizeof world) || memcmp(ram,stencil_expected_ram,sizeof ram))
            fprintf(stderr,"kernel map=%u entry=%x point=%u pattern=%u cycles=%u/%u pc=%x/%x\n",map,starts[k],point,pattern,fast,cycles,cpu->pc,expected.pc);
        assert(fast==cycles && !memcmp(&world,expected_world,sizeof world) && !memcmp(ram,stencil_expected_ram,sizeof ram));
        assert(!memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
        free(expected_world);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);cpu->nmiWanted=true;
        assert(!ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,20000));
    }
    puts("PASS: 135 spatial kernel cells match native registers, flags, cycles, stack, RAM and full fields");
}
static void empty_terrain_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    unsigned cases=0;
    for(unsigned map=0;map<3;++map) for(unsigned point=0;point<8;++point)
      for(unsigned mask=0;mask<16;++mask) for(unsigned aligned=0;aligned<2;++aligned)
      for(unsigned pattern=0;pattern<2;++pattern) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map==2;
        unsigned width=ScWorldWidth(&world)/2,height=ScWorldHeight(&world)/2;
        unsigned x=point==0?0:point==7?width-1:(point*67)%width;
        unsigned y=point==0?0:point==7?height-1:(point*73)%height;
        unsigned at=2*y*ScWorldWidth(&world)+2*x;
        const unsigned cells[]={at,at+1,at+ScWorldWidth(&world),at+ScWorldWidth(&world)+1};
        for(unsigned n=0;n<4;++n) {
            unsigned tile=(mask&(1<<n))?(n*11+mask)%39+1:0;
            world.tiles[2*cells[n]]=(uint8_t)tile;world.tiles[2*cells[n]+1]=pattern?0x80:0x40;
        }
        unsigned coarse=(y/2)*(width/2)+x/2;
        world.fields[15][coarse]=pattern?250:113;
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9cdf;cpu->dp=aligned?0x1e00:0x1df6;cpu->sp=0x1ffc;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=pattern!=0;cpu->i=cpu->c=cpu->v=true;
        cpu->a=0x1234;cpu->x=0x5555;cpu->y=0x7777;
        put(cpu->dp+8,x);put(cpu->dp+10,y);ScWorldGuestStep(&world,cpu,ram);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cycles=0;
        while(cpu->pc!=0x9dc9) {
            ScWorldGuestStep(&world,cpu,ram);if(cpu->pc==0x9dc9) break;
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned fast=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,20000);
        assert(fast==cycles && !memcmp(&world,expected_world,sizeof world) && !memcmp(ram,stencil_expected_ram,sizeof ram));
        assert(!memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
        /* A small beam/IRQ budget must fall back to interruptible opcodes. */
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        fast=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,100);
        assert(fast<=100 && cpu->pc!=0x9dc9);
        ++cases;
      }
    free(expected_world);
    printf("PASS: %u vacant terrain groups, byte/word entry modes, flags, overflow, power metadata, seams and beam budgets\n",cases);
}
static void terrain_field_equivalence(void) {
    for(unsigned tile=0;tile<0x28;++tile) for(unsigned pattern=0;pattern<4;++pattern)
      for(unsigned aligned=0;aligned<2;++aligned) {
        ScWorldReset(&world);world.active=true;
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9dca;cpu->dp=aligned?0x1e00:0x1df6;
        cpu->e=cpu->xf=false;cpu->mf=cpu->i=true;cpu->c=cpu->v=pattern&1;
        cpu->a=tile|0xc000;put(cpu->dp+0x22,pattern==0?0:pattern==1?0xffff:pattern==2?0x7ff8:0xfff8);
        Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cycles=0;
        while(cpu->pc!=0x9e0b) {ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);}
        Interp816 expected=*cpu;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned fast=ScWorldGuestFastStep(&world,cpu,ram,rom,sizeof rom);
        assert(fast==cycles && cpu->pc==expected.pc && cpu->a==expected.a && cpu->x==expected.x && cpu->y==expected.y);
        assert(interp816_getFlags(cpu)==interp816_getFlags(&expected) && !memcmp(ram,stencil_expected_ram,sizeof ram));
      }
}
int main(int argc,char **argv) {
    assert(argc==2); FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom); fclose(f);
    ScMapGenPrng pr={0}; sc_mapgen_prng_seed_from_spin(&pr,0xc7);
    ScWorldGenerate(&world,&pr); assert(world.active);
    for (unsigned i=0;i<SC_WORLD_FIELDS;++i) assert(ScWorldFieldSize(i)<=SC_WORLD_FIELD_BYTES);
    assert(!ScWorldPutCell(&world,-1,0,0xffff) && !ScWorldPutCell(&world,240,199,0xffff));
    for (int y=0;y<200;++y) for (int x=0;x<240;++x)
        assert(ScWorldPutCell(&world,x,y,(uint16_t)(y*240+x)));
    cpu=interp816_init(NULL,read_bus,write_bus); assert(cpu);
    const unsigned points[][2]={{0,0},{119,99},{120,100},{128,128},{239,199},{128,137},{129,137}};
    for (unsigned i=0;i<sizeof points/sizeof *points;++i) {
        unsigned x=points[i][0],y=points[i][1];
        routine(0x849e,x|(y<<8),0);
        assert(cpu->a==y*240+x && cpu->x==(uint16_t)(2*(y*240+x)));
        uint8_t before[sizeof ram]; memcpy(before,ram,sizeof ram);
        routine(0x84c4,x|(y<<8),0x8fff);
        assert(ScWorldCell(&world,x,y)==0x8fff);
        assert(cpu->a==0x8fff && cpu->n && !cpu->z);
        /* The host write never aliases guest tiles or other bank-7f fields. */
        assert(!memcmp(before+0x10000,ram+0x10000,0x10000));
    }
    routine(0xa29a,119|(99<<8),0); assert(cpu->x==11999);
    routine(0xa2b9,59|(49<<8),0); assert(cpu->x==2999);
    routine(0xa2d7,29|(24<<8),0); assert(cpu->x==749);
    /* The native fallback locator must also stay inside its 30x25 map on
     * a 240x200 city, and absolute camera jumps must reach the far half. */
    put(0x1bd,215); put(0x1bf,178); put(0xd7,0);
    routine_bank(1,0x8aa8,0,0);
    assert(mini_x==226 && mini_y==78);
    routine_bank(1,0xa688,239,0); assert(cpu->a==210);
    routine_bank(1,0xa6a1,199,0); assert(cpu->a==172);
    terrain_bounds();
    unsigned field; int displacement;
    assert(ScWorldFieldResolve(0xb150,&field,&displacement) && field==10 && displacement== -60);
    assert(ScWorldFieldResolve(0xc17c,&field,&displacement) && field==14 && displacement== -120);
    assert(!ScWorldFieldResolve(0xb151,&field,&displacement));
    assert(!ScWorldFieldResolve(0x5fc0,&field,&displacement)); /* temporal history */
    assert(ScWorldFieldResolve(0xb5c4,&field,&displacement) && field==13 && displacement== -120);
    memset(ram+0x10000,0xa5,0x10000);
    world.map_anchor=2*(190*240+200);
    cpu->k=3; cpu->pc=0x9a6c; cpu->x=(uint16_t)world.map_anchor; cpu->mf=false;
    ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
    uint8_t low,high; assert(ScWorldGuestRead(&guest,guest.address,&low));
    assert(ScWorldGuestRead(&guest,guest.address+1,&high));
    assert((unsigned)(low|(high<<8))==ScWorldCell(&world,200,191));
    assert(!ScWorldGuestRead(&guest,0x03849e,&low)); /* instruction fetch */
    cpu->pc=0x9daf; cpu->x=11999; cpu->mf=true;
    ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
    assert(ScWorldGuestWrite(&guest,guest.address,0x7b));
    assert(world.fields[0][11999]==0x7b && ram[0x16b00+11999]==0xa5);
    size_t size=ScWorldEncodedSize(); uint8_t *data=malloc(size); assert(data);
    assert(ScWorldEncode(&world,data,size)); assert(ScWorldDecode(&copy,data,size));
    assert(!memcmp(&world,&copy,sizeof world));
    /* The legacy Beta 1/2 payload imports with its original pitch. */
    size_t old_size=48+SC_WORLD_TILE_BYTES;
    for(unsigned i=0;i<SC_WORLD_FIELDS;++i) old_size+=ScWorldFieldSize(i);
    uint8_t *old=calloc(1,old_size);assert(old);
    memcpy(old,"SCWORLD",7);old[7]=2;old[8]=240;old[10]=200;old[12]=1;
    for(unsigned i=0;i<4;++i) old[20+i]=(uint8_t)(old_size>>(8*i));old[24]=19;
    old[48+95998]=0xbc;old[48+95999]=0x8a;
    assert(ScWorldDecode(&copy,old,old_size) && copy.active && !copy.huge);
    assert(ScWorldCell(&copy,239,199)==0x8abc);
    size_t old_cities_size=24+2*(16+old_size);
    uint8_t *old_cities=calloc(1,old_cities_size),*upgraded=malloc(ScWorldCitiesSize());assert(old_cities && upgraded);
    memcpy(old_cities,"SCWCITY",7);old_cities[7]=2;old_cities[16]=2;
    for(unsigned i=0;i<4;++i) {old_cities[8+i]=(uint8_t)(old_size>>(8*i));old_cities[12+i]=(uint8_t)(old_cities_size>>(8*i));}
    memcpy(old_cities+40,old,old_size);old_cities[28]=1;
    uint64_t old_hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<old_size;++i) old_hash=(old_hash^old[i])*UINT64_C(1099511628211);
    for(unsigned i=0;i<8;++i) old_cities[32+i]=(uint8_t)(old_hash>>(8*i));
    assert(ScWorldCitiesUpgrade(upgraded,old_cities,old_cities_size));
    assert(ScWorldCitiesValid(upgraded,ScWorldCitiesSize()));
    assert(ScWorldDecode(&copy,upgraded+40,ScWorldEncodedSize()) && ScWorldCell(&copy,239,199)==0x8abc);
    old_cities[40+old_size-1]^=1;assert(!ScWorldCitiesUpgrade(upgraded,old_cities,old_cities_size));
    free(upgraded);free(old_cities);free(old);
    copy=world;
    data[10]=100; assert(!ScWorldDecode(&copy,data,size)); assert(!memcmp(&world,&copy,sizeof world));
    assert(!ScWorldDecode(&copy,data,size-1));
    /* Native SRAM slots bind independently to full map/field records. A
     * changed native city or a truncated sidecar must never import another
     * city's large map. Normal cities also replace old large-slot metadata. */
    size_t cities_size=ScWorldCitiesSize(); uint8_t *cities=malloc(cities_size);
    static uint8_t sram[0x8000]; assert(cities);
    ScWorldCitiesInit(cities); assert(ScWorldCitiesValid(cities,cities_size));
    assert(ScWorldCitySave(cities,sram,0,&world));
    assert(ScWorldCityLoad(&copy,cities,cities_size,sram,0) && !memcmp(&world,&copy,sizeof world));
    cities[24+16+48+95999]^=1;
    assert(!ScWorldCityLoad(&copy,cities,cities_size,sram,0) && !memcmp(&world,&copy,sizeof world));
    cities[24+16+48+95999]^=1;
    ScWorldReset(&copy); assert(ScWorldCitySave(cities,sram,1,&copy));
    assert(ScWorldCityLoad(&copy,cities,cities_size,sram,1) && !copy.active);
    sram[0x5000]=1; assert(ScWorldCityLoad(&copy,cities,cities_size,sram,0));
    assert(!ScWorldCityLoad(&copy,cities,cities_size,sram,1));
    sram[0x100]=1; assert(!ScWorldCityLoad(&copy,cities,cities_size,sram,0));
    assert(!ScWorldCityLoad(&copy,cities,cities_size-1,sram,0)); free(cities);
    /* Run the ROM's entire sweep: each of the 48,000 cells, including the
     * far southeast quadrant and the 64-KiB crossing, is visited once. */
    memset(ram,0,sizeof ram); memset(visits,0,sizeof visits);
    memset(world.tiles,0,sizeof world.tiles);
    routine(0x8228,0,0); routine(0x8297,0,0);
    for (unsigned i=0;i<SC_WORLD_CELLS;++i) assert(visits[i]==1);
    assert(ram[0xb85]==240 && ram[0xb86]==200);
    /* Power writeback must neither stop nor alias at native X's wrap. */
    memset(world.fields[5],0,ScWorldFieldSize(5));
    world.fields[5][0]=0x80; world.fields[5][5999]=1;
    routine(0xb152,0,0);
    assert(ScWorldCell(&world,0,0)==0x8000 && ScWorldCell(&world,239,199)==0x8000);
    assert(!ScWorldCell(&world,238,199));
    /* Native bit lookup/set helpers use a 30-byte row across all 200 rows. */
    ram[0xb85]=239; ram[0xb86]=199;
    routine(0xb120,0,0); assert(cpu->x==5999 && cpu->y==7);
    memset(world.fields[5],0,ScWorldFieldSize(5));
    routine(0xb0e5,0,0); assert(world.fields[5][5999]==1 && !world.fields[5][1499]);
    routine(0xb0f8,0,0); assert(cpu->y==1);
    /* Drawing interrupts leave the simulation's full tile cursor intact. */
    world.map_anchor=90000; put(0x1d3,239); put(0x1d5,199);
    cpu->k=1; cpu->pc=0xc7b4; ScWorldGuestStep(&world,cpu,ram);
    assert(world.map_anchor==90000 && world.bank_anchor[1]==95998);
    /* Actual native compression, SRAM writes, decompression and city restore,
     * with the same completion hooks as the host. Presentation calls are
     * replaced by RTL; the saved-city routines and all storage run unchanged. */
    native_cities=malloc(ScWorldCitiesSize()); assert(native_cities);
    ScWorldCitiesInit(native_cities); put(0x423,1); put(0x421,1);
    ScWorldPutCell(&world,200,180,0x8abc); world.fields[0][11999]=123;
    copy=world; routine(0xcafd,0,0);
    assert(native_sram[5]==1 && world.active);
    routine(0xc8a1,0,0);
    assert(world.active && ScWorldCell(&world,200,180)==0x8abc && world.fields[0][11999]==123);
    assert(!memcmp(world.tiles,copy.tiles,sizeof world.tiles));
    free(native_cities); native_cities=NULL;
    /* The real overview bitmap covers the whole large map at half scale.
     * Compare the ROM's bitmap with an independently sampled stock map. */
    memset(ram,0,sizeof ram); put(0x3e,2);
    for (int y=0;y<200;++y) for (int x=0;x<240;++x)
        ScWorldPutCell(&world,x,y,(x/16+y/16)%26);
    for (unsigned y=0;y<100;++y) for(unsigned x=0;x<120;++x)
        put(0x10200+2*(y*120+x),ScWorldCell(&world,x*2,y*2));
    memcpy(bitmap_ram,ram,sizeof ram);
    routine_bank(2,0x899b,0,0); memcpy(bitmap,ram+0xa000,sizeof bitmap);
    world.active=false; memcpy(ram,bitmap_ram,sizeof ram);
    routine_bank(2,0x899b,0,0);
    assert(!memcmp(bitmap,ram+0xa000,sizeof bitmap)); world.active=true;
    /* Real dense-zone capacities across the expanded sweep must exceed the
     * native 16-bit counter safely, then feed the same 64-bit calculation. */
    memset(ram,0,sizeof ram); memset(world.tiles,0,sizeof world.tiles);
    for (unsigned y=0;y<198;y+=3) for(unsigned x=0;x<240;x+=3)
      for (unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx)
        ScWorldPutCell(&world,x+dx,y+dy,0x8000+0x120+dy*3+dx+((dy==2 && dx==2)?0x4000:0));
    population_enabled=true; memset(&population,0,sizeof population); population.valid=true;
    memset(visits,0,sizeof visits);
    routine(0x8228,0,0); routine(0x8297,0,0);
    fprintf(stderr,"large residential capacity=%llu native=%u\n",(unsigned long long)population.capacity[0],ram[0xb8b]|(ram[0xb8c]<<8));
    assert(population.capacity[0]>65535);
    uint64_t expected_population=population.capacity[0]*20;
    routine(0x8196,0,0); assert(population.value==expected_population);
    population_enabled=false;
    /* Huge geometry exceeds both packed bytes and the 16-bit map bank.
     * Run the entire real native sweep, not only synthetic pointer checks. */
    ScWorldReset(&world);world.active=world.huge=true;
    memset(ram,0,sizeof ram);memset(visits,0,sizeof visits);
    routine(0x8228,0,0);routine(0x8297,0,0);
    for(unsigned i=0;i<ScWorldCells(&world);++i) assert(visits[i]==1);
    assert(world.scan_y==400);
    world.coord[2][0]=479;world.coord[2][1]=399;
    ScWorldPutCell(&world,479,399,0x8abc);ScWorldPutCell(&world,223,143,0x123);
    routine(0x849e,223|(143<<8),0);assert(cpu->a==0x8abc);
    routine(0x84c4,223|(143<<8),0x8001);assert(ScWorldCell(&world,479,399)==0x8001);
    assert(ScWorldCell(&world,223,143)==0x123);
    routine(0xa29a,111|(71<<8),0);assert(cpu->x==199*240+239);
    world.fields[5][23999]=1;routine(0xb152,0,0);assert(ScWorldCell(&world,479,399)&0x8000);
    put(0x1bd,455);put(0x1bf,378);routine_bank(1,0x8aa8,0,0);
    routine_bank(1,0xa688,479,0);assert(cpu->a==450);
    routine_bank(1,0xa6a1,399,0);assert(cpu->a==372);
    terrain_bounds();
    world.scan_x=479;world.scan_y=399;
    free(data);size=ScWorldEncodedSize();data=malloc(size);assert(data);
    assert(ScWorldEncode(&world,data,size) && ScWorldDecode(&copy,data,size));
    assert(!memcmp(&world,&copy,sizeof world));
    memset(world.tiles,0,sizeof world.tiles);memset(ram,0,sizeof ram);
    for(unsigned y=0;y<399;y+=3) for(unsigned x=0;x<480;x+=3)
      for(unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx)
        ScWorldPutCell(&world,x+dx,y+dy,0x8120+dy*3+dx+((dy==2 && dx==2)?0x4000:0));
    memset(&population,0,sizeof population);
    assert(ScPopulationRefreshLive(&population,ram,&world,rom,sizeof rom));
    assert(population.value==UINT64_C(160)*133*40*20);
    population_enabled=true;memset(&population,0,sizeof population);population.valid=true;
    routine(0x8228,0,0);routine(0x8297,0,0);
    assert(population.capacity[0]==UINT64_C(160)*133*40);
    expected_population=population.capacity[0]*20;routine(0x8196,0,0);
    assert(population.value==expected_population);
    assert(ScPopulationRefreshLive(&population,ram,&world,rom,sizeof rom));
    assert(population.value>0 && population.value<=expected_population);
    population_enabled=false;
    uint64_t centers=0,sum_x=0,sum_y=0;
    for(unsigned y=0;y<400;++y) for(unsigned x=0;x<480;++x)
      if(rom[0x184eb+(ScWorldCell(&world,x,y)&1023)]&1) {++centers;sum_x+=x;sum_y+=y;}
    routine(0x9ad7,0,0);assert(world.center_valid);
    fprintf(stderr,"huge center %u,%u expected %llu,%llu (%llu centers)\n",world.center_x,world.center_y,
      (unsigned long long)(sum_x/centers),(unsigned long long)(sum_y/centers),(unsigned long long)centers);
    assert(world.center_x==sum_x/centers && world.center_y==sum_y/centers);
    routine(0x9c11,0,0);routine(0x9e8e,0,0);routine(0x9aa3,0,0);
    /* Real blank-map sweeps and derived fields: added land retains stock
     * elapsed time, rather than making Huge wait sixteen times as long.
     * Include the actual native loops; a zone-only fixture misses this bug. */
    const unsigned spatial_entries[]={0x8297,0x9c11,0x9ad7};
    for(unsigned entry=0;entry<3;++entry) {
        uint64_t stock=0;
        for(unsigned size_id=0;size_id<4;++size_id) {
            memset(ram,0,sizeof ram);ScWorldReset(&world);
            world.active=size_id>0;world.huge=size_id>=2;world.giant=size_id==3;
            routine(spatial_entries[entry],0,0);
            if(!size_id) {stock=spatial_cycles;assert(stock==raw_cycles);}
            else {
                assert(raw_cycles>stock*(size_id==1?2:8));
                assert(spatial_cycles>stock/2 && spatial_cycles<stock*3/2);
            }
            printf("spatial %04x map=%u raw=%llu elapsed=%llu stock=%llu\n",spatial_entries[entry],size_id,
                (unsigned long long)raw_cycles,(unsigned long long)spatial_cycles,(unsigned long long)stock);
        }
    }
    world.active=world.huge=true;world.giant=false;
    const uint32_t ordinary[]={0x008400,0x01897f,0x038026,0x03804f,0x0390a7,0x03ae1c,0x03b84b,0x03c474};
    for(unsigned i=0;i<sizeof ordinary/sizeof *ordinary;++i)
        assert(ScWorldGuestMasterCycles(&world,ordinary[i],48,&cycle_remainder)==48);
    stencil_equivalence();
    empty_cell_equivalence();
    terrain_field_equivalence();
    kernel_equivalence();
    empty_terrain_equivalence();
    building_repair_bounds();
    building_update_bounds();
    /* 960x800: exercise every cell through the original spatial routines,
     * including coarse fields beyond byte coordinates and 64 KiB indices. */
    ScWorldReset(&world);world.active=world.huge=world.giant=true;
    memset(ram,0,sizeof ram);memset(visits,0,sizeof visits);
    routine(0x8228,0,0);routine(0x8297,0,0);
    for(unsigned i=0;i<ScWorldCells(&world);++i) assert(visits[i]==1);
    assert(world.scan_y==800);
    world.coord[2][0]=959;world.coord[2][1]=799;
    ScWorldPutCell(&world,959,799,0x8abc);ScWorldPutCell(&world,191,31,0x123);
    routine(0x849e,191|(31<<8),0);assert(cpu->a==0x8abc);
    routine(0x84c4,191|(31<<8),0x8001);assert(ScWorldCell(&world,959,799)==0x8001);
    assert(ScWorldCell(&world,191,31)==0x123);
    routine(0xa29a,223|(143<<8),0);assert(cpu->x==(uint16_t)(399*480+479));
    world.fields[5][95999]=1;routine(0xb152,0,0);assert(ScWorldCell(&world,959,799)&0x8000);
    put(0x1bd,935);put(0x1bf,778);routine_bank(1,0x8aa8,0,0);
    routine_bank(1,0xa688,959,0);assert(cpu->a==930);
    routine_bank(1,0xa6a1,799,0);assert(cpu->a==772);
    for(unsigned f=0;f<SC_WORLD_FIELDS;++f) assert(ScWorldFieldSizeWorld(&world,f)<=SC_WORLD_FIELD_BYTES);
    memset(world.tiles,0,sizeof world.tiles);memset(ram,0,sizeof ram);
    for(unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx)
      ScWorldPutCell(&world,900+dx,750+dy,0x8120+dy*3+dx+((dy==2 && dx==2)?0x4000:0));
    memset(&population,0,sizeof population);population.valid=true;population_enabled=true;
    routine(0x8228,0,0);routine(0x8297,0,0);routine(0x8196,0,0);
    assert(population.value==40*20);population_enabled=false;
    routine(0x9ad7,0,0);assert(world.center_x==900 && world.center_y==750);assert(world.fields[3][375*480+450]>0);
    routine(0x9c11,0,0);routine(0x9e8e,0,0);routine(0x9aa3,0,0);
    /* More than 20,000 pending power entries must retain full coordinates. */
    world.coord[2][0]=900;world.coord[2][1]=750;ram[0xb85]=(uint8_t)900;ram[0xb86]=(uint8_t)750;
    put(0xc57,20000);cpu->k=3;cpu->pc=0xb0be;
    ScWorldGuestStep(&world,cpu,ram);
    assert((ram[0xc57]|(ram[0xc58]<<8))==20001);
    assert((world.fields[17][40002]|(world.fields[17][40003]<<8))==900);
    assert((world.fields[18][40002]|(world.fields[18][40003]<<8))==750);
    world.scan_x=959;world.scan_y=799;
    assert(ScWorldEncode(&world,data,size) && ScWorldDecode(&copy,data,size));
    assert(!memcmp(&world,&copy,sizeof world));
    native_cities=malloc(ScWorldCitiesSize());assert(native_cities);ScWorldCitiesInit(native_cities);
    assert(ScWorldCitySave(native_cities,native_sram,1,&world));
    assert(ScWorldCityLoad(&copy,native_cities,ScWorldCitiesSize(),native_sram,1));
    assert(!memcmp(&world,&copy,sizeof world));
    ScWorldCitiesInit(native_cities);put(0x423,1);put(0x421,1);
    copy=world;routine(0xcafd,0,0);routine(0xc8a1,0,0);
    assert(world.active && world.giant && ScWorldCell(&world,900,750)==ScWorldCell(&copy,900,750));
    assert(!memcmp(world.tiles,copy.tiles,sizeof world.tiles));
    assert(!memcmp(world.fields,copy.fields,sizeof world.fields));
    free(native_cities);native_cities=NULL;
    interp816_free(cpu); free(data);
    puts("PASS: 48,000/192,000/768,000 native visits, full-world census and fields, power, byte seams, full city center, legacy migration and portable saves");
}
