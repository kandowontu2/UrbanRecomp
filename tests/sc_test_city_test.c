#include "sc_test_city.h"
#include "sc_sram.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static ScWorld world,loaded;
static uint8_t ram[0x20000],rom[0x80000],sram[0x8000],original[0x8000];
int main(int argc,char **argv) {
    assert(argc==3);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    ScPopulation p,q;ScTestCityStats stats;
    assert(ScTestCityGenerate(&world,&p,ram,rom,sizeof rom,&stats));
    assert(ScWorldWidth(&world)==1920 && ScWorldHeight(&world)==1600);
    assert(stats.plants>5000 && stats.police>2000 && stats.fire>2000 && stats.rewards==13 &&
        stats.seaports && stats.airports && stats.stadiums && stats.residential && stats.commercial && stats.industrial &&
        stats.powered_zones==stats.zones && p.value>10000000);
    unsigned inhabited=0,covered=0;
    for(unsigned i=0;i<ScWorldFieldSizeWorld(&world,0);++i)inhabited+=world.fields[0][i]>0;
    for(unsigned i=0;i<ScWorldFieldSizeWorld(&world,8);++i)covered+=world.fields[8][i]>0;
    assert(inhabited>100000 && covered>10000); /* Real native field initialization. */
    /* An independent census must agree with the seed's actual developed art. */
    ScPopulationImport(&q,ram);assert(ScPopulationRefreshLive(&q,ram,&world,rom,sizeof rom) && q.value==p.value);
    for(unsigned i=0;i<sizeof sram;++i)sram[i]=(uint8_t)(i*17);sram[5]=1;memcpy(original,sram,sizeof sram);
    size_t size=ScTestCityRecordSize();uint8_t *record=malloc(size);assert(record);
    assert(ScTestCityEncode(record,size,sram,&world,&p) && ScTestCityDecode(record,size,&loaded,&q));
    assert(!memcmp(world.tiles,loaded.tiles,sizeof world.tiles) && q.value==p.value);
    assert(loaded.test_city && loaded.center_valid && loaded.center_x==world.center_x && loaded.center_y==world.center_y);
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
    printf("PASS: connected colossal test city, %u/%u powered zones, population %llu, 13 gifts, services/facilities, SRAM trailer roundtrip and slot preservation\n",
        stats.powered_zones,stats.zones,(unsigned long long)p.value);return 0;
}
