#include "sc_power_refresh.h"
#include "sc_construction.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t ram[0x20000],before[0x20000],rom[0x80000];
static ScPowerRefresh power;
static void put(unsigned p,unsigned v) {ram[p]=(uint8_t)v;ram[p+1]=(uint8_t)(v>>8);}
static unsigned word(unsigned p) {return ram[p]|ram[p+1]<<8;}
static void build(unsigned tool,int x,int y) {
    ScBuildPlan plan; unsigned cost;
    assert(ScConstructionPlan(&plan,tool,x,y,x,y));
    assert(ScConstructionCommit(ram,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
}
int main(int argc,char **argv) {
    assert(argc==2); FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    const int speeds[]={2,5,10,50};
    for(unsigned i=0;i<4;++i) {
        ScRefreshClock c;ScRefreshClockReset(&c,200);
        assert(ScRefreshClockDue(&c,0,speeds[i]));
        unsigned count=0;
        for(unsigned frame=1;frame<=1000;++frame) count+=ScRefreshClockDue(&c,frame,speeds[i]);
        assert(count==5*(unsigned)speeds[i]);
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
    puts("PASS: native-relative speed ratios, fractional cadence, nuclear-first connection, and in-flight bitmap ownership");
}
