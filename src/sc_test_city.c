#include "sc_test_city.h"
#include "sc_construction.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned word(const uint8_t *p,unsigned a) {return p[a]|p[a+1]<<8;}
static void put(uint8_t *p,unsigned a,unsigned v) {p[a]=(uint8_t)v;p[a+1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,unsigned a,uint32_t v) {put(p,a,v);put(p,a+2,v>>16);}
static uint32_t get32(const uint8_t *p,unsigned a) {return word(p,a)|(uint32_t)word(p,a+2)<<16;}
static bool commit(uint8_t *ram,ScWorld *w,const uint8_t *rom,ScBuildPlan *plan) {
    if(!plan->count)return true;
    unsigned price;ScBuildResult result=ScConstructionCommitWorld(ram,w,rom,0x80000,plan,&price);
    if(result!=SC_BUILD_OK)fprintf(stderr,"[test city] construction tool %u failed: %u\n",plan->tool,result);
    return result==SC_BUILD_OK;
}
static bool single(uint8_t *ram,ScWorld *w,const uint8_t *rom,ScBuildPlan *plan,unsigned tool,int x,int y) {
    plan->tool=tool;plan->count=1;plan->cells[0]=(ScBuildCell){x,y};return commit(ram,w,rom,plan);
}
static void clear_parcel(ScWorld *w,unsigned x,unsigned y) {
    for(unsigned dy=2;dy<8;++dy)for(unsigned dx=2;dx<8;++dx)ScWorldPutCell(w,x+dx,y+dy,0);
}
void ScTestCityInspect(const ScWorld *w,const ScPopulation *p,const uint8_t *rom,ScTestCityStats *s) {
    memset(s,0,sizeof *s);s->population=p->value;
    for(unsigned i=0;i<ScWorldCells(w);++i) {
        unsigned raw=word(w->tiles,2*i),t=raw&1023;
        if(t>=958 || !(rom[0x184eb+t]&1))continue;
        bool zone=false;
        if((t>=0x80 && t<0x129) || (t>=0x376 && t<0x39a)) {++s->residential;zone=true;}
        else if((t>=0x137 && t<0x1f4) || t>=0x39a) {++s->commercial;zone=true;}
        else if(t>=0x1f4 && t<0x249) {++s->industrial;zone=true;}
        else if(t==0x249)++s->police;
        else if(t==0x252)++s->fire;
        else if(t==0x27c || t==0x28c)++s->plants;
        else if(t==0x25c || t==0x36b)++s->stadiums;
        else if(t==0x26c)++s->seaports;
        else if(t==0x2a5)++s->airports;
        else if(t>=0x2bb && t<0x354)++s->rewards;
        if(zone) {++s->zones;if(raw&0x8000)++s->powered_zones;}
    }
}
bool ScTestCityGenerate(ScWorld *w,ScPopulation *population,uint8_t *ram,
    const uint8_t *rom,size_t size,ScTestCityStats *stats) {
    if(!w || !ram || !rom || size!=0x80000)return false;
    uint8_t *prototype=calloc(1,0x20000);ScBuildPlan *plan=calloc(1,sizeof *plan);
    if(!prototype || !plan) {free(prototype);free(plan);return false;}
    memcpy(prototype,ram,0x10000);put(prototype,0xb9d,0xffff);prototype[0xb9f]=0xff;
    /* A surrounded native prototype supplies real joins at repeated district
     * boundaries. Road/power corridors are separate; rail crosses both. */
    for(unsigned tool=1;tool<=3;++tool) {
        plan->tool=tool;plan->count=0;
        unsigned pitch=tool==2?32:8,offset=tool==2?29:tool==3?1:0;
        for(unsigned line=offset;line<97;line+=pitch)for(unsigned at=0;at<97;++at) {
            plan->cells[plan->count++]=(ScBuildCell){line,at};
            plan->cells[plan->count++]=(ScBuildCell){at,line};
        }
        if(!commit(prototype,NULL,rom,plan))goto fail;
    }
    for(unsigned by=0;by<96;by+=32)for(unsigned bx=0;bx<96;bx+=32) {
        if(!single(prototype,NULL,rom,plan,13,bx+2,by+2) ||
           !single(prototype,NULL,rom,plan,13,bx+18,by+18))goto fail;
        for(unsigned row=0;row<8;++row) {
            unsigned y=2+(row/2)*8+(row%2)*3;
            for(unsigned col=0;col<8;++col) {
                unsigned x=2+(col/2)*8+(col%2)*3;
                if(x>=29 || y>=29 || (x<8 && y<8) || (x>=16 && x<24 && y>=16 && y<24))continue;
                unsigned tool=x==10 && y==10?8:x==26 && y==26?9:x==5 && y==13?4:
                    ((x/3+y/3)%10<5?5:(x/3+y/3)%10<8?6:7);
                if(!single(prototype,NULL,rom,plan,tool,bx+x,by+y))goto fail;
                if(tool>=5 && tool<=7) {
                    /* Genuine ordinary-capacity developed forms. The census
                     * reads the ROM's usual rules; no people are fabricated. */
                    unsigned base=tool==5?0x113:tool==6?0x1ac:0x233;
                    for(unsigned dy=0;dy<3;++dy)for(unsigned dx=0;dx<3;++dx) {
                        unsigned a=0x10200+2*((by+y+dy)*120+bx+x+dx);
                        put(prototype,a,(word(prototype,a)&0xfc00)|base+dy*3+dx);
                    }
                }
            }
        }
    }
    if(!ScConstructionRefreshPower(prototype,NULL,rom,size) ||
       !ScConstructionPrimeFields(prototype,rom,size))goto fail;
    ScWorldReset(w);w->active=w->huge=w->giant=w->colossal=w->test_city=true;
    for(unsigned y=0;y<1600;++y)for(unsigned x=0;x<1920;++x)
        put(w->tiles,2*(y*1920+x),word(prototype,0x10200+2*((32+y%32)*120+32+x%32)));
    for(unsigned f=0;f<17;++f) {
        if(f==5)continue; /* The full-world power network is rebuilt below. */
        const ScWorldField *field=&ScWorldFields[f];unsigned divisor=120/field->stock_width;
        unsigned pitch=32/divisor,anchor=32/divisor,fw=ScWorldFieldWidth(w,f),fh=ScWorldFieldHeight(w,f);
        for(unsigned y=0;y<fh;++y)for(unsigned x=0;x<fw;++x)
            memcpy(w->fields[f]+(y*fw+x)*field->element_bytes,
                prototype+0x10000+field->base+((anchor+y%pitch)*field->stock_width+anchor+x%pitch)*field->element_bytes,
                field->element_bytes);
    }
    /* Genuine coast, with a connected waterfront and periodic seaports. */
    for(unsigned y=0;y<1600;++y)for(unsigned x=1888;x<1920;++x)ScWorldPutCell(w,x,y,1);
    put(ram,0xb9d,0xffff);ram[0xb9f]=0xff;
    for(unsigned y=32;y<1600;y+=128) {
        clear_parcel(w,1880,y);
        if(!single(ram,w,rom,plan,11,1884,y+2))goto fail;
    }
    /* Central civic precinct: one of every buildable gift, plus stadiums
     * and airports. Infrastructure around each cleared parcel is retained. */
    for(unsigned n=0;n<16;++n) {
        unsigned x=896+(n%8)*8,y=768+(n/8)*8;clear_parcel(w,x,y);
        unsigned tool=n<13?15:n==13?10:12;
        if(tool==15) {
            unsigned gift=n+1;if(gift>=6)++gift;
            ram[0x3f5]=(uint8_t)gift;put(ram,0x3f3,0);
        }
        if(!single(ram,w,rom,plan,tool,x+2,y+2))goto fail;
    }
    if(!ScConstructionRefreshPower(ram,w,rom,size))goto fail;
    w->center_valid=true;w->center_x=960;w->center_y=800;
    put(ram,0x1bd,896);put(ram,0x1bf,744);put(ram,0x1c5,1895);put(ram,0x1c9,1578);
    put(ram,0xb53,1960);put(ram,0xb55,1);put(ram,0xb57,0);
    put(ram,0xb9d,0x1200);ram[0xb9f]=0x7a; /* 8 million ordinary treasury */
    static const char name[]="TEST CITY";ram[0xb5b]=sizeof name-1;
    for(unsigned i=0;i<sizeof name-1;++i)ram[0xb5c+i]=name[i]==' '?0x2f:name[i]-'A'+0x0a;
    put(ram,0x3e,2);put(ram,0x38,0);put(ram,0x425,0);
    ScWorldMirror(w,ram);ScPopulationImport(population,ram);
    if(!ScPopulationRefreshLive(population,ram,w,rom,size))goto fail;
    population->previous=population->value;population->change=0;ScPopulationMirror(population,ram);
    ScWorldTilesTouch(w,0,SC_WORLD_MAX_TILE_BYTES);
    ScTestCityInspect(w,population,rom,stats);
    fprintf(stderr,"[test city] generated 1920x1600 population=%llu zones=%u powered=%u plants=%u police=%u fire=%u gifts=%u\n",
        (unsigned long long)population->value,stats->zones,stats->powered_zones,stats->plants,stats->police,stats->fire,stats->rewards);
    free(prototype);free(plan);return stats->zones && stats->powered_zones==stats->zones && stats->police && stats->fire;
fail:free(prototype);free(plan);return false;
}
size_t ScTestCityRecordSize(void) {return 24+0x8000+ScWorldEncodedSize()+SC_POPULATION_BYTES;}
bool ScTestCityRecordValid(const uint8_t *p,size_t n) {
    return p && n==ScTestCityRecordSize() && !memcmp(p,"SCTEST3",7) && p[7]==1 &&
        get32(p,8)==n && get32(p,12)==ScWorldEncodedSize() && get32(p,16)==SC_POPULATION_BYTES &&
        !get32(p,20) && p[24+5]==1;
}
const uint8_t *ScTestCityNativeSave(const uint8_t *p,size_t n) {return ScTestCityRecordValid(p,n)?p+24:NULL;}
bool ScTestCityEncode(uint8_t *p,size_t n,const uint8_t *sram,const ScWorld *w,const ScPopulation *pop) {
    if(n!=ScTestCityRecordSize() || !w->colossal || !sram || sram[5]!=1)return false;
    memset(p,0,24);memcpy(p,"SCTEST3",7);p[7]=1;put32(p,8,n);put32(p,12,ScWorldEncodedSize());put32(p,16,SC_POPULATION_BYTES);
    memcpy(p+24,sram,0x8000);
    if(!ScWorldEncode(w,p+24+0x8000,ScWorldEncodedSize()))return false;
    ScPopulationEncode(pop,p+24+0x8000+ScWorldEncodedSize());return true;
}
bool ScTestCityDecode(const uint8_t *p,size_t n,ScWorld *w,ScPopulation *pop) {
    return ScTestCityRecordValid(p,n) && ScWorldDecode(w,p+24+0x8000,ScWorldEncodedSize()) && w->active && w->colossal && w->test_city &&
        ScPopulationDecode(pop,p+24+0x8000+ScWorldEncodedSize(),SC_POPULATION_BYTES);
}
