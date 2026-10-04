/* Runs the actual US zone handlers, supplied by the user's ROM. */
#include "sc_development.h"
#include "sc_development_batches.h"
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
static uint8_t snapshot_ram[0x20000], expected_ram[0x20000], initial_ram[0x20000];
static ScWorld initial_world;
static uint16_t product, quotient, dividend;
static uint8_t multiplicand;
static bool large,huge,giant,colossal,mega;
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
    memset(&guest,0,sizeof guest); ScWorldReset(&world); world.active=large;world.huge=huge;world.giant=giant;world.colossal=colossal;world.mega=mega;
    /* Powered zone, centered away from map bounds. */
    unsigned zx=mega?3800:colossal?1800:giant?900:huge?300:large?200:60,zy=mega?3100:colossal?1500:giant?750:huge?280:large?180:50;
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
    memcpy(initial_ram,ram,sizeof ram);initial_world=world;
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
static void native_helpers(void) {
    const unsigned entries[]={0x842f,0x8456,0x847a,0x907e,0x927b,0x9327,0x93e5,0x93d1};
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);
    unsigned cases=0;
    for(unsigned routine=0;routine<8;++routine) for(unsigned sample=0;sample<(routine>=3?128:1024);++sample) {
        memset(&guest,0,sizeof guest);memset(ram,0x5a,sizeof ram);
        uint32_t random=sample*1664525u+1013904223u;
        for(unsigned at=0xccf;at<0xcdd;++at) {random=random*1664525u+1013904223u;ram[at]=random>>24;}
        put(0xb89,sample);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=entries[routine];cpu->sp=sample&16?0x1ffc:0x01fc;cpu->dp=sample&32?0x0300:sample&1?0x1df6:0x1e02;
        cpu->e=cpu->d=cpu->mf=cpu->xf=false;cpu->i=true;
        cpu->a=random;cpu->x=random>>16;cpu->y=random>>8;
        cpu->c=sample&1;cpu->v=sample&2;cpu->n=sample&4;cpu->z=sample&8;
        Interp816 initial=*cpu;memcpy(snapshot_ram,ram,sizeof ram);
        unsigned cycles=0,steps=0;
        while(routine<4?read_bus(NULL,0x30000|cpu->pc)!=0x60:
            cpu->pc!=(routine==4?0x9283:routine==5?0x932f:0x93ed) && cpu->pc!=(routine==4?0x92cc:routine==5?0x9378:0x9447)) {
            assert(++steps<1000);cycles+=interp816_runOpcode(cpu);
        }
        Interp816 expected=*cpu;memcpy(expected_ram,ram,sizeof ram);
        *cpu=initial;memcpy(ram,snapshot_ram,sizeof ram);
        unsigned cost=ScDevelopmentNativeStep(cpu,ram);
        if(cost!=cycles || memcmp(expected_ram,ram,sizeof ram))
            fprintf(stderr,"native helper %x sample %u cycles %u/%u pc %x/%x\n",entries[routine],sample,cost,cycles,cpu->pc,expected.pc);
        assert(cost==cycles && !memcmp(expected_ram,ram,sizeof ram));
        assert(!memcmp(&cpu->a,&expected.a,(char *)&cpu->cyclesUsed-(char *)&cpu->a+1));++cases;
    }
    interp816_free(cpu);printf("PASS: %u native C capacity/random oracle cases\n",cases);
}
static void native_house_candidates(void) {
    Interp816 *c=interp816_init(NULL,read_bus,write_bus);assert(c);unsigned cases=0;
    const unsigned best[]={0,1,5,255};
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<4;++point)
    for(unsigned pattern=0;pattern<8;++pattern) for(unsigned start=0;start<4;++start) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned x=point==0?0:point==1?ScWorldWidth(&world)/2:point==2?255:ScWorldWidth(&world)-1;
        unsigned y=point==0?0:point==1?ScWorldHeight(&world)/2:point==2?255:ScWorldHeight(&world)-1;
        if(y>=ScWorldHeight(&world)) y=ScWorldHeight(&world)-2;
        const unsigned artwork[]={0,0x80,0x84,0x88,0x89,0x15,0x40,0x399};
        for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx)
            ScWorldPutCell(&world,x+dx,y+dy,artwork[(pattern+dx+dy+8)%8]|0x8000);
        world.coord[2][0]=x;world.coord[2][1]=y;world.map_anchor=2*(y*ScWorldWidth(&world)+x);
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(c);
        c->k=c->db=3;c->pc=start==3?0x97c9:0x97db;c->dp=pattern&1?0x1e00:0x1ec7;c->sp=0x1f65;
        c->e=c->d=c->xf=false;c->mf=c->i=true;c->a=0xfa37;c->x=0x3210;c->y=start?start==1?4:8:1;
        c->c=pattern&1;c->n=pattern&2;c->z=pattern&4;c->v=point&1;
        ram[0xb85]=x;ram[0xb86]=y;put(c->dp,0xab00|best[pattern%4]);put(c->dp+2,7);
        uint32_t random=pattern*1664525u+point*1013904223u;
        for(unsigned p=0xccf;p<0xcdd;++p) {random=random*1664525u+1013904223u;ram[p]=random>>24;}
        snapshot_world=world;Interp816 initial=*c;memcpy(snapshot_ram,ram,sizeof ram);
        unsigned total=0,steps=0;
        while(c->pc!=0x980e) {
            assert(++steps<20000);uint16_t pc=c->pc;ScWorldGuestStep(&world,c,ram);if(c->pc!=pc) continue;
            ScWorldGuestBegin(&guest,&world,c,rom,sizeof rom);total+=interp816_runOpcode(c);
        }
        Interp816 expected=*c;expected_world=world;memcpy(expected_ram,ram,sizeof ram);
        *c=initial;world=snapshot_world;memcpy(ram,snapshot_ram,sizeof ram);
        ScDevelopment state={.repeating=true,.end=0xffff};
        unsigned cost=ScDevelopmentNativeBatch(&state,&world,c,ram,rom,sizeof rom);
        if(cost!=total || memcmp(ram,expected_ram,sizeof ram) || memcmp(&world,&expected_world,sizeof world) ||
           memcmp(&c->a,&expected.a,(char *)&c->cyclesUsed-(char *)&c->a+1))
            fprintf(stderr,"house candidates map=%u point=%u pattern=%u start=%u cycles=%u/%u pc=%x/%x A=%x/%x flags=%x/%x\n",
                map,point,pattern,start,cost,total,c->pc,expected.pc,c->a,expected.a,interp816_getFlags(c),interp816_getFlags(&expected));
        assert(cost==total && !memcmp(ram,expected_ram,sizeof ram) && !memcmp(&world,&expected_world,sizeof world));
        assert(!memcmp(&c->a,&expected.a,(char *)&c->cyclesUsed-(char *)&c->a+1));++cases;
    }
    interp816_free(c);printf("PASS: %u complete native residential candidate searches match original ROM\n",cases);
}
static void native_house_mutations(void) {
    Interp816 *c=interp816_init(NULL,read_bus,write_bus);assert(c);unsigned cases=0;
    const unsigned deltas[]={0,1,7,8,32,64,126,127,128,129,192,247,248,249,254,255};
    for(unsigned map=0;map<4;++map) for(unsigned point=0;point<4;++point)
    for(unsigned pattern=0;pattern<16;++pattern) for(unsigned kind=0;kind<3;++kind)
    for(unsigned start=0;start<(kind==1?3:1);++start) {
        ScWorldReset(&world);world.active=true;world.huge=map>0;world.giant=map>=2;world.colossal=map==3;
        unsigned x=point==0?0:point==1?ScWorldWidth(&world)/2:point==2?255:ScWorldWidth(&world)-1;
        unsigned y=point==0?0:point==1?ScWorldHeight(&world)/2:point==2?255:ScWorldHeight(&world)-1;
        if(x>=ScWorldWidth(&world)) x=ScWorldWidth(&world)-2;
        if(y>=ScWorldHeight(&world)) y=ScWorldHeight(&world)-2;
        world.coord[2][0]=x;world.coord[2][1]=y;world.map_anchor=2*(y*ScWorldWidth(&world)+x);
        unsigned anchor=world.map_anchor;
        for(unsigned i=0;i<9;++i) {
            unsigned at=anchor+2*(i/3)*ScWorldWidth(&world)+2*(i%3);
            const unsigned tiles[]={0x84,0x89,0x94,0x95,0x8089,0,0x40,0x3ff};
            if(at+1<2*ScWorldCells(&world)) {world.tiles[at]=tiles[(pattern+i)%8];world.tiles[at+1]=tiles[(pattern+i)%8]>>8;}
        }
        if(kind==1 && point==3 && pattern&8) world.map_anchor=UINT32_MAX;
        memset(ram,0x5a,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(c);
        c->k=c->db=3;c->pc=kind==2?0x9659:kind?0x970b:0x961d;c->dp=pattern&1?0x1e00:0x1ec7;c->sp=0x1f65;
        c->e=c->d=c->xf=false;c->mf=!kind && (pattern&1);c->i=true;
        c->a=0xfa00|deltas[pattern];c->x=0x3210;c->y=kind?start*8:0xabcd;
        c->c=pattern&1;c->n=pattern&2;c->z=pattern&4;c->v=point&1;
        ram[0xb85]=x;ram[0xb86]=y;put(c->dp+8,world.map_anchor);put(c->sp+1,0x6fff);
        if(kind==2) {put(0xb89,0x84);put(c->dp,pattern%15+1);}
        unsigned field=2*((y/8)*(ScWorldWidth(&world)/8)+x/8);
        const unsigned old[]={0,65535,32760,32768};
        world.fields[7][field]=old[pattern%4];world.fields[7][field+1]=old[pattern%4]>>8;
        snapshot_world=world;Interp816 initial=*c;memcpy(snapshot_ram,ram,sizeof ram);
        const char *control=getenv("SC_ZONE_CONTROL_REFERENCE");
        unsigned total=0,steps=0,end=(!control || *control!='1')?0x7000:kind?0x9732:0x7000;
        while(c->pc!=end) {
            assert(++steps<20000);uint16_t pc=c->pc;ScWorldGuestStep(&world,c,ram);if(c->pc!=pc) continue;
            ScWorldGuestBegin(&guest,&world,c,rom,sizeof rom);total+=interp816_runOpcode(c);
        }
        Interp816 expected=*c;expected_world=world;memcpy(expected_ram,ram,sizeof ram);
        *c=initial;world=snapshot_world;memcpy(ram,snapshot_ram,sizeof ram);
        ScDevelopment state={.repeating=true,.end=0xffff};
        unsigned cost=ScDevelopmentNativeBatch(&state,&world,c,ram,rom,sizeof rom);
        if(cost!=total || memcmp(ram,expected_ram,sizeof ram) || memcmp(&world,&expected_world,sizeof world) ||
           memcmp(&c->a,&expected.a,(char *)&c->cyclesUsed-(char *)&c->a+1))
            fprintf(stderr,"house mutation map=%u point=%u pattern=%u kind=%u start=%u cycles=%u/%u pc=%x/%x A=%x/%x flags=%x/%x\n",
                map,point,pattern,kind,start,cost,total,c->pc,expected.pc,c->a,expected.a,interp816_getFlags(c),interp816_getFlags(&expected));
        assert(cost==total && !memcmp(ram,expected_ram,sizeof ram) && !memcmp(&world,&expected_world,sizeof world));
        assert(!memcmp(&c->a,&expected.a,(char *)&c->cyclesUsed-(char *)&c->a+1));++cases;
    }
    interp816_free(c);printf("PASS: %u native residential density/removal calls match original ROM\n",cases);
}
static void native_batches(void) {
    const unsigned entries[]={0x923d,0x92dc,0x9388},ends[]={0x92cc,0x9378,0x9447};
    const unsigned skips[]={0x9242,0x92ee,0x93a4},attempts[]={0x926f,0x931b,0x93d1};
    const unsigned zones[]={0x922f,0x92ce,0x937a},tiles[]={0x201,0x144,0x99};
    Interp816 *c=interp816_init(NULL,read_bus,write_bus);assert(c);unsigned cases=0;
    for(unsigned map=0;map<5;++map) for(unsigned kind=0;kind<3;++kind) for(unsigned sample=0;sample<128;++sample) {
        ScWorldReset(&world);world.active=map>0;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
        unsigned width=world.active?ScWorldWidth(&world):120,height=world.active?ScWorldHeight(&world):100;
        unsigned x=width-40,y=height-30;
        memset(ram,0,sizeof ram);memset(&guest,0,sizeof guest);interp816_reset(c);
        c->k=c->db=3;c->pc=entries[kind];c->dp=kind==2?0x1df4:0x1df6;c->sp=sample&16?0x1ffa:0x01fa;
        c->e=c->d=c->mf=c->xf=false;c->i=true;
        uint32_t random=sample*1664525u+1013904223u;
        for(unsigned p=0xccf;p<0xcdd;++p) {random=random*1664525u+1013904223u;ram[p]=random>>24;}
        unsigned tile=kind==2 && sample%2?0x84:sample%4==0?(kind==1?0x39a:kind==2?0x376:0x1fc):tiles[kind]+9*(sample%4);
        unsigned neighbour=0;bool powered=tile==0x84?!(sample&8):!(sample&1);
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
            unsigned artwork=tile+dy*3+dx;
            if(tile==0x84 && (dx || dy) && neighbour++<(sample>>2)%9) artwork=0x89+(neighbour%12);
            ScWorldPutCell(&world,x+dx,y+dy,(powered?0x8000:0)|artwork);
            if(!world.active)put(0x10200+2*((y+dy)*width+x+dx),(powered?0x8000:0)|artwork);
        }
        world.coord[2][0]=x;world.coord[2][1]=y;world.map_anchor=2*(y*width+x);
        ram[0xb85]=x;ram[0xb86]=y;put(0xb49,world.map_anchor);
        put(0xb87,ScWorldCell(&world,x,y));put(0xb89,tile);put(c->dp+4,sample%7==0?65535:sample%2);
        put(0xbad,sample&2?2500:65536-2000);put(0xbaf,word(0xbad));put(0xbb1,word(0xbad));
        memset(world.fields[0],192,ScWorldFieldSizeWorld(&world,0));
        memset(world.fields[3],80,ScWorldFieldSizeWorld(&world,3));
        if(sample>=32) {
            const unsigned demand_edges[]={0,349,350,65536-350,65536-500,32767,32768,65535};
            unsigned value=demand_edges[sample%8];
            put(0xbad,value);put(0xbaf,value);put(0xbb1,value);
            memset(world.fields[0],(sample*37)&255,ScWorldFieldSizeWorld(&world,0));
            memset(world.fields[2],(sample*19)&255,ScWorldFieldSizeWorld(&world,2));
            unsigned field=2*((y/8)*(ScWorldWidth(&world)/8)+x/8);
            world.fields[12][field]=random;world.fields[12][field+1]=random>>8;
        }
        ScDevelopment initial_state={.entry=zones[kind],.end=ends[kind],.capacity=entries[kind],.skip=skips[kind],
            .attempt=attempts[kind],.cell=(uint16_t)world.map_anchor,.dp=c->dp+(kind==2?10:8),.sp=c->sp+2,
            .remaining=sample<32?49:3,.repeating=true,.attempts=2,.extra_attempts=1};
        snapshot_world=world;Interp816 initial=*c;memcpy(snapshot_ram,ram,sizeof ram);
        ScDevelopment expected_state=initial_state;unsigned total=0,steps=0;
        while(expected_state.remaining) {
            assert(++steps<200000);
            ScWorldGuestStep(&world,c,ram);
            uint16_t pc=ScDevelopmentStepWorld(&expected_state,&world,ram,c->pc,c->dp,c->sp,1);
            if(pc!=c->pc) {c->pc=pc;c->mf=c->xf=false;}
            if(!expected_state.remaining) break;
            ScWorldGuestBegin(&guest,&world,c,rom,sizeof rom);total+=interp816_runOpcode(c);
        }
        Interp816 expected=*c;expected_world=world;memcpy(expected_ram,ram,sizeof ram);
        *c=initial;world=snapshot_world;memcpy(ram,snapshot_ram,sizeof ram);memset(&guest,0,sizeof guest);
        ScDevelopment state=initial_state;unsigned cost=0,hits=0;steps=0;
        while(state.remaining) {
            assert(++steps<200000);
            ScWorldGuestStep(&world,c,ram);
            uint16_t pc=ScDevelopmentStepWorld(&state,&world,ram,c->pc,c->dp,c->sp,1);
            if(pc!=c->pc) {c->pc=pc;c->mf=c->xf=false;}
            if(!state.remaining) break;
            unsigned fast=ScDevelopmentNativeBatch(&state,&world,c,ram,rom,sizeof rom);
            if(!fast) fast=ScWorldGuestBatchStep(&world,c,ram,rom,sizeof rom,8000);
            if(fast) {cost+=fast;++hits;continue;}
            ScWorldGuestBegin(&guest,&world,c,rom,sizeof rom);cost+=interp816_runOpcode(c);
        }
        if(cost!=total || memcmp(ram,expected_ram,sizeof ram))
            fprintf(stderr,"batch map %u kind %u sample %u cycles %u/%u hits %u\n",map,kind,sample,cost,total,hits);
        assert(cost==total && hits && !memcmp(ram,expected_ram,sizeof ram));
        assert(!memcmp(&world,&expected_world,sizeof world));
        assert(state.entry==expected_state.entry && state.end==expected_state.end &&
            state.capacity==expected_state.capacity && state.attempt==expected_state.attempt &&
            state.remaining==expected_state.remaining && state.repeating==expected_state.repeating &&
            state.attempts==expected_state.attempts && state.extra_attempts==expected_state.extra_attempts);
        assert(!memcmp(&c->a,&expected.a,(char *)&c->cyclesUsed-(char *)&c->a+1));++cases;
    }
    interp816_free(c);printf("PASS: %u native C accelerated batches match complete zone execution\n",cases);
}
static uint64_t batch_test_ticks;
static uint64_t batch_test_clock(void) {return ++batch_test_ticks;}
static void distributed_batches(void) {
    const unsigned entries[]={0x937a,0x92ce,0x922f},tiles[]={0x84,0x13b,0x1f8};
    growth_fixture=true;
    unsigned cases=0;
    for(unsigned map=0;map<6;++map) for(unsigned kind=0;kind<3;++kind)
    for(unsigned powered=0;powered<2;++powered) {
        large=map>0;huge=map>=2;giant=map>=3;colossal=map>=4;mega=map==5;powered_fixture=powered;
        run(entries[kind],tiles[kind],1,true);
        memcpy(expected_ram,ram,sizeof ram);expected_world=world;
        memcpy(ram,initial_ram,sizeof ram);world=initial_world;
        ScDevelopmentBatches *b=ScDevelopmentBatchesCreate();assert(b);
        unsigned count=0;
        for(unsigned frame=0;count<1 && frame<1;frame+=8)
            count+=ScDevelopmentBatchesRun(b,&world,ram,rom,sizeof rom,frame,50,1,NULL,0,0,NULL);
        assert(count==1 && ScDevelopmentBatchesZones(b)==1 && ScDevelopmentBatchesPasses(b)==1);
        if(memcmp(ram+0xccf,expected_ram+0xccf,14))fprintf(stderr,"batch RNG map=%u kind=%u powered=%u\n",map,kind,powered);
        assert(!memcmp(ram+0xccf,expected_ram+0xccf,14));
        if(large) {
            if(memcmp(world.tiles,expected_world.tiles,2*ScWorldCells(&world))) {
                fprintf(stderr,"batch world tiles map=%u kind=%u powered=%u\n",map,kind,powered);
                for(unsigned at=0;at<2*ScWorldCells(&world);at+=2)if(memcmp(world.tiles+at,expected_world.tiles+at,2)) {
                    fprintf(stderr,"tile x=%u y=%u actual=%x expected=%x\n",at/2%ScWorldWidth(&world),at/2/ScWorldWidth(&world),world.tiles[at]|world.tiles[at+1]<<8,expected_world.tiles[at]|expected_world.tiles[at+1]<<8);break;
                }
            }
            assert(!memcmp(world.tiles,expected_world.tiles,2*ScWorldCells(&world)));
            for(unsigned f=0;f<17;++f)if(f!=4)
                assert(!memcmp(world.fields[f],expected_world.fields[f],ScWorldFieldSizeWorld(&world,f)));
            assert(!memcmp(world.fields[4],initial_world.fields[4],ScWorldFieldSizeWorld(&world,4)));
        } else {
            if(memcmp(ram+0x10200,expected_ram+0x10200,24000)) {
                fprintf(stderr,"batch tiles map=%u kind=%u powered=%u\n",map,kind,powered);
                for(unsigned at=0;at<24000;at+=2)if(word(0x10200+at)!=(expected_ram[0x10200+at]|expected_ram[0x10201+at]<<8)) {
                    fprintf(stderr,"tile at=%u actual=%x expected=%x\n",at,word(0x10200+at),expected_ram[0x10200+at]|expected_ram[0x10201+at]<<8);break;
                }
            }
            assert(!memcmp(ram+0x10200,expected_ram+0x10200,24000));
            assert(!memcmp(ram+0x1ae62,expected_ram+0x1ae62,390));
        }
        assert(!memcmp(world.coord,initial_world.coord,sizeof world.coord));
        assert(world.map_anchor==initial_world.map_anchor && world.field_scan==initial_world.field_scan);
        assert(!memcmp(ram,initial_ram,0xccf)); /* calendar, money, UI, stack and census */
        ScDevelopmentBatchesDestroy(b);++cases;
    }
    /* Exact requested weighting when work fits the time allowance; startup
     * synchronization is one pass, then fractional credits govern the rate. */
    const unsigned batch_speeds[]={1,2,3,5,10,20,50};
    for(unsigned i=0;i<sizeof batch_speeds/sizeof *batch_speeds;++i) {
        memcpy(ram,initial_ram,sizeof ram);world=initial_world;
        ScDevelopmentBatches *b=ScDevelopmentBatchesCreate();assert(b);
        for(unsigned frame=0;frame<=800;++frame)
            ScDevelopmentBatchesRun(b,&world,ram,rom,sizeof rom,frame,batch_speeds[i],1000,NULL,0,0,NULL);
        assert(ScDevelopmentBatchesAttempts(b)==batch_speeds[i]+1);
        unsigned before=(unsigned)ScDevelopmentBatchesAttempts(b);ram[0x193]=3;
        assert(!ScDevelopmentBatchesRun(b,&world,ram,rom,sizeof rom,900,50,1000,NULL,0,0,NULL));
        assert(ScDevelopmentBatchesAttempts(b)==before);
        ScDevelopmentBatchesDestroy(b);
    }
    ScWorldReset(&world);world.active=world.huge=world.giant=world.colossal=true;
    memset(ram,0,sizeof ram);ram[0x193]=2;
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<32;++x)
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            ScWorldPutCell(&world,10+x*59+dx,10+y*220+dy,0x80+(dy+1)*3+dx+1);
    ScDevelopmentBatches *b=ScDevelopmentBatchesCreate();assert(b);
    assert(ScDevelopmentBatchesRun(b,&world,ram,rom,sizeof rom,0,1,32,NULL,0,0,NULL)==32);
    assert(ScDevelopmentBatchesZones(b)==256 && ScDevelopmentBatchesLastQuadrants(b)==15);
    batch_test_ticks=0;
    unsigned limited=ScDevelopmentBatchesRun(b,&world,ram,rom,sizeof rom,1,1,256,batch_test_clock,1000,4,NULL);
    assert(limited==24 && batch_test_ticks==5); /* Bounded batches, including probes. */
    ScWorldReset(&world);world.active=world.huge=world.giant=world.colossal=true;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
        ScWorldPutCell(&world,1900+dx,1580+dy,0x80+(dy+1)*3+dx+1);
    assert(ScDevelopmentBatchesRun(b,&world,ram,rom,sizeof rom,2,1,256,NULL,0,0,NULL)==1);
    assert(ScDevelopmentBatchesZones(b)==1); /* A loaded/replaced city's old index is rejected. */
    uint8_t *encoded=malloc(ScWorldEncodedSize());assert(encoded);
    assert(ScWorldEncode(&world,encoded,ScWorldEncodedSize()));
    assert(ScWorldDecode(&world,encoded,ScWorldEncodedSize()));free(encoded);
    assert(ScDevelopmentBatchesRun(b,&world,ram,rom,sizeof rom,3,1,256,NULL,0,0,NULL)==1);
    assert(ScDevelopmentBatchesZones(b)==1 && ScDevelopmentBatchesLastQuadrants(b)==8);
    ScDevelopmentBatchesDestroy(b);
    puts("PASS: each short Normal batch spans all quadrants; host deadline bounds work; world/save load discards stale queues");
    printf("PASS: %u isolated city attempts match original ROM growth/RNG on all sizes; native traffic, census, calendar, suspended cursors and pauses preserved\n",cases);
}
int main(int argc,char **argv) {
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom); fclose(f);
    if(getenv("SC_BATCH_TEST_ONLY")) {distributed_batches();return 0;}
    if(!getenv("SC_SPEED_TEST_ONLY")) {
        native_helpers();native_house_candidates();native_house_mutations();native_batches();
        if(getenv("SC_NATIVE_HELPER_TEST")) return 0;
    }
    const uint16_t entries[]={0x937a,0x92ce,0x922f};
    const unsigned tiles[]={0x99,0x144,0x201};
    const int speeds[]={1,2,3,5,10,20,50};
    const int speed_count=sizeof speeds/sizeof *speeds;
    for (int z=0;z<3;++z) {
        run(entries[z],tiles[z],1,false); memcpy(baseline,ram,sizeof ram);
        run(entries[z],tiles[z],1,true); assert(!memcmp(baseline,ram,sizeof ram));
        for (int i=1;i<speed_count;++i) {
            ScDevelopment s=run(entries[z],tiles[z],speeds[i],true);
            printf("zone=%04x speed=%d attempts=%llu extras=%llu\n",entries[z],speeds[i],
                   (unsigned long long)s.attempts,(unsigned long long)s.extra_attempts);
            assert(s.attempts==(unsigned)speeds[i]);
            assert(s.extra_attempts==(unsigned)speeds[i]-1);
        }
    }
    large=true;
    for (int z=0;z<3;++z) for(int i=0;i<speed_count;++i) {
        ScDevelopment s=run(entries[z],tiles[z],speeds[i],true);
        if (speeds[i]>1) assert(s.attempts==(unsigned)speeds[i] && s.extra_attempts==(unsigned)speeds[i]-1);
    }
    growth_fixture=true;
    const unsigned empty[]={0x84,0x13b,0x1fc};
    for(int size=0;size<6;++size) for (int z=0;z<3;++z) {
        large=size>0;huge=size>=2;giant=size>=3;colossal=size>=4;mega=size==5;powered_fixture=true;
        run(entries[z],empty[z],1,true);uint64_t normal=grown_population;
        run(entries[z],empty[z],50,true);assert(grown_population>normal);
        powered_fixture=false;
        run(entries[z],empty[z],50,true);assert(!grown_population);
    }
    distributed_batches();
    puts("PASS: stock Normal equivalence, RCI attempts, actual growth and power gating on all map sizes, single tallies, calendar, stack integrity and mid-attempt restoration");
    return 0;
}
