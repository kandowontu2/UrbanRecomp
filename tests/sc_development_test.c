/* Runs the actual US zone handlers, supplied by the user's ROM. */
#include "sc_development.h"
#include "sc_population.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t rom[0x80000], ram[0x20000], baseline[0x20000];
static uint8_t snapshot_ram[0x20000], expected_ram[0x20000];
static uint16_t product, quotient, dividend;
static uint8_t multiplicand;
static bool large,huge,giant;
static bool growth_fixture, powered_fixture=true;
static uint64_t grown_population;
static ScWorld world,snapshot_world,expected_world;
static ScWorldGuest guest;
static uint8_t read_bus(void *ctx,uint32_t a) {
    (void)ctx;
    uint8_t value;
    if (ScWorldGuestRead(&guest,a,&value)) return value;
    unsigned bank=a>>16, p=a&0xffff;
    if (bank==0x7e || bank==0x7f) return ram[a-0x7e0000];
    if (p<0x2000) return ram[p];
    if (p==0x4214) return (uint8_t)quotient;
    if (p==0x4215) return (uint8_t)(quotient>>8);
    if (p==0x4216) return (uint8_t)product;
    if (p==0x4217) return (uint8_t)(product>>8);
    if (p>=0x8000) return rom[((bank&15)*0x8000)+(p-0x8000)];
    return ram[p];
}
static void write_bus(void *ctx,uint32_t a,uint8_t v) {
    (void)ctx;
    if (ScWorldGuestWrite(&guest,a,v)) return;
    unsigned bank=a>>16, p=a&0xffff;
    if (bank==0x7e || bank==0x7f) { ram[a-0x7e0000]=v; return; }
    if (p==0x4202) multiplicand=v;
    else if (p==0x4203) product=multiplicand*v;
    else if (p==0x4204) dividend=(dividend&0xff00)|v;
    else if (p==0x4205) dividend=(dividend&0xff)|((uint16_t)v<<8);
    else if (p==0x4206) {
        quotient=v?dividend/v:0xffff; product=v?dividend%v:dividend;
    } else if (p<0x8000) ram[p]=v;
}
static void put(unsigned p,unsigned v) { ram[p]=(uint8_t)v; ram[p+1]=(uint8_t)(v>>8); }
static unsigned word(unsigned p) { return ram[p]|((unsigned)ram[p+1]<<8); }
static ScDevelopment run(uint16_t entry,unsigned tile,int speed,bool hook) {
    memset(ram,0,sizeof ram);
    memset(&guest,0,sizeof guest); ScWorldReset(&world); world.active=large;world.huge=huge;world.giant=giant;
    /* Powered zone, centered away from map bounds. */
    unsigned zx=giant?900:huge?300:large?200:60,zy=giant?750:huge?280:large?180:50;
    unsigned cell=(zy*(large?ScWorldWidth(&world):120)+zx)*2;
    world.map_anchor=cell;
    world.coord[2][0]=zx;world.coord[2][1]=zy;
    put(0xb49,cell); ram[0xb85]=zx; ram[0xb86]=zy;
    put(0xb87,0x8000|tile); put(0xb89,tile);
    for (int y=0;y<3;++y) for (int x=0;x<3;++x)
        if (large) ScWorldPutCell(&world,zx+x,zy+y,0x8000|tile);
        else put(0x10200+cell+y*240+x*2,0x8000|tile);
    if (growth_fixture) {
        for (int y=0;y<3;++y) for (int x=0;x<3;++x)
            if (large) ScWorldPutCell(&world,zx+x,zy+y,0);
            else put(0x10200+cell+y*240+x*2,0);
        /* A real empty zone's nine tiles, with favorable demand, land value
         * and road traffic. Test tile changes, not just hook visit counts. */
        for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
            unsigned raw=tile-4+(y+1)*3+x+1;
            if (powered_fixture) raw|=0x8000;
            if (large) ScWorldPutCell(&world,zx+x,zy+y,raw);
            else put(0x10200+cell+y*240+x*2,raw);
        }
        put(0xb87,(powered_fixture?0x8000:0)|tile);
        put(0xbad,2000);put(0xbaf,1500);put(0xbb1,1500);
        memset(ram+0x16b00,192,3000); /* favorable native growth score */
        memset(ram+0x18e28,80,3000);  /* allow the native density upgrade */
        if (large) {
            memset(world.fields[0],192,ScWorldFieldSizeWorld(&world,0));
            memset(world.fields[3],80,ScWorldFieldSizeWorld(&world,3));
        }
    }
    put(0xb53,1991); put(0xb55,1); put(0xb51,100);
    for (unsigned p=0xccf;p<0xcdd;p+=2) put(p,12345+p);
    put(0x01fe,0x6fff);
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus); assert(cpu);
    interp816_reset(cpu);
    cpu->k=3; cpu->db=3; cpu->pc=entry; cpu->sp=0x01fd; cpu->dp=0x0300;
    cpu->mf=false; cpu->xf=false; cpu->e=false; cpu->i=true;
    ScDevelopment s={0};
    ScPopulation population={0};
    population.valid=true;
    for (unsigned i=0;i<3;++i) population.tally_active[i]=1;
    unsigned tally_hits=0;
    unsigned steps=0;
    bool captured=false;
    Interp816 snapshot_cpu; ScDevelopment snapshot_state; ScPopulation snapshot_population;
    while (!(cpu->k==3 && cpu->pc==0x7000)) {
        if (++steps>=2000000) { fprintf(stderr,"runaway pc=%02x:%04x sp=%04x dp=%04x stopped=%d waiting=%d\n",cpu->k,cpu->pc,cpu->sp,cpu->dp,cpu->stopped,cpu->waiting); abort(); }
        if (hook && cpu->k==3) {
            ScWorldGuestStep(&world,cpu,ram);
            if (cpu->pc==0x93b7 || cpu->pc==0x9301 || cpu->pc==0x9255) ++tally_hits;
            cpu->pc=ScPopulationStep(&population,ram,cpu->pc,cpu->dp);
            uint16_t pc=ScDevelopmentStepWorld(&s,&world,ram,cpu->pc,cpu->dp,cpu->sp,speed);
            if (pc!=cpu->pc) { cpu->mf=false; cpu->xf=false; }
            cpu->pc=pc;
        }
        if (!captured && s.extra_attempts==2) {
            memcpy(snapshot_ram,ram,sizeof ram); snapshot_cpu=*cpu;
            snapshot_state=s; snapshot_population=population; captured=true;
            snapshot_world=world;
        }
        ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
        interp816_runOpcode(cpu);
    }
    if (captured) {
        memcpy(expected_ram,ram,sizeof ram);
        ScPopulation expected_population=population;
        expected_world=world;
        memcpy(ram,snapshot_ram,sizeof ram); *cpu=snapshot_cpu; s=snapshot_state;
        population=snapshot_population;
        world=snapshot_world;
        steps=0;
        while (!(cpu->k==3 && cpu->pc==0x7000)) {
            assert(++steps<2000000);
            if (cpu->k==3) {
                ScWorldGuestStep(&world,cpu,ram);
                cpu->pc=ScPopulationStep(&population,ram,cpu->pc,cpu->dp);
                uint16_t pc=ScDevelopmentStepWorld(&s,&world,ram,cpu->pc,cpu->dp,cpu->sp,speed);
                if (pc!=cpu->pc) { cpu->mf=false; cpu->xf=false; }
                cpu->pc=pc;
            }
            ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
            interp816_runOpcode(cpu);
        }
        assert(!memcmp(expected_ram,ram,sizeof ram));
        assert(!memcmp(&expected_population,&population,sizeof population));
        assert(!memcmp(&expected_world,&world,sizeof world));
    }
    assert(cpu->sp==0x01ff && cpu->dp==0x0300);
    assert(word(0xb53)==1991 && word(0xb55)==1 && word(0xb51)==100);
    assert(word(entry==0x937a?0xb8d:entry==0x92ce?0xb95:0xb91)==1);
    assert(word(0xe1b)+word(0xe1d)==1);
    if (hook) {
        unsigned kind=entry==0x937a?0:entry==0x92ce?1:2;
        const unsigned counters[]={0xb8b,0xb93,0xb8f};
        assert(tally_hits==(word(counters[kind])?1u:0u) && population.capacity[kind]==word(counters[kind]));
    }
    interp816_free(cpu);
    if (growth_fixture) {
        ScPopulation census={0};
        assert(ScPopulationRefreshLive(&census,ram,large?&world:NULL,rom,sizeof rom));
        grown_population=census.value;
        printf("growth zone=%04x speed=%d powered=%d population=%llu center=%04x\n",entry,speed,powered_fixture,
            (unsigned long long)grown_population,large?ScWorldCell(&world,zx,zy):word(0x10200+cell));
    }
    return s;
}
int main(int argc,char **argv) {
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom); fclose(f);
    const uint16_t entries[]={0x937a,0x92ce,0x922f};
    const unsigned tiles[]={0x99,0x144,0x201};
    const int speeds[]={1,2,5,10,50};
    for (int z=0;z<3;++z) {
        run(entries[z],tiles[z],1,false); memcpy(baseline,ram,sizeof ram);
        run(entries[z],tiles[z],1,true); assert(!memcmp(baseline,ram,sizeof ram));
        for (int i=1;i<5;++i) {
            ScDevelopment s=run(entries[z],tiles[z],speeds[i],true);
            printf("zone=%04x speed=%d attempts=%llu extras=%llu\n",entries[z],speeds[i],
                   (unsigned long long)s.attempts,(unsigned long long)s.extra_attempts);
            assert(s.attempts==(unsigned)speeds[i]);
            assert(s.extra_attempts==(unsigned)speeds[i]-1);
        }
    }
    large=true;
    for (int z=0;z<3;++z) for(int i=0;i<5;++i) {
        ScDevelopment s=run(entries[z],tiles[z],speeds[i],true);
        if (speeds[i]>1) assert(s.attempts==(unsigned)speeds[i] && s.extra_attempts==(unsigned)speeds[i]-1);
    }
    growth_fixture=true;
    const unsigned empty[]={0x84,0x13b,0x1fc};
    for(int size=0;size<4;++size) for (int z=0;z<3;++z) {
        large=size>0;huge=size>=2;giant=size==3;powered_fixture=true;
        run(entries[z],empty[z],1,true);uint64_t normal=grown_population;
        run(entries[z],empty[z],50,true);assert(grown_population>normal);
        powered_fixture=false;
        run(entries[z],empty[z],50,true);assert(!grown_population);
    }
    puts("PASS: stock Normal equivalence, RCI attempts, actual growth and power gating on all map sizes, single tallies, calendar, stack integrity and mid-attempt restoration");
    return 0;
}
