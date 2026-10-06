/* Longitudinal layout audit. Run the cartridge's complete simulation loop,
 * without a display/event clock. Only presentation, modal UI and disasters
 * are excluded from this controlled comparison; demand, taxes, commuting,
 * growth RNG, population, services, power and calendar remain cartridge code.
 * End-to-end guest replays separately cover real scheduling and disasters. */
#include "sc_test_city.h"
#include "sc_program.h"
#include "sc_world_guest.h"
#include "sc_construction.h"
#include "sc_land_summary.h"
#include "snes/interp816.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static ScWorld world;
static ScPopulation population;
static ScWorldGuest guest;
static uint8_t ram[0x20000],rom[0x80000];
static uint16_t product,quotient,dividend;
static uint8_t multiplicand;
static unsigned word(const uint8_t *p,unsigned a) {return p[a]|p[a+1]<<8;}
static void put(unsigned a,unsigned v) {ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static uint8_t read_bus(void *context,uint32_t a) {
    (void)context;uint8_t v;
    if(ScWorldGuestRead(&guest,a,&v))return v;
    unsigned bank=a>>16,p=a&65535;
    /* The cartridge's redraw helpers are long calls, with no growth side
     * effects. The loop's three UI/disaster calls are short calls. */
    if(a==0x0194e2 || a==0x0194a3 || a==0x00842e)return 0x6b;
    if(bank==3 && (p==0xb84b || p==0xc030 || p==0xc086 || p==0xc0ad || p==0xc474 || p==0xc500))return 0x60;
    if(bank==0x7e || bank==0x7f)return ram[a-0x7e0000];
    if((bank&0x7f)<0x40 && p<0x2000)return ram[p];
    if(p==0x4214)return (uint8_t)quotient;
    if(p==0x4215)return quotient>>8;
    if(p==0x4216)return (uint8_t)product;
    if(p==0x4217)return product>>8;
    if(p>=0x8000 && (bank&0x7f)<16)return rom[(bank&15)*32768+p-32768];
    return 0;
}
static void write_bus(void *context,uint32_t a,uint8_t v) {
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
static void report(FILE *csv,unsigned ticks) {
    unsigned width=world.active?ScWorldWidth(&world):120,height=world.active?ScWorldHeight(&world):100;
    const uint8_t *tiles=world.active?world.tiles:ram+0x10200;
    const uint8_t *land=world.active?world.fields[0]:ram+0x16b00;
    const uint8_t *pollution=world.active?world.fields[2]:ram+0x18270;
    const uint8_t *density=world.active?world.fields[3]:ram+0x18e28;
    unsigned r=0,empty=0,houses=0,apartments=0,tops=0,low_density=0,polluted=0,poor=0,unpowered=0,c=0,c5=0;
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
        unsigned raw=word(tiles,2*(y*width+x)),t=raw&1023;
        if(!(rom[0x184eb+t]&1))continue;
        if(t>=0x137 && t<0x1f4){++c;c5+=t==0x168 || t==0x195 || t==0x1c2 || t==0x1ef;}
        if(t<0x80 || (t>=0x129 && t<0x376) || t>=0x39a)continue;
        ++r;unsigned i=(y/2)*(width/2)+x/2;
        if(t==0x84) {
            unsigned h=0;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                unsigned tile=word(tiles,2*((y+dy)*width+x+dx))&1023;
                h+=tile>=0x89 && tile<=0x94;
            }
            if(h)++houses;else ++empty;
            low_density+=density[i]<65;polluted+=pollution[i]>=129;
            poor+=land[i]<pollution[i] || land[i]-pollution[i]<94;
        } else {++apartments;tops+=t>=0x376;}
        unpowered+=!(raw&0x8000);
    }
    ScPopulationRefreshLive(&population,ram,&world,rom,sizeof rom);
    fprintf(csv,"%u,%u,%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%d,%d,%u\n",ticks,
        word(ram,0xb53),word(ram,0xb55),(unsigned long long)population.value,r,empty,houses,apartments,tops,
        low_density,polluted,poor,unpowered,c,c5,(int16_t)word(ram,0xbad),(int16_t)word(ram,0xbaf),(int16_t)word(ram,0xbb1),word(ram,0xb9d)+(ram[0xb9f]<<16));
    fflush(csv);
    printf("tick=%u %u/%02u population=%llu residential=%u empty=%u houses=%u developed=%u low-density=%u polluted=%u poor=%u power-missing=%u demand=%d/%d/%d\n",ticks,
        word(ram,0xb53),word(ram,0xb55),(unsigned long long)population.value,r,empty,houses,apartments,
        low_density,polluted,poor,unpowered,(int16_t)word(ram,0xbad),(int16_t)word(ram,0xbaf),(int16_t)word(ram,0xbb1));fflush(stdout);
}
static void original_routine(Interp816 *cpu,unsigned entry) {
    interp816_reset(cpu);memset(&guest,0,sizeof guest);
    cpu->k=cpu->db=3;cpu->pc=entry;cpu->sp=0x1ffd;cpu->dp=0x1e00;
    cpu->mf=cpu->xf=cpu->e=false;cpu->i=true;put(0x1ffe,0x6fff);
    unsigned steps=0;
    while(cpu->pc!=0x7000) {
        if(read_bus(NULL,0x30000|cpu->pc)==0)cpu->pc+=2;else ScProgramExecute(cpu);
        assert(++steps<50000000 && !cpu->stopped && !cpu->waiting);
    }
}
static int effects(const char *out) {
    uint8_t initial[sizeof ram];memcpy(initial,ram,sizeof ram);
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);
    ScBuildPlan *plan=calloc(1,sizeof *plan);assert(plan);
    FILE *csv=fopen(out,"w");assert(csv);
    fputs("tool,gift,alignment,owner,terrain_total,terrain_max,terrain_cells,pollution_per_footprint,annual_special,casino_crime\n",csv);
    for(unsigned tool=1;tool<=15;++tool)for(unsigned gift=tool==15?1:0;gift<=(tool==15?15:0);++gift)
        for(unsigned alignment=0;alignment<2;++alignment) {
            memcpy(ram,initial,sizeof ram);memset(ram+0x10200,0,0xfe00);
            put(0xb9d,0xffff);ram[0xb9f]=0xff;put(0xb51,1);
            unsigned x=alignment?43:40,y=x;
            if(gift==6)for(unsigned dy=0;dy<3;++dy)for(unsigned dx=0;dx<3;++dx)
                put(0x10200+2*((y+dy)*120+x+dx),1);
            ram[0x3f5]=gift;put(0x3f3,0);unsigned cost;
            assert(ScConstructionPlan(plan,tool,x,y,x,y));
            assert(ScConstructionCommit(ram,rom,sizeof rom,plan,&cost)==SC_BUILD_OK);
            unsigned owner=0,pollution=0;
            for(unsigned i=0;i<12000;++i) {
                unsigned t=word(ram,0x10200+2*i)&1023,c;
                if(rom[0x184eb+t]&1)owner=t;
                pollution=(pollution+(int16_t)ScLandTilePollution(t,&c))&65535;
                put(0x10200+2*i,word(ram,0x10200+2*i)|0x8000);
            }
            original_routine(cpu,0x821d);original_routine(cpu,0x8297);
            original_routine(cpu,0x9c11);
            unsigned total=0,max=0,nonzero=0;
            for(unsigned i=0;i<750;++i){unsigned v=ram[0x1ab74+i];total+=v;if(v>max)max=v;nonzero+=v!=0;}
            static const unsigned annual[16]={0,0,0,200,100,300,0,0,0,100,0,100,100,100,0,100};
            assert(word(ram,0xddd)==annual[gift]);
            assert(word(ram,0xc71)==(gift==5));
            if(tool==15 && gift!=1 && gift!=6)assert(total==(alignment?1012:250));
            fprintf(csv,"%u,%u,%u,%x,%u,%u,%u,%u,%u,%u\n",tool,gift,alignment,owner,total,max,nonzero,pollution,word(ram,0xddd),word(ram,0xc71));
            fflush(csv);
        }
    fclose(csv);free(plan);interp816_free(cpu);puts("PASS: native placement/terrain/economy audit for all tools and 15 gifts");return 0;
}
int main(int argc,char **argv) {
    assert(argc==6 || argc==5);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    f=fopen(argv[2],"rb");assert(f);assert(fread(ram,1,sizeof ram,f)==sizeof ram);fclose(f);
    assert(ScProgramSelectRom(0xec01686a));if(!strcmp(argv[3],"--effects"))return effects(argv[4]);
    unsigned mode=(unsigned)atoi(argv[3]),ticks=(unsigned)atoi(argv[4]);ScTestCityStats stats;
    assert(ScTestCityGenerate(&world,&population,ram,rom,sizeof rom,mode,1,&stats));
    /* Native initial funding, ordinary 7% tax and fast in-game speed. */
    put(0xbc5,32);put(0xbc7,1000);put(0xbc9,1000);if(getenv("SC_CITY_TAX"))put(0xdc5,atoi(getenv("SC_CITY_TAX")));put(0x193,2);put(0xb51,0);
    put(0x23d,0);put(0x1b3,0);put(0xd7,0);put(0x395,0);
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);interp816_reset(cpu);
    cpu->k=cpu->db=3;cpu->pc=0x90a7;cpu->sp=0x1ffd;cpu->dp=0x1e00;
    cpu->mf=cpu->xf=cpu->e=false;cpu->i=true;put(0x1ffe,0x6fff);
    while(cpu->pc!=0x7000)ScProgramExecute(cpu);
    cpu->pc=0x8016;unsigned completed=0;bool entered=false;unsigned long long instructions=0;
    FILE *csv=fopen(argv[5],"w");assert(csv);
    fputs("ticks,year,month,population,residential,empty,houses,developed,tops,low_density,polluted,poor,unpowered,commercial,c5,r_demand,c_demand,i_demand,treasury\n",csv);report(csv,0);
    while(completed<ticks) {
        if(cpu->k==3 && cpu->pc==0x8016) {
            if(entered){++completed;if(!(completed%48) || completed==ticks)report(csv,completed);}
            entered=true;if(completed==ticks)break;
        }
        ScWorldGuestStep(&world,cpu,ram);ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
        /* Confirm the annual budget's native figures, exactly as its UI
         * would do. Do not skip tax collection or funding recalculation. */
        if(cpu->k==3 && cpu->pc==0x8ece)put(0xdc3,0);
        /* $00 is the cooperative yield marker; it has no simulation effect. */
        if(read_bus(NULL,((uint32_t)cpu->k<<16)|cpu->pc)==0x00)cpu->pc+=2;
        else ScProgramExecute(cpu);
        assert(!cpu->stopped && !cpu->waiting && ++instructions<10000000000ull);
    }
    char path[1024];snprintf(path,sizeof path,"%s.ram",argv[5]);f=fopen(path,"wb");assert(f);fwrite(ram,1,sizeof ram,f);fclose(f);
    fclose(csv);interp816_free(cpu);printf("PASS: %u complete native simulation ticks, %llu instruction edges\n",completed,instructions);return 0;
}
