#include "sc_power_refresh.h"
#include "sc_construction.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint8_t ram[0x20000],before[0x20000],rom[0x80000];
static ScPowerRefresh power;
static ScWorld world;
static uint8_t expected[SC_WORLD_MAX_CELLS/8],actual[SC_WORLD_MAX_CELLS/8];
static void compare_power(const ScWorld *w) {
    memcpy(before,ram,sizeof ram);
    assert(ScConstructionPowerBitmapReference(ram,w,rom,sizeof rom,expected,sizeof expected));
    assert(ScConstructionPowerBitmap(ram,w,rom,sizeof rom,actual,sizeof actual));
    unsigned bytes=w?ScWorldCells(w)/8:1500;
    if(memcmp(expected,actual,bytes)) {
        for(unsigned i=0;i<bytes;++i) if(expected[i]!=actual[i]) {
            fprintf(stderr,"power oracle mismatch byte %u: %02x != %02x, map %ux%u, b89 %02x%02x\n",
                i,expected[i],actual[i],w?ScWorldWidth(w):120,w?ScWorldHeight(w):100,ram[0xb8a],ram[0xb89]);break;
        }
        assert(!"power oracle mismatch");
    }
    assert(!memcmp(before,ram,sizeof ram));
}
static void put(unsigned p,unsigned v) {ram[p]=(uint8_t)v;ram[p+1]=(uint8_t)(v>>8);}
static unsigned word(unsigned p) {return ram[p]|ram[p+1]<<8;}
static void build(unsigned tool,int x,int y) {
    static ScBuildPlan plan; unsigned cost;
    assert(ScConstructionPlan(&plan,tool,x,y,x,y));
    assert(ScConstructionCommit(ram,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
}
static void build_world(unsigned tool,int x,int y) {
    static ScBuildPlan plan;unsigned cost;
    assert(ScConstructionPlanWorld(&plan,&world,tool,x,y,x,y));
    assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
}
static void regional_cache_tests(void) {
    for(unsigned map=0;map<4;++map) {
        memset(ram,0,sizeof ram);put(0xb9d,60000);ram[0x193]=2;ScWorldReset(&world);
        world.active=true;world.huge=map>=1;world.giant=map>=2;world.colossal=map==3;
        int x=ScWorldWidth(&world)-70,y=ScWorldHeight(&world)-20;
        build_world(14,x,y);build_world(5,x+5,y);build_world(3,x+4,y+1);build_world(14,20,20);
        assert(ScPowerRefreshRestore(&power,ram,&world,rom,sizeof rom,0));
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,0,50,false));
        unsigned wire=ScWorldCell(&world,x+4,y+1),owner=ScWorldCell(&world,x+6,y+1),other=ScWorldCell(&world,21,21);
        assert((owner&0x8000) && (other&0x8000));
        world.fields[5][0]=0x5a;
        ScWorldPutCell(&world,10,10,0x4000); /* metadata is not topology */
        ScWorldPutCell(&world,x+6,y+1,owner&0x7fff);
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,1,50,false));
        assert(ScWorldCell(&world,x+6,y+1)==owner && ScWorldCell(&world,10,10)==0x4000);
        assert(world.fields[5][0]==0x5a);
        /* A topology edit waits for the selected cadence. Power changes in a
         * different chunk remain pending, then repair when that edit is undone. */
        ScWorldPutCell(&world,x+4,y+1,0);
        ScWorldPutCell(&world,21,21,other&0x7fff);
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,2,50,false));
        assert(!(ScWorldCell(&world,21,21)&0x8000));
        ScWorldPutCell(&world,x+4,y+1,wire);
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,3,50,false));
        assert(ScWorldCell(&world,21,21)==other && world.fields[5][0]==0x5a);
        ScWorldPutCell(&world,x+4,y+1,0);
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,4,50,true));
        assert(!(ScWorldCell(&world,x+6,y+1)&0x8000));
        assert(ScWorldCell(&world,21,21)&0x8000);
        ScWorldPutCell(&world,x+4,y+1,wire);
        for(unsigned frame=5;frame<8;++frame)
            assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,frame,50,true));
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,8,50,true));
        assert(ScWorldCell(&world,x+6,y+1)&0x8000);
    }
    puts("PASS: regional power cache keeps metadata, native bitmap ownership, edit/undo publication and exact refresh cadence on all expanded sizes");
}
static void electrical_signature_tests(void) {
    for(unsigned map=0;map<4;++map) {
        memset(ram,0,sizeof ram);put(0xb9d,60000);ram[0x193]=2;ScWorldReset(&world);
        world.active=true;world.huge=map>=1;world.giant=map>=2;world.colossal=map==3;
        int x=ScWorldWidth(&world)-70,y=ScWorldHeight(&world)-20;
        build_world(14,x,y);build_world(5,x+5,y);build_world(3,x+4,y+1);
        assert(ScPowerRefreshRestore(&power,ram,&world,rom,sizeof rom,0));
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,0,50,false));
        /* Different ordinary tile IDs with the same conductivity cannot
         * alter the ordered flood fill. Cover both classes and all 1024 IDs,
         * keeping both plant identities distinct. */
        unsigned replacements[2]={0,0x62};
        assert(!(rom[0x184eb+replacements[0]]&128) && (rom[0x184eb+replacements[1]]&128));
        for(unsigned tile=0;tile<1024;++tile) if(tile!=0x27c && tile!=0x28c) {
            unsigned conductivity=(rom[0x184eb+tile]&128)!=0;
            ScWorldPutCell(&world,tile%64,tile/64,(uint16_t)replacements[conductivity]);
        }
        assert(ScPowerRefreshRestore(&power,ram,&world,rom,sizeof rom,0));
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,0,50,false));
        memcpy(expected,power.bitmap,ScWorldCells(&world)/8);
        for(unsigned tile=0;tile<1024;++tile) if(tile!=0x27c && tile!=0x28c)
            ScWorldPutCell(&world,tile%64,tile/64,tile);
        assert(ScConstructionPowerBitmap(ram,&world,rom,sizeof rom,actual,sizeof actual));
        assert(!memcmp(expected,actual,ScWorldCells(&world)/8));
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,4,50,true));
        assert(!memcmp(expected,world.fields[5],ScWorldCells(&world)/8));
        /* Raw snapshots must advance on an equivalent refresh. A later
         * scratch-only change does not schedule a solve, but the next tile
         * change must use the solver's new traversal input. The full word
         * matters: a powered plant value is not an unflagged seed ID. */
        put(0xb89,0x27c);
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,8,50,true));
        assert(!memcmp(expected,world.fields[5],ScWorldCells(&world)/8));
        ScWorldPutCell(&world,0,0,1); /* both nonconductive */
        assert(!(rom[0x184eb]&128) && !(rom[0x184ec]&128));
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,12,50,true));
        assert(ScConstructionPowerBitmap(ram,&world,rom,sizeof rom,actual,sizeof actual));
        assert(!memcmp(actual,world.fields[5],ScWorldCells(&world)/8));
        assert(memcmp(expected,actual,ScWorldCells(&world)/8));
        memcpy(expected,actual,ScWorldCells(&world)/8);
        put(0xb89,0x827c);
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,16,50,true));
        assert(!memcmp(expected,world.fields[5],ScWorldCells(&world)/8));
        ScWorldPutCell(&world,0,0,0);
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,20,50,true));
        assert(ScConstructionPowerBitmap(ram,&world,rom,sizeof rom,actual,sizeof actual));
        assert(!memcmp(actual,world.fields[5],ScWorldCells(&world)/8));
        assert(memcmp(expected,actual,ScWorldCells(&world)/8));
        /* A plant-type change must still schedule a solve, even though both
         * tile IDs have the same conductivity bit. */
        unsigned plant=ScWorldCell(&world,x+1,y+1)&1023;
        if(!getenv("SC_POWER_POLICY_CACHE_REFERENCE"))assert(power.policy_reuses==1);
        assert(plant==0x27c || plant==0x28c);
        ScWorldPutCell(&world,x+1,y+1,plant==0x27c?0x28c:0x27c);
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,21,50,false));
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,24,50,true));
        assert(ScConstructionPowerBitmap(ram,&world,rom,sizeof rom,actual,sizeof actual));
        assert(!memcmp(actual,world.fields[5],ScWorldCells(&world)/8));
    }
    puts("PASS: all 1024 electrical signatures preserve ordered power results across four expanded sizes; plant types, scratch traversal transitions and cadence remain distinct");
}
static void exhausted_policy_cache_tests(void) {
    for(unsigned map=0;map<4;++map) {
        memset(ram,0,sizeof ram);ram[0x193]=2;ScWorldReset(&world);
        world.active=true;world.huge=map>=1;world.giant=map>=2;world.colossal=map==3;
        unsigned width=ScWorldWidth(&world),height=ScWorldHeight(&world);
        for(unsigned cell=0;cell<ScWorldCells(&world);++cell) {
            world.tiles[2*cell]=0x62;world.tiles[2*cell+1]=0;
        }
        ScWorldTilesTouch(&world,0,2*ScWorldCells(&world));
        ScWorldPutCell(&world,0,0,0x27c);
        ScWorldPutCell(&world,width-1,height-1,0x28c);
        ScWorldPutCell(&world,width/2,height/2,0);
        assert(ScPowerRefreshRestore(&power,ram,&world,rom,sizeof rom,0));
        compare_power(&world);
        assert(!memcmp(actual,power.bitmap,ScWorldCells(&world)/8));
        memcpy(expected,power.bitmap,ScWorldCells(&world)/8);
        /* An exhausted network must reuse precisely the original ordered
         * brownout result when the scratch traversal policy returns. */
        put(0xb89,0x27c);ScWorldPutCell(&world,width/2,height/2,1);
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,4,50,true));
        assert(ScConstructionPowerBitmapReference(ram,&world,rom,sizeof rom,actual,sizeof actual));
        assert(!memcmp(actual,power.bitmap,ScWorldCells(&world)/8));
        assert(memcmp(expected,power.bitmap,ScWorldCells(&world)/8));
        put(0xb89,0x827c);ScWorldPutCell(&world,width/2,height/2,0);
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,8,50,true));
        assert(!memcmp(expected,power.bitmap,ScWorldCells(&world)/8));
        assert(ScConstructionPowerBitmapReference(ram,&world,rom,sizeof rom,actual,sizeof actual));
        assert(!memcmp(actual,power.bitmap,ScWorldCells(&world)/8));
        if(!getenv("SC_POWER_POLICY_CACHE_REFERENCE"))assert(power.policy_reuses==1);
        /* Changing generator capacity invalidates both cached policies. */
        uint64_t solves=power.network_solves;
        ScWorldPutCell(&world,0,0,0x28c);
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,12,50,true));
        assert(power.network_solves==solves+1);
        assert(ScConstructionPowerBitmapReference(ram,&world,rom,sizeof rom,actual,sizeof actual));
        assert(!memcmp(actual,power.bitmap,ScWorldCells(&world)/8));
        assert(memcmp(expected,power.bitmap,ScWorldCells(&world)/8));
        put(0xb89,0x27c);ScWorldPutCell(&world,width/2,height/2,1);
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,16,50,true));
        assert(power.network_solves==solves+2);
        assert(ScConstructionPowerBitmapReference(ram,&world,rom,sizeof rom,actual,sizeof actual));
        assert(!memcmp(actual,power.bitmap,ScWorldCells(&world)/8));
    }
    puts("PASS: exhausted-network policy reuse and generator-capacity invalidation match the ordered interpreter oracle on all expanded sizes");
}
int main(int argc,char **argv) {
    assert(argc==2); FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    if(getenv("SC_POWER_REGIONS_TEST")) {regional_cache_tests();electrical_signature_tests();exhausted_policy_cache_tests();return 0;}
    unsigned conductive[1024],conductive_count=0;
    for(unsigned i=0;i<958;++i)
        if((rom[0x184eb+i]&128) && i!=0x27c && i!=0x28c) conductive[conductive_count++]=i;
    assert(conductive_count);
    /* Differential oracle: disconnected islands, branching cycles,
     * exhausted mixed generation, map edges and 256-coordinate seams.
     * Metadata must not change conductivity; b0f8's scratch input stays exact. */
    for(unsigned map_size=0;map_size<5;++map_size) for(unsigned pattern=0;pattern<6;++pattern) {
        memset(ram,0,sizeof ram);ScWorldReset(&world);
        world.active=map_size>0;world.huge=map_size>=2;world.giant=map_size>=3;world.colossal=map_size==4;
        unsigned width=world.active?ScWorldWidth(&world):120,height=world.active?ScWorldHeight(&world):100;
        uint8_t *tiles=world.active?world.tiles:ram+0x10200;
        uint32_t random=0x9e3779b9u+pattern;
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
            random=random*1664525u+1013904223u;
            bool connected=pattern==0 || (pattern==1?(x%3!=0 || y%3==0):(random>>24)<180);
            unsigned tile=connected?conductive[(random>>16)%conductive_count]:0;
            unsigned at=2*(y*width+x),value=tile|((random>>4)&0xfc00);
            tiles[at]=value;tiles[at+1]=value>>8;
        }
        unsigned seeds[]={width-1,(height-1)*width,width*height-1,(height/2)*width+width/2};
        for(unsigned i=0;i<4;++i) {
            unsigned value=i&1?0x28c:0x27c,at=2*seeds[i];tiles[at]=value;tiles[at+1]=value>>8;
        }
        if(pattern==3) put(0xb89,0x27c);
        if(pattern==4) put(0xb89,0x28c);
        if(pattern==5) put(0xb89,0x827c);
        compare_power(world.active?&world:NULL);
    }
    const int clock_speeds[]={2,3,5,10,20,50};
    const int speeds[]={2,5,10,50};
    for(unsigned i=0;i<6;++i) {
        ScRefreshClock c;ScRefreshClockReset(&c,200);
        assert(ScRefreshClockDue(&c,0,clock_speeds[i]));
        unsigned count=0;
        for(unsigned frame=1;frame<=1000;++frame) count+=ScRefreshClockDue(&c,frame,clock_speeds[i]);
        assert(count==5*(unsigned)clock_speeds[i]);
        assert(!ScRefreshClockDue(&c,1001,1));
    }
    ScRefreshClock c;ScRefreshClockReset(&c,200);
    ScRefreshClockObserve(&c,0);ScRefreshClockObserve(&c,249);ScRefreshClockObserve(&c,402);
    assert(c.budget==402); /* mean native interval 201, not nominal 60 */
    ScRefreshClockReset(&c,125);assert(ScRefreshClockDue(&c,0,50));
    assert(!ScRefreshClockDue(&c,1,50) && !ScRefreshClockDue(&c,2,50));
    assert(ScRefreshClockDue(&c,3,50) && !ScRefreshClockDue(&c,4,50) && ScRefreshClockDue(&c,5,50));
    assert(ScRefreshClockDue(&c,1,10)); /* state-load rewind */
    for(unsigned i=0;i<4;++i) {
        memset(ram,0,sizeof ram);put(0xb9d,60000);ram[0x193]=2;
        build(14,40,40);build(5,45,40); /* actual first nuclear plant */
        ScPowerRefreshReset(&power);
        assert(ScPowerRefreshStep(&power,ram,NULL,rom,sizeof rom,0,speeds[i],true));
        assert(word(0x10200+2*(41*120+41))&0x8000);
        assert(!(word(0x10200+2*(41*120+46))&0x8000));
        build(3,44,41); /* complete the connection just after a refresh */
        for(unsigned frame=1;frame<200/(unsigned)speeds[i];++frame) {
            assert(!ScPowerRefreshStep(&power,ram,NULL,rom,sizeof rom,frame,speeds[i],true));
            assert(!(word(0x10200+2*(41*120+46))&0x8000));
        }
        assert(ScPowerRefreshStep(&power,ram,NULL,rom,sizeof rom,200/speeds[i],speeds[i],true));
        assert(word(0x10200+2*(41*120+46))&0x8000);
        memset(ram+0x1a598,0x5a,1500);ram[0x10201+2*(41*120+46)]&=0x7f;
        memcpy(before,ram,sizeof ram);
        assert(!ScPowerRefreshStep(&power,ram,NULL,rom,sizeof rom,201/speeds[i],speeds[i],false));
        assert(word(0x10200+2*(41*120+46))&0x8000);
        assert(!memcmp(before+0x1a598,ram+0x1a598,1500)); /* native flood fill owns bitmap */
        ScPowerRefreshStep(&power,ram,NULL,rom,sizeof rom,201/speeds[i],speeds[i],true);
        assert(memcmp(before+0x1a598,ram+0x1a598,1500));
    }
    for(unsigned map_size=0;map_size<4;++map_size) for(unsigned i=0;i<4;++i) {
        memset(ram,0,sizeof ram);put(0xb9d,60000);ram[0x193]=2;
        ScWorldReset(&world);world.active=true;world.huge=map_size>=1;world.giant=map_size>=2;world.colossal=map_size==3;
        int x=world.colossal?1880:world.giant?920:world.huge?440:200,y=world.colossal?1570:world.giant?770:world.huge?370:180;
        build_world(14,x,y);build_world(5,x+5,y);
        ScPowerRefreshReset(&power);
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,0,speeds[i],true));
        assert(ScWorldCell(&world,x+1,y+1)&0x8000);
        assert(!(ScWorldCell(&world,x+6,y+1)&0x8000));
        build_world(3,x+4,y+1);
        for(unsigned frame=1;frame<200/(unsigned)speeds[i];++frame)
            assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,frame,speeds[i],true));
        assert(ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,200/speeds[i],speeds[i],true));
        assert(ScWorldCell(&world,x+6,y+1)&0x8000);
        /* The save codec can discard flags while native census counters are
         * still zero. Rebuild from plant tiles before any decline attempt. */
        for(unsigned cell=0;cell<ScWorldCells(&world);++cell) world.tiles[2*cell+1]&=127;
        put(0xe0d,0);put(0xe0f,0);
        assert(ScPowerRefreshRestore(&power,ram,&world,rom,sizeof rom,800));
        assert(ScWorldCell(&world,x+1,y+1)&0x8000);
        assert(ScWorldCell(&world,x+6,y+1)&0x8000);
        build_world(5,x+15,y); /* disconnected zone stays genuinely unpowered */
        assert(ScPowerRefreshRestore(&power,ram,&world,rom,sizeof rom,801));
        assert(!(ScWorldCell(&world,x+16,y+1)&0x8000));
        world.fields[5][0]=0x5a;ScWorldPutCell(&world,x+6,y+1,ScWorldCell(&world,x+6,y+1)&0x7fff);
        assert(!ScPowerRefreshStep(&power,ram,&world,rom,sizeof rom,802,1,false));
        assert(ScWorldCell(&world,x+6,y+1)&0x8000);
        assert(world.fields[5][0]==0x5a);
    }
    memset(ram,0,sizeof ram);put(0xb9d,60000);build(14,40,40);build(5,45,40);build(5,60,40);
    build(3,44,41);put(0xe0d,0);put(0xe0f,0);
    assert(ScPowerRefreshRestore(&power,ram,NULL,rom,sizeof rom,0));
    assert(word(0x10200+2*(41*120+46))&0x8000);
    assert(!(word(0x10200+2*(41*120+61))&0x8000));
    for(unsigned bits=0;bits<256;++bits) {
        power.bitmap[0]=(uint8_t)bits;
        for(unsigned cell=0;cell<8;++cell) put(0x10200+2*cell,0x6c00);
        assert(!ScPowerRefreshStep(&power,ram,NULL,rom,sizeof rom,bits+1,1,false));
        for(unsigned cell=0;cell<8;++cell)
            assert(word(0x10200+2*cell)==(0x6c00|(bits&(128>>cell)?0x8000:0)));
    }
    /* A valid 4x4 plant grid can have more than 65,534 seeds on 1920x1600.
     * The complete host solve and restore must retain every disconnected seed,
     * even when the guest's plant/branch counters cannot represent them. */
    memset(ram,0,sizeof ram);ScWorldReset(&world);
    world.active=world.huge=world.giant=world.colossal=true;
    for(unsigned i=0;i<70000;++i) ScWorldPutCell(&world,1+4*(i%480),1+4*(i/480),0x28c);
    assert(ScConstructionPowerBitmap(ram,&world,rom,sizeof rom,actual,sizeof actual));
    for(unsigned i=0;i<70000;++i) {
        unsigned at=(1+4*(i/480))*1920+1+4*(i%480);
        assert(actual[at/8]&(128>>(at&7)));
    }
    assert(ScPowerRefreshRestore(&power,ram,&world,rom,sizeof rom,0));
    for(unsigned i=0;i<70000;++i) assert(ScWorldCell(&world,1+4*(i%480),1+4*(i/480))==0x828c);
    assert(!ScWorldCell(&world,1919,1599));
    assert(!memcmp(actual,world.fields[5],ScWorldCells(&world)/8));
    regional_cache_tests();
    electrical_signature_tests();
    exhausted_policy_cache_tests();
    puts("PASS: 70,000 full-width power seeds and reload publication on 1920x1600");
    puts("PASS: 30 ordered power oracle comparisons, speed ratios on every map size, fractional cadence, nuclear-first connection, full-map reload recovery, disconnected zones and native bitmap ownership");
}
