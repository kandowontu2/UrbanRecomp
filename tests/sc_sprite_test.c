#include "sc_sprite.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t rom[0x80000],ram[0x20000],initial_ram[0x20000],expected_ram[0x20000];
static unsigned spans,emissions,virtual_reads,edges;
static uint8_t read_bus(void *ctx,uint32_t address) {
    (void)ctx;unsigned bank=address>>16,p=address&65535;
    if(bank==0x7e || bank==0x7f) return ram[address-0x7e0000];
    if(p<0x2000) return ram[p];
    assert(p>=0x8000 && (bank&0x7f)<0x10);
    /* A synthetic cartridge record, as used by native-font menus. Both
     * paths must see the callback's bytes rather than a raw ROM pointer. */
    if(p==0xe003) {++virtual_reads;return 0x5b;}
    return rom[(bank&15)*0x8000+p-0x8000];
}
static void write_bus(void *ctx,uint32_t address,uint8_t value) {
    (void)ctx;unsigned bank=address>>16,p=address&65535;
    if(bank==0x7e || bank==0x7f) ram[address-0x7e0000]=value;
    else {assert(p<0x2000);ram[p]=value;}
}
static void put(uint8_t *r,unsigned p,unsigned v) {r[p]=(uint8_t)v;r[p+1]=(uint8_t)(v>>8);}
static void equal(Interp816 *cpu,const Interp816 *actual,unsigned cost,unsigned elapsed,unsigned entry,unsigned budget) {
    if(cost!=elapsed || memcmp(cpu,actual,sizeof *cpu) || memcmp(ram,expected_ram,sizeof ram)) {
        fprintf(stderr,"sprite entry=%04x budget=%u cycles=%u/%u PC=%04x/%04x A=%04x/%04x X=%04x/%04x Y=%04x/%04x S=%04x/%04x flags=%02x/%02x last=%u/%u\n",
            entry,budget,cost,elapsed,cpu->pc,actual->pc,cpu->a,actual->a,cpu->x,actual->x,
            cpu->y,actual->y,cpu->sp,actual->sp,interp816_getFlags(cpu),interp816_getFlags((Interp816 *)actual),cpu->cyclesUsed,actual->cyclesUsed);
        for(unsigned j=0;j<sizeof ram;++j) if(ram[j]!=expected_ram[j]) {fprintf(stderr,"RAM[%x]=%02x/%02x\n",j,ram[j],expected_ram[j]);break;}
        for(unsigned j=0;j<sizeof *cpu;++j) if(((uint8_t *)cpu)[j]!=((uint8_t *)actual)[j]) {fprintf(stderr,"CPU byte[%u]=%02x/%02x\n",j,((uint8_t *)cpu)[j],((uint8_t *)actual)[j]);break;}
        abort();
    }
}
static void span(Interp816 *cpu,unsigned budget) {
    Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
    unsigned cost=ScSpriteStep(cpu,ram,budget);assert(cost<=budget);
    Interp816 actual=*cpu;memcpy(expected_ram,ram,sizeof ram);
    *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned elapsed=0;
    while(elapsed<cost) elapsed+=interp816_runOpcode(cpu);
    equal(cpu,&actual,cost,elapsed,initial.pc,budget);++spans;
    *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
}
static void atomic(Interp816 *cpu) {
    Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
    unsigned cost=ScSpriteInstructionStep(cpu,ram);Interp816 actual=*cpu;memcpy(expected_ram,ram,sizeof ram);
    *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
    unsigned elapsed=cost?(unsigned)interp816_runOpcode(cpu):0;
    equal(cpu,&actual,cost,elapsed,initial.pc,8);++edges;
    *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
}
static void setup(Interp816 *cpu,unsigned sample,unsigned count) {
    memset(ram,0x5a,sizeof ram);cpu->read=read_bus;interp816_reset(cpu);
    cpu->pc=0x905c;cpu->e=false;cpu->d=false;cpu->db=sample%3==0?0:sample%3==1?0x80:1;
    cpu->sp=sample%3==0?0x404:sample%3==1?0x1eec:0x1fc8;
    cpu->dp=(uint16_t)(sample*197);cpu->a=(uint16_t)(sample*73);cpu->x=(uint16_t)(sample*317);cpu->y=(uint16_t)(sample*419);
    cpu->mf=sample&1;cpu->xf=sample&2;cpu->c=sample&4;cpu->n=sample&8;cpu->z=sample&16;cpu->v=sample&32;
    cpu->i=sample&64;cpu->irqWanted=cpu->i && (sample&128);
    put(ram,cpu->sp+1,0x3456);put(ram,0x261,3);
    put(ram,0x253,4*(sample%(129-count)));ram[0x25d]=(uint8_t)sample;ram[0x25f]=(uint8_t)(sample*73+31);
    put(rom,0xdaa6-0x8000+6,0xe000);
    for(unsigned bank=0;bank<2;++bank) {
        unsigned p=bank*0x8000+0x6000;put(rom,p,count);
        for(unsigned i=0;i<count*4;++i) rom[p+2+i]=(uint8_t)(i*137+sample*17);
    }
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);
    const unsigned counts[]={1,2,7,8,9,16,31,63,64,127,128};
    const unsigned budgets[]={0,1,2,13,14,15,21,22,24,25,34,35,36,46,47,49,50,51,52,68,69,81,82,83,84,127,170,4096};
    for(unsigned sample=0;sample<256;++sample) {
        if(getenv("SC_SPRITE_TEST_DIAG")) fprintf(stderr,"sample=%u\n",sample);
        unsigned count=counts[sample%(sizeof counts/sizeof *counts)];setup(cpu,sample,count);
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        unsigned expected_cost=0,steps=0;
        while(cpu->pc!=0x3457 && steps++<20000) expected_cost+=interp816_runOpcode(cpu);
        assert(cpu->pc==0x3457);Interp816 expected=*cpu;memcpy(expected_ram,ram,sizeof ram);
        *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned actual_cost=0;steps=0;
        while(cpu->pc!=0x3457 && steps++<20000) {
            unsigned budget=budgets[(steps*17+sample)%(sizeof budgets/sizeof *budgets)];
            unsigned cost=ScSpriteStep(cpu,ram,budget);assert(cost<=budget);
            if(!cost) cost=ScSpriteInstructionStep(cpu,ram);
            actual_cost+=cost?cost:interp816_runOpcode(cpu);
        }
        equal(cpu,&expected,actual_cost,expected_cost,initial.pc,4096);++emissions;
        /* Every reachable native stage, at every deadline edge, including
         * first/last records, high-bit remainder counts and both branches. */
        setup(cpu,sample,count);bool seen[256]={0};
        while(cpu->pc!=0x3457) {
            if(ScSpriteOwns(cpu->pc)) {
                unsigned stage=cpu->pc&255;
                if(!seen[stage] || (cpu->pc==0x9080 && ram[0x287]==1 && !ram[0x288])) {
                    for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) span(cpu,budgets[b]);
                    atomic(cpu);
                    seen[stage]=true;
                }
            }
            interp816_runOpcode(cpu);
        }
    }
    for(unsigned guard=0;guard<12;++guard) {
        if(getenv("SC_SPRITE_TEST_DIAG")) fprintf(stderr,"guard=%u\n",guard);
        setup(cpu,1,8);
        switch(guard) {
            case 0:cpu->k=1;break;case 1:cpu->e=true;break;case 2:cpu->d=true;break;
            case 3:cpu->db=0x7e;break;case 4:cpu->sp=0x303;break;case 5:cpu->sp=0x1ffa;break;
            case 6:cpu->read=NULL;break;case 7:cpu->waiting=true;break;case 8:cpu->stopped=true;break;
            case 9:cpu->nmiWanted=true;break;case 10:cpu->i=false;cpu->irqWanted=true;break;
            case 11:put(ram,0x253,512);break;
        }
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        assert(!ScSpriteStep(cpu,ram,4096));assert(!memcmp(cpu,&initial,sizeof initial));assert(!memcmp(ram,initial_ram,sizeof ram));
        if(guard<11) {
            assert(!ScSpriteInstructionStep(cpu,ram));assert(!memcmp(cpu,&initial,sizeof initial));assert(!memcmp(ram,initial_ram,sizeof ram));
        }
    }
    assert(virtual_reads>100);interp816_free(cpu);
    printf("PASS: %u complete native sprite emissions, %u bounded ROM-oracle spans, %u exact instruction edges; all registers/flags/stack/OAM/WRAM/clocks, 1..128 sprites, callback records, unsafe/interrupt fallback\n",emissions,spans,edges);
    return 0;
}
