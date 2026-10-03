#include "sc_population.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t rom[0x80000], ram[0x20000], baseline[0x20000];
static uint8_t read_bus(void *ctx,uint32_t a) {
    (void)ctx; unsigned bank=a>>16, p=a&65535;
    if (bank==0x7e || bank==0x7f) return ram[a-0x7e0000];
    if (p<0x2000) return ram[p];
    if (p>=0x8000) return rom[(bank&15)*32768+p-0x8000];
    return 0;
}
static void write_bus(void *ctx,uint32_t a,uint8_t v) {
    (void)ctx; unsigned bank=a>>16,p=a&65535;
    if (bank==0x7e || bank==0x7f) ram[a-0x7e0000]=v;
    else if (p<0x2000) ram[p]=v;
}
static void put(unsigned p,unsigned v) { ram[p]=(uint8_t)v; ram[p+1]=(uint8_t)(v>>8); }
static uint32_t word(unsigned p) { return ram[p]|((unsigned)ram[p+1]<<8); }
static uint32_t dword(unsigned p) { return word(p)|(word(p+2)<<16); }
static void routine(ScPopulation *s,bool enhanced) {
    put(0x1ffe,0x6fff);
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus); assert(cpu);
    interp816_reset(cpu); cpu->k=cpu->db=3; cpu->pc=0x8196;
    cpu->sp=0x1ffd; cpu->dp=0x1e00; cpu->e=cpu->mf=cpu->xf=false; cpu->i=true;
    unsigned steps=0;
    while (cpu->pc!=0x7000) {
        assert(++steps<10000);
        if (enhanced) {
            uint16_t next=ScPopulationStep(s,ram,cpu->pc,cpu->dp);
            if (next!=cpu->pc) { cpu->y=(uint16_t)ScPopulationClass(s->value); cpu->mf=cpu->xf=false; }
            cpu->pc=next;
        }
        interp816_runOpcode(cpu);
    }
    assert(cpu->sp==0x1fff && cpu->dp==0x1e00);
    interp816_free(cpu);
}
/* Oracle: stop the original zone handler immediately after its capacity
 * helper, before transport/development changes anything in the fixture. */
static unsigned native_capacity(unsigned tile,unsigned kind) {
    const unsigned entries[]={0x937a,0x92ce,0x922f};
    const unsigned stops[]={0x93a4,0x92ee,0x9242};
    put(0xb89,tile); put(0xb87,tile); ram[0xb85]=60; ram[0xb86]=50;
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus); assert(cpu);
    interp816_reset(cpu); cpu->k=cpu->db=3; cpu->pc=entries[kind];
    cpu->sp=0x1ffd; cpu->dp=0x1e00; cpu->e=cpu->mf=cpu->xf=false; cpu->i=true;
    unsigned steps=0;
    while (cpu->pc!=stops[kind]) { assert(++steps<10000); interp816_runOpcode(cpu); }
    unsigned result=word(cpu->dp);
    interp816_free(cpu); return result;
}
static void live_census_tests(void) {
    ScPopulation s={0};
    /* Compare every RCI centre from the ROM's own dispatch table with the
     * original capacity code, including stadium/combined zone variants. */
    for (unsigned tile=0x80;tile<958;++tile) {
        if (!(rom[0x184eb+tile]&1)) continue;
        unsigned kind;
        if (tile<0x129 || (tile>=0x376 && tile<0x39a)) kind=0;
        else if ((tile>=0x137 && tile<0x1f4) || tile>=0x39a) kind=1;
        else if (tile>=0x1f4 && tile<0x249) kind=2;
        else continue;
        memset(ram,0,sizeof ram); memset(&s,0,sizeof s);
        put(0x10200+2*(50*120+60),tile|0xc000);
        unsigned expected=native_capacity(tile,kind)*(kind?160:20);
        memcpy(baseline,ram,sizeof ram);
        assert(ScPopulationRefreshLive(&s,ram,NULL,rom,sizeof rom));
        assert(s.live && s.value==expected);
        assert(!memcmp(ram,baseline,sizeof ram));
    }
    /* Empty free zones contribute zero; actual houses count one each. The
     * eight surrounding cells never become eight duplicate zone tallies. */
    memset(ram,0,sizeof ram); memset(&s,0,sizeof s);
    put(0x10200+2*(50*120+60),0x84);
    for (unsigned houses=0;houses<=8;++houses) {
        unsigned n=0;
        for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
            if (!dx && !dy) continue;
            put(0x10200+2*((50+dy)*120+60+dx),n++<houses?0x89+n%12:0x80);
        }
        unsigned expected=native_capacity(0x84,0)*20;
        assert(expected==houses*20);
        assert(ScPopulationRefreshLive(&s,ram,NULL,rom,sizeof rom) && s.value==expected);
    }
    /* Growth/demolition are visible without a native census. Partial native
     * sweep accumulators, history and calendar must not be replaced. */
    s.previous=100; s.capacity[0]=1234; s.tally_active[0]=1;
    s.history[0]=77; s.history_count=1;
    put(0xb53,1991); put(0xb55,6);
    put(0x10200+2*(50*120+60),0x99);
    memcpy(baseline,ram,sizeof ram);
    assert(ScPopulationRefreshLive(&s,ram,NULL,rom,sizeof rom) && s.value==320 && s.change==220);
    assert(s.capacity[0]==1234 && s.tally_active[0]==1 && s.previous==100);
    assert(s.history[0]==77 && s.history_count==1 && !memcmp(baseline,ram,sizeof ram));
    routine(&s,true); /* The stale sweep would incorrectly produce 24,680. */
    assert(s.value==320 && dword(0xba5)==320 && s.change==220);
    ScPopulationReport(&s,ram,false);
    assert(word(0x2840+0x11c*2)==0x420 && word(0x2840+0x11b*2)==0x422);
    put(0x10200+2*(50*120+60),0);
    assert(ScPopulationRefreshLive(&s,ram,NULL,rom,sizeof rom) && s.value==0 && s.change==-100);
    assert(word(0xb53)==1991 && word(0xb55)==6 && s.history_count==1);
    uint8_t data[SC_POPULATION_BYTES]; ScPopulationEncode(&s,data);
    ScPopulation restored={0}; assert(ScPopulationDecode(&restored,data,sizeof data) && restored.live);
    data[77]=0; assert(ScPopulationDecode(&restored,data,sizeof data) && !restored.live); /* Beta 1/2 */
    data[77]=2; assert(!ScPopulationDecode(&restored,data,sizeof data));
    /* Full-width and second-bank centres, including houses at the far edge. */
    ScWorld *w=calloc(1,sizeof *w); assert(w); w->active=true;
    memset(ram,0,sizeof ram); memset(&s,0,sizeof s);
    put(0x10200+2*(50*120+60),0x99); /* normal mirror must not be counted */
    unsigned pos=2*(198*240+238);
    w->tiles[pos]=0x84;
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
        if (!dx && !dy) continue;
        w->tiles[2*((198+dy)*240+238+dx)]=0x89;
    }
    assert(ScPopulationRefreshLive(&s,ram,w,rom,sizeof rom) && s.value==160);
    pos=2*(100*240+200); w->tiles[pos]=0x44; w->tiles[pos+1]=1; /* C: 0x144 */
    pos=2*(180*240+200); w->tiles[pos]=1; w->tiles[pos+1]=2; /* I: 0x201 */
    assert(ScPopulationRefreshLive(&s,ram,w,rom,sizeof rom) && s.value==480);
    ScPopulation snapshot=s;
    assert(!ScPopulationRefreshLive(&s,ram,w,NULL,sizeof rom) && !memcmp(&s,&snapshot,sizeof s));
    free(w);

}
static void compare_cached_census(ScPopulationCensus *c,ScPopulation *s,ScWorld *w) {
    ScPopulation reference=*s;
    memcpy(baseline,ram,sizeof ram);
    uint64_t epoch=ScWorldStateEpoch();
    assert(ScPopulationRefreshLive(&reference,ram,w,rom,sizeof rom));
    assert(ScPopulationRefreshCached(c,s,ram,w,rom,sizeof rom));
    assert(!memcmp(s,&reference,sizeof reference));
    assert(!memcmp(ram,baseline,sizeof ram) && epoch==ScWorldStateEpoch());
}
static void cached_census_tests(void) {
    ScWorld *w=calloc(1,sizeof *w),*other=calloc(1,sizeof *other);assert(w && other);
    ScPopulationCensus *c=ScPopulationCensusCreate();assert(c);
    ScPopulation s={0};uint32_t random=0x892f321u;
    for(unsigned scale=0;scale<4;++scale) {
        ScWorldReset(w);w->active=true;w->huge=scale>=1;w->giant=scale>=2;w->colossal=scale==3;
        unsigned width=ScWorldWidth(w),height=ScWorldHeight(w),cells=ScWorldCells(w);
        memset(ram,0,sizeof ram);memset(&s,0,sizeof s);
        s.valid=true;s.previous=98765;s.capacity[0]=1234;s.tally_active[0]=1;s.history[0]=77;s.history_count=1;
        /* Dense mixed RCI, non-RCI and metadata patterns, including every
         * original center variant and the far banks of the largest map. */
        for(unsigned i=0;i<cells;++i) {
            unsigned tile=(i*37+i/width*13)%1024;
            w->tiles[2*i]=(uint8_t)tile;w->tiles[2*i+1]=(uint8_t)(tile>>8)|((i&3)<<6);
        }
        ScWorldTilesTouch(w,0,cells*2);
        compare_cached_census(c,&s,w);assert(ScPopulationCensusEvaluatedCells(c)==cells);
        compare_cached_census(c,&s,w);assert(ScPopulationCensusEvaluatedCells(c)==0);
        /* Power/animation changes must not cause a recount or alter totals. */
        for(unsigned i=0;i<cells;i+=113) ScWorldPutCell(w,i%width,i/width,ScWorldCell(w,i%width,i/width)^0xc000);
        compare_cached_census(c,&s,w);assert(ScPopulationCensusEvaluatedCells(c)==0);
        /* Free centers immediately across a chunk seam, one row above it,
         * and in the last corner depend on edits outside their own chunk. */
        unsigned positions[]={256*11,256*11+width,256*11-width,cells-1};
        for(unsigned j=0;j<4;++j) {
            unsigned pos=positions[j],x=pos%width,y=pos/width;
            ScWorldPutCell(w,x,y,0x84);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx)
                if(dx || dy) ScWorldPutCell(w,(int)x+dx,(int)y+dy,0);
            compare_cached_census(c,&s,w);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                if((!dx && !dy) || !ScWorldContains(w,(int)x+dx,(int)y+dy)) continue;
                ScWorldPutCell(w,(int)x+dx,(int)y+dy,0x89);
                compare_cached_census(c,&s,w);
                assert(ScPopulationCensusEvaluatedCells(c)<4096);
                ScWorldPutCell(w,(int)x+dx,(int)y+dy,0);
                compare_cached_census(c,&s,w);
            }
        }
        for(unsigned pass=0;pass<80;++pass) {
            for(unsigned edit=0;edit<1+pass%19;++edit) {
                random=random*1664525u+1013904223u;unsigned pos=random%cells;
                random=random*1664525u+1013904223u;unsigned tile=random&65535;
                ScWorldPutCell(w,pos%width,pos/width,tile);
            }
            compare_cached_census(c,&s,w);
        }
        /* Switching the tracked world invalidates shared tile revisions.
         * Returning to the first world must still notice subsequent edits. */
        ScWorldReset(other);other->active=true;ScWorldPutCell(other,100,100,0x99);
        compare_cached_census(c,&s,other);compare_cached_census(c,&s,w);
        ScWorldPutCell(w,width-1,height-1,0x144);compare_cached_census(c,&s,w);
        /* Decode/load and a same-pointer reset cannot reuse the old census. */
        size_t bytes=ScWorldEncodedSize();uint8_t *encoded=malloc(bytes);assert(encoded);
        assert(ScWorldEncode(w,encoded,bytes));ScWorldPutCell(w,width-1,height-1,0x201);
        compare_cached_census(c,&s,w);assert(ScWorldDecode(w,encoded,bytes));free(encoded);
        compare_cached_census(c,&s,w);assert(ScPopulationCensusEvaluatedCells(c)==cells);
        ScWorldReset(w);w->active=true;w->huge=scale>=1;w->giant=scale>=2;w->colossal=scale==3;
        compare_cached_census(c,&s,w);assert(s.value==0);
    }
    w->active=false;put(0x10200+2*(50*120+60),0x99);
    compare_cached_census(c,&s,w);assert(s.value==320);
    ScPopulation saved=s;
    assert(!ScPopulationRefreshCached(c,&s,ram,w,NULL,sizeof rom));assert(!memcmp(&saved,&s,sizeof s));
    assert(ScPopulationRefreshCached(NULL,&s,ram,w,rom,sizeof rom));assert(s.value==320);
    ScPopulationCensusDestroy(c);free(other);free(w);
    puts("PASS: incremental census equals full census on all expanded sizes through dense/sparse edits, house halos/chunk seams/edges, metadata, world switches and reload/reset; unchanged maps recount zero cells");
}
static void census_benchmark(void) {
    if(!getenv("SC_CENSUS_BENCH")) return;
    ScWorld *w=calloc(1,sizeof *w);assert(w);
    ScWorldReset(w);w->active=w->huge=w->giant=w->colossal=true;
    unsigned cells=ScWorldCells(w),width=ScWorldWidth(w);
    const unsigned types[]={0x84,0x99,0x144,0x201,0x89,0x137,0x95,0x376,0x39a};
    for(unsigned i=0;i<cells;++i) {
        unsigned tile=types[(i+i/width)%9]|0x8000;
        w->tiles[2*i]=(uint8_t)tile;w->tiles[2*i+1]=(uint8_t)(tile>>8);
    }
    ScWorldTilesTouch(w,0,2*cells);
    ScPopulation a={0},b={0};ScPopulationCensus *c=ScPopulationCensusCreate();assert(c);
    assert(ScPopulationRefreshLive(&a,ram,w,rom,sizeof rom));
    assert(ScPopulationRefreshCached(c,&b,ram,w,rom,sizeof rom));assert(!memcmp(&a,&b,sizeof a));
    clock_t start=clock();
    for(unsigned i=0;i<64;++i) assert(ScPopulationRefreshLive(&a,ram,w,rom,sizeof rom));
    double full=(double)(clock()-start)/CLOCKS_PER_SEC/64;
    start=clock();
    for(unsigned i=0;i<2048;++i) assert(ScPopulationRefreshCached(c,&b,ram,w,rom,sizeof rom));
    double unchanged=(double)(clock()-start)/CLOCKS_PER_SEC/2048;
    assert(!memcmp(&a,&b,sizeof a));
    start=clock();uint64_t evaluated=0;
    for(unsigned i=0;i<512;++i) {
        unsigned pos=(i*23741u+5127)%cells;
        ScWorldPutCell(w,pos%width,pos/width,(i&1)?0x99:0x201);
        assert(ScPopulationRefreshCached(c,&b,ram,w,rom,sizeof rom));
        evaluated+=ScPopulationCensusEvaluatedCells(c);
    }
    double sparse=(double)(clock()-start)/CLOCKS_PER_SEC/512;
    assert(ScPopulationRefreshLive(&a,ram,w,rom,sizeof rom));assert(!memcmp(&a,&b,sizeof a));
    printf("CENSUS BENCH: 1920x1600 filled map, full %.6f ms/call, unchanged %.6f ms/call, single-edit %.6f ms/call, mean %.1f recounted cells/edit; totals exact\n",
        full*1000,unchanged*1000,sparse*1000,(double)evaluated/512);
    ScPopulationCensusDestroy(c);free(w);
}
int main(int argc,char **argv) {
    assert(argc==2); FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom); fclose(f);
    memset(ram,0,sizeof ram); put(0xb8b,12345); put(0xb93,300); put(0xb8f,200);
    put(0xbcd,31000); ScPopulation s={0}; ScPopulationImport(&s,ram);
    memcpy(baseline,ram,sizeof ram); routine(&s,false);
    uint8_t expected[sizeof ram]; memcpy(expected,ram,sizeof ram);
    memcpy(ram,baseline,sizeof ram); routine(&s,true);
    assert(!memcmp(ram,expected,sizeof ram)); assert(s.value==326900);

    memset(ram,0,sizeof ram); memset(&s,0,sizeof s);
    put(0xb8b,65535); put(0xb93,65535); put(0xb8f,65535);
    routine(&s,true); assert(s.value==UINT64_C(22281900));
    assert(dword(0xba5)==999999 && word(0xdeb)==5);
    /* Two tallies cross the 16-bit accumulator boundary, without adding the
     * extra development attempts a second time. */
    ScPopulationStep(&s,ram,0x8228,0x1e00); put(0x1e00,65530);
    ScPopulationStep(&s,ram,0x93b7,0x1e00); put(0x1e00,48);
    ScPopulationStep(&s,ram,0x93b7,0x1e00); assert(s.capacity[0]==65578);
    routine(&s,true); assert(s.value==(UINT64_C(65578)+UINT64_C(131070)*8)*20);

    /* Exercise the CALCULATION near ten billion, without a display override.
     * A fully counted sweep can exceed both guest 16-bit capacity words and
     * its old 32-bit population words. */
    for (unsigned i=0;i<3;++i) s.tally_active[i]=1;
    s.capacity[0]=UINT64_C(499999999); s.capacity[1]=s.capacity[2]=0;
    routine(&s,true); assert(s.value==UINT64_C(9999999980));
    s.capacity[0]=UINT64_C(500000000);
    routine(&s,true); assert(s.value==UINT64_C(10000000000));
    s.capacity[0]=0; s.capacity[1]=UINT64_C(62499999);
    routine(&s,true); assert(s.value==UINT64_C(9999999840));
    s.capacity[1]=0; s.capacity[2]=UINT64_C(62500000);
    routine(&s,true); assert(s.value==UINT64_C(10000000000));
    s.capacity[0]=UINT64_C(250000000); s.capacity[1]=UINT64_C(20000000); s.capacity[2]=UINT64_C(11250000);
    routine(&s,true); assert(s.value==UINT64_C(10000000000));
    /* The actual capacity calculation also crosses 32 bits and the former
     * ten-billion limit, then saturates at the new thirteen-digit ceiling. */
    s.capacity[0]=UINT64_C(499999999999);s.capacity[1]=s.capacity[2]=0;
    routine(&s,true);assert(s.value==UINT64_C(9999999999980));
    s.capacity[0]=UINT64_C(500000000000);
    routine(&s,true);assert(s.value==SC_POPULATION_MAX);
    s.capacity[0]=0;s.capacity[1]=UINT64_C(62499999999);
    routine(&s,true);assert(s.value==UINT64_C(9999999999840));
    s.capacity[1]=0;s.capacity[2]=UINT64_C(62500000000);
    routine(&s,true);assert(s.value==SC_POPULATION_MAX);
    s.capacity[0]=s.capacity[1]=s.capacity[2]=SC_POPULATION_MAX;
    routine(&s,true);assert(s.value==SC_POPULATION_MAX);
    ScPopulationStep(&s,ram,0xb564,0);
    s.capacity[0]=s.capacity[1]=s.capacity[2]=0;
    routine(&s,true); assert(s.value==0 && s.change==-(int64_t)SC_POPULATION_MAX);
    /* A subsequent visit to the epilogue must not replace the true delta with
     * the six-digit compatibility delta in guest memory. */
    ScPopulationStep(&s,ram,0x821b,0); assert(s.change==-(int64_t)SC_POPULATION_MAX);

    ScPopulationSet(&s,ram,SC_POPULATION_MAX);
    assert(s.value==SC_POPULATION_MAX && word(0xdeb)==5);
    ScPopulationStep(&s,ram,0xb564,0); assert(s.previous==SC_POPULATION_MAX);
    ScPopulationSet(&s,ram,UINT64_MAX); assert(s.value==SC_POPULATION_MAX);
    ScPopulationSet(&s,ram,0); assert(s.change==-(int64_t)SC_POPULATION_MAX);
    ScPopulationReport(&s,ram,true); assert(word(0x2840+(0x19c-13)*2)==0x142f);
    ScPopulationSet(&s,ram,SC_POPULATION_MAX); ScPopulationReport(&s,ram,false);
    for (unsigned i=0;i<13;++i) assert(word(0x2840+(0x13c-12+i)*2)==0x429);
    for (unsigned i=0x117;i<=0x11c;++i) assert(word(0x2840+i*2)==0x3ff);
    ScPopulationSet(&s,ram,1); memcpy(baseline,ram,sizeof ram);
    ScPopulationReport(&s,ram,false); assert(!memcmp(baseline,ram,sizeof ram));
    const uint64_t boundaries[]={0,1999,2000,9999,10000,49999,50000,99999,100000,499999,500000,
        999999,1000000,UINT64_C(4294967296),UINT64_C(9999999999),UINT64_C(10000000000),
        UINT64_C(9999999999980),SC_POPULATION_MAX};
    for (unsigned i=0;i<sizeof boundaries/sizeof *boundaries;++i) {
        ScPopulationSet(&s,ram,boundaries[i]);
        uint8_t bytes[SC_POPULATION_BYTES]; ScPopulationEncode(&s,bytes);
        ScPopulation t={0}; assert(ScPopulationDecode(&t,bytes,sizeof bytes));
        assert(!memcmp(&s,&t,sizeof s));
        assert(!ScPopulationDecode(&t,bytes,sizeof bytes-1));
        bytes[7]=2; assert(!ScPopulationDecode(&t,bytes,sizeof bytes));
    }
    unsigned start_head=s.history_head;
    for (unsigned i=0;i<SC_POPULATION_HISTORY+20;++i) ScPopulationStep(&s,ram,0xb564,0);
    assert(s.history_count==SC_POPULATION_HISTORY && s.history_head==(start_head+20)%SC_POPULATION_HISTORY);
    uint8_t cities[SC_POPULATION_CITIES_BYTES], sram[0x8000];
    for (unsigned i=0;i<sizeof sram;++i) sram[i]=(uint8_t)(i*17+i/43);
    ScPopulationCitiesInit(cities); ScPopulation t={0};
    assert(!ScPopulationCityLoad(&t,cities,sizeof cities,sram,0));
    ScPopulationSet(&s,ram,SC_POPULATION_MAX); ScPopulationCitySave(cities,sram,0,&s);
    ScPopulationSet(&s,ram,UINT64_C(4294967296)); ScPopulationCitySave(cities,sram,1,&s);
    assert(ScPopulationCityLoad(&t,cities,sizeof cities,sram,0) && t.value==SC_POPULATION_MAX);
    assert(ScPopulationCityLoad(&t,cities,sizeof cities,sram,1) && t.value==UINT64_C(4294967296));
    ++sram[0x5000]; assert(!ScPopulationCityLoad(&t,cities,sizeof cities,sram,1));
    assert(ScPopulationCityLoad(&t,cities,sizeof cities,sram,0));
    assert(!ScPopulationCityLoad(&t,cities,sizeof cities-1,sram,0));
    cities[7]=2; assert(!ScPopulationCityLoad(&t,cities,sizeof cities,sram,0));
    live_census_tests();
    cached_census_tests();
    census_benchmark();
    puts("PASS: native small-city equivalence, 9,999,999,999,999 calculation/report/history/save support, live census vs every native RCI capacity, houses, growth/removal, stale sweep protection, full-world counts and cadence");
}
