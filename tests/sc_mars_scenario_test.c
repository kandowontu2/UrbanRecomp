#include "sc_mars_scenario.h"
#include "sc_land_type.h"
#include "sc_construction.h"
#include "sc_program.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ScWorld world;
static ScPopulation population;
static ScWorldGuest guest;
static uint8_t ram[0x20000],rom[0x80000];
static uint16_t product,quotient,dividend;
static uint8_t multiplicand;
static unsigned word(unsigned a){return ram[a]|(unsigned)ram[a+1]<<8;}
static void put(unsigned a,unsigned v){ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static uint8_t read_bus(void *context,uint32_t a){
    (void)context;uint8_t v;if(ScWorldGuestRead(&guest,a,&v))return v;
    unsigned bank=a>>16,p=a&65535;
    if(a==0x0194e2 || a==0x0194a3 || a==0x00842e)return 0x6b;
    /* Only presentation/modal/disaster waits are removed for longitudinal QA.
     * All city computation, money, commute and growth stays cartridge-native. */
    if(bank==3 && (p==0xb84b || p==0xc030 || p==0xc086 || p==0xc0ad || p==0xc474 || p==0xc500))return 0x60;
    if(bank==0x7e || bank==0x7f)return ram[a-0x7e0000];
    if((bank&0x7f)<0x40 && p<0x2000)return ram[p];
    if(p==0x4214)return quotient;
    if(p==0x4215)return quotient>>8;
    if(p==0x4216)return product;
    if(p==0x4217)return product>>8;
    if(p>=0x8000 && (bank&0x7f)<16)return rom[(bank&15)*32768+p-32768];
    return 0;
}
static void write_bus(void *context,uint32_t a,uint8_t v){
    (void)context;if(ScWorldGuestWrite(&guest,a,v))return;
    unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f)ram[a-0x7e0000]=v;
    else if((bank&0x7f)<0x40 && p<0x2000)ram[p]=v;
    else if(p==0x4202)multiplicand=v;
    else if(p==0x4203)product=multiplicand*v;
    else if(p==0x4204)dividend=(dividend&0xff00)|v;
    else if(p==0x4205)dividend=(dividend&255)|(v<<8);
    else if(p==0x4206){quotient=v?dividend/v:65535;product=v?dividend%v:dividend;}
}
static void goals(void){
    uint8_t *original=malloc(sizeof ram);assert(original);memcpy(original,ram,sizeof ram);
    size_t n=ScWorldEncodedSize();uint8_t *data=malloc(n);ScWorld *copy=malloc(sizeof *copy);assert(data && copy);
    world.mars_progress=994;world.mars_sample=37;world.mars_clean_months=11;
    world.mars_notice=3;world.mars_announcing=true;
    assert(ScWorldEncode(&world,data,n) && data[7]==10 && ScWorldDecode(copy,data,n));
    assert(copy->mars_status==1 && copy->mars_progress==994 && copy->mars_sample==37 &&
        copy->mars_clean_months==11 && copy->mars_notice==3 && copy->mars_announcing);
    uint8_t *cities=malloc(ScWorldCitiesSize()),sram[0x8000]={0};assert(cities);
    ScWorldCitiesInit(cities);
    assert(ScWorldCitySave(cities,sram,0,&world));
    assert(ScWorldCityLoad(copy,cities,ScWorldCitiesSize(),sram,0));
    assert(copy->mars_progress==994 && copy->mars_sample==37 && copy->mars_clean_months==11 && copy->mars_notice==3);
    sram[16]=1;assert(!ScWorldCityLoad(copy,cities,ScWorldCitiesSize(),sram,0));sram[16]=0;
    uint8_t *upgraded=malloc(ScWorldCitiesSize());assert(upgraded);
    /* Legacy active Mars saves retain the colony and deadline, but previous
     * easier qualification must not count toward the tougher objectives. */
    cities[7]=9;uint8_t *payload=cities+24+16;payload[7]=9;
    uint32_t legacy_mars=1|(634u<<2)|(37u<<12)|(5u<<19)|(3u<<22)|(1u<<25);
    for(unsigned i=0;i<4;++i)payload[84+i]=legacy_mars>>(8*i);
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<n;++i)hash=(hash^payload[i])*1099511628211ull;
    for(unsigned i=0;i<8;++i)cities[24+8+i]=(uint8_t)(hash>>(i*8));
    assert(ScWorldCitiesUpgrade(upgraded,cities,ScWorldCitiesSize()));
    assert(ScWorldCityLoad(copy,upgraded,ScWorldCitiesSize(),sram,0) && copy->mars_status==1 &&
        !copy->mars_progress && !copy->mars_sample && !copy->mars_clean_months &&
        !copy->mars_notice && !copy->mars_announcing && copy->calendar_year==2050 && copy->land_type==SC_LAND_MARS);
    assert(!memcmp(copy->tiles,world.tiles,sizeof world.tiles));
    payload[84]=(payload[84]&~3u)|2; /* Preserve earned v9 victories. */
    assert(ScWorldDecode(copy,payload,n) && copy->mars_status==2 && copy->mars_clean_months==SC_MARS_CLEAN_MONTHS);
    cities[7]=8;payload[7]=8;memset(payload+84,0,4);
    hash=14695981039346656037ull;
    for(size_t i=0;i<n;++i)hash=(hash^payload[i])*1099511628211ull;
    for(unsigned i=0;i<8;++i)cities[24+8+i]=(uint8_t)(hash>>(i*8));
    assert(ScWorldCitiesUpgrade(upgraded,cities,ScWorldCitiesSize()));
    assert(ScWorldCityLoad(copy,upgraded,ScWorldCitiesSize(),sram,0) && !copy->mars_status);
    free(upgraded);free(cities);
    data[87]|=0x80;assert(!ScWorldDecode(copy,data,n));
    world.mars_clean_months=13;assert(!ScWorldEncode(&world,data,n));
    world.mars_progress=world.mars_sample=world.mars_clean_months=world.mars_notice=0;world.mars_announcing=false;
    assert(ScWorldEncode(&world,data,n));data[7]=8;memset(data+84,0,4);
    assert(ScWorldDecode(copy,data,n) && !copy->mars_status);
    free(copy);free(data);
    /* Objective-boundary fixture, not a natural-growth sustainability claim. */
    memset(ram+0x10200,0,24000);memset(ram+0x18270,0,3000);
    for(unsigned i=0;i<SC_MARS_PARKS;++i)put(0x10200+2*i,20);
    for(unsigned i=0;i<20;++i)put(0x10200+2*(5000+i*9),0x8000|0x400|0x120);
    ScPopulationSet(&population,ram,SC_MARS_POPULATION);
    put(0xb53,2050);put(0xb55,4);world.calendar_year=2050;world.mars_status=1;
    ScMarsStats s;ScMarsScenarioInspect(&world,&population,ram,rom,&s);assert(s.clean && s.months_left==60);
    unsigned last=0x10200+2*(SC_MARS_PARKS-1);
    put(last,0);ScMarsScenarioInspect(&world,&population,ram,rom,&s);assert(!s.clean);put(last,20);
    ScPopulationSet(&population,ram,SC_MARS_POPULATION-1);
    ScMarsScenarioInspect(&world,&population,ram,rom,&s);assert(!s.clean);
    ScPopulationSet(&population,ram,SC_MARS_POPULATION);
    memset(ram+0x18270,SC_MARS_POLLUTION,3000);ScMarsScenarioInspect(&world,&population,ram,rom,&s);assert(s.clean);
    ram[0x18270]++;ScMarsScenarioInspect(&world,&population,ram,rom,&s);assert(!s.clean);
    memset(ram+0x18270,0,3000);memset(ram+0x18270,80,30);ScMarsScenarioInspect(&world,&population,ram,rom,&s);assert(s.clean);
    ram[0x18270+30]=80;ScMarsScenarioInspect(&world,&population,ram,rom,&s);assert(!s.clean);
    memset(ram+0x18270,0,3000);
    put(0x10200+2*5000,0x400|0x120);ScMarsScenarioInspect(&world,&population,ram,rom,&s);assert(s.clean); /* 19/20 = 95% */
    put(0x10200+2*5009,0x400|0x120);ScMarsScenarioInspect(&world,&population,ram,rom,&s);assert(!s.clean);
    put(0x10200+2*5000,0x8000|0x400|0x120);put(0x10200+2*5009,0x8000|0x400|0x120);
    memset(ram+0x18270,120,3000);ScMarsScenarioObserve(&world,&population,ram,rom);
    assert(world.mars_progress==470 && !world.mars_clean_months);
    memset(ram+0x18270,0,3000);world.mars_sample=0;
    ScMarsScenarioObserve(&world,&population,ram,rom);
    for(unsigned i=0;i<100;++i)ScMarsScenarioObserve(&world,&population,ram,rom);
    assert(!world.mars_clean_months && world.mars_status==1 && world.mars_progress==940);
    for(unsigned m=5;m<=10;++m){put(0xb55,m);ScMarsScenarioObserve(&world,&population,ram,rom);}
    assert(world.mars_clean_months==6 && world.mars_status==1); /* Old six-month win no longer suffices. */
    ScPopulationSet(&population,ram,SC_MARS_POPULATION-1);ScMarsScenarioObserve(&world,&population,ram,rom);
    assert(!world.mars_clean_months); /* Losing any goal resets the whole streak, even within one month. */
    ScPopulationSet(&population,ram,SC_MARS_POPULATION);
    for(unsigned m=11;m<=22;++m){world.calendar_year=2050+(m-1)/12;put(0xb53,world.calendar_year);put(0xb55,1+(m-1)%12);
        ScMarsScenarioObserve(&world,&population,ram,rom);assert(world.mars_status==(m==22?2:1));}
    assert(world.mars_clean_months==12 && world.mars_progress==1000 && world.mars_notice==2 && world.land_type==SC_LAND_NATIVE);
    Interp816 c={0};c.k=1;c.pc=0x8b9f;put(0x397,0);put(0x391,0);put(0xd7,0);
    ScMarsScenarioStep(&world,&population,&c,ram,rom);assert(word(0x397)==4 && !world.mars_announcing);
    c.pc=0xe59b;ScMarsScenarioStep(&world,&population,&c,ram,rom);assert(c.a==0xfc00 && world.mars_announcing);
    uint8_t v;assert(ScMarsScenarioMessageRead(&world,0x0ffc00,&v) && v=='T');
    c.pc=0xa63c;ScMarsScenarioStep(&world,&population,&c,ram,rom);assert(word(0xd87)==2 && !world.mars_notice);
    c.k=3;c.pc=0xe30a;c.y=18;c.a=5;ScMarsScenarioStep(&world,&population,&c,ram,rom);assert(c.a==0x205 && c.pc==0xe30d);
    world.mars_status=1;world.mars_clean_months=0;world.mars_sample=1;world.calendar_year=2050;put(0xb53,2050);
    put(0xb55,8);ScMarsScenarioObserve(&world,&population,ram,rom);assert(!world.mars_clean_months);
    world.calendar_year=2055;put(0xb53,2055);put(0xb55,3);put(last,0);
    ScMarsScenarioObserve(&world,&population,ram,rom);assert(world.mars_status==1);
    put(0xb55,4);ScMarsScenarioObserve(&world,&population,ram,rom);assert(world.mars_status==3);
    memcpy(ram,original,sizeof ram);free(original);
    c.k=3;c.pc=0xddc3;put(0x42,0);ScMarsScenarioSelectorStep(&c,ram,false);assert(ram[0x79]==4);
    c.pc=0xde1c;put(0x52,3);put(0x54,0);ram[0xca]=1;
    ScMarsScenarioSelectorStep(&c,ram,false);assert(word(0x52)==4 && word(0x54)==1 && word(0x40)==9);
    c.pc=0xde31;ScMarsScenarioSelectorStep(&c,ram,false);assert(word(0x22)==160);
    c.pc=0xde1c;put(0x52,3);ram[0xca]=2;ScMarsScenarioSelectorStep(&c,ram,false);assert(word(0x52)==2);
    put(0x52,4);put(0x54,0);put(0x42,0x8000);ScMarsScenarioSelectorStep(&c,ram,true);assert(word(0x40)==8);
    put(0x54,1);ScMarsScenarioSelectorStep(&c,ram,true);assert(word(0x40)==9);
    puts("PASS: tougher objective boundaries, twelve-month streak/reset, v8/v9/v10 saves, messages and Mars win bit");
}
static void plant(unsigned target,bool local){
    ScMarsStats s;ScMarsScenarioInspect(&world,&population,ram,rom,&s);
    ScBuildPlan *plan=calloc(1,sizeof *plan);assert(plan);unsigned cost;
    plan->tool=4;
    for(unsigned y=0;y<100 && s.parks+plan->count<target;++y)
        for(unsigned x=local?0:45;x<120 && s.parks+plan->count<target;++x)
            if(!(word(0x10200+2*(y*120+x))&1023))plan->cells[plan->count++]=(ScBuildCell){(int)x,(int)y};
    assert(s.parks+plan->count==target);
    if(plan->count)assert(ScConstructionCommit(ram,rom,sizeof rom,plan,&cost)==SC_BUILD_OK);free(plan);
}
static bool cleanup(bool restore){
    ScBuildPlan *plan=calloc(1,sizeof *plan);assert(plan);unsigned cost;
    plan->tool=0;unsigned plants[20][2],count=0;
    for(unsigned y=0;y<100;++y)for(unsigned x=0;x<120;++x){
        unsigned t=word(0x10200+2*(y*120+x))&1023;
        if(t==0x28c){assert(count<20);plants[count][0]=x-1;plants[count++][1]=y-1;}
        if(t==0x28c || t==0x21c)plan->cells[plan->count++]=(ScBuildCell){(int)x,(int)y};
    }
    assert(ScConstructionCommit(ram,rom,sizeof rom,plan,&cost)==SC_BUILD_OK);
    /* Real demolition leaves rubble and charges its actual cost. */
    plan->count=0;
    for(unsigned y=0;y<100;++y)for(unsigned x=0;x<120;++x){
        unsigned t=word(0x10200+2*(y*120+x))&1023;
        if(t>=40 && t<48)plan->cells[plan->count++]=(ScBuildCell){(int)x,(int)y};
    }
    assert(ScConstructionCommit(ram,rom,sizeof rom,plan,&cost)==SC_BUILD_OK);
    plan->tool=13;plan->count=3;
    for(unsigned n=0;n<3;++n)plan->cells[n]=(ScBuildCell){(int)plants[n*8][0],(int)plants[n*8][1]};
    assert(ScConstructionCommit(ram,rom,sizeof rom,plan,&cost)==SC_BUILD_OK);free(plan);
    /* The former demonstration used 1200 eastern parks. The larger cleanup
     * also adds parks in cleared western coal parcels, near existing homes. */
    plant(restore?SC_MARS_PARKS:1200,restore);
    assert(ScConstructionRefreshPower(ram,&world,rom,sizeof rom));
    assert(ScConstructionPrimeFields(ram,rom,sizeof rom));return true;
}
static void report(FILE *csv,unsigned tick){
    ScPopulationRefreshLive(&population,ram,&world,rom,sizeof rom);
    ScMarsScenarioObserve(&world,&population,ram,rom);
    ScMarsStats s;ScMarsScenarioInspect(&world,&population,ram,rom,&s);
    fprintf(csv,"%u,%u,%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",tick,word(0xb53),word(0xb55),
        (unsigned long long)s.population,s.parks,s.pollution,s.hot_cells,s.powered,s.zones,world.mars_progress,world.mars_clean_months,world.mars_status,word(0xb9d)|(ram[0xb9f]<<16));
    fflush(csv);printf("Mars tick=%u %u/%u pop=%llu parks=%u pollution=%u hot=%u power=%u/%u progress=%u streak=%u status=%u demand=%d/%d/%d funds=%u\n",tick,
        word(0xb53),word(0xb55),(unsigned long long)s.population,s.parks,s.pollution,s.hot_cells,s.powered,s.zones,world.mars_progress,world.mars_clean_months,world.mars_status,(int16_t)word(0xbad),(int16_t)word(0xbaf),(int16_t)word(0xbb1),word(0xb9d)|(ram[0xb9f]<<16));fflush(stdout);
}
int main(int argc,char **argv){
    assert(argc==3 || argc==5 || argc==6);
    FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    f=fopen(argv[2],"rb");assert(f);assert(fread(ram,1,sizeof ram,f)==sizeof ram);fclose(f);
    uint8_t *video=malloc(0x6000);assert(video);memcpy(video,ram+0x8000,0x6000);
    unsigned screen=word(0x14),scheduler=word(0x1e7);
    assert(ScProgramSelectRom(0xec01686a));assert(ScMarsScenarioGenerate(&world,&population,ram,rom,sizeof rom));
    assert(word(0x14)==screen && word(0x1e7)==scheduler && !memcmp(video,ram+0x8000,0x6000));free(video);
    ScMarsStats s;ScMarsScenarioInspect(&world,&population,ram,rom,&s);
    assert(!s.clean && !s.parks && s.population>10000 && s.pollution>=40 && s.hot_cells>30);
    if(argc==3){goals();return 0;}
    bool restore=argc==6 && !strcmp(argv[5],"--restore");
    if(argc==6)assert(restore || !strcmp(argv[5],"--clean"));
    if(argc==6)assert(cleanup(restore));
    put(0x193,2);put(0xb51,0);put(0x23d,0);put(0x1b3,0);put(0xd7,0);put(0x395,0);
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);interp816_reset(cpu);
    cpu->k=cpu->db=3;cpu->pc=0x90a7;cpu->sp=0x1ffd;cpu->dp=0x1e00;
    cpu->mf=cpu->xf=cpu->e=false;cpu->i=true;put(0x1ffe,0x6fff);
    while(cpu->pc!=0x7000)ScProgramExecute(cpu);
    cpu->pc=0x8016;unsigned completed=0,ticks=atoi(argv[3]);bool entered=false;
    uint64_t instructions=0;FILE *csv=fopen(argv[4],"w");assert(csv);
    fputs("tick,year,month,population,parks,pollution,hot,powered,zones,progress,clean_months,result,funds\n",csv);report(csv,0);
    while(completed<ticks){
        if(cpu->k==3 && cpu->pc==0x8016){
            if(entered){++completed;if(!(completed%4) || completed==ticks)report(csv,completed);}
            entered=true;if(completed==ticks)break;
        }
        ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
        if(cpu->k==3 && cpu->pc==0x8ece)put(0xdc3,0);
        if(read_bus(NULL,((uint32_t)cpu->k<<16)|cpu->pc)==0)cpu->pc+=2;
        else ScProgramExecute(cpu);
        assert(!cpu->stopped && !cpu->waiting && ++instructions<10000000000ull);
    }
    if(ticks==240){
        ScMarsScenarioInspect(&world,&population,ram,rom,&s);
        assert(world.mars_status==(restore?2:3));
        if(restore)assert(world.mars_clean_months==SC_MARS_CLEAN_MONTHS && world.land_type==SC_LAND_NATIVE);
    }
    fclose(csv);interp816_free(cpu);puts("PASS: full native multi-year scenario simulation (presentation/disasters excluded)");
}
