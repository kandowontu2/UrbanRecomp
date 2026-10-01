#include "sc_population.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    routine(&s,true); assert(s.value==SC_POPULATION_MAX);
    s.capacity[0]=0; s.capacity[1]=UINT64_C(62499999);
    routine(&s,true); assert(s.value==UINT64_C(9999999840));
    s.capacity[1]=0; s.capacity[2]=UINT64_C(62500000);
    routine(&s,true); assert(s.value==SC_POPULATION_MAX);
    s.capacity[0]=UINT64_C(250000000); s.capacity[1]=UINT64_C(20000000); s.capacity[2]=UINT64_C(11250000);
    routine(&s,true); assert(s.value==SC_POPULATION_MAX);
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
    ScPopulationReport(&s,ram,true); assert(word(0x2840+(0x19c-10)*2)==0x142f);
    ScPopulationSet(&s,ram,SC_POPULATION_MAX); ScPopulationReport(&s,ram,false);
    for (unsigned i=0;i<10;++i) assert(word(0x2840+(0x13c-9+i)*2)==0x429);
    for (unsigned i=0x117;i<=0x11c;++i) assert(word(0x2840+i*2)==0x3ff);
    ScPopulationSet(&s,ram,1); memcpy(baseline,ram,sizeof ram);
    ScPopulationReport(&s,ram,false); assert(!memcmp(baseline,ram,sizeof ram));
    const uint64_t boundaries[]={0,1999,2000,9999,10000,49999,50000,99999,100000,499999,500000,
        999999,1000000,UINT64_C(4294967296),SC_POPULATION_MAX};
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
    puts("PASS: native small-city equivalence, overflow-free population, 9,999,999,999 cap, classes, signed change, history and portable save encoding");
}
