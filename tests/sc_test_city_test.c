#include "sc_test_city.h"
#include "sc_sram.h"
#include "sc_program.h"
#include "sc_construction.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static ScWorld world,loaded;
static uint8_t ram[0x20000],rom[0x80000],sram[0x8000],original[0x8000];
static unsigned word(const uint8_t *p,unsigned a) {return p[a]|p[a+1]<<8;}
static void put(uint8_t *p,unsigned a,unsigned v) {p[a]=(uint8_t)v;p[a+1]=(uint8_t)(v>>8);}
/* An analysis fixture only: occupied free houses beside mature ordinary
 * commerce must be capable of crossing the ROM's density>=65 apartment gate.
 * Never initialize the playable city with these occupants or field values. */
static void density_bootstrap(void) {
    uint8_t *sample=malloc(sizeof ram);assert(sample);memcpy(sample,ram,sizeof ram);
    unsigned residential=0,eligible=0;
    for(unsigned y=1;y<99;++y)for(unsigned x=1;x<119;++x) {
        unsigned t=word(ram,0x10200+2*(y*120+x))&1023;
        if(t!=0x84 && t!=0x13b && t!=0x1f8)continue;
        if(t==0x84)++residential;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            if(t==0x84 && !dx && !dy)continue;
            unsigned at=0x10200+2*((y+dy)*120+x+dx);
            unsigned art=t==0x84?0x89:(t==0x13b?0x164:0x218)+(dy+1)*3+dx+1;
            put(sample,at,(word(sample,at)&~1023)|art);
        }
    }
    assert(ScConstructionPrimeFields(sample,rom,sizeof rom));
    for(unsigned y=0;y<100;++y)for(unsigned x=0;x<120;++x)
        if((word(ram,0x10200+2*(y*120+x))&1023)==0x84 &&
            sample[0x18e28+(y/2)*60+x/2]>=65)++eligible;
    printf("Native density bootstrap: %u/%u housing lots meet the apartment gate beside ordinary mature jobs\n",eligible,residential);
    assert(eligible>residential/2);free(sample);
}
int main(int argc,char **argv) {
    assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    assert(ScProgramSelectRom(0xec01686a));
    ScPopulation p,q;ScTestCityStats stats;
    size_t size=ScTestCityRecordSize();uint8_t *record=malloc(size);assert(record);
    for(unsigned mode=0;mode<6;++mode) {
        memset(ram,0,sizeof ram);
        assert(ScTestCityGenerate(&world,&p,ram,rom,sizeof rom,mode,20,&stats));
        unsigned width=mode?120u<<mode:120,height=mode?100u<<mode:100;
        assert(world.active==(mode!=0) && (!mode || (ScWorldWidth(&world)==width && ScWorldHeight(&world)==height)));
        assert(stats.plants && stats.police && stats.fire && stats.rewards==27 && stats.seaports==1 &&
            stats.airports && stats.stadiums && stats.residential && stats.commercial && stats.industrial &&
            stats.powered_zones==stats.zones && p.value==0 && world.development_speed==20 && world.calendar_year==1900);
        const uint8_t *tiles=world.active?world.tiles:ram+0x10200;
        unsigned connected=0;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            unsigned at=2*(y*width+x),tile=(tiles[at]|tiles[at+1]<<8)&1023;
            assert(tile==0 || tile>=38); /* Map 31337: no water/shore/forest. Parks are 38/39. */
            if(!(rom[0x184eb+tile]&1))continue;
            bool zone=tile==0x84 || tile==0x13b || tile==0x1f8;
            assert(!((tile>=0x95 && tile<0x245) && tile!=0x13b && tile!=0x1f8)); /* No developed zones. */
            if(tile==0x1f8) {
                unsigned edge=x;if(y<edge)edge=y;if(width-x-1<edge)edge=width-x-1;if(height-y-1<edge)edge=height-y-1;
                assert(edge<4*(width/120)); /* Industry belongs on the global edge. */
            }
            assert(tile!=0x2fe); /* Shared amusement/casino awards choose no crime penalty. */
            if(zone) {
                bool access=false;
                for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
                    int xx=(int)x+dx,yy=(int)y+dy;
                    if(xx<0 || yy<0 || xx>=(int)width || yy>=(int)height || (abs(dx)!=2 && abs(dy)!=2))continue;
                    unsigned p=2*(yy*width+xx),t=(tiles[p]|tiles[p+1]<<8)&1023;
                    access|=t>=0x30 && t<0x80 && (t<0x60 || t>=0x6d);
                }
                if(!access) {
                    fprintf(stderr,"no transport %u,%u tile=%x perimeter:",x,y,tile);
                    for(int dx=-2;dx<=2;++dx)if((int)x+dx>=0 && x+dx<width) {
                        unsigned p=2*((y-2)*width+x+dx);fprintf(stderr," %x",(tiles[p]|tiles[p+1]<<8)&1023);
                    }
                    fputc('\n',stderr);
                }
                assert(access);++connected;
            }
        }
        assert(connected==stats.zones);
        if(mode==5) {
            uint64_t workers=5*(uint64_t)stats.residential,jobs=5*(uint64_t)stats.commercial+4*(uint64_t)stats.industrial;
            assert(jobs>=workers);
            printf("Full-density employment: R=%u C=%u I=%u workers=%llu jobs=%llu\n",
                stats.residential,stats.commercial,stats.industrial,(unsigned long long)workers,(unsigned long long)jobs);
        }
        unsigned covered=0,bytes=mode?ScWorldFieldSizeWorld(&world,8):195;
        const uint8_t *coverage=mode?world.fields[8]:ram+0x1afe8;
        for(unsigned i=0;i<bytes;++i)covered+=coverage[i]>0;
        assert(covered);
        sram[5]=1;
        assert(ScTestCityEncode(record,size,sram,&world,&p) && ScTestCityDecode(record,size,&loaded,&q));
        assert(loaded.active==world.active && loaded.mega==world.mega && loaded.test_city &&
            loaded.development_speed==20 && q.value==0);
        printf("PASS: empty %ux%u city, %u powered/connected zones, ordinary ceiling %llu\n",
            width,height,stats.zones,(unsigned long long)stats.ordinary_capacity);
        if(!mode)density_bootstrap();
    }
    /* An independent census must agree with the seed's actual developed art. */
    ScPopulationImport(&q,ram);assert(ScPopulationRefreshLive(&q,ram,&world,rom,sizeof rom) && q.value==p.value);
    for(unsigned i=0;i<sizeof sram;++i)sram[i]=(uint8_t)(i*17);sram[5]=1;memcpy(original,sram,sizeof sram);
    world.calendar_year=123456;
    assert(ScTestCityEncode(record,size,sram,&world,&p) && ScTestCityDecode(record,size,&loaded,&q));
    assert(!memcmp(world.tiles,loaded.tiles,sizeof world.tiles) && q.value==p.value);
    assert(loaded.test_city && loaded.center_valid && loaded.center_x==world.center_x && loaded.center_y==world.center_y && loaded.calendar_year==123456);
    for(unsigned field=0;field<SC_WORLD_FIELDS;++field)
        assert(!memcmp(world.fields[field],loaded.fields[field],ScWorldFieldSizeWorld(&world,field)));
    remove(argv[2]);assert(ScSram_Open(sram,sizeof sram,argv[2]));
    assert(ScSram_SetExtra(record,size));ScSram_Flush();
    assert(ScSram_Open(sram,sizeof sram,argv[2]));uint32_t got;
    assert(!memcmp(sram,original,sizeof sram) && ScSram_Extra(&got) && got==size &&
        !memcmp(ScSram_Extra(NULL),record,size));
    /* Temporary native-codec writes cannot escape to the user's two slots. */
    ScSram_Suspend(true);sram[200]^=99;ScSram_Tick();ScSram_Flush();
    memcpy(sram,original,sizeof sram);ScSram_Suspend(false);
    assert(ScSram_Open(sram,sizeof sram,argv[2]) && !memcmp(sram,original,sizeof sram));
    /* A corrupt trailer disables writes and leaves the entire file alone. */
    f=fopen(argv[2],"r+b");assert(f);fseek(f,0x8000+24+99,SEEK_SET);int old=fgetc(f);
    fseek(f,-1,SEEK_CUR);fputc(old^1,f);fclose(f);
    assert(!ScSram_Open(sram,sizeof sram,argv[2]) && !ScSram_Active());
    ScSram_Flush();free(record);
    printf("PASS: six empty test cities, %u/%u powered zones, population %llu, 27 gifts, services/facilities, SRAM trailer roundtrip and slot preservation\n",
        stats.powered_zones,stats.zones,(unsigned long long)p.value);return 0;
}
