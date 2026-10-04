#include "sc_world.h"
#include "sc_world_guest.h"
#include "sc_land.h"
#include "sc_power_traversal.h"
#include "sc_zoning.h"
#include "sc_density.h"
#include "sc_smoothing.h"
#include "sc_service.h"
#include "sc_tile_lookup.h"
#include "sc_transport.h"
#include "sc_population.h"
#include "snes/interp816.h"
#include "sc_program.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
        assert(++steps<800000000);
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
static void city_radius(void) {
    /* Equal relative positions must retain the stock land-value bonus on
     * every map size, including far corners across the packed-byte seams. */
    const unsigned deltas[][2]={{0,0},{1,0},{8,0},{0,16},{12,12},{31,0},{32,0},{40,0}};
    for(unsigned map=0;map<5;++map) for(unsigned far=0;far<2;++far)
    for(unsigned p=0;p<sizeof deltas/sizeof *deltas;++p) {
        ScWorldReset(&world);world.active=map>0;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
        memset(ram,0,sizeof ram);
        unsigned scale=1u<<map,width=120*scale,height=100*scale;
        unsigned cx=far?width-16*scale:16*scale,cy=far?height-16*scale:16*scale;
        unsigned x=far?cx-2*scale*deltas[p][0]:cx+2*scale*deltas[p][0];
        unsigned y=far?cy-2*scale*deltas[p][1]:cy+2*scale*deltas[p][1];
        world.center_valid=true;world.center_x=cx;world.center_y=cy;
        world.coord[2][0]=x;world.coord[2][1]=y;
        ram[0xbab]=(uint8_t)(cx/2);ram[0xbac]=(uint8_t)(cy/2);
        unsigned distance=deltas[p][0]+deltas[p][1],expected=distance>32?32:distance;
        routine(0x9e61,((x/2)&255)|(((y/2)&255)<<8),0);
        assert((cpu->a&255)==expected && cpu->mf && cpu->c==(distance>=32));
    }
    puts("PASS: stock city-center falloff at equivalent positions on all five map sizes and both map corners");
}
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
/* Compare exactly one real original opcode, never a clock-equivalent span.
 * Reachable inputs come from the ROM family walks below. */
static unsigned atomic_cases,atomic_rejections;
static unsigned char atomic_coverage[5][2][65536];
static void atomic_instruction_equivalence(void) {
    static ScWorld *before_world,*actual_world;
    static uint8_t *before_ram,*actual_ram;
    if(!getenv("SC_WORLD_ATOMIC_TEST")) return;
    unsigned bank=cpu->k==1?0:1;
    unsigned map=!world.active?0:world.colossal?4:world.giant?3:world.huge?2:1;
    if((cpu->k!=1 && cpu->k!=3) || (cpu->k==1?!ScTileLookupOwns(cpu->pc):
       !(ScPowerTraversalOwns(cpu->pc) || ScDensityOwns(cpu->pc) || ScLandOwns(cpu->pc) || ScTransportOwns(cpu->pc)))) return;
    if(atomic_coverage[map][bank][cpu->pc]>=4) return;
    if(!before_world) {
        before_world=malloc(sizeof world);actual_world=malloc(sizeof world);
        before_ram=malloc(sizeof ram);actual_ram=malloc(sizeof ram);
        assert(before_world && actual_world && before_ram && actual_ram);
    }
    uint16_t start=cpu->pc;Interp816 before=*cpu;ScWorldGuest before_guest=guest;
    *before_world=world;memcpy(before_ram,ram,sizeof ram);
    uint16_t old_product=product,old_quotient=quotient,old_dividend=dividend;
    uint8_t old_multiplicand=multiplicand;
    unsigned cost=ScWorldGuestInstructionStep(&world,cpu,ram,rom,sizeof rom);
    Interp816 actual=*cpu;*actual_world=world;memcpy(actual_ram,ram,sizeof ram);
    uint16_t actual_product=product,actual_quotient=quotient,actual_dividend=dividend;
    uint8_t actual_multiplicand=multiplicand;
    if(!cost) {
        assert(!memcmp(cpu,&before,sizeof before) && !memcmp(ram,before_ram,sizeof ram));
        assert(!memcmp(&world,before_world,sizeof world));
        assert(product==old_product && quotient==old_quotient && dividend==old_dividend && multiplicand==old_multiplicand);
        ++atomic_rejections;
    } else {
        *cpu=before;world=*before_world;memcpy(ram,before_ram,sizeof ram);guest=before_guest;
        product=old_product;quotient=old_quotient;dividend=old_dividend;multiplicand=old_multiplicand;
        ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
        unsigned elapsed=interp816_runOpcode(cpu);
        if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,actual_ram,sizeof ram) ||
           memcmp(&world,actual_world,sizeof world) || product!=actual_product || quotient!=actual_quotient ||
           dividend!=actual_dividend || multiplicand!=actual_multiplicand) {
            fprintf(stderr,"atomic bank=%x pc=%x clocks=%u/%u end=%x/%x flags=%x/%x A=%x/%x X=%x/%x Y=%x/%x world=%d ram=%d\n",
                before.k,start,elapsed,cost,cpu->pc,actual.pc,interp816_getFlags(cpu),interp816_getFlags(&actual),
                cpu->a,actual.a,cpu->x,actual.x,cpu->y,actual.y,memcmp(&world,actual_world,sizeof world),memcmp(ram,actual_ram,sizeof ram));abort();
        }
        ++atomic_cases;
        *cpu=before;cpu->nmiWanted=true;Interp816 pending=*cpu;
        assert(!ScWorldGuestInstructionStep(&world,cpu,ram,rom,sizeof rom));assert(!memcmp(cpu,&pending,sizeof pending));
        cpu->nmiWanted=false;cpu->irqWanted=true;cpu->i=false;pending=*cpu;
        assert(!ScWorldGuestInstructionStep(&world,cpu,ram,rom,sizeof rom));assert(!memcmp(cpu,&pending,sizeof pending));
    }
    ++atomic_coverage[map][bank][start];*cpu=before;world=*before_world;memcpy(ram,before_ram,sizeof ram);guest=before_guest;
    product=old_product;quotient=old_quotient;dividend=old_dividend;multiplicand=old_multiplicand;
}
static uint32_t *test_stencil;
static const uint32_t *test_stencil_data(void *ctx,const ScWorld *w,unsigned source) {
    (void)ctx;(void)w;(void)source;return test_stencil;
}
static void gpu_field_span_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    test_stencil=malloc(SC_WORLD_FIELD_BYTES*sizeof *test_stencil);assert(test_stencil);
    unsigned cases=0,yields=0;
    ScStencilBackend backend={NULL,NULL,test_stencil_data};ScWorldGuestSetStencilBackend(&backend);
    const unsigned budgets[]={0,1,160,200,256,1024,4096};
    for(unsigned map=0;map<4;++map) for(unsigned phase=0;phase<2;++phase)
    for(unsigned pattern=0;pattern<3;++pattern) for(unsigned align=0;align<2;++align)
    for(unsigned point=0;point<6;++point) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldFieldWidth(&world,13),height=ScWorldFieldHeight(&world,13),source=phase?14:13;
        for(unsigned n=0;n<width*height;++n) world.fields[source][n]=pattern==0?0:pattern==1?255:(n*173+71)&255;
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
            test_stencil[y*width+x]=ScStencilPack(world.fields[source],width,height,x,y);
        unsigned x=point==0?0:point==1?width-3:point==2?127%width:point==3?width-1:width/2;
        unsigned y=point==0?0:point==1?height-1:point==2?128%height:point==3?height-2:point==4?height/2:height-1;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=phase?0xa0c6:0xa040;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=cpu->c=cpu->v=true;cpu->a=0xabcd;cpu->x=0x53;cpu->y=0x71;
        put(cpu->dp,x);put(cpu->dp+2,y);copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            world=copy;*cpu=initial;memcpy(ram,bitmap_ram,sizeof ram);
            unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);assert(cost<=budgets[b]);
            Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
            if(!cost) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(&world,&copy,sizeof world) && !memcmp(ram,bitmap_ram,sizeof ram));++yields;continue;}
            world=copy;*cpu=initial;memcpy(ram,bitmap_ram,sizeof ram);unsigned elapsed=0;
            while(elapsed<cost) {ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);}
            if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world)) {
                fprintf(stderr,"GPU field span map=%u phase=%u pattern=%u align=%u point=%u budget=%u clocks=%u/%u pc=%x/%x A=%x/%x flags=%x/%x world=%d ram=%d\n",
                    map,phase,pattern,align,point,budgets[b],elapsed,cost,cpu->pc,actual.pc,cpu->a,actual.a,interp816_getFlags(cpu),interp816_getFlags(&actual),memcmp(&world,expected_world,sizeof world),memcmp(ram,stencil_expected_ram,sizeof ram));abort();
            }
            ++cases;
        }
    }
    assert(ScWorldGuestStencilCells()>1000);ScWorldGuestSetStencilBackend(NULL);free(test_stencil);test_stencil=NULL;free(expected_world);
    printf("PASS: %u packed whole-field spans, %u immutable yields, both smoothing phases/all expanded maps match original ROM clocks/CPU/RAM/world\n",cases,yields);
}



static void service_field_span_equivalence(void) {
    ScWorld *expected=malloc(sizeof world);assert(expected);
    test_stencil=malloc(SC_WORLD_FIELD_BYTES*sizeof *test_stencil);assert(test_stencil);
    unsigned cases=0,yields=0;const unsigned budgets[]={0,1,31,140,256,1024};
    for(unsigned cached=0;cached<2;++cached) {
        ScStencilBackend backend={NULL,NULL,cached?test_stencil_data:NULL};ScWorldGuestSetStencilBackend(&backend);
        for(unsigned map=0;map<4;++map) for(unsigned phase=0;phase<2;++phase)
        for(unsigned pattern=0;pattern<3;++pattern) for(unsigned align=0;align<2;++align)
        for(unsigned point=0;point<4;++point) {
            ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
            unsigned source=phase?11:10,width=ScWorldFieldWidth(&world,source),height=ScWorldFieldHeight(&world,source);
            for(unsigned n=0;n<width*height;++n) {unsigned value=pattern==0?0:pattern==1?0x7fff:(n*173+37171)&65535;world.fields[source][2*n]=value;world.fields[source][2*n+1]=value>>8;}
            for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) test_stencil[y*width+x]=ScServicePack(world.fields[source],width,height,x,y);
            unsigned x=point==0?0:point==1?width-3:point==2?width-1:width/2;
            unsigned y=point==0?0:point==1?height-1:point==2?height-2:height/2;
            memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
            cpu->k=cpu->db=3;cpu->pc=phase?0xa1e3:0xa164;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
            cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=cpu->c=cpu->v=true;
            cpu->a=0xabcd;cpu->x=0x53;cpu->y=0x71;put(cpu->dp,x);put(cpu->dp+2,y);
            copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
            for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
                world=copy;*cpu=initial;memcpy(ram,bitmap_ram,sizeof ram);
                unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);assert(cost<=budgets[b]);
                Interp816 actual=*cpu;*expected=world;memcpy(stencil_expected_ram,ram,sizeof ram);
                if(!cost) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(&world,&copy,sizeof world) && !memcmp(ram,bitmap_ram,sizeof ram));++yields;continue;}
                world=copy;*cpu=initial;memcpy(ram,bitmap_ram,sizeof ram);unsigned elapsed=0,steps=0;
                while(elapsed<cost) {assert(++steps<1000);ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);}
                if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected,sizeof world)) {
                    fprintf(stderr,"service span cached=%u map=%u phase=%u pattern=%u align=%u point=%u budget=%u clocks=%u/%u pc=%x/%x A=%x/%x flags=%x/%x world=%d ram=%d\n",cached,map,phase,pattern,align,point,budgets[b],elapsed,cost,cpu->pc,actual.pc,cpu->a,actual.a,interp816_getFlags(cpu),interp816_getFlags(&actual),memcmp(&world,expected,sizeof world),memcmp(ram,stencil_expected_ram,sizeof ram));abort();
                }
                ++cases;
            }
        }
        assert(ScWorldGuestServiceCells()>1000);if(cached) assert(ScWorldGuestServiceGpuCells()>1000);
    }
    ScWorldGuestSetStencilBackend(NULL);free(test_stencil);test_stencil=NULL;free(expected);
    printf("PASS: %u word-field GPU/C publication spans and %u immutable yields match original ROM CPU/RAM/world/clocks on all expanded maps/both services\n",cases,yields);
}
static unsigned smoothing_submissions,last_smoothing_source;
static void smoothing_begin_test(void *context,const ScWorld *w,unsigned source) {
    (void)context;assert(w->active);++smoothing_submissions;last_smoothing_source=source;
}
static void smoothing_handoff_equivalence(void) {
    ScWorldReset(&world);world.active=world.huge=world.giant=world.colossal=true;
    memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
    cpu->k=cpu->db=3;cpu->dp=0x1e00;cpu->sp=0x1f75;cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=true;
    ScStencilBackend backend={NULL,smoothing_begin_test,NULL};ScWorldGuestSetStencilBackend(&backend);
    smoothing_submissions=0;cpu->pc=0x9b6c;
    /* Density's connected C JSR enters the first smoothing pass. Its entry
     * hook belongs to the scheduler and must not be swallowed by batching. */
    unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,4096);
    assert(cost==6 && cpu->pc==0xa02f && !smoothing_submissions);
    ScWorldGuestStep(&world,cpu,ram);assert(smoothing_submissions==1 && last_smoothing_source==13);
    cpu->pc=0x9b6f;cpu->sp=0x1f75;
    cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,4096);
    assert(cost==6 && cpu->pc==0xa0b5 && smoothing_submissions==1);
    ScWorldGuestStep(&world,cpu,ram);assert(smoothing_submissions==2 && last_smoothing_source==14);
    /* A direct return into another pass has the same handoff contract. */
    cpu->pc=0xa0b4;cpu->sp=0x1f75;put(cpu->sp+1,0xa0b4);
    cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,4096);
    assert(cost==6 && cpu->pc==0xa0b5 && smoothing_submissions==2);
    ScWorldGuestStep(&world,cpu,ram);assert(smoothing_submissions==3 && last_smoothing_source==14);
    cpu->pc=0xa1cb;cpu->sp=0x1f75;cpu->mf=false;put(cpu->sp+1,0xa14c);
    cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,4096);
    assert(cost==6 && cpu->pc==0xa14d && smoothing_submissions==3);
    ScWorldGuestStep(&world,cpu,ram);assert(smoothing_submissions==4 && last_smoothing_source==10);
    cpu->pc=0xa24a;cpu->sp=0x1f75;put(cpu->sp+1,0xa1cb);
    cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,4096);
    assert(cost==6 && cpu->pc==0xa1cc && smoothing_submissions==4);
    ScWorldGuestStep(&world,cpu,ram);assert(smoothing_submissions==5 && last_smoothing_source==11);
    ScWorldGuestSetStencilBackend(NULL);
    puts("PASS: connected density JSRs and byte/word service returns yield before new GPU submission/invalidation hook");
}
static void service_family_equivalence(void) {
    ScWorld *expected=malloc(sizeof world);assert(expected);
    unsigned cases=0,atomic=0,yields=0,seen[65536]={0};
    const unsigned budgets[]={0,1,12,31,160};
    for(unsigned map=0;map<4;++map) for(unsigned phase=0;phase<2;++phase)
    for(unsigned pattern=0;pattern<3;++pattern) for(unsigned align=0;align<2;++align)
    for(unsigned point=0;point<6;++point) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldFieldWidth(&world,10),height=ScWorldFieldHeight(&world,10);
        for(unsigned field=10;field<=11;++field) for(unsigned n=0;n<width*height;++n) {unsigned value=pattern==0?0:pattern==1?0x7fff:(n*173+field*37171)&65535;world.fields[field][2*n]=value;world.fields[field][2*n+1]=value>>8;}
        for(unsigned n=0;n<width*height*2;++n) world.fields[16][n]=(n*171+73)&255;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        unsigned cell=phase?0xa1e3:0xa164,entry=phase?0xa1cc:0xa14d,copy_entry=phase?0xa23a:0xa1bb;
        unsigned x=point==0?0:point==1?width/2:point==2?width-1:0;
        unsigned y=point==0?0:point==1?height/2:point==2?height-1:height-1;
        cpu->k=cpu->db=3;cpu->pc=point==3?entry:point>=4?copy_entry:cell;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=cpu->c=cpu->v=true;
        cpu->a=0xabcd;cpu->x=0x53;cpu->y=0x71;
        put(cpu->dp,x);put(cpu->dp+2,y);put(cpu->sp+1,cpu->dp+4);put(cpu->sp+3,0x6fff);
        if(point==3) put(cpu->sp+1,0x6fff);
        if(point>=4) {unsigned end=width*height*2,start=point==4?end-2:end>65536?65534:(end/2)&~1u;world.field_scan=start;cpu->x=(uint16_t)start;cpu->mf=false;}
        unsigned guard=0;
        for(;;) {
            assert(++guard<160);
            if(ScServiceOwns(cpu->pc)) {
                ++seen[cpu->pc];Interp816 initial=*cpu;copy=world;memcpy(bitmap_ram,ram,sizeof ram);
                for(unsigned trial=0;trial<6;++trial) {
                    *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
                    unsigned cost=trial==5?ScServiceInstructionStep(&world,cpu,ram,rom):ScServiceStep(&world,cpu,ram,rom,budgets[trial]);
                    if(trial<5) assert(cost<=budgets[trial]);
                    Interp816 actual=*cpu;*expected=world;memcpy(stencil_expected_ram,ram,sizeof ram);
                    if(!cost) {
                        assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(&world,&copy,sizeof world) && !memcmp(ram,bitmap_ram,sizeof ram));
                        ++yields;continue;
                    }
                    if(trial==5) ++atomic;
                    *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned elapsed=0,steps=0;
                    while(elapsed<cost) {
                        assert(++steps<200);ScWorldGuestStep(&world,cpu,ram);
                        ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);
                    }
                    if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected,sizeof world)) {
                        fprintf(stderr,"service map=%u phase=%u pattern=%u align=%u point=%u entry=%x budget=%u atomic=%u clocks=%u/%u pc=%x/%x A=%x/%x flags=%x/%x world=%d ram=%d\n",
                            map,phase,pattern,align,point,initial.pc,trial<5?budgets[trial]:12,trial==5,elapsed,cost,cpu->pc,actual.pc,cpu->a,actual.a,interp816_getFlags(cpu),interp816_getFlags(&actual),memcmp(&world,expected,sizeof world),memcmp(ram,stencil_expected_ram,sizeof ram));abort();
                    }
                    if(trial==5) assert(steps==1);
                    ++cases;
                }
                *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
            }
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);interp816_runOpcode(cpu);
            if(point==3?cpu->pc==cell:cpu->pc==cell || cpu->pc==copy_entry || !ScServiceOwns(cpu->pc) && cpu->pc!=0xa2d7 && cpu->pc!=0xa2f4) break;
        }
    }
    unsigned boundaries=0;for(unsigned p=0;p<65536;++p) boundaries+=seen[p]!=0;
    assert(boundaries==124 && atomic>1000 && yields>1000);
    /* Pending CPU interrupts must be serviced before the native family. */
    cpu->k=cpu->db=3;cpu->pc=0xa164;cpu->dp=0x1e00;cpu->sp=0x1f75;cpu->e=cpu->xf=cpu->d=false;
    cpu->nmiWanted=true;Interp816 before=*cpu;assert(!ScServiceInstructionStep(&world,cpu,ram,rom));assert(!memcmp(cpu,&before,sizeof before));
    cpu->nmiWanted=false;cpu->irqWanted=true;cpu->i=false;before=*cpu;
    assert(!ScServiceStep(&world,cpu,ram,rom,4096));assert(!memcmp(cpu,&before,sizeof before));
    free(expected);printf("PASS: %u connected C service/ROM comparisons, %u atomic opcodes, %u immutable yields, all %u setup/cell/control/return boundaries on all expanded maps; IRQ/NMI preserved\n",cases,atomic,yields,boundaries);
}

static void smoothing_family_equivalence(void) {
    ScWorld *expected=malloc(sizeof world);assert(expected);
    unsigned cases=0,atomic=0,yields=0,seen[65536]={0};
    const unsigned budgets[]={0,1,12,31,160};
    for(unsigned map=0;map<4;++map) for(unsigned phase=0;phase<2;++phase)
    for(unsigned pattern=0;pattern<3;++pattern) for(unsigned align=0;align<2;++align)
    for(unsigned point=0;point<4;++point) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldFieldWidth(&world,13),height=ScWorldFieldHeight(&world,13);
        for(unsigned field=13;field<=14;++field) for(unsigned n=0;n<width*height;++n)
            world.fields[field][n]=pattern==0?0:pattern==1?255:(n*173+field*71)&255;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        unsigned cell=phase?0xa0c6:0xa040,entry=phase?0xa0b5:0xa02f;
        unsigned x=point==0?0:point==1?width/2:point==2?width-1:0;
        unsigned y=point==0?0:point==1?height/2:point==2?height-1:height-1;
        cpu->k=cpu->db=3;cpu->pc=point==3?entry:cell;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=cpu->c=cpu->v=true;
        cpu->a=0xabcd;cpu->x=0x53;cpu->y=0x71;
        put(cpu->dp,x);put(cpu->dp+2,y);put(cpu->sp+1,cpu->dp+6);put(cpu->sp+3,0x6fff);
        if(point==3) put(cpu->sp+1,0x6fff);
        unsigned guard=0;
        for(;;) {
            assert(++guard<160);
            if(ScSmoothingOwns(cpu->pc)) {
                ++seen[cpu->pc];Interp816 initial=*cpu;copy=world;memcpy(bitmap_ram,ram,sizeof ram);
                for(unsigned trial=0;trial<6;++trial) {
                    *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
                    unsigned cost=trial==5?ScSmoothingInstructionStep(&world,cpu,ram,rom):ScSmoothingStep(&world,cpu,ram,rom,budgets[trial]);
                    if(trial<5) assert(cost<=budgets[trial]);
                    Interp816 actual=*cpu;*expected=world;memcpy(stencil_expected_ram,ram,sizeof ram);
                    if(!cost) {
                        assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(&world,&copy,sizeof world) && !memcmp(ram,bitmap_ram,sizeof ram));
                        ++yields;continue;
                    }
                    if(trial==5) ++atomic;
                    *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned elapsed=0,steps=0;
                    while(elapsed<cost) {
                        assert(++steps<200);ScWorldGuestStep(&world,cpu,ram);
                        ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);
                    }
                    if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected,sizeof world)) {
                        fprintf(stderr,"smoothing map=%u phase=%u pattern=%u align=%u point=%u entry=%x budget=%u atomic=%u clocks=%u/%u pc=%x/%x A=%x/%x flags=%x/%x world=%d ram=%d\n",
                            map,phase,pattern,align,point,initial.pc,trial<5?budgets[trial]:12,trial==5,elapsed,cost,cpu->pc,actual.pc,cpu->a,actual.a,interp816_getFlags(cpu),interp816_getFlags(&actual),memcmp(&world,expected,sizeof world),memcmp(ram,stencil_expected_ram,sizeof ram));abort();
                    }
                    if(trial==5) assert(steps==1);
                    ++cases;
                }
                *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
            }
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);interp816_runOpcode(cpu);
            if(point==3?cpu->pc==cell:cpu->pc==cell || !ScSmoothingOwns(cpu->pc) && cpu->pc!=0xa29a && cpu->pc!=0xa2b8) break;
        }
    }
    unsigned boundaries=0;for(unsigned p=0;p<65536;++p) boundaries+=seen[p]!=0;
    assert(boundaries==130 && atomic>1000 && yields>1000);
    /* Pending CPU interrupts must be serviced before the native family. */
    cpu->k=cpu->db=3;cpu->pc=0xa040;cpu->dp=0x1e00;cpu->sp=0x1f75;cpu->e=cpu->xf=cpu->d=false;
    cpu->nmiWanted=true;Interp816 before=*cpu;assert(!ScSmoothingInstructionStep(&world,cpu,ram,rom));assert(!memcmp(cpu,&before,sizeof before));
    cpu->nmiWanted=false;cpu->irqWanted=true;cpu->i=false;before=*cpu;
    assert(!ScSmoothingStep(&world,cpu,ram,rom,4096));assert(!memcmp(cpu,&before,sizeof before));
    free(expected);printf("PASS: %u connected C smoothing/ROM comparisons, %u atomic opcodes, %u immutable yields, all %u setup/cell/control/return boundaries on all expanded maps; IRQ/NMI preserved\n",cases,atomic,yields,boundaries);
}

static void tile_lookup_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world),*actual_world=malloc(sizeof world);
    uint8_t *actual_ram=malloc(sizeof ram);assert(expected_world && actual_world && actual_ram);
    unsigned cases=0,spans=0,yields=0,coverage[65536]={0};
    const unsigned budgets[]={0,1,2,6,7,15,16,18,19,24,25,31,36,37,38,39,40,41,42,43,128,4096};
    for(unsigned map=0;map<5;++map) for(unsigned point=0;point<12;++point)
    for(unsigned pattern=0;pattern<4;++pattern) for(unsigned align=0;align<2;++align) {
        ScWorldReset(&world);world.active=map>0;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
        unsigned width=120u<<map,height=100u<<map;
        int x=point==0?0:point==1?width-1:point==2?width/2:point==3?127:point==4?128:
              point==5?255:point==6?256:point==7?width:point==8?-1:width-2;
        int y=point==0?0:point==1?height-1:point==2?height/2:point==3?127:point==4?128:
              point==5?255:point==6?256:point==9?height:point==10?-1:height-2;
        unsigned tile=(const unsigned[]){0,0x8015,0xffff,0x8364}[pattern];
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        if(x>=0 && y>=0 && (unsigned)x<width && (unsigned)y<height) {
            if(world.active) ScWorldPutCell(&world,x,y,tile);
            else put(0x10200+2*(y*120+x),tile);
        }
        put(0x1d3,x);put(0x1d5,y);cpu->k=1;
        cpu->db=(const unsigned[]){0,1,0x7e,0x80}[pattern];cpu->pc=0xc772;
        cpu->dp=align?0x1e35:0;cpu->sp=0x1ff5;cpu->e=cpu->d=false;
        cpu->mf=pattern&1;cpu->xf=pattern&2;cpu->i=true;cpu->v=pattern&1;cpu->c=pattern&2;
        cpu->a=0xabcd;cpu->x=0x53;cpu->y=0x72;put(cpu->sp+1,0x6fff);
        product=0xbeef;multiplicand=0x29;Interp816 initial=*cpu;copy=world;
        memcpy(bitmap_ram,ram,sizeof ram);unsigned clocks=0,steps=0;
        while(cpu->pc!=0x7000) {
            atomic_instruction_equivalence();
            assert(++steps<100);ScWorldGuestStep(&world,cpu,ram);
            if(ScTileLookupOwns(cpu->pc) && (coverage[cpu->pc]<2 || steps%13==pattern%13)) {
                uint16_t start=cpu->pc;Interp816 before=*cpu;*expected_world=world;
                memcpy(stencil_expected_ram,ram,sizeof ram);unsigned old_product=product,old_multiplicand=multiplicand;
                unsigned count=sizeof budgets/sizeof *budgets;
                unsigned first=coverage[start]<2?0:(map+point+pattern+align+steps)%count;
                unsigned end=coverage[start]<2?count:first+1;
                for(unsigned b=first;b<end;++b) {
                    *cpu=before;world=*expected_world;memcpy(ram,stencil_expected_ram,sizeof ram);
                    product=old_product;multiplicand=old_multiplicand;
                    unsigned cost=ScTileLookupStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);assert(cost<=budgets[b]);
                    Interp816 actual=*cpu;*actual_world=world;memcpy(actual_ram,ram,sizeof ram);
                    unsigned actual_product=product,actual_multiplicand=multiplicand;
                    if(!cost) {
                        assert(!memcmp(cpu,&before,sizeof before) && !memcmp(ram,stencil_expected_ram,sizeof ram));
                        assert(!memcmp(&world,expected_world,sizeof world) && product==old_product && multiplicand==old_multiplicand);++yields;
                    } else {
                        *cpu=before;world=*expected_world;memcpy(ram,stencil_expected_ram,sizeof ram);
                        product=old_product;multiplicand=old_multiplicand;unsigned elapsed=0;
                        while(elapsed<cost) {
                            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
                            elapsed+=interp816_runOpcode(cpu);
                        }
                        if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,actual_ram,sizeof ram) ||
                           memcmp(&world,actual_world,sizeof world) || product!=actual_product || multiplicand!=actual_multiplicand)
                            fprintf(stderr,"tile lookup map=%u point=%u pattern=%u align=%u pc=%x budget=%u clocks=%u/%u end=%x/%x A=%x/%x X=%x/%x flags=%x/%x product=%x/%x\n",
                                map,point,pattern,align,start,budgets[b],elapsed,cost,cpu->pc,actual.pc,cpu->a,actual.a,cpu->x,actual.x,
                                interp816_getFlags(cpu),interp816_getFlags(&actual),product,actual_product);
                        assert(elapsed==cost && !memcmp(cpu,&actual,sizeof actual) && !memcmp(ram,actual_ram,sizeof ram));
                        assert(!memcmp(&world,actual_world,sizeof world) && product==actual_product && multiplicand==actual_multiplicand);++spans;
                    }
                }
                ++coverage[start];*cpu=before;world=*expected_world;memcpy(ram,stencil_expected_ram,sizeof ram);
                product=old_product;multiplicand=old_multiplicand;
            }
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);clocks+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        unsigned final_product=product,final_multiplicand=multiplicand;
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);product=0xbeef;multiplicand=0x29;
        unsigned elapsed=ScTileLookupStep(&world,cpu,ram,rom,sizeof rom,4096);
        assert(elapsed==clocks && !memcmp(cpu,&expected,sizeof expected) && !memcmp(ram,stencil_expected_ram,sizeof ram));
        assert(!memcmp(&world,expected_world,sizeof world) && product==final_product && multiplicand==final_multiplicand);++cases;
    }
    unsigned seen=0;for(unsigned p=0;p<65536;++p) seen+=coverage[p]!=0;
    assert(seen>=69);free(expected_world);free(actual_world);free(actual_ram);
    printf("PASS: %u complete tile lookups, %u interrupted spans, %u immutable yields, %u instruction boundaries on all five maps\n",cases,spans,yields,seen);
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
        if(sweep?(tile>=958 || tile==0x7f || tile==0x364 || tile==0x365 || (property&0x71)):(property&1)) continue;
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
static void sweep_span_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned tiles[]={0,0x15,0x26,0x80,0x377,0x2bb};
    const unsigned budgets[]={255,256,500,2000,8000};
    for(unsigned map=0;map<4;++map) for(unsigned pattern=0;pattern<6;++pattern)
    for(unsigned point=0;point<3;++point) for(unsigned budget=0;budget<5;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=point?width-1:width/2,y=point==2?height-1:height/2;
        for(unsigned yy=y;yy<height && yy<y+2;++yy) for(unsigned xx=0;xx<width;++xx)
            ScWorldPutCell(&world,xx,yy,tiles[pattern]|0xc000);
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x82ac;cpu->dp=pattern&1?0x1e00:0x1ef5;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=cpu->c=cpu->v=true;
        world.scan_x=x;world.scan_y=y;ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;
        put(cpu->dp,2*(y*width+x));
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {
            assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram));
            assert(!memcmp(&world,&copy,sizeof world));continue;
        }
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cycles<cost) {
            assert(++guard<5000);
            uint16_t pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);if(cpu->pc!=pc) continue;
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        /* Whole-cell fusions include the zero-clock coordinate hook; a
         * resumed C span may stop just before it at the beam deadline. */
        if(world.huge && cpu->pc==0x8343 && actual.pc!=cpu->pc) ScWorldGuestStep(&world,cpu,ram);
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1)) {
            fprintf(stderr,"sweep span map=%u tile=%x point=%u budget=%u cycles=%u/%u pc=%x/%x flags=%x/%x\n",
                map,tiles[pattern],point,budgets[budget],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu));
            for(unsigned p=0,shown=0;p<sizeof ram && shown<8;++p) if(ram[p]!=stencil_expected_ram[p]) {fprintf(stderr,"RAM %x=%x/%x\n",p,ram[p],stencil_expected_ram[p]);++shown;}
        }
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u bounded complete tile-sweep spans, all enlarged map sizes and row/map ends\n",cases);
}
static void power_visit_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned entries[]={0xb00d,0xb036,0xb0e5,0xb05a,0xaffd,0xb000,0xb005,0xb03d},budgets[]={31,32,64,100,512};
    const unsigned lows[]={0,65534,65535,32768};
    for(unsigned map=0;map<4;++map) for(unsigned entry=0;entry<8;++entry)
    for(unsigned point=0;point<4;++point) for(unsigned state=0;state<16;++state)
    for(unsigned align=0;align<2;++align) for(unsigned budget=0;budget<5;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=point==0?0:point==1?width-1:point==2?width/2:width-2,y=point==0?0:point==1?height-1:height/2;
        world.coord[2][0]=x;world.coord[2][1]=y;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=entries[entry];cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->mf=cpu->xf=cpu->d=false;cpu->i=cpu->c=cpu->v=true;cpu->a=0x9234;cpu->x=0x8765;cpu->y=0x4321;
        ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;
        unsigned low=lows[state%4],high=state<8?0:65535,used=(high<<16)|low;
        unsigned cap=state<4?used+1:state<8?used:state<12?0:0xffffffffu;
        put(cpu->dp,state%4);put(cpu->dp+2,state%4);put(cpu->dp+4,0x1234);
        put(cpu->dp+14,cap);put(cpu->dp+16,cap>>16);put(cpu->dp+18,low);put(cpu->dp+20,high);
        put(0xc57,state<8?0:65534);
        if(entry>=4) {
            unsigned stack=state<8?0:65534;
            put(0xc59,state&4?stack:0x9234);
            world.fields[17][2*stack]=(uint8_t)x;world.fields[17][2*stack+1]=(uint8_t)(x>>8);
            world.fields[18][2*stack]=(uint8_t)y;world.fields[18][2*stack+1]=(uint8_t)(y>>8);
        }
        world.fields[5][(y*width+x)/8]=(uint8_t)(state*37);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));continue;}
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cycles<cost) {
            assert(++guard<200);ScWorldGuestStep(&world,cpu,ram);
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1)) {
            fprintf(stderr,"power visit map=%u entry=%x point=%u state=%u align=%u budget=%u cycles=%u/%u pc=%x/%x flags=%x/%x A=%x/%x\n",
                map,entries[entry],point,state,align,budgets[budget],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a);
            for(unsigned p=0,shown=0;p<sizeof ram && shown<8;++p) if(ram[p]!=stencil_expected_ram[p]) {fprintf(stderr,"RAM %x=%x/%x\n",p,ram[p],stencil_expected_ram[p]);++shown;}
        }
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u bounded power counter/bitmap/branch spans against original ROM\n",cases);
}
static void transport_neighbor_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0,rejected=0;
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<10;++point)
    for(unsigned direction=0;direction<4;++direction) for(unsigned variant=0;variant<4;++variant) {
        ScWorldReset(&world);world.active=true;world.huge=map>=1;world.giant=map>=2;world.colossal=map==3;
        int width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        int x=point==0?1:point==1?width/2:point==2?width-1:point==3?0:point==4?255:point==5?256:point==6?257:width-2;
        int y=point==0?1:point==2?height-1:point==3?0:point==6?256:point==7?255:point==8?height-2:height/2;
        if(!ScWorldContains(&world,x,y)) continue;
        int nx=x+(direction==1)-(direction==3),ny=y+(direction==2)-(direction==0);
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0xb370;cpu->dp=variant&1?0x1e06:0x1e00;cpu->sp=variant&2?0x1f75:0x1fd;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=variant&2;cpu->i=true;
        cpu->c=variant&1;cpu->v=variant&2;cpu->n=variant&1;cpu->z=variant&2;
        cpu->a=0x8100|direction;cpu->x=0xfedc;cpu->y=0xba98;
        put(cpu->sp+1,0x6fff);
        world.coord[2][0]=x;world.coord[2][1]=y;ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;
        ScWorldPutCell(&world,nx,ny,(uint16_t)(0xc000+point*37+variant*211));
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        assert(!ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,1));
        assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,1000);
        if(!ScWorldContains(&world,nx,ny)) {
            assert(!cost && !memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));
            ++rejected;continue;
        }
        assert(cost);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        assert(!ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,cost-1));
        assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));
        assert(ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,cost)==cost);
        assert(!memcmp(&world,expected_world,sizeof world) && !memcmp(ram,stencil_expected_ram,sizeof ram));
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cpu->pc!=0x7000) {
            assert(++guard<100);ScWorldGuestStep(&world,cpu,ram);
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1)) {
            fprintf(stderr,"transport map=%u point=%u direction=%u variant=%u cycles=%u/%u flags=%x/%x A=%x/%x X=%x/%x Y=%x/%x\n",map,point,direction,variant,cost,cycles,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a,actual.x,cpu->x,actual.y,cpu->y);
            for(unsigned p=0,shown=0;p<sizeof ram && shown<8;++p) if(ram[p]!=stencil_expected_ram[p]) {fprintf(stderr,"RAM %x=%x/%x\n",p,ram[p],stencil_expected_ram[p]);++shown;}
        }
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u native transport neighbours against original ROM, %u immutable boundary yields\n",cases,rejected);
}
static void housing_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    for(unsigned map=0;map<5;++map) for(unsigned point=0;point<9;++point)
    for(unsigned houses=0;houses<9;++houses) for(unsigned variant=0;variant<4;++variant) {
        ScWorldReset(&world);world.active=map>0;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
        unsigned width=map?ScWorldWidth(&world):120,height=map?ScWorldHeight(&world):100;
        if(!map && point>=4) continue;
        unsigned x=point==0?2:point==1?width/2:point==4?0:point==5?width-1:point==6?256:point==7?257:width-2;
        unsigned y=point==3?height-2:point==4 || point==8?0:point==5?height-1:point==6 && height>256?256:height/2;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9a3e;cpu->dp=variant&1?0x1e06:0x1e00;cpu->sp=variant&2?0x1f75:0x1fc;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=variant&2;cpu->i=cpu->c=cpu->v=true;cpu->a=0x8123;cpu->y=0x7654;
        world.coord[2][0]=x;world.coord[2][1]=y;ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;
        const unsigned dx[]={0,1,2,0,2,0,1,2},dy[]={0,0,0,1,1,2,2,2};
        for(unsigned n=0;n<8;++n) {
            unsigned tile=(n<houses?0x89+n%12:n&1?0x95:0x88)|0xc000;
            if(map) ScWorldPutCell(&world,x-1+dx[n],y-1+dy[n],tile);
            else put(0x10200+2*((y-1+dy[n])*width+x-1+dx[n]),tile);
        }
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        assert(!ScWorldGuestHousingStep(&world,cpu,ram,1));
        assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));
        unsigned cost=ScWorldGuestHousingStep(&world,cpu,ram,1000);assert(cost);
        Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cpu->pc!=0x9a92) {
            assert(++guard<300);ScWorldGuestStep(&world,cpu,ram);
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1)) {
            fprintf(stderr,"housing map=%u point=%u houses=%u variant=%u cycles=%u/%u flags=%x/%x A=%x/%x X=%x/%x\n",
                map,point,houses,variant,cost,cycles,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a,actual.x,cpu->x);
            for(unsigned p=0,shown=0;p<sizeof ram && shown<8;++p) if(ram[p]!=stencil_expected_ram[p]) {fprintf(stderr,"RAM %x=%x/%x\n",p,ram[p],stencil_expected_ram[p]);++shown;}
        }
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u complete native housing probes against original ROM\n",cases);
}
static void land_finish_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned pollution_values[]={0,249,250,65535},budgets[]={120,200,300,399,400,512};
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<4;++point)
    for(unsigned state=0;state<16;++state) for(unsigned align=0;align<2;++align)
    for(unsigned budget=0;budget<sizeof budgets/sizeof *budgets;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=point==0?0:point==1?width/4:point==2?width/2-1:width/2-2;
        unsigned y=point==0?0:point==1?height/4:height/2-1;
        unsigned index=y*(width/2)+x,coarse=(y/2)*(width/4)+x/2;
        world.coord[2][0]=x*2;world.coord[2][1]=y*2;world.center_valid=true;world.center_x=width/2;world.center_y=height/2;
        world.field_anchor[1]=coarse;world.fields[0][index]=123;world.fields[6][coarse]=(uint8_t)(state*37);
        world.fields[2][index]=(uint8_t)(state*41);world.fields[1][index]=state&2?190:189;
        world.fields[15][coarse]=0xf1;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9d30;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->mf=cpu->xf=cpu->d=false;cpu->i=true;cpu->c=state&1;cpu->v=state&2;
        cpu->a=0x9876;cpu->x=(uint16_t)coarse;cpu->y=0x4321;
        ram[0xb85]=(uint8_t)(x*2);ram[0xb86]=(uint8_t)(y*2);ram[0xbab]=(uint8_t)(width/4);ram[0xbac]=(uint8_t)(height/4);
        put(cpu->dp+8,x);put(cpu->dp+10,y);put(cpu->dp+14,pollution_values[state%4]);put(cpu->dp+16,state&4?1:0);
        put(cpu->dp+4,state&8?65520:17);put(cpu->dp+6,state&8?65535:0);put(cpu->dp+24,state&8?65535:0);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));continue;}
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        /* The connected land kernel can yield partway through this finish.
         * Replay precisely the retired prefix, rather than the whole finish. */
        while(cycles<cost) {
            assert(++guard<200);ScWorldGuestStep(&world,cpu,ram);
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1)) {
            fprintf(stderr,"land finish map=%u point=%u state=%u align=%u budget=%u cycles=%u/%u A=%x/%x flags=%x/%x X=%x/%x\n",map,point,state,align,budgets[budget],cost,cycles,actual.a,cpu->a,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.x,cpu->x);
            for(unsigned p=0,shown=0;p<sizeof ram && shown<8;++p) if(ram[p]!=stencil_expected_ram[p]) {fprintf(stderr,"RAM %x=%x/%x\n",p,ram[p],stencil_expected_ram[p]);++shown;}
        }
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u resumable land-value finishes against original ROM\n",cases);
}
static void house_site_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned values[]={0,0x5f,0x60,0xffff},budgets[]={32,128,512};
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<7;++point)
    for(unsigned index=0;index<4;++index) for(unsigned value=0;value<4;++value)
    for(unsigned align=0;align<2;++align) for(unsigned budget=0;budget<3;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=point==0?0:point==1?1:point==2?127:point==3?width-1:point==4?255:point==5?256:width-2;
        unsigned y=point==0?0:x<height?x-1:height-1;
        world.coord[2][0]=x;world.coord[2][1]=y;
        for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) ScWorldPutCell(&world,x+dx,y+dy,values[value]);
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x987f;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=cpu->c=cpu->v=true;
        cpu->x=index;cpu->y=0x7654;cpu->a=0x1234;
        ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;put(cpu->dp+6,x|(y<<8));ram[cpu->dp+10]=0xfe;
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestHouseSiteStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));continue;}
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cycles<cost) {
            assert(++guard<200);ScWorldGuestStep(&world,cpu,ram);
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"house site map=%u point=%u index=%u value=%u align=%u budget=%u cycles=%u/%u pc=%x/%x A=%x/%x X=%x/%x flags=%x/%x\n",
                map,point,index,value,align,budgets[budget],cost,cycles,actual.pc,cpu->pc,actual.a,cpu->a,actual.x,cpu->x,interp816_getFlags(&actual),interp816_getFlags(cpu));
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u bounded house-site scans against original ROM\n",cases);
}
static void field_sweep_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned entries[]={0x88fa,0x9b7a,0xb676},values[]={0,23,24,127,128,151,199,200,255};
    const unsigned budgets[]={12,13,20,31,64,512,8192};
    for(unsigned map=0;map<4;++map) for(unsigned kind=0;kind<3;++kind)
    for(unsigned point=0;point<4;++point) for(unsigned pattern=0;pattern<9;++pattern)
    for(unsigned budget=0;budget<sizeof budgets/sizeof *budgets;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned end=ScWorldFieldSizeWorld(&world,0),x=point==0?0:point==1?end/2:point==2?(end>65536?65534:end-2):end-1;
        assert(ScWorldGuestClockScale(&world,0x30000|entries[kind])==(kind==2?1:ScWorldCells(&world)/12000));
        for(unsigned i=x;i<end && i<x+1000;++i) {
            world.fields[0][i]=pattern&1?0:17;
            world.fields[4][i]=values[(pattern+i-x)%9];
            world.fields[14][i]=values[(pattern+i-x)%9];
        }
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=entries[kind];cpu->dp=pattern&1?0x1e00:0x1ef5;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=cpu->c=cpu->v=true;
        cpu->a=0x7b44;cpu->y=65535;cpu->x=(uint16_t)x;world.field_scan=x;
        put(cpu->dp,pattern&2?65535:32767);put(cpu->dp+2,65535);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));continue;}
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cycles<cost) {
            assert(++guard<10000);uint16_t pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);if(pc!=cpu->pc) continue;
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"field sweep map=%u kind=%u point=%u pattern=%u budget=%u cycles=%u/%u pc=%x/%x flags=%x/%x A=%x/%x\n",
                map,kind,point,pattern,budgets[budget],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a);
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u bounded native whole-field postpasses against original ROM\n",cases);
}
static void field_word_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned values[]={0,1,199,200,201,255,32767,32768,0xff37,0xff38,0xff39,65535};
    const unsigned budgets[]={12,13,16,32,63,512,8192};
    for(unsigned map=0;map<4;++map) for(unsigned kind=0;kind<2;++kind)
    for(unsigned point=0;point<4;++point) for(unsigned pattern=0;pattern<12;++pattern)
    for(unsigned budget=0;budget<sizeof budgets/sizeof *budgets;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned field=kind?7:15,end=ScWorldFieldSizeWorld(&world,field);
        unsigned x=point==0?0:point==1?end/2:point==2?(end>65536?65534:end-4):end-2;x&=~1u;
        for(unsigned i=x;i<end && i<x+2000;i+=2) {
            unsigned value=values[(pattern+(i-x)/2)%12];
            world.fields[field][i]=value;world.fields[field][i+1]=value>>8;
        }
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=kind?0x8924:0x9c22;cpu->dp=pattern&1?0x1e00:0x1ef5;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=cpu->mf=false;cpu->i=cpu->c=cpu->v=true;
        cpu->a=kind?0x7b44:0;cpu->y=65535;cpu->x=x;world.field_scan=x;
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));continue;}
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cycles<cost) {
            assert(++guard<10000);uint16_t pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);if(pc!=cpu->pc) continue;
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"field words map=%u kind=%u point=%u pattern=%u budget=%u cycles=%u/%u pc=%x/%x flags=%x/%x A=%x/%x\n",
                map,kind,point,pattern,budgets[budget],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a);
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u bounded native word-field clear/decay spans against original ROM\n",cases);
}
static void density_scan_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned tiles[]={0,0x15,0x26,0x80,0x377,0x2bb,0x84,0x99,0x13b,0x144,0x1fc,0x201,0x376,0x380,0x300},budgets[]={127,128,799,800,2000};
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<3;++point)
    for(unsigned pattern=0;pattern<sizeof tiles/sizeof *tiles;++pattern)
    for(unsigned budget=0;budget<sizeof budgets/sizeof *budgets;++budget) for(unsigned entry=0;entry<3;++entry) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=point?width-1:width/2,y=point==2?height-1:height/2;
        for(unsigned yy=y;yy<height && yy<y+2;++yy) for(unsigned xx=0;xx<width;++xx) ScWorldPutCell(&world,xx,yy,tiles[pattern]|0xc000);
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=entry==2?0x9b12:entry?0x9b5a:0x9af7;cpu->dp=pattern&1?0x1e00:0x1ef5;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=entry!=2;cpu->i=cpu->c=cpu->v=true;
        world.coord[2][0]=x;world.coord[2][1]=y;ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;
        put(cpu->dp+0x10,x);put(cpu->dp+0x12,y);
        put(0xb89,tiles[pattern]);cpu->y=tiles[pattern];
        if(pattern>=6) {put(cpu->dp,65535);put(cpu->dp+4,65535);put(cpu->dp+8,65535);}
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));continue;}
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cycles<cost) {
            assert(++guard<5000);uint16_t pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);if(pc!=cpu->pc) continue;
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(world.huge && cpu->pc==0x9b5c) ScWorldGuestStep(&world,cpu,ram);
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1)) {
            fprintf(stderr,"density scan map=%u point=%u tile=%x budget=%u cycles=%u/%u pc=%x/%x flags=%x/%x\n",
                map,point,tiles[pattern],budgets[budget],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu));
            for(unsigned p=0,shown=0;p<sizeof ram && shown<8;++p) if(ram[p]!=stencil_expected_ram[p]) {fprintf(stderr,"RAM %x=%x/%x\n",p,ram[p],stencil_expected_ram[p]);++shown;}
        }
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u bounded native density scans against original ROM\n",cases);
}
static void tile_pollution_equivalence(void) {
    ScWorldReset(&world);world.active=true;
    for(unsigned tile=0;tile<1024;++tile) for(unsigned aligned=0;aligned<2;++aligned) {
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9dca;cpu->dp=aligned?0x1e00:0x1df6;cpu->sp=0x1ffc;
        cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=true;cpu->c=cpu->v=true;
        cpu->a=0x8000|tile;cpu->y=0x5678;
        Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);unsigned cycles=0;
        memset(&guest,0,sizeof guest);
        while(cpu->pc!=0x9e0b) cycles+=interp816_runOpcode(cpu);
        Interp816 expected=*cpu;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned fast=ScWorldGuestFastStep(&world,cpu,ram,rom,sizeof rom);
        if(fast!=cycles || memcmp(ram,stencil_expected_ram,sizeof ram) ||
            memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"tile %x dp %x cycles %u/%u flags %x/%x last %u/%u Y %x/%x\n",tile,cpu->dp,fast,cycles,interp816_getFlags(cpu),interp816_getFlags(&expected),cpu->cyclesUsed,expected.cyclesUsed,cpu->y,expected.y);
        assert(fast==cycles && !memcmp(ram,stencil_expected_ram,sizeof ram));
        assert(!memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
    }
    puts("PASS: 2048 native tile pollution classifiers match the ROM oracle");
}
static void kernel_equivalence(void) {
    tile_pollution_equivalence();
    const unsigned starts[]={0x9cdf,0x9c77,0x9eb0,0xa040,0xa0c6,0x9fb7,0xa164,0xa1e3,0xa25c};
    for(unsigned map=0;map<3;++map) for(unsigned k=0;k<9;++k)
    for(unsigned point=0;point<3;++point) for(unsigned pattern=0;pattern<8;++pattern) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map==2;
        unsigned divisor=k>=6?8:k==5?4:2;
        unsigned width=ScWorldWidth(&world)/divisor,height=ScWorldHeight(&world)/divisor;
        unsigned x=point==0?0:point==1?width/2:width-1,y=point==0?0:point==1?height/2:height-1;
        for(unsigned f=0;f<17;++f) for(unsigned i=0;i<ScWorldFieldSizeWorld(&world,f);++i)
            world.fields[f][i]=pattern==0?0:pattern==1?255:(i*197+71)&255;
        for(unsigned i=0;i<ScWorldCells(&world);++i) {
            unsigned tile=pattern==0?0:pattern==1?0x15:(i*31+57)%958;
            world.tiles[2*i]=(uint8_t)tile;world.tiles[2*i+1]=(uint8_t)(tile>>8);
        }
        memset(ram,pattern<3?0x5a:pattern&1?0xff:0,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=starts[k];cpu->dp=0x1df6;cpu->sp=0x1ffc;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=k>=6;cpu->i=true;cpu->c=cpu->v=true;
        if(pattern>=3) {
            const unsigned bias[]={0,10,65286,65535,300};put(0xc71,bias[pattern-3]);
            unsigned index=y*width+x;
            world.fields[0][index]=pattern==4?120:pattern==6?20:255;
            world.fields[3][index]=pattern==4?200:pattern==6?10:255;
            unsigned coarse=(y/4)*(width/4)+x/4;
            world.fields[11][2*coarse]=pattern==7?200:0;world.fields[11][2*coarse+1]=0;
        }
        put(cpu->dp+(k>=3?0:8),x);put(cpu->dp+(k>=3?2:10),y);
        ScWorldGuestStep(&world,cpu,ram);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        /* Connected helpers can continue beyond the former cell endpoint.
         * Replay their actual bounded clocks with the independent CPU, so
         * every continued cell and restored boundary state is checked. */
        unsigned fast=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,20000);
        assert(fast && fast<=20000);
        Interp816 expected=*cpu;ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
        *expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned cycles=0,steps=0;
        while(cycles<fast) {
            assert(++steps<20000);ScWorldGuestStep(&world,cpu,ram);
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(fast!=cycles || memcmp(&world,expected_world,sizeof world) || memcmp(ram,stencil_expected_ram,sizeof ram))
            fprintf(stderr,"kernel map=%u entry=%x point=%u pattern=%u cycles=%u/%u pc=%x/%x\n",map,starts[k],point,pattern,fast,cycles,cpu->pc,expected.pc);
        if(memcmp(ram,stencil_expected_ram,sizeof ram)) for(unsigned p=0;p<sizeof ram;++p)
            if(ram[p]!=stencil_expected_ram[p]) fprintf(stderr,"RAM %x = %x/%x\n",p,ram[p],stencil_expected_ram[p]);
        if(memcmp(&world,expected_world,sizeof world)) {
            unsigned shown=0;const uint8_t *a=(const uint8_t *)&world,*b=(const uint8_t *)expected_world;
            for(unsigned p=0;p<sizeof world && shown<10;++p) if(a[p]!=b[p]) {fprintf(stderr,"world byte %x = %x/%x\n",p,a[p],b[p]);++shown;}
        }
        assert(fast==cycles && !memcmp(&world,expected_world,sizeof world) && !memcmp(ram,stencil_expected_ram,sizeof ram));
        assert(!memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
        free(expected_world);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);cpu->nmiWanted=true;
        assert(!ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,20000));
    }
    puts("PASS: 648 spatial kernel cells match native registers, flags, cycles, stack, RAM and full fields");
}
static void developed_land_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    unsigned cases=0,maximum=0;
    for(unsigned map=0;map<3;++map) for(unsigned sample=0;sample<256;++sample) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map==2;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        uint32_t random=sample*1664525u+1013904223u;
        unsigned x=sample%4==0?0:sample%4==1?width/2-1:(sample*67)%(width/2);
        unsigned y=sample%4==0?0:sample%4==1?height/2-1:(sample*73)%(height/2);
        unsigned cell=2*y*width+2*x,index=y*(width/2)+x,coarse=(y/2)*(width/4)+x/2;
        const unsigned cells[]={cell,cell+1,cell+width,cell+width+1};
        for(unsigned n=0;n<4;++n) {
            random=random*1664525u+1013904223u;
            unsigned tile=(random>>16)&1023;
            world.tiles[2*cells[n]]=(uint8_t)tile;world.tiles[2*cells[n]+1]=(uint8_t)(0x80|(tile>>8));
        }
        world.fields[1][index]=random;world.fields[2][index]=random>>8;
        world.fields[6][coarse]=random>>16;world.fields[15][coarse]=random>>24;
        world.center_valid=true;world.center_x=sample*31%width;world.center_y=sample*47%height;
        memset(ram,sample&1?0xff:0,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9cdf;cpu->dp=sample&2?0x1df6:0x1e00;cpu->sp=0x1ffc;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=sample&4;cpu->i=true;cpu->c=sample&8;cpu->v=sample&16;
        ram[0xbab]=sample*3;ram[0xbac]=sample*7;
        put(cpu->dp+8,x);put(cpu->dp+10,y);ScWorldGuestStep(&world,cpu,ram);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cycles=0;
        while(cpu->pc!=0x9dc9) {
            ScWorldGuestStep(&world,cpu,ram);if(cpu->pc==0x9dc9) break;
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(cycles>maximum) maximum=cycles;
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,20000);
        if(cost!=cycles || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
            memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"land map=%u sample=%u cycles=%u/%u flags=%x/%x A=%x/%x\n",map,sample,cost,cycles,interp816_getFlags(cpu),interp816_getFlags(&expected),cpu->a,expected.a);
        assert(cost==cycles && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u developed land cells match the ROM oracle (maximum %u cycles)\n",cases,maximum);
}
static void land_begin_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0,native=0;
    const unsigned budgets[]={128,256,512,768,1400};
    const unsigned tiles[]={0,0x15,0x28,0x40,0x7f,0x84,0x201,0x2bf,0x307,0x310,0x353,0x354,0x364,0x3ff};
    for(unsigned map=0;map<4;++map) for(unsigned sample=0;sample<64;++sample)
    for(unsigned budget=0;budget<sizeof budgets/sizeof *budgets;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=sample%4==0?0:sample%4==1?width/2-1:(sample*67)%(width/2);
        unsigned y=sample%4==0?0:sample%4==1?height/2-1:(sample*73)%(height/2);
        unsigned cell=2*y*width+2*x,coarse=(y/2)*(width/4)+x/2;
        const unsigned cells[]={cell,cell+1,cell+width,cell+width+1};
        for(unsigned n=0;n<4;++n) {
            unsigned tile=tiles[(sample+n)%14];
            world.tiles[2*cells[n]]=tile;world.tiles[2*cells[n]+1]=0x80|(tile>>8);
        }
        world.fields[15][coarse]=sample*91;
        memset(ram,sample&1?0xff:0,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9cdf;cpu->dp=sample&2?0x1df6:0x1e00;cpu->sp=0x1ffc;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=sample&4;cpu->i=true;cpu->c=sample&8;cpu->v=sample&16;
        put(cpu->dp+8,x);put(cpu->dp+10,y);ScWorldGuestStep(&world,cpu,ram);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));continue;}
        if(cpu->pc==0x9d30) ++native;
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cycles<cost) {
            assert(++guard<1000);uint16_t pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);if(pc!=cpu->pc) continue;
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"land begin map=%u sample=%u budget=%u cycles=%u/%u pc=%x/%x flags=%x/%x A=%x/%x X=%x/%x\n",
                map,sample,budgets[budget],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a,actual.x,cpu->x);
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    assert(native>256);free(expected_world);printf("PASS: %u bounded land-statistics spans (%u complete C beginnings) against original ROM\n",cases,native);
}
static void transport_family_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world),*span_initial_world=malloc(sizeof world),*span_actual_world=malloc(sizeof world);
    uint8_t *span_initial_ram=malloc(sizeof ram),*span_actual_ram=malloc(sizeof ram);
    assert(expected_world && span_initial_world && span_actual_world && span_initial_ram && span_actual_ram);
    const unsigned starts[]={0xb1a5,0xb26b,0xb2d6,0xb1f5,0xb3b0},budgets[]={31,128,4096};
    unsigned cases=0,spans=0,success=0;
    for(unsigned map=0;map<4;++map) for(unsigned kind=0;kind<5;++kind)
    for(unsigned point=0;point<5;++point) for(unsigned pattern=0;pattern<5;++pattern)
    for(unsigned align=0;align<2;++align) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=point==0?0:point==1?width-1:point==2?127:point==3?256%width:width/2;
        unsigned y=point==0?0:point==1?height-1:point==2?127%height:point==3?256%height:height/2;
        unsigned type=point%3,destination=type?0x90:0x140;
        for(int yy=(int)y-35;yy<=(int)y+35;++yy) for(int xx=(int)x-35;xx<=(int)x+35;++xx) {
            unsigned tile=pattern==0?0:pattern==1?0x30:pattern==2?((xx+yy)%7==0?destination:0x30):
                pattern==3?((xx+yy)%7==0?destination:0x6d):0x60;
            ScWorldPutCell(&world,xx,yy,tile|0xc000);
        }
        memset(world.fields[4],pattern==0?0:pattern==1?239:pattern==2?240:pattern==3?255:199,ScWorldFieldSizeWorld(&world,4));
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=starts[kind];cpu->dp=align?0x1df6:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=cpu->mf=false;cpu->i=cpu->c=cpu->v=true;
        cpu->a=type;cpu->x=point*47;cpu->y=pattern*139;put(cpu->sp+1,0x6fff);
        world.coord[2][0]=x;world.coord[2][1]=y;ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;
        put(0xc55,2*type);put(0xc53,5);put(0xc13,kind==3?3:0);
        for(unsigned i=0;i<32;++i) {ram[0xc15+i]=(uint8_t)x;ram[0xc34+i]=(uint8_t)y;}
        for(unsigned i=0;i<6;++i) put(0x59+2*i,point*197+pattern*71+i*139+1);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned expected_cycles=0,guard=0;
        while(cpu->pc!=0x7000) {
            assert(++guard<100000);atomic_instruction_equivalence();ScWorldGuestStep(&world,cpu,ram);
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);expected_cycles+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        if(cpu->a==1) ++success;
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0;guard=0;
            while(cpu->pc!=0x7000) {
                assert(++guard<100000);ScWorldGuestStep(&world,cpu,ram);
                bool audit=align==0 && b==0 && guard%7==0;
                Interp816 before=*cpu;
                if(audit) {*span_initial_world=world;memcpy(span_initial_ram,ram,sizeof ram);}
                unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);
                if(cost && audit) {
                    Interp816 actual=*cpu;*span_actual_world=world;memcpy(span_actual_ram,ram,sizeof ram);
                    *cpu=before;world=*span_initial_world;memcpy(ram,span_initial_ram,sizeof ram);unsigned elapsed=0,check=0;
                    while(elapsed<cost) {
                        assert(++check<10000);ScWorldGuestStep(&world,cpu,ram);
                        ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);
                    }
                    if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(&world,span_actual_world,sizeof world) || memcmp(ram,span_actual_ram,sizeof ram)) {
                        fprintf(stderr,"transport span map=%u kind=%u before=%x budget=%u clocks=%u/%u pc=%x/%x flags=%x/%x\n",map,kind,before.pc,budgets[b],cost,elapsed,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu));abort();
                    }
                    *cpu=actual;world=*span_actual_world;memcpy(ram,span_actual_ram,sizeof ram);++spans;
                }
                if(!cost) {ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cost=interp816_runOpcode(cpu);}
                cycles+=cost;
            }
            if(cycles!=expected_cycles || memcmp(cpu,&expected,sizeof expected) || memcmp(&world,expected_world,sizeof world) || memcmp(ram,stencil_expected_ram,sizeof ram)) {
                fprintf(stderr,"transport full map=%u kind=%u point=%u pattern=%u align=%u budget=%u clocks=%u/%u A=%x/%x X=%x/%x Y=%x/%x DP=%x/%x SP=%x/%x flags=%x/%x\n",
                    map,kind,point,pattern,align,budgets[b],cycles,expected_cycles,cpu->a,expected.a,cpu->x,expected.x,cpu->y,expected.y,cpu->dp,expected.dp,cpu->sp,expected.sp,interp816_getFlags(cpu),interp816_getFlags(&expected));
                for(unsigned p=0,shown=0;p<sizeof ram && shown<12;++p) if(ram[p]!=stencil_expected_ram[p]) {fprintf(stderr,"ram %x = %x/%x\n",p,ram[p],stencil_expected_ram[p]);++shown;}
                const uint8_t *actual=(const uint8_t *)&world,*want=(const uint8_t *)expected_world;
                for(unsigned p=0,shown=0;p<sizeof world && shown<8;++p) if(actual[p]!=want[p]) {fprintf(stderr,"world %x = %x/%x\n",p,actual[p],want[p]);++shown;}
                abort();
            }
            ++cases;
        }
    }
    fprintf(stderr,"transport coverage: cases=%u spans=%u success=%u\n",cases,spans,success);
    assert(success>100 && spans>100);free(expected_world);free(span_initial_world);free(span_actual_world);free(span_initial_ram);free(span_actual_ram);
    printf("PASS: %u complete bounded transport calls, %u intermediate spans, %u successful routes/destinations match original ROM CPU/RAM/world/clocks\n",cases,spans,success);
}
static void power_neighbor_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    for(unsigned search=0;search<2;++search)
    for(unsigned map=0;map<2;++map) for(unsigned direction=0;direction<(search?5:4);++direction)
    for(unsigned point=0;point<3;++point) for(unsigned pattern=0;pattern<12;++pattern) {
        ScWorldReset(&world);world.active=world.huge=true;world.giant=map!=0;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=point==0?0:point==1?width/2:width-1,y=point==0?0:point==1?height/2:height-1;
        world.coord[2][0]=x;world.coord[2][1]=y;
        unsigned tile=pattern%3==0?0x14:pattern%3==1?0x60:0x84;
        for(unsigned i=0;i<ScWorldCells(&world);++i) {world.tiles[2*i]=tile;world.tiles[2*i+1]=tile>>8;}
        memset(world.fields[5],pattern&2?255:0,ScWorldFieldSizeWorld(&world,5));
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=search?0xb03d:0xb06c;cpu->dp=pattern&4?0x1e00:0x1ef5;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=true;cpu->c=pattern&1;cpu->v=pattern&2;
        cpu->a=direction;ram[0xb85]=x;ram[0xb86]=y;put(0xb89,pattern>=8?0x28c:pattern>=4?0x27c:tile);
        put(cpu->dp+2,pattern%3);put(cpu->dp+4,direction);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cycles=0;
        do {
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            cycles+=interp816_runOpcode(cpu);
        } while(search?(cpu->pc!=0xb03d && cpu->pc!=0xb05a):(cpu->pc!=0xb099 && cpu->pc!=0xb0a2));
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        const unsigned budgets[]={cycles,1000};
        for(unsigned budget_case=0;budget_case<2;++budget_case) {
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budgets[budget_case]);
        if(cost!=cycles || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
            memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"power search=%u map=%u dir=%u point=%u pattern=%u cycles=%u/%u pc=%x/%x flags=%x/%x\n",
                search,map,direction,point,pattern,cost,cycles,cpu->pc,expected.pc,interp816_getFlags(cpu),interp816_getFlags(&expected));
        assert(cost==cycles && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
        }
    }
    free(expected_world);printf("PASS: %u native C power neighbours/search iterations match the ROM oracle\n",cases);
}
static void service_copy_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned budgets[]={32,66,5000};
    for(unsigned map=0;map<4;++map) for(unsigned field=0;field<2;++field)
    for(unsigned point=0;point<3;++point) for(unsigned budget=0;budget<3;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned size=ScWorldFieldSizeWorld(&world,16),x=point==0?0:point==1?size/2:size-2;
        for(unsigned i=0;i<size;++i) world.fields[16][i]=(i*173+57)&255;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=field?0xa23a:0xa1bb;cpu->dp=0x1ef5;cpu->sp=0x1f75;
        cpu->x=x;cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=cpu->c=cpu->v=true;
        world.field_anchor[2]=size/2-1;world.field_scan=x;
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        assert(cost && cost<=budgets[budget]);
        Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0;
        while(cycles<cost) {
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            cycles+=interp816_runOpcode(cpu);
        }
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u bounded native C coverage copies match all state and cycles\n",cases);
}
static void power_publish_equivalence(void) {
    uint8_t actual[1040],expected[1040],bitmap[65];unsigned cases=0;
    for(unsigned bits=0;bits<256;++bits) for(unsigned first=0;first<16;++first)
    for(unsigned tail=0;tail<16;++tail) {
        for(unsigned i=0;i<sizeof actual;++i) actual[i]=(uint8_t)(i*37+bits);
        memcpy(expected,actual,sizeof actual);memset(bitmap,bits,sizeof bitmap);
        unsigned end=520-tail;
        for(unsigned i=first;i<end;++i) expected[2*i+1]=(expected[2*i+1]&127)|(bitmap[i/8]&(128>>(i&7))?128:0);
        ScWorldPublishPower(NULL,actual,bitmap,first,end);
        assert(!memcmp(actual,expected,sizeof actual));++cases;
    }
    uint64_t *before=malloc(SC_WORLD_TILE_CHUNKS*sizeof *before);assert(before);
    uint8_t *dirty=malloc(SC_WORLD_TILE_CHUNKS);assert(dirty);
    for(unsigned map=0;map<4;++map) for(unsigned slice=0;slice<2;++slice) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned cells=ScWorldCells(&world),first=slice?5:0,end=cells-(slice?5:0);
        for(unsigned i=0;i<cells*2;++i) world.tiles[i]=(uint8_t)(i*91+(i>>8));
        for(unsigned i=0;i<cells/8;++i) world.fields[5][i]=(uint8_t)(i*17+5);
        copy=world;memset(dirty,0,SC_WORLD_TILE_CHUNKS);
        for(unsigned i=first;i<end;++i) {
            uint8_t value=(copy.tiles[2*i+1]&127)|(copy.fields[5][i/8]&(128>>(i&7))?128:0);
            if(value!=copy.tiles[2*i+1]) dirty[2*i/SC_WORLD_TILE_CHUNK_BYTES]=1;
            copy.tiles[2*i+1]=value;
        }
        const uint64_t *versions=ScWorldTileRevisions(&world);memcpy(before,versions,SC_WORLD_TILE_CHUNKS*sizeof *before);
        ScWorldPublishPower(&world,world.tiles,world.fields[5],first,end);
        assert(!memcmp(&world,&copy,sizeof world));
        for(unsigned i=0;i<SC_WORLD_TILE_CHUNKS;++i) assert(dirty[i]?versions[i]>before[i]:versions[i]==before[i]);
        memcpy(before,versions,SC_WORLD_TILE_CHUNKS*sizeof *before);
        ScWorldPublishPower(&world,world.tiles,world.fields[5],first,end);
        assert(!memcmp(before,versions,SC_WORLD_TILE_CHUNKS*sizeof *before));++cases;
    }
    free(dirty);free(before);
    printf("PASS: %u bulk power publications match scalar bits, partial ranges, metadata and exact changed-chunk invalidation\n",cases);
}
static void power_continuation_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world),*actual_world=malloc(sizeof world);
    uint8_t *actual_ram=malloc(sizeof ram);assert(expected_world && actual_world && actual_ram);
    unsigned cases=0,spans=0,yields=0,coverage[65536]={0};
    const unsigned budgets[]={1,7,31,128,4096};
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<5;++point)
    for(unsigned pattern=0;pattern<5;++pattern) for(unsigned align=0;align<2;++align) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=point==0?0:point==1?width-1:point==2?127:point==3?width/2:255%width;
        unsigned y=point==0?0:point==1?height-1:point==2?127%height:point==3?height/2:255%height;
        for(int yy=(int)y-3;yy<=(int)y+3;++yy) for(int xx=(int)x-3;xx<=(int)x+3;++xx)
            ScWorldPutCell(&world,xx,yy,pattern==0?0:pattern==2 && (xx+yy)%3==0?0:0x8090);
        if(world.huge) {world.fields[17][2]=x;world.fields[17][3]=x>>8;world.fields[18][2]=y;world.fields[18][3]=y>>8;}
        else {world.fields[17][1]=x;world.fields[18][1]=y;}
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0xaffd;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1ff1;
        cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=true;cpu->c=pattern&1;cpu->v=pattern&2;
        put(0xc57,1);put(0xc59,0);put(0xb89,pattern==3?0x27c:0x90);
        put(cpu->dp+18,pattern==4?65535:0);put(cpu->dp+20,0);
        put(cpu->dp+14,pattern==4?0:65535);put(cpu->dp+16,pattern==4?0:15);
        put(cpu->sp+1,0x1e18);put(cpu->sp+3,0x6fff);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned clocks=0,steps=0;
        while(cpu->pc!=0x7000) {
            atomic_instruction_equivalence();
            assert(++steps<40000);
            if(ScPowerTraversalOwns(cpu->pc) && (coverage[cpu->pc]<2 || steps%211==pattern)) {
                unsigned budget=budgets[(steps+pattern+point+map)%5];Interp816 before=*cpu;
                *expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
                unsigned cost=ScPowerTraversalStep(&world,cpu,ram,rom,budget);assert(cost<=budget);
                Interp816 actual=*cpu;
                if(!cost) {
                    assert(!memcmp(&before.a,&cpu->a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
                    assert(!memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));++yields;
                } else {
                    *actual_world=world;memcpy(actual_ram,ram,sizeof ram);
                    *cpu=before;world=*expected_world;memcpy(ram,stencil_expected_ram,sizeof ram);
                    unsigned elapsed=0;
                    while(elapsed<cost) {ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);}
                    if(elapsed!=cost || memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,actual_world,sizeof world)) {
                        fprintf(stderr,"power map=%u point=%u pattern=%u pc=%x budget=%u clocks=%u/%u end=%x/%x flags=%x/%x\n",map,point,pattern,before.pc,budget,elapsed,cost,cpu->pc,actual.pc,interp816_getFlags(cpu),interp816_getFlags(&actual));
                        abort();
                    }
                    ++spans;
                }
                ++coverage[before.pc];*cpu=before;world=*expected_world;memcpy(ram,stencil_expected_ram,sizeof ram);
            }
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);clocks+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0;
        while(cpu->pc!=0x7000) {
            unsigned cost=ScPowerTraversalStep(&world,cpu,ram,rom,4096);
            if(!cost) {ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cost=interp816_runOpcode(cpu);}
            cycles+=cost;
        }
        assert(clocks==cycles && !memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
        assert(!memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));++cases;
    }
    unsigned boundaries=0;for(unsigned pc=0;pc<65536;++pc) boundaries+=coverage[pc]!=0;
    assert(boundaries>75);free(actual_ram);free(actual_world);free(expected_world);
    printf("PASS: %u complete power traversals, %u interrupted spans, %u immutable yields, %u instruction boundaries\n",cases,spans,yields,boundaries);
}
static void density_family_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world),*actual_world=malloc(sizeof world);
    uint8_t *actual_ram=malloc(sizeof ram);assert(expected_world && actual_world && actual_ram);
    unsigned cases=0,spans=0,yields=0,coverage[65536]={0};
    const unsigned budgets[]={1,7,31,128,4096};
    const unsigned tiles[]={0,0x84,0x99,0x144,0x201,0x376};
    for(unsigned map=0;map<4;++map) for(unsigned stage=0;stage<5;++stage)
    for(unsigned pattern=0;pattern<6;++pattern) for(unsigned align=0;align<2;++align) {
        if((stage==2 || stage==3) && pattern) continue; /* Whole-field data already covers all byte values. */
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=width-3,y=height-1;
        for(unsigned xx=x;xx<width;++xx) ScWorldPutCell(&world,xx,y,tiles[pattern]);
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=stage==0?0x9af7:stage==1?0x9bd5:stage==2?0x9b75:stage==3?0x9c11:0x9b90;
        cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1ff1;cpu->a=0x4321;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=stage==2 || stage==4;cpu->i=true;cpu->c=pattern&1;cpu->v=pattern&2;
        put(0xb89,tiles[pattern]);put(cpu->dp+16,x);put(cpu->dp+18,y);
        put(cpu->dp,65530);put(cpu->dp+2,1);put(cpu->dp+4,65530);put(cpu->dp+6,2);
        put(cpu->dp+8,65535);put(cpu->dp+10,0);put(cpu->sp+1,0x6fff);
        unsigned end=stage==0?0x9b6c:stage==1 || stage==4?0x7000:stage==2?0x9b8d:0x9c3b;
        if(stage==4) {
            put(cpu->dp+8,pattern?17:0);put(cpu->dp+10,0);
            world.center_x=width/2;world.center_y=height/2;world.center_valid=true;
            put(cpu->sp+1,0x1e18);put(cpu->sp+3,0x6fff);
        }
        if(stage==2) for(unsigned n=0;n<ScWorldFieldSizeWorld(&world,14);++n) world.fields[14][n]=(uint8_t)(n*197+pattern);
        if(stage==3) memset(world.fields[15],0xa5,ScWorldFieldSizeWorld(&world,15));
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned clocks=0,steps=0;
        while(cpu->pc!=end) {
            atomic_instruction_equivalence();
            /* Huge's final advance is a zero-clock redirect. Stop at its
             * preceding instruction boundary, before entering smoothing. */
            if(stage==0 && world.huge && cpu->pc==0x9b5c &&
               (ram[cpu->dp+16]|(unsigned)ram[cpu->dp+17]<<8)==width-1 &&
               (ram[cpu->dp+18]|(unsigned)ram[cpu->dp+19]<<8)==height-1) break;
            if(steps==19999999) fprintf(stderr,"density oracle runaway map=%u stage=%u pattern=%u pc=%x x=%x dp=%x field_scan=%u end=%x\n",map,stage,pattern,cpu->pc,cpu->x,cpu->dp,world.field_scan,end);
            assert(++steps<20000000);
            if(ScDensityOwns(cpu->pc) && (coverage[cpu->pc]<2 ||
               ((cpu->x==65534 || cpu->x==65535) && coverage[cpu->pc]<12))) {
                unsigned budget=budgets[(steps+pattern+stage+map)%5];Interp816 before=*cpu;
                *expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
                unsigned cost=ScDensityStep(&world,cpu,ram,rom,budget);assert(cost<=budget);
                Interp816 actual=*cpu;
                if(!cost) {
                    assert(!memcmp(&before.a,&cpu->a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
                    assert(!memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));++yields;
                } else {
                    *actual_world=world;memcpy(actual_ram,ram,sizeof ram);
                    *cpu=before;world=*expected_world;memcpy(ram,stencil_expected_ram,sizeof ram);
                    unsigned elapsed=0;
                    while(elapsed<cost) {ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);}
                    if(elapsed!=cost || memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,actual_world,sizeof world)) {
                        fprintf(stderr,"density map=%u stage=%u pattern=%u pc=%x budget=%u clocks=%u/%u end=%x/%x flags=%x/%x\n",map,stage,pattern,before.pc,budget,elapsed,cost,cpu->pc,actual.pc,interp816_getFlags(cpu),interp816_getFlags(&actual));abort();
                    }
                    ++spans;
                }
                ++coverage[before.pc];*cpu=before;world=*expected_world;memcpy(ram,stencil_expected_ram,sizeof ram);
            }
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);clocks+=interp816_runOpcode(cpu);
        }
        if(stage==0 && world.huge && cpu->pc==0x9b5c) ScWorldGuestStep(&world,cpu,ram);
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0;
        while(cycles<clocks) {
            unsigned cost=ScDensityStep(&world,cpu,ram,rom,clocks-cycles<4096?clocks-cycles:4096);
            if(!cost) {ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cost=interp816_runOpcode(cpu);}
            cycles+=cost;
        }
        if(stage==0 && world.huge && cpu->pc==0x9b5c) ScWorldGuestStep(&world,cpu,ram);
        if(clocks!=cycles || memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1) || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world)) {
            fprintf(stderr,"complete density map=%u stage=%u pattern=%u clocks=%u/%u end=%x/%x flags=%x/%x\n",map,stage,pattern,clocks,cycles,cpu->pc,expected.pc,interp816_getFlags(cpu),interp816_getFlags(&expected));abort();
        }
        ++cases;
        if(!(cases%16)) fprintf(stderr,"[density oracle] %u stages compared\n",cases);
    }
    unsigned boundaries=0;for(unsigned pc=0;pc<65536;++pc) boundaries+=coverage[pc]!=0;
    assert(boundaries>80);free(actual_ram);free(actual_world);free(expected_world);
    printf("PASS: %u complete density stages, %u interrupted spans, %u immutable yields, %u instruction boundaries\n",cases,spans,yields,boundaries);
}
static void land_stage_equivalence(void) {
    ScWorldReset(&world);world.active=true;world.huge=world.giant=world.colossal=true;copy=world;
    unsigned cases=0,yields=0;
    for(unsigned stage=0;stage<2;++stage) for(unsigned tile=0;tile<1028;++tile)
    for(unsigned mode=0;mode<4;++mode) {
        unsigned value=tile<1024?tile:(const unsigned[]){0x8000,0x8364,0xfffe,0xffff}[tile-1024];
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=stage?0x9e0c:0x9dca;cpu->dp=mode&1?0x1ef5:0x1e00;cpu->sp=0x1ff5;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=mode&2;cpu->i=true;cpu->c=mode&1;cpu->v=mode&2;cpu->a=value;
        put(cpu->dp+14,65530);put(cpu->dp+16,65535);put(cpu->dp+34,65530);put(cpu->sp+1,0x6fff);
        Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned clocks=0,steps=0,end=stage?0x7000:0x9e0b;
        while(cpu->pc!=end) {
            assert(++steps<100);ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            clocks+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;memcpy(stencil_expected_ram,ram,sizeof ram);
        for(unsigned budget_case=0;budget_case<3;++budget_case) {
            unsigned budget=clocks+(budget_case==0?-1:budget_case==1?0:6);
            *cpu=initial;memcpy(ram,bitmap_ram,sizeof ram);
            unsigned cost=ScWorldGuestLandStageStep(&world,cpu,ram,budget);
            if(!budget_case) {
                assert(!cost && !memcmp(&cpu->a,&initial.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
                assert(!memcmp(ram,bitmap_ram,sizeof ram));++yields;
            } else {
                if(cost!=clocks || memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1) || memcmp(ram,stencil_expected_ram,sizeof ram))
                    fprintf(stderr,"land stage=%u tile=%x mode=%u clocks=%u/%u flags=%x/%x\n",stage,value,mode,cost,clocks,interp816_getFlags(cpu),interp816_getFlags(&expected));
                assert(cost==clocks && !memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
                assert(!memcmp(ram,stencil_expected_ram,sizeof ram));
            }
            ++cases;
        }
        if((tile&255)==0) assert(!memcmp(&world,&copy,sizeof world));
    }
    assert(!memcmp(&world,&copy,sizeof world));
    printf("PASS: %u fused land stages and %u immutable deadline rejections match ROM state/clocks\n",cases,yields);
}
static void land_continuation_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    ScWorld *actual_world=malloc(sizeof world);uint8_t *actual_ram=malloc(sizeof ram);
    assert(actual_world && actual_ram);
    unsigned cases=0,audits=0,yields=0,coverage[65536]={0};
    const unsigned tiles[]={0,0x15,0x26,0x40,0x50,0x7f,0x1fc,0x26c,0x2bf,0x307,0x310,0x364};
    const unsigned budgets[]={1,7,31,128,4096};
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<6;++point)
    for(unsigned pattern=0;pattern<12;++pattern) for(unsigned align=0;align<2;++align) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=point==0?0:point==1?width/2-1:point==2?63:point==3?127:point==4?width/4:width/2-2;
        unsigned y=point==0?0:point==1?height/2-1:point==2?height/4:point==3?height/2-2:point==4?height/4+1:1;
        if(x>=width/2) x=width/2-2;
        unsigned index=y*(width/2)+x,coarse=(y/2)*(width/4)+x/2;
        for(unsigned n=0;n<4;++n) ScWorldPutCell(&world,2*x+(n&1),2*y+n/2,tiles[(pattern+n)%12]|(pattern&1?0x8000:0));
        world.fields[15][coarse]=pattern*23;world.fields[6][coarse]=pattern*31;
        world.fields[2][index]=pattern*47;world.fields[1][index]=pattern*83;
        world.center_valid=pattern&1;world.center_x=width/3;world.center_y=height/3;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9cdf;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1ff5;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=pattern&1;cpu->i=true;cpu->c=pattern&2;cpu->v=pattern&4;
        put(cpu->dp+4,pattern%3==0?65530:0x5a5a);
        put(cpu->dp+8,x);put(cpu->dp+10,y);put(cpu->sp+1,0x6fff);
        ram[0xbab]=pattern*19;ram[0xbac]=pattern*11;
        ScWorldGuestStep(&world,cpu,ram);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned clocks=0,steps=0;
        /* Sample real interrupted states throughout every complete call. */
        while(cpu->pc!=0x7000) {
            atomic_instruction_equivalence();
            assert(++steps<3000);
            if(ScLandOwns(cpu->pc) && (coverage[cpu->pc]<2 || steps%37==pattern%37)) {
                unsigned budget=budgets[(steps+pattern+point+map)%5];
                Interp816 before=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
                unsigned cost=ScLandStep(&world,cpu,ram,budget);
                assert(cost<=budget);
                Interp816 actual=*cpu;
                if(!cost) {
                    assert(!memcmp(&before.a,&cpu->a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
                    assert(!memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));++yields;
                } else {
                    /* Save output in the shared complete-call snapshot, then
                     * replay precisely that cost from the interrupted input. */
                    *actual_world=world;memcpy(actual_ram,ram,sizeof ram);
                    *cpu=before;world=*expected_world;memcpy(ram,stencil_expected_ram,sizeof ram);
                    unsigned elapsed=0;
                    while(elapsed<cost) {ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);}
                    if(elapsed!=cost || memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,actual_world,sizeof world))
                        fprintf(stderr,"land map=%u point=%u pattern=%u pc=%x budget=%u clocks=%u/%u end=%x/%x flags=%x/%x\n",map,point,pattern,before.pc,budget,elapsed,cost,cpu->pc,actual.pc,interp816_getFlags(cpu),interp816_getFlags(&actual));
                    assert(elapsed==cost && !memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
                    assert(!memcmp(ram,actual_ram,sizeof ram) && !memcmp(&world,actual_world,sizeof world));
                    ++audits;
                }
                ++coverage[before.pc];
                /* Restore the sampled input and advance its real oracle once. */
                *cpu=before;world=*expected_world;memcpy(ram,stencil_expected_ram,sizeof ram);
            }
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            clocks+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned cycles=0;
        while(cpu->pc!=0x7000) {
            unsigned cost=ScLandStep(&world,cpu,ram,4096);
            assert(cost);cycles+=cost;
        }
        assert(clocks==cycles && !memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
        assert(!memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));++cases;
    }
    unsigned boundaries=0;
    for(unsigned pc=0;pc<65536;++pc) boundaries+=coverage[pc]!=0;
    assert(boundaries>100);
    free(actual_ram);free(actual_world);free(expected_world);printf("PASS: %u complete land calls, %u interrupted spans, %u immutable yields, %u instruction boundaries\n",cases,audits,yields,boundaries);
}
static void density_batch_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    const unsigned budgets[]={0,1,31,128,256,1024,8192};
    unsigned cases=0,yields=0,fused=0;
    for(unsigned point=0;point<8;++point) for(unsigned pattern=0;pattern<3;++pattern)
    for(unsigned align=0;align<2;++align) for(unsigned mode=0;mode<2;++mode) {
        ScWorldReset(&world);world.active=world.huge=world.giant=world.colossal=true;
        unsigned width=ScWorldWidth(&world)/4,height=ScWorldHeight(&world)/4;
        unsigned x=point==0?0:point==1?width-2:point==2?width-1:point==3?127:point==4?255:point==5?256:point==6?width-2:width-1;
        unsigned y=point<3?0:point<6?255:height-1;
        for(unsigned i=0;i<width*height;++i) world.fields[15][i]=pattern==0?0:pattern==1?255:(i*197+71)&255;
        memset(world.fields[6],0x5a,width*height);memset(ram,0x5a,sizeof ram);
        memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0xa01d;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=mode;cpu->i=cpu->c=cpu->v=true;
        cpu->a=0xabcd;cpu->x=0x4321;cpu->y=0x5678;put(cpu->dp,x);put(cpu->dp+2,y);
        /* Deliberately snapshot before the zero-clock full-coordinate hook.
         * A successful span must charge the next cell; a rejected one must
         * leave that hook and its coordinate/flag changes entirely pending. */
        Interp816 initial=*cpu;copy=world;memcpy(bitmap_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
            unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);
            assert(cost<=budgets[b]);Interp816 actual=*cpu;*expected_world=world;
            memcpy(stencil_expected_ram,ram,sizeof ram);
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
            while(cycles<cost) {
                assert(++guard<10000);uint16_t pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);
                if(pc!=cpu->pc) continue;
                ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
            }
            if(cycles!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(&world,expected_world,sizeof world) || memcmp(ram,stencil_expected_ram,sizeof ram)) {
                fprintf(stderr,"density batch point=%u pattern=%u align=%u mode=%u budget=%u clocks=%u/%u pc=%x/%x flags=%x/%x A=%x/%x X=%x/%x Y=%x/%x\n",
                    point,pattern,align,mode,budgets[b],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a,actual.x,cpu->x,actual.y,cpu->y);
                abort();
            }
            ++cases;if(!cost) ++yields;if(cost>512) ++fused;
        }
    }
    assert(yields>0 && fused>0);free(expected_world);
    printf("PASS: %u Colossal density batches (%u fused, %u immutable yields), zero-clock advances, seams, row/map ends and ROM state/clocks\n",cases,fused,yields);
}
static void diffusion_begin_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    const unsigned offsets[]={0,2,4,5,7},budgets[]={0,2,6,31,200};
    unsigned cases=0,yields=0;
    for(unsigned map=0;map<4;++map) for(unsigned phase=0;phase<2;++phase)
    for(unsigned point=0;point<4;++point) for(unsigned align=0;align<2;++align) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldFieldWidth(&world,13),height=ScWorldFieldHeight(&world,13);
        unsigned x=point==0?0:point==1?width-1:point==2?width/2:127%width;
        unsigned y=point==0?0:point==1?height-1:point==2?height/2:127%height;
        memset(world.fields[13],point&1?255:0,width*height);
        memset(world.fields[14],point&2?255:0,width*height);
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        unsigned base=phase?0xa0c6:0xa040;
        cpu->k=cpu->db=3;cpu->pc=base;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=point&1;cpu->i=cpu->c=cpu->v=true;
        cpu->a=0xabcd;cpu->x=0x4321;cpu->y=0x5678;put(cpu->dp,x);put(cpu->dp+2,y);
        for(unsigned stage=0;stage<5;++stage) {
            assert(cpu->pc==base+offsets[stage]);ScWorldGuestStep(&world,cpu,ram);
            Interp816 initial=*cpu;copy=world;memcpy(bitmap_ram,ram,sizeof ram);
            for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
                *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
                unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);
                assert(cost<=budgets[b]);Interp816 actual=*cpu;*expected_world=world;
                memcpy(stencil_expected_ram,ram,sizeof ram);
                *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
                while(cycles<cost) {
                    assert(++guard<1000);uint16_t pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);
                    if(pc!=cpu->pc) continue;
                    ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
                }
                if(cycles!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(&world,expected_world,sizeof world) || memcmp(ram,stencil_expected_ram,sizeof ram)) {
                    fprintf(stderr,"diffusion begin map=%u phase=%u point=%u align=%u stage=%u budget=%u clocks=%u/%u pc=%x/%x flags=%x/%x A=%x/%x X=%x/%x SP=%x/%x\n",
                        map,phase,point,align,stage,budgets[b],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a,actual.x,cpu->x,actual.sp,cpu->sp);
                    abort();
                }
                ++cases;if(!cost) ++yields;
            }
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);interp816_runOpcode(cpu);
        }
    }
    assert(yields>0);free(expected_world);
    printf("PASS: %u reachable native smoothing setups (%u immutable tiny-budget yields) match full ROM CPU/RAM/world/clocks\n",cases,yields);
}
static void interpreter_profile_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    const unsigned entries[]={0xa040,0xa040,0x9eb0},budgets[]={32,200,32};
    for(unsigned sample=0;sample<3;++sample) {
        unsigned budget=budgets[sample];
        ScWorldReset(&world);world.active=true;
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);memset(&guest,0,sizeof guest);
        cpu->k=cpu->db=3;cpu->pc=entries[sample];cpu->dp=0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=true;
        put(cpu->dp,0);put(cpu->dp+2,0);put(cpu->dp+8,0);put(cpu->dp+10,0);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        ScWorldGuestInterpreterProfile(false);
        unsigned expected_cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budget);
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);ScWorldGuestInterpreterProfile(true);
        uint64_t program_before=ScProgramCalls();
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budget);
        assert(cost==expected_cost && !memcmp(cpu,&expected,sizeof expected) &&
            !memcmp(&world,expected_world,sizeof world) && !memcmp(ram,stencil_expected_ram,sizeof ram));
        uint64_t total=0;const uint64_t *counts=ScWorldGuestInterpreterCounts();
        for(unsigned pc=0;pc<65536;++pc) total+=counts[pc];
        uint64_t program_edges=ScProgramCalls()-program_before;
        assert(sample==2?(program_edges?total==0:total>0):total==0);
    }
    ScWorldGuestInterpreterProfile(false);free(expected_world);
    puts("PASS: actual interpreter counters distinguish compatibility opcodes from native program edges/C spans without changing CPU/RAM/world/clocks");
}
static void diffusion_stencil_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    const unsigned budgets[]={0,2,5,12,31,200};
    const unsigned offsets[]={0,2,4,6,8,9,13,16,18,19,23,25,27,29,31,32,36,38,40,43,45,46,50,52,54,55,59,61,63};
    unsigned seen[65]={0},cases=0,yields=0;
    /* Snapshot reachable original-ROM states, including carry branches, then
     * compare every bounded C span with the same original instructions. */
    for(unsigned map=0;map<4;++map) for(unsigned phase=0;phase<2;++phase)
    for(unsigned point=0;point<5;++point) for(unsigned pattern=0;pattern<3;++pattern)
    for(unsigned align=0;align<2;++align) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldFieldWidth(&world,13),height=ScWorldFieldHeight(&world,13);
        unsigned x=point==0?0:point==1?width-1:point==2?width/2:point==3?0:width-1;
        unsigned y=point==0?0:point==1?height-1:point==2?height/2:point==3?height-1:0;
        for(unsigned field=13;field<=14;++field) for(unsigned i=0;i<width*height;++i)
            world.fields[field][i]=pattern==0?0:pattern==1?255:(i*197+field*71)&255;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        unsigned base=phase?0xa0d0:0xa04a;
        cpu->k=cpu->db=3;cpu->pc=base;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=cpu->c=cpu->v=true;
        cpu->a=0xabcd;cpu->x=(uint16_t)(y*width+x);cpu->y=0x4321;
        put(cpu->dp,x);put(cpu->dp+2,y);world.field_anchor[0]=y*width+x;
        while(cpu->pc<base+65) {
            assert(cpu->pc>=base);++seen[cpu->pc-base];
            Interp816 initial=*cpu;copy=world;memcpy(bitmap_ram,ram,sizeof ram);
            for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
                *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
                unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);
                assert(cost<=budgets[b]);Interp816 actual=*cpu;
                *expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
                *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
                while(cycles<cost) {
                    assert(++guard<100);uint16_t pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);
                    if(pc!=cpu->pc) continue;
                    ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cycles+=interp816_runOpcode(cpu);
                }
                if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
                   memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
                    fprintf(stderr,"stencil map=%u phase=%u point=%u pattern=%u align=%u stage=%u budget=%u cycles=%u/%u pc=%x/%x flags=%x/%x A=%x/%x Y=%x/%x\n",
                        map,phase,point,pattern,align,initial.pc-base,budgets[b],cost,cycles,actual.pc,cpu->pc,
                        interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a,actual.y,cpu->y);
                assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
                assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
                ++cases;if(cost && cost<32) ++yields;
            }
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);interp816_runOpcode(cpu);
        }
    }
    for(unsigned i=0;i<sizeof offsets/sizeof *offsets;++i) assert(seen[offsets[i]]);
    assert(yields>1000);free(expected_world);
    printf("PASS: %u reachable smoothing spans, all 29 instruction boundaries, %u short native yields match original ROM state and clocks\n",cases,yields);
}
static void diffusion_finish_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned offsets[]={0,2,4,5,6,9,11,14,16},values[]={0,249,1000,1250},budgets[]={32,64};
    for(unsigned map=0;map<4;++map) for(unsigned phase=0;phase<2;++phase)
    for(unsigned stage=0;stage<9;++stage) for(unsigned pattern=0;pattern<4;++pattern)
    for(unsigned align=0;align<2;++align) for(unsigned budget=0;budget<2;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldFieldWidth(&world,13),x=pattern&1?width-1:0,y=ScWorldFieldHeight(&world,13)/2;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=(phase?0xa111:0xa08b)+offsets[stage];
        cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;cpu->e=cpu->xf=cpu->d=false;
        cpu->mf=stage==0 || stage==8;cpu->i=cpu->v=true;cpu->c=(pattern&1)!=0;
        cpu->a=values[pattern];cpu->x=(uint16_t)(y*width+x);cpu->y=y;
        put(cpu->dp,x);put(cpu->dp+2,y);put(cpu->dp+4,values[pattern]);world.field_anchor[0]=y*width+x;
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        assert(cost && cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0;
        while(cycles<cost) {
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"diffusion finish map=%u phase=%u stage=%u value=%u cycles=%u/%u pc=%x/%x flags=%x/%x\n",
                map,phase,offsets[stage],values[pattern],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu));
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u native smoothing finishes match original ROM state and clocks\n",cases);
}
static void terrain_quality_scratch_equivalence(void) {
    ScWorld *actual_world=malloc(sizeof world);assert(actual_world);unsigned cases=0;
    const uint8_t unused[]={0,1,0x5a,0xff};
    for(unsigned map=0;map<5;++map)for(unsigned point=0;point<3;++point)
    for(unsigned align=0;align<2;++align)for(unsigned pattern=0;pattern<4;++pattern) {
        ScWorldReset(&world);world.active=map!=0;world.huge=map>=2;
        world.giant=map>=3;world.colossal=map==4;
        /* The native kernel applies to expanded worlds, including Big. */
        if(!world.active)continue;
        unsigned width=ScWorldWidth(&world)/8,height=ScWorldHeight(&world)/8;
        unsigned x=point==0?0:point==1?width/2:width-1,y=point==2?height-1:height/2;
        world.center_x=ScWorldWidth(&world)/3;world.center_y=ScWorldHeight(&world)/3;world.center_valid=true;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0xa25c;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=true;cpu->c=pattern&1;cpu->v=pattern&2;
        cpu->a=0xabcd;ram[cpu->dp]=x;ram[cpu->dp+2]=y;
        ram[cpu->dp+1]=unused[pattern];ram[cpu->dp+3]=unused[3-pattern];
        ScWorldGuestStep(&world,cpu,ram);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,140);
        assert(cost && cost<=140 && cpu->pc==0xa288);
        Interp816 actual=*cpu;*actual_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned elapsed=0,guard=0;
        while(cpu->pc!=0xa288) {
            assert(++guard<64);ScWorldGuestStep(&world,cpu,ram);
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);
        }
        if(cost!=elapsed || memcmp(&actual,cpu,sizeof actual) || memcmp(ram,stencil_expected_ram,sizeof ram) ||
           memcmp(&world,actual_world,sizeof world))
            fprintf(stderr,"terrain quality map=%u point=%u align=%u scratch=%u clocks=%u/%u flags=%x/%x\n",
                map,point,align,pattern,cost,elapsed,interp816_getFlags(&actual),interp816_getFlags(cpu));
        assert(cost==elapsed && !memcmp(&actual,cpu,sizeof actual));
        assert(!memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,actual_world,sizeof world));++cases;
    }
    free(actual_world);printf("PASS: %u terrain-quality cells with dirty adjacent coordinate scratch match original CPU/RAM/world/clocks on all expanded sizes\n",cases);
}
static void terrain_quality_span_equivalence(void) {
    ScWorld *actual_world=malloc(sizeof world);assert(actual_world);
    unsigned cases=0,fused=0,final=0;
    const unsigned budgets[]={0,139,140,159,239,240,249,250,4096};
    for(unsigned map=0;map<4;++map)for(unsigned point=0;point<4;++point)
    for(unsigned align=0;align<2;++align)for(unsigned pattern=0;pattern<2;++pattern) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;
        world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world)/8,height=ScWorldHeight(&world)/8;
        unsigned x=point==0?0:point==1?width-2:width-1,y=point==3?height-1:height/2;
        world.center_x=ScWorldWidth(&world)/3;world.center_y=ScWorldHeight(&world)/3;world.center_valid=true;
        memset(world.fields[12],0x69,ScWorldFieldSizeWorld(&world,12));
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0xa25c;cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=true;cpu->c=pattern;cpu->v=!pattern;
        cpu->a=0xabcd;cpu->x=0x1234;cpu->y=0x4321;
        ram[cpu->dp]=x;ram[cpu->dp+2]=y;
        ram[cpu->dp+1]=pattern?0xff:1;ram[cpu->dp+3]=pattern?1:0x5a;
        ScWorldGuestStep(&world,cpu,ram);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
            unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);
            assert(cost<=budgets[b]);Interp816 actual=*cpu;*actual_world=world;
            memcpy(stencil_expected_ram,ram,sizeof ram);
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned elapsed=0,guard=0;
            while(elapsed<cost) {
                assert(++guard<cost+512);uint16_t pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);
                if(pc!=cpu->pc)continue;
                ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);
            }
            if(cost!=elapsed || memcmp(&actual,cpu,sizeof actual) || memcmp(ram,stencil_expected_ram,sizeof ram) ||
               memcmp(&world,actual_world,sizeof world))
                fprintf(stderr,"terrain quality span map=%u point=%u align=%u pattern=%u budget=%u clocks=%u/%u pc=%x/%x flags=%x/%x\n",
                    map,point,align,pattern,budgets[b],cost,elapsed,actual.pc,cpu->pc,
                    interp816_getFlags(&actual),interp816_getFlags(cpu));
            assert(cost==elapsed && !memcmp(&actual,cpu,sizeof actual));
            assert(!memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,actual_world,sizeof world));
            if(cost>=250)++fused;if(actual.pc==0xa298)++final;++cases;
        }
    }
    assert(fused && final);free(actual_world);
    printf("PASS: %u terrain-quality spans including %u fused and %u final-field boundaries match original CPU/RAM/world/clocks with dirty scratch\n",cases,fused,final);
}
static void terrain_quality_benchmark(void) {
    ScWorldReset(&world);world.active=world.huge=world.giant=world.colossal=true;
    world.center_x=942;world.center_y=798;world.center_valid=true;
    memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
    cpu->k=cpu->db=3;cpu->dp=0x1ef5;cpu->sp=0x1f75;
    cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=true;
    struct timespec start,end;timespec_get(&start,TIME_UTC);
    uint64_t total=0,spans=0;
    for(unsigned pass=0;pass<256;++pass) {
        cpu->pc=0xa25c;ram[cpu->dp]=ram[cpu->dp+2]=0;
        while(cpu->pc!=0xa298) {
            unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,4096);
            assert(cost && cost<=4096);total+=cost;++spans;
        }
    }
    timespec_get(&end,TIME_UTC);
    double ms=(end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0;
    printf("BENCH: terrain quality cells=12288000 spans=%llu guest-clocks=%llu elapsed-ms=%.3f\n",
        (unsigned long long)spans,(unsigned long long)total,ms);
}
static void field_shadow_benchmark(void) {
    ScWorldReset(&world);world.active=world.huge=world.giant=world.colossal=true;
    test_stencil=malloc(SC_WORLD_FIELD_BYTES*sizeof *test_stencil);assert(test_stencil);
    ScStencilBackend backend={NULL,NULL,test_stencil_data};ScWorldGuestSetStencilBackend(&backend);
    for(unsigned family=0;family<2;++family) {
        unsigned source=family?13:10,width=ScWorldFieldWidth(&world,source),height=ScWorldFieldHeight(&world,source);
        for(unsigned n=0;n<width*height;++n) {
            if(family)world.fields[source][n]=(n*173+71)&255;
            else {unsigned v=(n*173+37171)&65535;world.fields[source][2*n]=v;world.fields[source][2*n+1]=v>>8;}
        }
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            test_stencil[y*width+x]=family?ScStencilPack(world.fields[source],width,height,x,y):ScServicePack(world.fields[source],width,height,x,y);
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->dp=0x1ef5;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=cpu->i=true;
        struct timespec start,end;timespec_get(&start,TIME_UTC);
        uint64_t total=0,spans=0;
        for(unsigned pass=0;pass<(family?16:256);++pass) {
            cpu->pc=family?0xa040:0xa164;cpu->mf=true;put(cpu->dp,0);put(cpu->dp+2,0);
            while(cpu->pc!=(family?0xa0b3:0xa1b6)) {
                unsigned cost=family?ScWorldGuestSmoothingStageStep(&world,cpu,ram,4096):ScWorldGuestServiceStageStep(&world,cpu,ram,4096);
                assert(cost && cost<=4096);total+=cost;++spans;
            }
        }
        timespec_get(&end,TIME_UTC);
        double ms=(end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0;
        printf("BENCH: %s cells=12288000 spans=%llu guest-clocks=%llu elapsed-ms=%.3f\n",
            family?"pollution":"service",(unsigned long long)spans,(unsigned long long)total,ms);
    }
    ScWorldGuestSetStencilBackend(NULL);free(test_stencil);test_stencil=NULL;
}
static ScCrimeSample *crime_samples;
static unsigned crime_bias;
static const ScCrimeSample *crime_test_data(void *context,const ScWorld *w,unsigned bias) {
    (void)context;(void)w;return bias==crime_bias?crime_samples:NULL;
}
static void crime_gpu_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    crime_samples=calloc(SC_WORLD_FIELD_BYTES,sizeof *crime_samples);assert(crime_samples);
    ScStencilBackend backend={.crime_data=crime_test_data};
    unsigned cases=0,used=0,small=0;
    const unsigned budgets[]={1,32,250,4096};
    const unsigned lands[]={0,1,127,128,129,255,5,80,240,100,20,255};
    const unsigned densities[]={0,255,128,200,250,255,0,50,250,255,10,255};
    const unsigned coverages[]={0,0,0,50,300,65535,0,100,250,0,0,80};
    const unsigned biases[]={0,10,65535,65286,300,32767,32768,65535,0,65000,0,0};
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<3;++point)
    for(unsigned align=0;align<2;++align) for(unsigned pattern=0;pattern<12;++pattern) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>1;world.colossal=map>2;
        unsigned width=ScWorldWidth(&world)/2,height=ScWorldHeight(&world)/2;
        unsigned x=point==0?0:point==1?width-2:width-1,y=point==2?height-1:height/2;
        memset(world.fields[0],lands[pattern],width*height);
        memset(world.fields[3],densities[pattern],width*height);
        for(unsigned n=0;n<width*height/16;++n) {
            world.fields[11][2*n]=(uint8_t)coverages[pattern];world.fields[11][2*n+1]=(uint8_t)(coverages[pattern]>>8);
        }
        crime_bias=biases[pattern];
        for(unsigned at=y*width+x;at<width*height && at<y*width+x+64;++at)
            crime_samples[at]=ScCrimePack(lands[pattern],densities[pattern],coverages[pattern],crime_bias);
        /* A source changed after dispatch must reject that sample and still
         * match the original CPU through the normal C fallback. */
        if(pattern==11) {unsigned coarse=(y/4)*(width/4)+x/4;world.fields[11][2*coarse]^=1;}
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9eb0;cpu->dp=align?0x1eeb:0x1e00;cpu->sp=0x1f79;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=false;cpu->i=true;cpu->c=cpu->v=true;
        cpu->a=0x31ea;cpu->x=0x1234;cpu->y=0x9876;
        put(cpu->dp+8,x);put(cpu->dp+10,y);put(0xc71,crime_bias);
        put(cpu->dp,65535);put(cpu->dp+4,65532);put(cpu->dp+6,65535);
        ScWorldGuestStep(&world,cpu,ram);copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
            ScWorldGuestSetStencilBackend(&backend);
            unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);
            used+=ScWorldGuestCrimeGpuCells()>0;small+=cost && budgets[b]<400;
            Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
            ScWorldGuestSetStencilBackend(NULL);
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
            unsigned clocks=0,steps=0;
            while(clocks<cost) {
                assert(++steps<10000);unsigned pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);
                if(pc!=cpu->pc) continue;
                ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);clocks+=interp816_runOpcode(cpu);
            }
            if(clocks!=cost || memcmp(&world,expected_world,sizeof world) || memcmp(ram,stencil_expected_ram,sizeof ram) ||
               memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
                fprintf(stderr,"crime GPU map=%u point=%u align=%u pattern=%u budget=%u clocks=%u/%u pc=%x/%x flags=%x/%x\n",
                    map,point,align,pattern,budgets[b],cost,clocks,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu));
            assert(clocks==cost && cost<=budgets[b]);
            assert(!memcmp(&world,expected_world,sizeof world) && !memcmp(ram,stencil_expected_ram,sizeof ram));
            assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
        }
    }
    assert(used>100 && small>100);free(expected_world);free(crime_samples);crime_samples=NULL;
    printf("PASS: %u crime publication/original-ROM comparisons, %u GPU spans, %u short spans; all expanded sizes, wraps, seams, stale sources and exact clocks\n",cases,used,small);
}
static ScLandSummary *land_samples;
static const ScLandSummary *land_test_data(void *context,const ScWorld *w) {
    (void)context;(void)w;return land_samples;
}
static void land_gpu_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);
    land_samples=calloc(SC_WORLD_FIELD_BYTES,sizeof *land_samples);assert(land_samples);
    ScStencilBackend backend={.land_data=land_test_data};unsigned cases=0,used=0,small=0;
    const unsigned budgets[]={1,32,128,512,700,1000,1500,4096};
    const uint16_t kinds[]={0,0x15,0x364,0x2bf,0x307,0x310,0x353,0x354,0x7f,0x50,0x3bf,1023,0x28,0x2fd,0x40};
    for(unsigned entry=0;entry<2;++entry)
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<3;++point)
    for(unsigned align=0;align<2;++align) for(unsigned pattern=0;pattern<15;++pattern) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>1;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),half=width/2,height=ScWorldHeight(&world)/2;
        unsigned x=point==0?0:half-2,y=point==2?height-1:height/2;
        for(unsigned n=0;n<64 && y*half+x+n<half*height;++n) {
            unsigned at=y*half+x+n,offset=2*(at/half)*width+2*(at%half);
            unsigned positions[]={offset,offset+1,offset+width,offset+width+1};uint16_t tiles[4];
            for(unsigned t=0;t<4;++t) {
                tiles[t]=pattern<2?kinds[pattern]:kinds[(pattern+t)%15]|((t&1)?0xc000:0);
                world.tiles[2*positions[t]]=(uint8_t)tiles[t];world.tiles[2*positions[t]+1]=(uint8_t)(tiles[t]>>8);
            }
            land_samples[at]=ScLandSummaryPack(tiles);
        }
        if(pattern>=11) {
            unsigned positions[]={2*y*width+2*x,2*y*width+2*x+1,(2*y+1)*width+2*x,(2*y+1)*width+2*x+1};
            world.tiles[2*positions[pattern-11]+1]^=0x20;
        }
        memset(world.fields[15],0xfe,half*height/4);memset(world.fields[6],0x80,half*height/4);
        memset(world.fields[2],pattern*17,half*height);memset(world.fields[1],pattern*19,half*height);
        world.center_valid=true;world.center_x=width/2;world.center_y=ScWorldHeight(&world)/2;
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=entry?0x9c3b:0x9cdf;cpu->dp=align?0x1eeb:0x1e00;cpu->sp=0x1f79;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=true;cpu->i=true;cpu->c=cpu->v=true;
        cpu->a=0x31ea;cpu->x=0x1234;cpu->y=0x9876;
        put(cpu->dp+8,x);put(cpu->dp+10,y);put(cpu->dp+4,65532);put(cpu->dp+6,65535);
        ScWorldGuestStep(&world,cpu,ram);copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);ScWorldGuestSetStencilBackend(&backend);
            unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[b]);
            used+=ScWorldGuestLandGpuCells()>0;small+=cost && budgets[b]<1500;
            /* The fused call can eagerly retire the zero-clock full-map
             * coordinate hook. Compare both paths at the prepared boundary. */
            if(world.huge && cpu->pc==0x9c40) ScWorldGuestStep(&world,cpu,ram);
            Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
            ScWorldGuestSetStencilBackend(NULL);*cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
            unsigned clocks=0,steps=0;
            while(clocks<cost) {
                assert(++steps<20000);unsigned pc=cpu->pc;ScWorldGuestStep(&world,cpu,ram);if(pc!=cpu->pc) continue;
                ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);clocks+=interp816_runOpcode(cpu);
            }
            if(world.huge && cpu->pc==0x9c40) ScWorldGuestStep(&world,cpu,ram);
            if(clocks!=cost || memcmp(&world,expected_world,sizeof world) || memcmp(ram,stencil_expected_ram,sizeof ram) ||
               memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
                fprintf(stderr,"land GPU map=%u point=%u align=%u pattern=%u budget=%u clocks=%u/%u pc=%x/%x flags=%x/%x\n",
                    map,point,align,pattern,budgets[b],cost,clocks,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu));
            assert(clocks==cost && cost<=budgets[b]);
            assert(!memcmp(&world,expected_world,sizeof world) && !memcmp(ram,stencil_expected_ram,sizeof ram));
            assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
        }
    }
    assert(used>100 && small>100);free(expected_world);free(land_samples);land_samples=NULL;
    printf("PASS: %u land summary publication/original-ROM comparisons, %u GPU spans, %u short spans; all expanded sizes, flags, seams, stale tiles and exact clocks\n",cases,used,small);
}
static void spatial_batch_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned starts[]={0x9c3b,0x9c77,0x9eb0,0xa040,0xa0c6,0x9fb7,0xa164,0xa1e3,0xa25c,0xa04a,0xa0d0,0xa09f,0xa125};
    const unsigned budgets[]={32,48,64,80,128,160,200,249,250,500,2000,8000};
    for(unsigned map=0;map<4;++map) for(unsigned kind=0;kind<sizeof starts/sizeof *starts;++kind)
    for(unsigned point=0;point<3;++point) for(unsigned pattern=0;pattern<4;++pattern)
    for(unsigned budget=0;budget<sizeof budgets/sizeof *budgets;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned divisor=kind>=6 && kind<9?8:kind==5?4:2,width=ScWorldWidth(&world)/divisor,height=ScWorldHeight(&world)/divisor;
        unsigned x=point==0?0:point==1?width-2:width-1,y=point==2?height-1:height/2;
        for(unsigned f=0;f<17;++f) for(unsigned i=0;i<ScWorldFieldSizeWorld(&world,f);++i)
            world.fields[f][i]=pattern==0?0:pattern==1?255:(i*197+71)&255;
        for(unsigned yy=y*divisor;yy<ScWorldHeight(&world) && yy<(y+1)*divisor;++yy)
        for(unsigned xx=0;xx<ScWorldWidth(&world);++xx) {
            unsigned tile=pattern==0?0:pattern==1?0x15:(xx*31+yy*57)%958;
            ScWorldPutCell(&world,xx,yy,tile);
        }
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=starts[kind];cpu->dp=pattern&1?0x1e00:0x1ef5;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=kind==0 || (kind>=6 && kind<11);cpu->i=cpu->c=cpu->v=true;
        put(cpu->dp+(kind<3?8:0),x);put(cpu->dp+(kind<3?10:2),y);ScWorldGuestStep(&world,cpu,ram);
        if(kind>=9) {
            cpu->x=(uint16_t)(y*width+x);
            world.field_anchor[0]=y*width+x;
        }
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) continue;
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0;
        while(cycles<cost) {
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            cycles+=interp816_runOpcode(cpu);
        }
        if(world.huge && cpu->pc==0x9c40) ScWorldGuestStep(&world,cpu,ram);
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"batch map=%u start=%x point=%u pattern=%u budget=%u cycles=%u/%u pc=%x/%x flags=%x/%x\n",
                map,starts[kind],point,pattern,budgets[budget],cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu));
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u fused native C spatial spans match ROM state at exact beam budgets\n",cases);
}
static void zone_score_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned starts[]={0x99d2,0x9a0e,0x9a31},ends[]={0x9a0d,0x9a30,0x9a3d};
    for(unsigned map=0;map<3;++map) for(unsigned kind=0;kind<3;++kind)
    for(unsigned point=0;point<3;++point) for(unsigned pattern=0;pattern<32;++pattern) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map==2;
        unsigned x=point==0?0:point==1?ScWorldWidth(&world)/2:ScWorldWidth(&world)-1;
        unsigned y=point==0?0:point==1?ScWorldHeight(&world)/2:ScWorldHeight(&world)-1;
        unsigned index=(y/2)*(ScWorldWidth(&world)/2)+x/2,coarse=(y/8)*(ScWorldWidth(&world)/8)+x/8;
        world.coord[2][0]=x;world.coord[2][1]=y;
        world.fields[0][index]=pattern*47;world.fields[2][index]=pattern*139;
        world.fields[12][2*coarse]=pattern*73;world.fields[12][2*coarse+1]=pattern*173;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=starts[kind];cpu->dp=0x1ef5;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=true;cpu->c=pattern&1;cpu->v=pattern&2;
        cpu->a=pattern%4==0?0:pattern%4==1?1:pattern%4==2?32768:65535;
        ram[0xb85]=x;ram[0xb86]=y;copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cycles=0;
        while(cpu->pc!=ends[kind]) {
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            cycles+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned cost=ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,1000);
        if(cost!=cycles || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"score map=%u kind=%u point=%u pattern=%u cycles=%u/%u A=%x/%x flags=%x/%x\n",
                map,kind,point,pattern,cost,cycles,cpu->a,expected.a,interp816_getFlags(cpu),interp816_getFlags(&expected));
        assert(cost==cycles && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u native C zone growth scores match ROM registers, flags, cycles and fields\n",cases);
}
static void zone_replacement_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned obstacles[]={0x28,0x40,0x60,0x7f,0x364,0x365};
    const unsigned bases[]={0x80,0x137,0x1fc,0x3ff,0x7fff,0xffff};
    for(unsigned map=0;map<3;++map) for(unsigned point=0;point<8;++point)
    for(unsigned pattern=0;pattern<24;++pattern) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map==2;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        int x=point<4?(point&1?width-1:0):point==4?127:point==5?255:point==6?width/2:width-2;
        int y=point<4?(point&2?height-1:0):point==4?128:point==5?256:point==6?height/2:height-2;
        if(y>=height) y=height-2;if(x>=width) x=width-2;
        world.coord[2][0]=x;world.coord[2][1]=y;
        for(unsigned n=0;n<9;++n) {
            unsigned tile=pattern>=8 && n==pattern%9?obstacles[pattern%6]:pattern%4==0?0x15:pattern%4==1?0x84:pattern%4==2?0x137:0x1fc;
            ScWorldPutCell(&world,x+(int)(n%3)-1,y+(int)(n/3)-1,tile|((n+pattern)%3==0?0x8000:0));
        }
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9940;cpu->dp=pattern&1?0x1e04:0x1ef5;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=pattern&2;cpu->i=true;cpu->c=pattern&1;cpu->v=pattern&2;
        cpu->a=bases[pattern%6];ram[0xb85]=x;ram[0xb86]=y;
        /* Native callers can leave a nearby full-coordinate anchor after a
         * nested scan. The byte proxies still name the zone being replaced. */
        if(world.huge && (pattern&1)) {world.coord[2][0]+=11;world.coord[2][1]-=12;}
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);unsigned cycles=0;
        while(cpu->pc!=0x99be) {
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            cycles+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,3000);
        if(cost!=cycles || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"replace map=%u point=%u pattern=%u cycles=%u/%u A=%x/%x flags=%x/%x\n",
                map,point,pattern,cost,cycles,cpu->a,expected.a,interp816_getFlags(cpu),interp816_getFlags(&expected));
        assert(cost==cycles && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);
        assert(!ScWorldGuestKernelStep(&world,cpu,ram,rom,sizeof rom,31));
        assert(!memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));
        while(cpu->pc!=0x99be) {
            Interp816 before=*cpu;copy=world;memcpy(bitmap_ram,ram,sizeof ram);
            unsigned span=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,120);
            assert(span && span<=120);
            Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
            *cpu=before;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned reference=0;
            while(reference<span) {
                ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
                reference+=interp816_runOpcode(cpu);
            }
            if(reference!=span || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
               memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
                fprintf(stderr,"replace span map=%u point=%u pattern=%u start=%x cycles=%u/%u pc=%x/%x A=%x/%x flags=%x/%x\n",
                    map,point,pattern,before.pc,span,reference,actual.pc,cpu->pc,actual.a,cpu->a,interp816_getFlags(&actual),interp816_getFlags(cpu));
            if(memcmp(ram,stencil_expected_ram,sizeof ram)) for(unsigned p=0;p<sizeof ram;++p)
                if(ram[p]!=stencil_expected_ram[p]) fprintf(stderr,"RAM %x=%x/%x\n",p,stencil_expected_ram[p],ram[p]);
            if(memcmp(&world,expected_world,sizeof world)) {
                const uint8_t *a=(const uint8_t *)&world,*b=(const uint8_t *)expected_world;unsigned shown=0;
                for(unsigned p=0;p<sizeof world && shown<10;++p) if(a[p]!=b[p]) {fprintf(stderr,"world %x=%x/%x\n",p,b[p],a[p]);++shown;}
            }
            assert(reference==span && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
            assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));
        }
    }
    free(expected_world);printf("PASS: %u native C zone replacements match ROM blocked footprints, power, border/seam writes and all state\n",cases);
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
static void spatial_counter_regression(void) {
    for(unsigned map=1;map<4;++map) {
        ScWorldReset(&world);world.active=world.huge=true;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world)/2,height=ScWorldHeight(&world)/2;
        memset(ram,0x75,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->dp=0x1edb;cpu->sp=0x1f79;cpu->e=cpu->xf=cpu->d=false;cpu->i=cpu->mf=true;
        cpu->a=0x5937;cpu->c=cpu->v=true;
        const unsigned starts[]={0x9c37,0x9c39},offsets[]={10,8};
        for(unsigned kind=0;kind<2;++kind) {
            put(cpu->dp+offsets[kind],0x75ff);cpu->pc=starts[kind];Interp816 before=*cpu;
            ScWorldGuestStep(&world,cpu,ram);
            assert(!memcmp(cpu,&before,sizeof before));
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);interp816_runOpcode(cpu);
            assert(ram[cpu->dp+offsets[kind]]==0 && ram[cpu->dp+offsets[kind]+1]==0);
        }
        cpu->pc=0x9c3b;put(cpu->dp+8,29952);put(cpu->dp+10,0x7501);Interp816 before=*cpu;
        ScWorldGuestStep(&world,cpu,ram);
        assert(!memcmp(cpu,&before,sizeof before));assert((ram[cpu->dp+8]|ram[cpu->dp+9]<<8)==0);
        assert((ram[cpu->dp+10]|ram[cpu->dp+11]<<8)==1);
        put(cpu->dp+8,width-1);put(cpu->dp+10,height-1);memcpy(bitmap_ram,ram,sizeof ram);
        ScWorldGuestStep(&world,cpu,ram);assert(!memcmp(ram,bitmap_ram,sizeof ram));
        /* Run the whole land pass with dirty scratch high bytes, rather than
         * beginning tests at a pre-sanitized native cell. Every expanded grid
         * cell must receive land value, including columns and rows over 255. */
        ScWorldReset(&world);world.active=world.huge=true;world.giant=map>=2;world.colossal=map==3;
        for(unsigned i=0;i<ScWorldCells(&world);++i) {world.tiles[2*i]=0x3b;world.tiles[2*i+1]=1;}
        memset(ram,0,sizeof ram);put(cpu->dp+8,0x75ff);put(cpu->dp+10,0x75ff);
        cpu->pc=0x9c35;cpu->mf=false;put(cpu->dp+24,0);
        unsigned guard=0;
        while(cpu->pc!=0x9c50) {
            assert(++guard<2000000);ScWorldGuestStep(&world,cpu,ram);
            unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,8192);
            if(!cost) {ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);interp816_runOpcode(cpu);}
        }
        for(unsigned i=0;i<width*height;++i) assert(world.fields[0][i]!=0);
        assert((ram[cpu->dp+8]|ram[cpu->dp+9]<<8)==0);
        assert((ram[cpu->dp+10]|ram[cpu->dp+11]<<8)==height);
        assert((ram[cpu->dp+24]|ram[cpu->dp+25]<<8)==(uint16_t)(width*height));
        printf("PASS: full land grid %ux%u, dirty high-byte initialization, legacy resume and valid full coordinates\n",width,height);
    }
}
static void coverage_clear_equivalence(void) {
    ScWorld *expected=malloc(sizeof world);assert(expected);unsigned cases=0;
    const unsigned values[]={0,0xffff,0x8000,0x1275},budgets[]={18,19,21,22,32,43,44,2000};
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<4;++point)
    for(unsigned value=0;value<4;++value) for(unsigned budget=0;budget<8;++budget) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned end=ScWorldFieldSizeWorld(&world,10);
        unsigned x=point==0?0:point==1?end/2:point==2?end-2:end>65536?65534:end-4;
        memset(world.fields[10],0x5a,end);memset(world.fields[11],0xa5,end);
        memset(ram,0x75,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x8287;cpu->dp=0x1edb;cpu->sp=0x1f79;
        cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=true;cpu->v=cpu->c=true;
        cpu->a=values[value];cpu->x=(uint16_t)x;cpu->y=0xa137;world.field_scan=x;
        world.field_anchor[2]=end/3;
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {
            assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));
            continue;
        }
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0;
        while(cycles<cost) {
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"coverage clear map=%u point=%u value=%x budget=%u cycles=%u/%u pc=%x/%x X=%x/%x flags=%x/%x scan=%u/%u\n",map,point,values[value],budgets[budget],cost,cycles,actual.pc,cpu->pc,actual.x,cpu->x,interp816_getFlags(&actual),interp816_getFlags(cpu),expected->field_scan,world.field_scan);
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    /* Start at the real initialization boundary, then clear every cell.
     * A stale spatial anchor and X wrapping at 65536 must not lose the tail. */
    for(unsigned map=0;map<4;++map) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned end=ScWorldFieldSizeWorld(&world,10);
        memset(world.fields[10],0x5a,end);memset(world.fields[11],0xa5,end);
        memset(ram,0,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x8281;cpu->dp=0x1edb;cpu->sp=0x1f79;
        cpu->e=cpu->xf=cpu->mf=cpu->d=false;cpu->i=true;world.field_scan=0x75ff;world.field_anchor[2]=end/3;
        ScWorldGuestStep(&world,cpu,ram);
        ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);interp816_runOpcode(cpu);
        ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);interp816_runOpcode(cpu);
        unsigned guard=0;
        while(cpu->pc!=0x8296) {
            assert(++guard<100000);ScWorldGuestStep(&world,cpu,ram);
            unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,8192);
            if(!cost) {ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);interp816_runOpcode(cpu);}
        }
        for(unsigned i=0;i<end;++i) assert(world.fields[10][i]==0 && world.fields[11][i]==0);
        assert(cpu->x==(uint16_t)end && (!world.colossal || world.field_scan==end));
    }
    free(expected);printf("PASS: %u native coverage clears match ROM state and clocks; all four full fields clear through 65536\n",cases);
}
static void coverage_pack_equivalence(void) {
    ScWorld *expected_world=malloc(sizeof world);assert(expected_world);unsigned cases=0;
    const unsigned values[]={0,1,255,256,300,32767,32768,65535},budgets[]={32,45,46,47,48,49,2000};
    for(unsigned map=0;map<4;++map) for(unsigned kind=0;kind<2;++kind)
    for(unsigned point=0;point<4;++point) for(unsigned pattern=0;pattern<8;++pattern)
    for(unsigned budget=0;budget<7;++budget) for(unsigned mode=0;mode<2;++mode) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned field=kind?11:10,target=kind?8:9,end=ScWorldFieldSizeWorld(&world,target);
        unsigned y=point==0?0:point==1?end/2:point==2?end-1:end>32768?32767:end-2;
        for(unsigned i=y;i<end && i<y+64;++i) {
            unsigned value=values[(pattern+i-y)%8];world.fields[field][i*2]=value;world.fields[field][i*2+1]=value>>8;
            world.fields[target][i]=0x5a;
        }
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=kind?0x9f7d:0x9ab2;cpu->dp=mode?0x1ef5:0x1e00;cpu->sp=mode?0x1f75:0x1ef5;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=mode!=0;cpu->i=true;cpu->c=(pattern&1)!=0;cpu->v=(pattern&2)!=0;
        cpu->a=0x5a37;cpu->x=(uint16_t)(2*y);cpu->y=y;world.field_anchor[2]=end/2;
        ScWorldGuestStep(&world,cpu,ram);
        copy=world;Interp816 initial=*cpu;memcpy(bitmap_ram,ram,sizeof ram);
        unsigned cost=ScWorldGuestBatchStep(&world,cpu,ram,rom,sizeof rom,budgets[budget]);
        if(!cost) {
            assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,bitmap_ram,sizeof ram) && !memcmp(&world,&copy,sizeof world));
            continue;
        }
        assert(cost<=budgets[budget]);Interp816 actual=*cpu;*expected_world=world;memcpy(stencil_expected_ram,ram,sizeof ram);
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);unsigned cycles=0,guard=0;
        while(cycles<cost) {
            assert(++guard<2000);ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            cycles+=interp816_runOpcode(cpu);
        }
        if(cycles!=cost || memcmp(ram,stencil_expected_ram,sizeof ram) || memcmp(&world,expected_world,sizeof world) ||
           memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1))
            fprintf(stderr,"coverage pack map=%u kind=%u point=%u pattern=%u budget=%u mode=%u cycles=%u/%u pc=%x/%x flags=%x/%x A=%x/%x X=%x/%x Y=%x/%x\n",map,kind,point,pattern,budgets[budget],mode,cost,cycles,actual.pc,cpu->pc,interp816_getFlags(&actual),interp816_getFlags(cpu),actual.a,cpu->a,actual.x,cpu->x,actual.y,cpu->y);
        assert(cycles==cost && !memcmp(ram,stencil_expected_ram,sizeof ram) && !memcmp(&world,expected_world,sizeof world));
        assert(!memcmp(&cpu->a,&actual.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    free(expected_world);printf("PASS: %u native word-to-byte coverage spans preserve ROM state, clamps, index seams and exact budgets\n",cases);
}
static void tile_revision_equivalence(void) {
    ScWorldReset(&world);world.active=world.colossal=world.giant=world.huge=true;
    const uint64_t *versions=ScWorldTileRevisions(&world);
    uint64_t first=versions[0],second=versions[1],last=versions[SC_WORLD_TILE_CHUNKS-1];
    assert(ScWorldPutCell(&world,255,0,0x13b));
    assert(versions[0]>first && versions[1]==second && versions[SC_WORLD_TILE_CHUNKS-1]==last);
    first=versions[0];assert(ScWorldPutCell(&world,255,0,0x13b));assert(versions[0]==first);
    /* A two-byte guest access straddles the tracked byte boundary. Both
     * chunks must change without marking the rest of the 1920x1600 map. */
    ScWorldGuest write={0};write.address=0x7f0200;write.mapped=true;write.bytes=2;
    write.data=world.tiles+511;write.tile_world=&world;write.tile_offset=511;
    assert(ScWorldGuestWrite(&write,0x7f0200,0x80));
    assert(ScWorldGuestWrite(&write,0x7f0201,0x27));
    assert(versions[0]>first && versions[1]>second && versions[SC_WORLD_TILE_CHUNKS-1]==last);
    uint8_t *saved=malloc(ScWorldEncodedSize()),*again=malloc(ScWorldEncodedSize());assert(saved && again);
    assert(ScWorldEncode(&world,saved,ScWorldEncodedSize()));
    ScWorldTilesTouch(&world,0,SC_WORLD_MAX_TILE_BYTES);
    assert(ScWorldEncode(&world,again,ScWorldEncodedSize()));
    assert(!memcmp(saved,again,ScWorldEncodedSize())); /* tracking is not city state */
    last=versions[SC_WORLD_TILE_CHUNKS-1];
    assert(ScWorldDecode(&world,saved,ScWorldEncodedSize()));assert(versions[SC_WORLD_TILE_CHUNKS-1]>last);
    last=versions[SC_WORLD_TILE_CHUNKS-1];ScWorldReset(&world);
    assert(versions[SC_WORLD_TILE_CHUNKS-1]>last);
    free(saved);free(again);
    puts("PASS: tile edits, unchanged writes, cross-chunk guest writes, full-map reload/reset and serialization-independent render revisions");
}
static void house_art_equivalence(void) {
    static const unsigned entries[]={0x980e,0x9814,0x9817,0x9829,0x9846,0x9847};
    static const unsigned budgets[]={0,4,5,6,10,32,55,512};
    static const unsigned inputs[]={0xff,0x1234,0x7f80,0xffff},lots[]={0,1,8,2};
    ScWorld *before_world=malloc(sizeof world),*actual_world=malloc(sizeof world);
    uint8_t *before_ram=malloc(sizeof ram),*actual_ram=malloc(sizeof ram);
    assert(before_world && actual_world && before_ram && actual_ram);
    unsigned scenarios=0,spans=0,yields=0;
    for(unsigned map=0;map<4;++map) for(unsigned entry=0;entry<6;++entry)
    for(unsigned pattern=0;pattern<4;++pattern) for(unsigned position=0;position<4;++position) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned x=position==0?0:position==1?ScWorldWidth(&world)/2:position==2?255:ScWorldWidth(&world)-1;
        unsigned y=position==0?0:position==1?ScWorldHeight(&world)/2:position==2?255:ScWorldHeight(&world)-1;
        if(x>=ScWorldWidth(&world)) x=ScWorldWidth(&world)-1;
        if(y>=ScWorldHeight(&world)) y=ScWorldHeight(&world)-1;
        world.coord[2][0]=x;world.coord[2][1]=y;
        if(world.huge && (pattern&1)) {world.coord[2][0]+=11;world.coord[2][1]-=12;}
        ScWorldPutCell(&world,x,y,0x8084);
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=entries[entry];cpu->dp=pattern&1?0x1ef5:0x1e00;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=entry==3?false:(pattern&2)!=0;
        cpu->i=true;cpu->c=pattern&1;cpu->v=pattern&2;cpu->a=inputs[pattern];cpu->x=0x9876;cpu->y=0xabcd;
        ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;put(cpu->dp+2,lots[pattern]);put(cpu->dp+4,pattern*77);
        put(cpu->sp+1,entry==4?0x1e00:0x6fff);put(cpu->sp+3,0x6fff);
        Interp816 initial=*cpu;*before_world=world;memcpy(before_ram,ram,sizeof ram);
        for(unsigned trial=0;trial<sizeof budgets/sizeof *budgets;++trial) {
            *cpu=initial;world=*before_world;memcpy(ram,before_ram,sizeof ram);memset(&guest,0,sizeof guest);
            unsigned cost=ScWorldGuestHouseArtStep(&world,cpu,ram,rom,budgets[trial]);assert(cost<=budgets[trial]);
            Interp816 actual=*cpu;*actual_world=world;memcpy(actual_ram,ram,sizeof ram);
            *cpu=initial;world=*before_world;memcpy(ram,before_ram,sizeof ram);memset(&guest,0,sizeof guest);
            unsigned elapsed=0,steps=0;
            while(elapsed<cost) {
                assert(++steps<100);ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
                elapsed+=interp816_runOpcode(cpu);
            }
            if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,actual_world,sizeof world)) {
                fprintf(stderr,"house art map=%u entry=%x pattern=%u pos=%u budget=%u clocks=%u/%u pc=%x/%x A=%x/%x X=%x/%x Y=%x/%x flags=%x/%x ram=%d world=%d\n",
                    map,entries[entry],pattern,position,budgets[trial],elapsed,cost,cpu->pc,actual.pc,cpu->a,actual.a,cpu->x,actual.x,cpu->y,actual.y,
                    interp816_getFlags(cpu),interp816_getFlags(&actual),memcmp(ram,actual_ram,sizeof ram),memcmp(&world,actual_world,sizeof world));abort();
            }
            if(cost) ++spans;else ++yields;
        }
        for(unsigned mode=0;mode<3;++mode) {
            *cpu=initial;world=*before_world;memcpy(ram,before_ram,sizeof ram);
            if(mode==0) cpu->nmiWanted=true;
            if(mode==1) {cpu->irqWanted=true;cpu->i=false;}
            if(mode==2) cpu->sp=cpu->dp;
            Interp816 pending=*cpu;
            assert(!ScWorldGuestHouseArtStep(&world,cpu,ram,rom,512) && !memcmp(cpu,&pending,sizeof pending));
            assert(!memcmp(ram,before_ram,sizeof ram) && !memcmp(&world,before_world,sizeof world));++yields;
        }
        ++scenarios;
    }
    free(before_world);free(actual_world);free(before_ram);free(actual_ram);
    printf("PASS: %u free-house artwork scenarios, %u bounded C/ROM spans, %u immutable budget/interrupt/alias yields, full coordinates, placement and frame restoration\n",scenarios,spans,yields);
}
static void zone_art_equivalence(void) {
    static const unsigned entries[]={0x98b8,0x98dd,0x9906};
    static const unsigned inputs[]={0,1,0x0205,0x0306,0x40ff,0x80ff,0xffff,0x1234};
    static const unsigned budgets[]={0,31,63,90,120,512,3000};
    ScWorld *before_world=malloc(sizeof world),*actual_world=malloc(sizeof world);
    uint8_t *before_ram=malloc(sizeof ram),*actual_ram=malloc(sizeof ram);
    assert(before_world && actual_world && before_ram && actual_ram);
    unsigned scenarios=0,spans=0,yields=0;
    for(unsigned map=0;map<4;++map) for(unsigned type=0;type<3;++type)
    for(unsigned pattern=0;pattern<8;++pattern) for(unsigned position=0;position<4;++position) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned x=position==0?0:position==1?ScWorldWidth(&world)/2:position==2?255:ScWorldWidth(&world)-2;
        unsigned y=position==0?0:position==1?ScWorldHeight(&world)/2:position==2?ScWorldHeight(&world)/2:ScWorldHeight(&world)-2;
        if(x>=ScWorldWidth(&world)) x=ScWorldWidth(&world)-2;
        for(unsigned n=0;n<9;++n) {
            unsigned tile=pattern>=4 && n==pattern?0x28:pattern%3==0?0x15:pattern%3==1?0x84:0x137;
            ScWorldPutCell(&world,(int)x+(int)(n%3)-1,(int)y+(int)(n/3)-1,tile|((n+pattern)%3==0?0x8000:0));
        }
        world.coord[2][0]=x;world.coord[2][1]=y;
        if(world.huge && (pattern&1)) {world.coord[2][0]+=11;world.coord[2][1]-=12;}
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=entries[type];cpu->dp=pattern&1?0x1ef5:0x1e04;cpu->sp=0x1f75;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=pattern&2;cpu->i=true;cpu->c=pattern&1;cpu->v=pattern&2;
        cpu->a=inputs[pattern];cpu->x=0x7654;cpu->y=0xabcd;ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;put(cpu->sp+1,0x6fff);
        Interp816 initial=*cpu;*before_world=world;memcpy(before_ram,ram,sizeof ram);
        for(unsigned trial=0;trial<sizeof budgets/sizeof *budgets;++trial) {
            *cpu=initial;world=*before_world;memcpy(ram,before_ram,sizeof ram);memset(&guest,0,sizeof guest);
            unsigned cost=ScWorldGuestZoneArtStep(&world,cpu,ram,rom,budgets[trial]);assert(cost<=budgets[trial]);
            Interp816 actual=*cpu;*actual_world=world;memcpy(actual_ram,ram,sizeof ram);
            *cpu=initial;world=*before_world;memcpy(ram,before_ram,sizeof ram);memset(&guest,0,sizeof guest);
            unsigned elapsed=0,steps=0;
            while(elapsed<cost) {
                assert(++steps<1000);ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
                elapsed+=interp816_runOpcode(cpu);
            }
            if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,actual_world,sizeof world)) {
                fprintf(stderr,"art map=%u type=%u pattern=%u pos=%u budget=%u clocks=%u/%u pc=%x/%x A=%x/%x X=%x/%x Y=%x/%x flags=%x/%x ram=%d world=%d\n",
                    map,type,pattern,position,budgets[trial],elapsed,cost,cpu->pc,actual.pc,cpu->a,actual.a,cpu->x,actual.x,cpu->y,actual.y,
                    interp816_getFlags(cpu),interp816_getFlags(&actual),memcmp(ram,actual_ram,sizeof ram),memcmp(&world,actual_world,sizeof world));abort();
            }
            if(cost) ++spans;else ++yields;
            if(trial==6) assert(cpu->pc==0x7000 && cpu->dp==initial.dp && cpu->sp==initial.sp+2);
        }
        *cpu=initial;world=*before_world;memcpy(ram,before_ram,sizeof ram);
        cpu->sp=cpu->dp;Interp816 alias=*cpu;
        assert(!ScWorldGuestZoneArtStep(&world,cpu,ram,rom,3000) && !memcmp(cpu,&alias,sizeof alias));
        assert(!memcmp(ram,before_ram,sizeof ram) && !memcmp(&world,before_world,sizeof world));
        ++scenarios;
    }
    free(before_world);free(actual_world);free(before_ram);free(actual_ram);
    printf("PASS: %u complete RCI redraw scenarios, %u bounded C/ROM spans, %u immutable yields, all expanded sizes, artwork widths, obstacles, power and frame restoration\n",scenarios,spans,yields);
}
static void zoning_quality_equivalence(void) {
    static const unsigned values[][2]={{0,0},{0,255},{29,0},{30,0},{79,0},{80,0},
        {149,0},{150,0},{255,0},{255,128},{128,255},{200,171},{200,170},{200,120},{255,105}};
    ScWorld *before_world=malloc(sizeof world),*actual_world=malloc(sizeof world);
    uint8_t *before_ram=malloc(sizeof ram),*actual_ram=malloc(sizeof ram);
    assert(before_world && actual_world && before_ram && actual_ram);
    unsigned cases=0,yields=0;
    for(unsigned map=0;map<4;++map) for(unsigned position=0;position<4;++position)
    for(unsigned value=0;value<sizeof values/sizeof *values;++value) for(unsigned byte=0;byte<2;++byte) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned x=position==0?0:position==1?ScWorldWidth(&world)/2:position==2?255:ScWorldWidth(&world)-1;
        unsigned y=position==0?0:position==1?ScWorldHeight(&world)/2:position==2?ScWorldHeight(&world)/2:ScWorldHeight(&world)-1;
        if(x>=ScWorldWidth(&world)) x=ScWorldWidth(&world)-1;
        unsigned index=(y/2)*ScWorldFieldWidth(&world,0)+x/2;
        world.coord[2][0]=x;world.coord[2][1]=y;world.fields[0][index]=values[value][0];world.fields[2][index]=values[value][1];
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0x9468;cpu->dp=0x1ef5;cpu->sp=byte?0xb86:0x1ff1;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=byte!=0;cpu->i=true;cpu->a=0xdead;cpu->x=0x1234;cpu->y=0xabcd;
        ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;put(cpu->sp+1,0x6fff);
        Interp816 before=*cpu;*before_world=world;memcpy(before_ram,ram,sizeof ram);
        unsigned cost=ScZoningQualityStep(&world,cpu,ram,1024);assert(cost);
        Interp816 actual=*cpu;*actual_world=world;memcpy(actual_ram,ram,sizeof ram);
        *cpu=before;world=*before_world;memcpy(ram,before_ram,sizeof ram);
        unsigned elapsed=0,steps=0;
        while(cpu->pc!=0x7000) {
            assert(++steps<100);ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            elapsed+=interp816_runOpcode(cpu);
        }
        if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,actual_world,sizeof world)) {
            fprintf(stderr,"quality map=%u pos=%u value=%u byte=%u clocks=%u/%u A=%x/%x X=%x/%x Y=%x/%x flags=%x/%x ram=%d world=%d\n",
                map,position,value,byte,elapsed,cost,cpu->a,actual.a,cpu->x,actual.x,cpu->y,actual.y,
                interp816_getFlags(cpu),interp816_getFlags(&actual),memcmp(ram,actual_ram,sizeof ram),memcmp(&world,actual_world,sizeof world));abort();
        }
        ++cases;
        for(unsigned deadline=0;deadline<2;++deadline) {
            *cpu=before;world=*before_world;memcpy(ram,before_ram,sizeof ram);
            assert(!ScZoningQualityStep(&world,cpu,ram,deadline?cost-1:0));
            assert(!memcmp(cpu,&before,sizeof before) && !memcmp(ram,before_ram,sizeof ram) && !memcmp(&world,before_world,sizeof world));++yields;
        }
        for(unsigned interrupt=0;interrupt<2;++interrupt) {
            *cpu=before;cpu->nmiWanted=!interrupt;cpu->irqWanted=interrupt;cpu->i=false;Interp816 pending=*cpu;
            assert(!ScZoningQualityStep(&world,cpu,ram,1024));
            assert(!memcmp(cpu,&pending,sizeof pending) && !memcmp(ram,before_ram,sizeof ram) && !memcmp(&world,before_world,sizeof world));++yields;
        }
    }
    free(before_world);free(actual_world);free(before_ram);free(actual_ram);
    printf("PASS: %u complete quality C/ROM calls, %u immutable deadline/interrupt yields, thresholds, overflow, coordinate seams and stack aliases\n",cases,yields);
}
static void zoning_instruction_equivalence(void) {
    static const uint16_t boundaries[]={
0x9468,0x946a,0x946d,0x946e,0x946f,0x9472,0x9473,0x9476,0x9479,0x947d,0x947e,0x9482,
0x9484,0x9486,0x9488,0x9489,0x948b,0x948d,0x948e,0x9490,0x9492,0x9493,0x9494,0x9495,
0x9497,0x949a,0x949b,0x949c,0x949f,0x94a0,0x94a3,0x94a7,0x94a9,0x94ab,0x94ad,0x94b0,
0x94b3,0x94b5,0x94b7,0x94ba,0x94bc,0x94bd,0x94c0,0x94c1,0x94c3,0x94c5,0x94c8,0x94ca,
0x94cc,0x94d0,0x94d2,0x94d4,0x94d7,0x94d8,0x94da,0x94dd,0x94df,0x94e1,0x94e4,0x94e6,
0x94e8,0x94ea,0x94ed,0x94ef,0x94f2,0x94f3,0x94f5,0x94f6,0x94f7,0x94f8,0x94f9,0x94fc,
0x94fe,0x9500,0x9503,0x9504,0x9506,0x9509,0x950c,0x950e,0x9511,0x9515,0x9518,0x951b,
0x951d,0x9520,0x9523,0x9526,0x9527,0x952a,0x952b,0x952e,0x9531,0x9534,0x9537,0x9538,
0x953b,0x953c,0x9540,0x9543,0x9546,0x9548,0x954b,0x954e,0x9551,0x9552,0x9555,0x9556,
0x9559,0x955c,0x955f,0x9562,0x9563,0x9566,0x9567,0x9569,0x956c,0x956d,0x956e,0x9571,
0x9572,0x9575,0x9579,0x957a,0x957b,0x957c,0x957d,0x957e,0x9580,0x9582,0x9584,0x9586,
0x9588,0x958b,0x958c,0x958e,0x9591,0x9593,0x9595,0x9598,0x9599,0x959b,0x959e,0x95a1,
0x95a3,0x95a6,0x95aa,0x95ad,0x95b0,0x95b2,0x95b5,0x95b8,0x95bb,0x95bc,0x95bf,0x95c0,
0x95c3,0x95c6,0x95c9,0x95cc,0x95cd,0x95d0,0x95d1,0x95d5,0x95d8,0x95db,0x95dd,0x95e0,
0x95e3,0x95e6,0x95e7,0x95ea,0x95eb,0x95ee,0x95f1,0x95f4,0x95f7,0x95f8,0x95fb,0x95fc,
0x95fe,0x9600,0x9603,0x9605,0x9607,0x960a,0x960d,0x960f,0x9610,0x9612,0x9615,0x9617,
0x9619,0x961c,0x9653,0x9656,0x9659,0x965b,0x965e,0x9661,0x9663,0x9666,0x9668,0x966b,
0x966d,0x9670,0x9672,0x9674,0x9676,0x9678,0x967a,0x967c,0x967e,0x9681,0x9683,0x9686,
0x9689,0x968c,0x968f,0x9691,0x9694,0x9696,0x9697,0x9699,0x969c,0x969e,0x96a0,0x96a3,
0x96a4,0x96a5,0x96a8,0x96a9,0x96ac,0x96ae,0x96b1,0x96b3,0x96b4,0x96b7,0x96b8,0x96bc,
0x96bf,0x96c1,0x96c4,0x96c7,0x96c8,0x96ca,0x96ce,0x96cf,0x96d0,0x96d3,0x96d5,0x96d7,
0x96d9,0x96db,0x96de,0x96df,0x96e1,0x96e2,0x96e4,0x96e5,0x96e6,0x96e7,0x96ea,0x96ec,
0x96ee,0x96f1,0x96f3,0x96f5,0x96f8,0x96fa,0x96fd,0x96fe,0x96ff,0x9702,0x9703,0x9706,
0x9708,0x970b,0x970d,0x970e,0x9711,0x9712,0x9716,0x9719,0x971b,0x971e,0x9720,0x9721,
0x9722,0x9725,0x9729,0x972b,0x972c,0x972d,0x9730,0x9732,0x9745,0x9748,0x974b,0x974d,
0x9750,0x9753,0x9755,0x9758,0x975a,0x975d,0x975f,0x9762,0x9764,0x9766,0x9768,0x976b,
0x976d,0x9770,0x9771,0x9773,0x9774,0x9775,0x9778,0x977a,0x977c,0x977f,0x9781,0x9784,
0x9786,0x9789,0x978c,0x978e,0x9790,0x9793,0x9794,0x9796,0x9798,0x979b,0x979d,0x97a0,
0x97a3,0x97a5,0x97a6,0x97a8,0x97a9,0x97aa,0x97ad,0x97af,0x97b1,0x97b4,0x97b6,0x97b9,
0x97bb,0x97be,0x97c1,0x97c3,0x97c5,0x97c8
    };
    ScWorld *before_world=malloc(sizeof world),*actual_world=malloc(sizeof world);
    uint8_t *before_ram=malloc(sizeof ram),*actual_ram=malloc(sizeof ram);
    assert(before_world && actual_world && before_ram && actual_ram);
    unsigned cases=0,yields=0;uint8_t coverage[354]={0};
    for(unsigned map=0;map<4;++map) for(unsigned p=0;p<354;++p)
    for(unsigned byte=0;byte<2;++byte) for(unsigned pattern=0;pattern<2;++pattern) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned x=pattern?ScWorldWidth(&world)-2:ScWorldWidth(&world)/2,y=pattern?ScWorldHeight(&world)-2:ScWorldHeight(&world)/2;
        world.coord[2][0]=x;world.coord[2][1]=y;world.map_anchor=2*(y*ScWorldWidth(&world)+x);
        world.field_anchor[0]=(y/2)*ScWorldFieldWidth(&world,0)+x/2;
        memset(ram,pattern?0xff:0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=boundaries[p];cpu->dp=pattern?0x1ef5:0x1e00;cpu->sp=0x1ff1;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=byte!=0;cpu->i=true;cpu->c=pattern;cpu->v=pattern;cpu->n=pattern;cpu->z=!pattern;
        cpu->a=pattern?0xffff:0x7fff;cpu->x=(uint16_t)world.map_anchor;cpu->y=pattern?16:2;
        ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;put(cpu->sp+1,0x6fff);
        Interp816 before=*cpu;*before_world=world;memcpy(before_ram,ram,sizeof ram);
        unsigned cost=ScZoningInstructionStep(&world,cpu,ram,rom);Interp816 actual=*cpu;*actual_world=world;memcpy(actual_ram,ram,sizeof ram);
        *cpu=before;world=*before_world;memcpy(ram,before_ram,sizeof ram);
        unsigned elapsed=0;
        if(cost) {ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed=interp816_runOpcode(cpu);}
        if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,actual_world,sizeof world)) {
            fprintf(stderr,"zoning instruction map=%u pc=%x byte=%u pattern=%u clocks=%u/%u flags=%x/%x end=%x/%x\n",map,boundaries[p],byte,pattern,elapsed,cost,interp816_getFlags(cpu),interp816_getFlags(&actual),cpu->pc,actual.pc);abort();
        }
        if(cost) {++cases;coverage[p]=1;}else ++yields;
        world=*before_world;memcpy(ram,before_ram,sizeof ram);
        *cpu=before;cpu->nmiWanted=true;Interp816 pending=*cpu;
        assert(!ScZoningStep(&world,cpu,ram,rom,4096) && !memcmp(cpu,&pending,sizeof pending));
        cpu->nmiWanted=false;cpu->irqWanted=true;cpu->i=false;pending=*cpu;
        assert(!ScZoningInstructionStep(&world,cpu,ram,rom) && !memcmp(cpu,&pending,sizeof pending));
        assert(!memcmp(ram,before_ram,sizeof ram) && !memcmp(&world,before_world,sizeof world));
    }
    for(unsigned p=0;p<354;++p) assert(coverage[p]);
    free(before_world);free(actual_world);free(before_ram);free(actual_ram);
    printf("PASS: %u atomic zoning C/ROM comparisons, %u immutable mode rejections, all 354 boundaries, all expanded maps and IRQ/NMI\n",cases,yields);
}

static void zoning_family_equivalence(void) {
    bool control=getenv("SC_WORLD_ZONE_CONTROL_TEST")!=NULL;
    ScWorld *before_world=malloc(sizeof world),*actual_world=malloc(sizeof world);
    uint8_t *before_ram=malloc(sizeof ram),*actual_ram=malloc(sizeof ram);
    assert(before_world && actual_world && before_ram && actual_ram);
    unsigned cases=0,spans=0,yields=0,atomics=0,coverage[4][65536]={0};
    const unsigned entries[]={0x9468,0x9495,0x9567,0x95fc,0x9659,0x974b,0x9794};
    const unsigned capacities[]={0,1,2,3,4,5,6,16,24,40,48,255};
    const unsigned res[]={0x84,0x99,0x120,0x120,0x37a,0x38c,0x383,0x84,0x395,0x99,0x120,0x120};
    const unsigned com[]={0x137,0x140,0x1ef,0x1ef,0x39e,0x1ef,0x1ef,0x3b9,0x3b0,0x3a7,0x137,0x1ef};
    const unsigned budgets[]={0,1,7,31,128,4096};
    for(unsigned map=0;map<4;++map) for(unsigned stage=0;stage<7;++stage)
    for(unsigned pattern=0;pattern<12;++pattern) for(unsigned align=0;align<2;++align) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        unsigned x=pattern%4==0?1:pattern%4==1?width-2:pattern%4==2?255%width:width/2;
        unsigned y=pattern%4==0?1:pattern%4==1?height-2:pattern%4==2?255%height:height/2;
        unsigned tile=stage==2 || stage==5?com[pattern]:stage==3 || stage==6?0x201:res[pattern];
        for(int yy=(int)y-3;yy<=(int)y+5;++yy) for(int xx=(int)x-3;xx<=(int)x+5;++xx)
            ScWorldPutCell(&world,xx,yy,(uint16_t)(0x8000|(stage==4?0x84:0x90)));
        ScWorldPutCell(&world,x,y,(uint16_t)(tile|0xc000));
        if(tile==0x120 || tile==0x1ef) {
            ScWorldPutCell(&world,x+(pattern&1?0:3),y+(pattern&1?3:0),(uint16_t)(tile|0xc000));
        }
        memset(world.fields[0],pattern%4==0?0:255,ScWorldFieldSizeWorld(&world,0));
        memset(world.fields[2],pattern%4==0?255:pattern%4==1?129:pattern%4==2?80:0,ScWorldFieldSizeWorld(&world,2));
        memset(world.fields[3],pattern%3==0?0:pattern%3==1?64:255,ScWorldFieldSizeWorld(&world,3));
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=entries[stage];cpu->dp=align?0x1ef5:0x1e00;cpu->sp=0x1ff1;
        cpu->e=cpu->xf=cpu->d=false;cpu->mf=pattern&1;cpu->i=true;cpu->c=pattern&1;cpu->v=pattern&2;
        cpu->a=0x7321;cpu->x=0x7654;cpu->y=0x9876;
        world.coord[2][0]=x;world.coord[2][1]=y;ram[0xb85]=(uint8_t)x;ram[0xb86]=(uint8_t)y;
        world.map_anchor=2*(y*width+x);put(0xb49,world.map_anchor);put(0xb89,tile);
        put(cpu->dp,capacities[pattern]);put(cpu->dp+4,0);put(cpu->sp+1,0x6fff);
        Interp816 initial=*cpu;copy=world;memcpy(bitmap_ram,ram,sizeof ram);
        product=quotient=dividend=0;multiplicand=0;
        unsigned steps=0,original_clocks=0;
        while(cpu->pc!=0x7000) {
            assert(++steps<12000);
            if(ScZoningOwns(cpu->pc) && (coverage[map][cpu->pc]<2 || steps%113==pattern)) {
                Interp816 before=*cpu;*before_world=world;memcpy(before_ram,ram,sizeof ram);
                for(unsigned tier=0;tier<7;++tier) {
                    unsigned budget=tier<6?budgets[tier]:12;
                    unsigned cost=tier<6?(control?ScZoningControlStep(&world,cpu,ram,rom,budget):ScZoningStep(&world,cpu,ram,rom,budget)):ScZoningInstructionStep(&world,cpu,ram,rom);
                    assert(cost<=budget);Interp816 actual=*cpu;*actual_world=world;memcpy(actual_ram,ram,sizeof ram);
                    *cpu=before;world=*before_world;memcpy(ram,before_ram,sizeof ram);
                    unsigned elapsed=0;
                    if(cost) {
                        do {ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);elapsed+=interp816_runOpcode(cpu);} while(elapsed<cost);
                    }
                    if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,actual_world,sizeof world)) {
                        fprintf(stderr,"zoning map=%u stage=%u pattern=%u align=%u tier=%u pc=%x budget=%u clocks=%u/%u end=%x/%x flags=%x/%x A=%x/%x X=%x/%x Y=%x/%x world=%d ram=%d\n",map,stage,pattern,align,tier,before.pc,budget,elapsed,cost,cpu->pc,actual.pc,interp816_getFlags(cpu),interp816_getFlags(&actual),cpu->a,actual.a,cpu->x,actual.x,cpu->y,actual.y,memcmp(&world,actual_world,sizeof world),memcmp(ram,actual_ram,sizeof ram));
                        for(unsigned p=0,shown=0;p<sizeof ram && shown<6;++p) if(ram[p]!=actual_ram[p]) {fprintf(stderr,"RAM %x=%x/%x\n",p,ram[p],actual_ram[p]);++shown;}
                        abort();
                    }
                    if(!cost) ++yields;else if(tier==6) ++atomics;else ++spans;
                    *cpu=before;world=*before_world;memcpy(ram,before_ram,sizeof ram);
                }
                ++coverage[map][before.pc];
            }
            ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);original_clocks+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;*before_world=world;memcpy(before_ram,ram,sizeof ram);
        uint16_t expected_product=product,expected_quotient=quotient,expected_dividend=dividend;uint8_t expected_multiplicand=multiplicand;
        *cpu=initial;world=copy;memcpy(ram,bitmap_ram,sizeof ram);memset(&guest,0,sizeof guest);
        product=quotient=dividend=0;multiplicand=0;unsigned native_clocks=0;steps=0;
        while(cpu->pc!=0x7000) {
            assert(++steps<12000);unsigned cost=control?ScZoningControlStep(&world,cpu,ram,rom,4096):ScZoningStep(&world,cpu,ram,rom,4096);
            if(!cost) {ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);cost=interp816_runOpcode(cpu);}
            native_clocks+=cost;
        }
        assert(native_clocks==original_clocks && !memcmp(cpu,&expected,sizeof expected));
        assert(!memcmp(ram,before_ram,sizeof ram) && !memcmp(&world,before_world,sizeof world));
        assert(product==expected_product && quotient==expected_quotient && dividend==expected_dividend && multiplicand==expected_multiplicand);
        ++cases;
    }
    unsigned reached=0;for(unsigned p=0;p<65536;++p) reached+=(coverage[0][p] || coverage[1][p] || coverage[2][p] || coverage[3][p]);
    printf("zoning coverage: %u boundaries, %u atomic, %u spans, %u yields\n",reached,atomics,spans,yields);
    assert(reached>250 && atomics>1000 && spans>1000);
    free(before_world);free(actual_world);free(before_ram);free(actual_ram);
    printf("PASS: %u zoning scenarios, %u bounded C/ROM spans, %u atomic instructions, %u immutable yields, %u reached boundaries\n",cases,spans,atomics,yields,reached);
}

static unsigned preparation_bindings;
static void preparation_submission(void *context,const ScWorld *w,unsigned source) {
    unsigned *result=context;assert(w->active);++result[0];result[1]=source;
}
static void preparation_binding(const Interp816 *c,const uint8_t *image,size_t bytes) {
    ScWorldGuest a,b;
    ScWorldGuestBegin(&a,&world,c,image,bytes);
    ScWorldGuestBeginPrepared(&b,&world,c,image,bytes);
    /* Stack-local immediate pointers differ; their bytes and ownership must not. */
    bool ai=a.data==a.immediate,bi=b.data==b.immediate;
    if(ai) a.data=NULL;if(bi) b.data=NULL;
    if(ai!=bi || memcmp(&a,&b,sizeof a)) {
        fprintf(stderr,"preparation binding mismatch %02x:%04x M%u X%u DB%02x idx=%04x/%04x size=%zu\n",
            c->k,c->pc,c->mf,c->xf,c->db,c->x,c->y,bytes);abort();
    }
    if(ai) a.data=a.immediate;if(bi) b.data=b.immediate;
    uint32_t probes[]={a.address-1,a.address,a.address+1,a.address+a.bytes};
    for(unsigned i=0;i<4;++i) {
        uint8_t av=0x55,bv=0x55;
        assert(ScWorldGuestRead(&a,probes[i],&av)==ScWorldGuestRead(&b,probes[i],&bv));assert(av==bv);
    }
    ++preparation_bindings;
}
static void preparation_equivalence(void) {
    /* Exhaust every ROM offset, all width combinations and every map size.
     * The original hooks are the oracle, independent of compiled ownership. */
    static uint8_t second_ram[sizeof ram],patched[sizeof rom];
    memcpy(patched,rom,sizeof rom);
    Interp816 seed=*cpu;
    unsigned hook_cases=0;
    unsigned submissions[2]={0};
    ScStencilBackend backend={submissions,preparation_submission,NULL};ScWorldGuestSetStencilBackend(&backend);
    const unsigned submit_pc[]={0xa02f,0xa0b5,0xa14d,0xa1cc},submit_field[]={13,14,10,11};
    for(unsigned map=0;map<5;++map) for(unsigned site=0;site<4;++site) for(unsigned irq=0;irq<4;++irq) {
        ScWorldReset(&world);world.active=map>0;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
        Interp816 a=seed,b;a.k=3;a.pc=submit_pc[site];a.nmiWanted=irq==1;a.irqWanted=irq>=2;a.i=irq==3;b=a;
        submissions[0]=0;ScWorldGuestStep(&world,&a,ram);
        unsigned expected=submissions[0],source=submissions[1];
        assert(expected==(unsigned)(map>0 && (irq==0 || irq==3)));
        if(expected) assert(source==submit_field[site]);
        submissions[0]=0;ScWorldGuestStepPrepared(&world,&b,ram);
        assert(submissions[0]==expected && (!expected || submissions[1]==source));assert(!memcmp(&a,&b,sizeof a));
        ++hook_cases;
    }
    ScWorldGuestSetStencilBackend(NULL);
    /* Native coordinate entries must retain wrap lifting, out-of-map reads,
     * tile writes, stack-independent flags and full indices above 64 KiB. */
    const unsigned coordinates[]={0,1,127,128,255,256,511,1023,1599,1919,1920,UINT32_MAX};
    const unsigned helpers[]={0x849e,0x84c4,0xa29a,0xa2b9,0xa2d7};
    for(unsigned map=0;map<5;++map) for(unsigned helper=0;helper<5;++helper)
    for(unsigned point=0;point<sizeof coordinates/sizeof *coordinates;++point) {
        ScWorldReset(&world);world.active=map>0;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
        unsigned x=coordinates[point],y=coordinates[(point*3)%(sizeof coordinates/sizeof *coordinates)];
        world.coord[2][0]=x;world.coord[2][1]=y;
        if(ScWorldContains(&world,x,y)) ScWorldPutCell(&world,x,y,0x8364);
        copy=world;memset(ram,0xa5,sizeof ram);memcpy(second_ram,ram,sizeof ram);
        Interp816 a=seed,b;a.k=3;a.pc=helpers[helper];a.a=((y&255)<<8)|(x&255);
        a.y=0x8310;a.mf=point&1;a.xf=point&2;b=a;
        ScWorldGuestStep(&world,&a,ram);ScWorldGuestStepPrepared(&copy,&b,second_ram);
        assert(!memcmp(&a,&b,sizeof a));assert(!memcmp(ram,second_ram,sizeof ram));assert(!memcmp(&world,&copy,sizeof world));
        ++hook_cases;
    }
    for(unsigned map=0;map<5;++map) {
        ScWorldReset(&world);world.active=map>0;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
        for(unsigned i=0;i<sizeof world.tiles;++i) world.tiles[i]=(uint8_t)(i*37+11);
        for(unsigned f=0;f<SC_WORLD_FIELDS;++f) for(unsigned i=0;i<SC_WORLD_FIELD_BYTES;++i) world.fields[f][i]=(uint8_t)(i*13+f*19);
        for(unsigned widths=0;widths<4;++widths) {
            Interp816 c=seed;c.mf=widths&1;c.xf=widths&2;
            for(unsigned bank=0;bank<16;++bank) for(unsigned pc=0x8000;pc<0x10000;++pc) {
                c.k=bank;c.pc=pc;c.db=(pc&1)?0x7f:bank;
                c.x=(uint16_t)(pc*17);c.y=(uint16_t)(pc*31);
                world.map_anchor=world.bank_anchor[0]=world.bank_anchor[1]=world.bank_anchor[2]=(pc&2)?0x41000:0;
                world.field_scan=(pc&4)?0x12000:0;
                for(unsigned i=0;i<3;++i) world.field_anchor[i]=(pc&8)?0x18000:0;
                preparation_binding(&c,rom,sizeof rom);
            }
        }
        printf("preparation ROM bindings: map %u (%u total)\n",map,preparation_bindings);
        /* Sequential original/prepared sweeps expose memory-only hook effects,
         * including full-map power publication and scratch clears. */
        for(unsigned widths=0;widths<4;++widths) {
            copy=world;memset(ram,0x5a,sizeof ram);memcpy(second_ram,ram,sizeof ram);
            for(unsigned bank=0;bank<5;++bank) for(unsigned pc=0;pc<65536;++pc) {
                Interp816 a=seed,b;
                a.k=bank;a.pc=pc;a.mf=widths&1;a.xf=widths&2;
                a.a=0x0101;a.x=32;a.y=16;a.dp=0x1e00;a.sp=0x1ffd;b=a;
                ScWorldGuestStep(&world,&a,ram);ScWorldGuestStepPrepared(&copy,&b,second_ram);
                ScWorldGuestVehicles(&world,&a,ram,3);ScWorldGuestVehiclesPrepared(&copy,&b,second_ram,3);
                if(memcmp(&a,&b,sizeof a) || memcmp(&world,&copy,offsetof(ScWorld,tiles))) {
                    fprintf(stderr,"preparation hook mismatch map%u %02x:%04x width%u\n",map,bank,pc,widths);abort();
                }
                /* These byte STZ hooks only change scratch RAM; compare at
                 * their exact entry before later hooks can overwrite it. */
                if(bank==3 && (pc==0x9c37 || pc==0x9c39))
                    assert(!memcmp(ram,second_ram,sizeof ram));
                ++hook_cases;
            }
            assert(!memcmp(ram,second_ram,sizeof ram));assert(!memcmp(&world,&copy,sizeof world));
        }
    }
    /* Reuse the same cache slots while changing all live operand bytes, array
     * bases, long banks and width/index state. Also check mirrored ROM banks,
     * cache collisions, pointer replacement and truncated ROM bounds. */
    static const unsigned sites[]={0x008000,0x00ac85,0x018abf,0x028000,0x038287,0x0396b4,
        0x039c22,0x03a1bb,0x03a8c6,0x03a8fd,0x03b676,0x03c877,0x03cf80,0x0ffffc,0x808000,0x8396b4};
    for(unsigned site=0;site<sizeof sites/sizeof *sites;++site) for(unsigned op=0;op<256;++op)
    for(unsigned field=0;field<SC_WORLD_FIELDS+3;++field) {
        unsigned pc=sites[site],p=((pc>>16)&0x7f)*32768+(pc&32767);
        uint8_t saved[4];memcpy(saved,patched+p,4);
        unsigned base=field<SC_WORLD_FIELDS?ScWorldFields[field].base:field==SC_WORLD_FIELDS?0x200:field==SC_WORLD_FIELDS+1?0x0202:0xffff;
        if(op&1 && field<SC_WORLD_FIELDS) base+=ScWorldFields[field].width*ScWorldFields[field].element_bytes;
        patched[p]=op;patched[p+1]=base;patched[p+2]=base>>8;patched[p+3]=(op&2)?0x7f:3;
        Interp816 c=seed;c.k=pc>>16;c.pc=pc;c.db=(op&4)?0x7f:3;c.mf=op&8;c.xf=op&16;c.x=0xfffe;c.y=0x8001;
        preparation_binding(&c,patched,sizeof patched);preparation_binding(&c,patched,sizeof patched);
        preparation_binding(&c,patched,p+3);preparation_binding(&c,patched,p+4);
        preparation_binding(&c,rom,sizeof rom);preparation_binding(&c,patched,sizeof patched);
        const unsigned banks[]={0,1,0x7e,0x80,0xff};
        for(unsigned bank=0;bank<sizeof banks/sizeof *banks;++bank) {
            c.db=banks[bank];preparation_binding(&c,patched,sizeof patched);
        }
        memcpy(patched+p,saved,4);preparation_binding(&c,patched,sizeof patched);
    }
    printf("PASS: %u cached/original bindings and %u complete hook comparisons; live ROM edits, aliases, bounds and full world/RAM exact\n",preparation_bindings,hook_cases);
}

static void wide_centroid_count(void) {
    uint8_t *encoded=malloc(ScWorldEncodedSize());assert(encoded);
    ScWorld *expected=malloc(sizeof world);assert(expected);
    unsigned carries=0,total_owners=0;
    for(unsigned map=2;map<=3;++map) {
        ScWorldReset(&world);world.active=world.huge=world.giant=true;world.colossal=map==3;
        memset(ram,0,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->dp=0x1e00;cpu->sp=0x1ff1;
        cpu->e=cpu->xf=cpu->d=false;cpu->i=true;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world),count=0;
        uint64_t sum_x=0,sum_y=0;
        for(unsigned y=1;y+1<height;y+=3) for(unsigned x=1;x+1<width;x+=3) {
            ScWorldPutCell(&world,x,y,0x99);put(cpu->dp+16,x);put(cpu->dp+18,y);put(0xb89,0x99);
            cpu->pc=0x9b12;cpu->mf=false;
            assert(ScWorldGuestDensityStageStep(&world,cpu,ram,rom,800));
            assert(cpu->pc==0x9b5a);assert(ScDensityInstructionStep(&world,cpu,ram,rom)==3);
            assert(cpu->pc==0x9b5c);
            Interp816 saved=*cpu;
            bool wrap=(++count&65535)==0;
            if(wrap) {
                copy=world;memcpy(bitmap_ram,ram,sizeof ram);
                assert(ScWorldEncode(&world,encoded,ScWorldEncodedSize()));
            }
            ScWorldGuestStep(&world,cpu,ram);sum_x+=x;sum_y+=y;
            unsigned actual=(unsigned)ram[cpu->dp+8]|(unsigned)ram[cpu->dp+9]<<8|
                            (unsigned)ram[cpu->dp+10]<<16|(unsigned)ram[cpu->dp+11]<<24;
            assert(actual==count);
            if(wrap) {
                *expected=world;Interp816 expected_cpu=*cpu;
                memcpy(stencil_expected_ram,ram,sizeof ram);
                assert(ScWorldDecode(&world,encoded,ScWorldEncodedSize()));
                memcpy(ram,bitmap_ram,sizeof ram);*cpu=saved;
                ScWorldGuestStepPrepared(&world,cpu,ram);
                assert(!memcmp(&world,expected,sizeof world));
                assert(!memcmp(cpu,&expected_cpu,sizeof *cpu));
                assert(!memcmp(ram,stencil_expected_ram,sizeof ram));++carries;
                /* Trailing empty cells after a wrapped count must not carry it
                 * a second time merely because the low word remains zero. */
                unsigned high=ram[cpu->dp+10]|(unsigned)ram[cpu->dp+11]<<8;
                for(unsigned empty=0;empty<3;++empty) {
                    put(cpu->dp+16,empty==0?x-1:x+1);put(cpu->dp+18,empty==2?y-1:y);
                    cpu->pc=0x9b5c;ScWorldGuestStep(&world,cpu,ram);
                    assert((ram[cpu->dp+10]|(unsigned)ram[cpu->dp+11]<<8)==high);
                }
                world=*expected;*cpu=expected_cpu;memcpy(ram,stencil_expected_ram,sizeof ram);
            }
        }
        cpu->pc=0x9b6f;ScWorldGuestStep(&world,cpu,ram);
        assert(world.center_valid && world.center_x==sum_x/count && world.center_y==sum_y/count);
        assert(ScWorldEncode(&world,encoded,ScWorldEncodedSize()) && ScWorldDecode(&copy,encoded,ScWorldEncodedSize()));
        assert(copy.center_valid && copy.center_x==world.center_x && copy.center_y==world.center_y);
        world.center_x=width+83;world.center_valid=true;
        assert(ScWorldEncode(&world,encoded,ScWorldEncodedSize()) && ScWorldDecode(&copy,encoded,ScWorldEncodedSize()));
        assert(!copy.center_valid && copy.center_x==width/2 && copy.center_y==height/2);
        assert(!memcmp(world.tiles,copy.tiles,sizeof world.tiles) && !memcmp(world.fields,copy.fields,sizeof world.fields));
        total_owners+=count;
    }
    free(expected);free(encoded);printf("PASS: %u actual native zone-owner updates, %u full-width count carries with mid-carry save/resume, exact 64-bit reference centroids and invalid-center recovery\n",total_owners,carries);
}
int main(int argc,char **argv) {
    if(argc==3 && !strcmp(argv[2],"--scan-order")) {
        for(unsigned scale=1;scale<=3;++scale) {
            ScWorldReset(&world);world.active=world.huge=true;world.giant=scale>=2;world.colossal=scale>=3;world.scan_spread=true;
            memset(visits,0,sizeof visits);unsigned total=ScWorldCells(&world),quadrants=0;
            for(unsigned i=0;i<total;++i) {
                unsigned cell=world.scan_y*ScWorldWidth(&world)+world.scan_x;assert(cell<total);assert(!visits[cell]++);
                if(i==12345) {
                    size_t bytes=ScWorldEncodedSize();uint8_t *state=malloc(bytes);assert(state);
                    assert(ScWorldEncode(&world,state,bytes) && ScWorldDecode(&copy,state,bytes));
                    assert(copy.scan_spread && copy.scan_x==world.scan_x && copy.scan_y==world.scan_y);
                    assert(ScWorldAdvanceScan(&copy));
                    unsigned next_x=copy.scan_x,next_y=copy.scan_y;
                    assert(ScWorldAdvanceScan(&world));
                    assert(next_x==world.scan_x && next_y==world.scan_y);
                    /* Restore the cell being visited; loop advancement below
                     * must run exactly once, including after save/resume. */
                    assert(ScWorldDecode(&world,state,bytes));free(state);
                }
                if(i<16)quadrants|=1u<<((world.scan_x>=ScWorldWidth(&world)/2)+2*(world.scan_y>=ScWorldHeight(&world)/2));
                assert(ScWorldAdvanceScan(&world)==(i+1<total));
            }
            assert(quadrants==15 && world.scan_y==ScWorldHeight(&world));
            for(unsigned i=0;i<total;++i)assert(visits[i]==1);
        }
        puts("PASS: distributed scan visits every 480x400, 960x800 and 1920x1600 cell once; first batches cover all quadrants");return 0;
    }

    setvbuf(stdout,NULL,_IONBF,0);
    if(getenv("SC_WORLD_ZONING_INSTRUCTION_TEST") || getenv("SC_WORLD_ZONING_FAMILY_TEST") || getenv("SC_WORLD_ZONE_CONTROL_TEST")) {
#ifdef _WIN32
        _putenv("SC_ZONING_REFERENCE=0");
#else
        setenv("SC_ZONING_REFERENCE","0",1);
#endif
    }
    if(getenv("SC_WORLD_ZONE_CONTROL_TEST")) {
#ifdef _WIN32
        _putenv("SC_ZONE_CONTROL_REFERENCE=0");
#else
        setenv("SC_ZONE_CONTROL_REFERENCE","0",1);
#endif
    }
    assert(argc==2 || argc==3); FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom); fclose(f);
    ScWorldGuestBindRom(rom,sizeof rom);
    ScProgramEnable(getenv("SC_WORLD_PROGRAM_TEST")!=NULL);
    ScMapGenPrng pr={0}; sc_mapgen_prng_seed_from_spin(&pr,0xc7);
    ScWorldGenerate(&world,&pr); assert(world.active);
    for (unsigned i=0;i<SC_WORLD_FIELDS;++i) assert(ScWorldFieldSize(i)<=SC_WORLD_FIELD_BYTES);
    assert(!ScWorldPutCell(&world,-1,0,0xffff) && !ScWorldPutCell(&world,240,199,0xffff));
    for (int y=0;y<200;++y) for (int x=0;x<240;++x)
        assert(ScWorldPutCell(&world,x,y,(uint16_t)(y*240+x)));
    cpu=interp816_init(NULL,read_bus,write_bus); assert(cpu);
    if(argc==3 && !strcmp(argv[2],"--centroid-count")) {wide_centroid_count();interp816_free(cpu);return 0;}
    if(argc==3 && !strcmp(argv[2],"--bench-field-shadow")) {field_shadow_benchmark();interp816_free(cpu);return 0;}
    if(argc==3 && !strcmp(argv[2],"--bench-terrain-quality")) {terrain_quality_benchmark();interp816_free(cpu);return 0;}
    if(argc==3 && !strcmp(argv[2],"--terrain-quality-span")) {terrain_quality_span_equivalence();interp816_free(cpu);return 0;}
    if(argc==3 && !strcmp(argv[2],"--terrain-quality-scratch")) {terrain_quality_scratch_equivalence();interp816_free(cpu);return 0;}
#ifdef SC_WORLD_PREPARATION_ONLY
    preparation_equivalence();interp816_free(cpu);return 0;
#endif
    if(getenv("SC_WORLD_PREPARATION_TEST")) {preparation_equivalence();interp816_free(cpu);return 0;}
    size_t size=ScWorldEncodedSize(); uint8_t *data=malloc(size); assert(data);
    if(getenv("SC_WORLD_ZONE_ART_TEST")) {zone_art_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_HOUSE_ART_TEST")) {house_art_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_ZONING_QUALITY_TEST")) {zoning_quality_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_ZONING_INSTRUCTION_TEST")) {zoning_instruction_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_ZONING_FAMILY_TEST")) {zoning_family_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_ZONE_CONTROL_TEST")) {zoning_family_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_SERVICE_FAMILY_TEST")) {service_family_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_SERVICE_SPAN_TEST")) {service_field_span_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_SMOOTHING_HANDOFF_TEST")) {smoothing_handoff_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_SMOOTHING_FAMILY_TEST")) {smoothing_family_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_GPU_FIELD_SPAN_TEST")) {gpu_field_span_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_GPU_CRIME_TEST")) {crime_gpu_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_GPU_LAND_TEST")) {land_gpu_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_ATOMIC_TEST")) {
        tile_lookup_equivalence();power_continuation_equivalence();density_family_equivalence();
        land_stage_equivalence();land_continuation_equivalence();transport_family_equivalence();
        unsigned boundaries=0;for(unsigned map=0;map<5;++map) for(unsigned bank=0;bank<2;++bank) for(unsigned p=0;p<65536;++p) boundaries+=atomic_coverage[map][bank][p]!=0;
        assert(atomic_cases>1000 && boundaries>500);
        printf("PASS: %u one-instruction C/ROM comparisons, %u immutable rejections, %u reachable boundaries, IRQ/NMI preserved\n",atomic_cases,atomic_rejections,boundaries);
        interp816_free(cpu);free(data);return 0;
    }
    if(getenv("SC_WORLD_TILE_REVISIONS_TEST")) {tile_revision_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_TRANSPORT_FAMILY_TEST")) {transport_family_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_DIFFUSION_BEGIN_TEST")) {diffusion_begin_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_DENSITY_BATCH_TEST")) {density_batch_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_POWER_EXACT_TEST")) {power_neighbor_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_POWER_PUBLISH_TEST")) {power_publish_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_DENSITY_FAMILY_TEST")) {density_family_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_TILE_LOOKUP_TEST")) {tile_lookup_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_POWER_CONTINUATION_TEST")) {power_continuation_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_LAND_CONTINUATION_TEST")) {land_stage_equivalence();land_continuation_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_TRANSPORT_NEIGHBOR_TEST")) {transport_neighbor_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_COVERAGE_PACK_TEST")) {coverage_pack_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_COVERAGE_CLEAR_TEST")) {coverage_clear_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_COUNTER_TEST")) {spatial_counter_regression();interp816_free(cpu);free(data);return 0;}
    if(argc==3 && !strcmp(argv[2],"--colossal")) goto colossal;
    if(getenv("SC_WORLD_POWER_VISIT_TEST")) {power_visit_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_HOUSE_SITE_TEST")) {house_site_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_LAND_FINISH_TEST")) {land_finish_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_LAND_CELL_TEST")) {developed_land_equivalence();empty_terrain_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_HOUSING_TEST")) {housing_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_DENSITY_SCAN_TEST")) {density_scan_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_FIELD_SWEEP_TEST")) {field_sweep_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_FIELD_WORD_TEST")) {field_word_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_LAND_BEGIN_TEST")) {land_begin_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_SPATIAL_BATCH_TEST")) {spatial_batch_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_INTERPRETER_PROFILE_TEST")) {interpreter_profile_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_DIFFUSION_STENCIL_TEST")) {diffusion_stencil_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_DIFFUSION_FINISH_TEST")) {diffusion_finish_equivalence();interp816_free(cpu);free(data);return 0;}
    if(getenv("SC_WORLD_SWEEP_TEST")) {
        empty_cell_equivalence();sweep_span_equivalence();interp816_free(cpu);free(data);return 0;
    }
    if(getenv("SC_WORLD_SWEEP_SPAN_TEST")) {
        sweep_span_equivalence();interp816_free(cpu);free(data);return 0;
    }
    if(getenv("SC_WORLD_KERNEL_TEST")) {
        kernel_equivalence();developed_land_equivalence();power_neighbor_equivalence();transport_family_equivalence();service_copy_equivalence();spatial_batch_equivalence();power_publish_equivalence();power_continuation_equivalence();density_family_equivalence();tile_lookup_equivalence();land_stage_equivalence();land_continuation_equivalence();interpreter_profile_equivalence();diffusion_begin_equivalence();diffusion_stencil_equivalence();diffusion_finish_equivalence();density_batch_equivalence();zone_score_equivalence();zone_replacement_equivalence();sweep_span_equivalence();interp816_free(cpu);return 0;
    }
    if(getenv("SC_WORLD_PROGRAM_REMAINING_TEST")) {
        interpreter_profile_equivalence();diffusion_begin_equivalence();diffusion_stencil_equivalence();
        diffusion_finish_equivalence();density_batch_equivalence();zone_score_equivalence();
        zone_replacement_equivalence();sweep_span_equivalence();interp816_free(cpu);return 0;
    }
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
        for(unsigned size_id=0;size_id<5;++size_id) {
            memset(ram,0,sizeof ram);ScWorldReset(&world);
            world.active=size_id>0;world.huge=size_id>=2;world.giant=size_id>=3;world.colossal=size_id==4;
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
    world.active=world.huge=true;world.giant=world.colossal=false;
    const uint32_t ordinary[]={0x008400,0x01897f,0x038026,0x03804f,0x0390a7,0x03ae1c,0x03b66a,0x03b676,0x03b692,0x03b84b,0x03c474};
    for(unsigned i=0;i<sizeof ordinary/sizeof *ordinary;++i)
        assert(ScWorldGuestMasterCycles(&world,ordinary[i],48,&cycle_remainder)==48);
    stencil_equivalence();
    empty_cell_equivalence();
    sweep_span_equivalence();
    transport_neighbor_equivalence();housing_equivalence();density_scan_equivalence();field_sweep_equivalence();field_word_equivalence();land_begin_equivalence();power_visit_equivalence();house_site_equivalence();land_finish_equivalence();
    terrain_field_equivalence();
    kernel_equivalence();
    developed_land_equivalence();
    power_neighbor_equivalence();
    service_copy_equivalence();
    terrain_quality_scratch_equivalence();
    terrain_quality_span_equivalence();
    spatial_batch_equivalence();
    zone_score_equivalence();
    zone_replacement_equivalence();
    empty_terrain_equivalence();
    city_radius();
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
colossal:
    /* The fifth size crosses both byte-coordinate and word-index limits. */
    ScWorldReset(&world);world.active=world.huge=world.giant=world.colossal=true;
    memset(ram,0,sizeof ram);memset(visits,0,sizeof visits);
    routine(0x8228,0,0);routine(0x8297,0,0);
    for(unsigned i=0;i<ScWorldCells(&world);++i) assert(visits[i]==1);
    assert(world.scan_y==1600);
    world.coord[2][0]=1919;world.coord[2][1]=1599;
    ScWorldPutCell(&world,1919,1599,0x8abc);ScWorldPutCell(&world,127,63,0x123);
    routine(0x849e,127|(63<<8),0);assert(cpu->a==0x8abc);
    routine(0x84c4,127|(63<<8),0x8001);assert(ScWorldCell(&world,1919,1599)==0x8001);
    assert(ScWorldCell(&world,127,63)==0x123);
    world.fields[5][383999]=1;routine(0xb152,0,0);assert(ScWorldCell(&world,1919,1599)&0x8000);
    put(0x1bd,1895);put(0x1bf,1578);routine_bank(1,0x8aa8,0,0);
    routine_bank(1,0xa688,1919,0);assert(cpu->a==1890);
    routine_bank(1,0xa6a1,1599,0);assert(cpu->a==1572);
    for(unsigned f=0;f<SC_WORLD_FIELDS;++f) assert(ScWorldFieldSizeWorld(&world,f)<=SC_WORLD_FIELD_BYTES);
    memset(world.tiles,0,sizeof world.tiles);memset(ram,0,sizeof ram);
    for(unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx)
      ScWorldPutCell(&world,1800+dx,1500+dy,0x8120+dy*3+dx+((dy==2 && dx==2)?0x4000:0));
    memset(&population,0,sizeof population);population.valid=true;population_enabled=true;
    routine(0x8228,0,0);routine(0x8297,0,0);routine(0x8196,0,0);
    assert(population.value==40*20);population_enabled=false;
    routine(0x9ad7,0,0);assert(world.center_x==1800 && world.center_y==1500);
    assert(world.fields[3][750*960+900]>0);
    routine(0x9c11,0,0);routine(0x9e8e,0,0);routine(0x9aa3,0,0);
    /* Density's quarter-grid counters must pass 255 on both axes. */
    memset(world.fields[15],0,sizeof world.fields[15]);
    world.fields[15][375*480+450]=200;
    routine(0x9fa6,0,0);
    fprintf(stderr,"colossal density center=%u left=%u source=%u counters=%u,%u\n",world.fields[6][375*480+450],world.fields[6][375*480+449],world.fields[15][375*480+450],ram[0x1dfa]|ram[0x1dfb]<<8,ram[0x1dfc]|ram[0x1dfd]<<8);
    assert(world.fields[6][375*480+450]==100);
    assert(world.fields[6][375*480+449]==25);
    /* Growth decay scans 96,000 bytes, including the final word. */
    memset(world.fields[7],0,sizeof world.fields[7]);
    world.fields[7][95998]=10;
    routine(0x891f,0,0);assert(world.fields[7][95998]==9 && world.field_scan==96000);
    world.scan_x=1919;world.scan_y=1599;
    assert(ScWorldEncode(&world,data,size) && ScWorldDecode(&copy,data,size));
    assert(!memcmp(&world,&copy,sizeof world));
    native_cities=malloc(ScWorldCitiesSize());assert(native_cities);ScWorldCitiesInit(native_cities);
    assert(ScWorldCitySave(native_cities,native_sram,1,&world));
    assert(ScWorldCityLoad(&copy,native_cities,ScWorldCitiesSize(),native_sram,1));
    assert(!memcmp(&world,&copy,sizeof world));
    free(native_cities);native_cities=NULL;
    interp816_free(cpu); free(data);
    puts("PASS: 48,000/192,000/768,000/3,072,000 native visits, full-world census and fields, power, byte seams, full city center, legacy migration and portable saves");
}
