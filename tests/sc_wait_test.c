#include "sc_wait.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t rom[0x80000],ram[0x20000],initial_ram[0x20000],expected_ram[0x20000];
static uint8_t read_bus(void *ctx,uint32_t address) {
    (void)ctx;unsigned bank=address>>16,p=address&65535;
    if(p<0x2000) return ram[p];
    assert(bank==0 && p>=0x8000);return rom[p-0x8000];
}
static void write_bus(void *ctx,uint32_t address,uint8_t value) {
    (void)ctx;assert(address<0x2000);ram[address]=value;
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);unsigned cases=0,fused=0;
    const unsigned starts[]={0x9311,0x9313,0x9315},pages[]={0,1,0x1e00,0x1ef5};
    const unsigned budgets[]={0,1,2,3,4,5,8,10,11,12,13,31,32,127,170,4096};
    for(unsigned stage=0;stage<3;++stage) for(unsigned sample=0;sample<256;++sample)
    for(unsigned emulation=0;emulation<2;++emulation) {
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
        cpu->k=0;cpu->db=(uint8_t)sample;cpu->pc=starts[stage];
        cpu->dp=pages[(sample>>2)&3];cpu->sp=emulation?0x1fe:0x1ffd;
        cpu->a=(uint16_t)(0x7300+sample);cpu->x=sample;cpu->y=sample*197;
        cpu->e=emulation;cpu->mf=true;cpu->xf=emulation || (sample&2);cpu->d=sample&4;
        cpu->i=sample&8;cpu->irqWanted=cpu->i && (sample&16);
        cpu->c=sample&32;cpu->v=sample&64;cpu->z=sample&128;cpu->n=!cpu->z;
        ram[cpu->dp+0xb9]=sample%4==0?0:sample%4==1?1:sample%4==2?128:255;
        ram[cpu->dp+0xc7]=(uint8_t)sample;
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
            unsigned cost=ScWaitStep(cpu,ram,budgets[b]);assert(cost<=budgets[b]);
            Interp816 actual=*cpu;memcpy(expected_ram,ram,sizeof ram);
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned elapsed=0;
            while(elapsed<cost) elapsed+=interp816_runOpcode(cpu);
            if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,expected_ram,sizeof ram)) {
                fprintf(stderr,"wait stage=%u sample=%u E=%u budget=%u clocks=%u/%u pc=%x/%x A=%x/%x flags=%x/%x counter=%u/%u\n",
                    stage,sample,emulation,budgets[b],cost,elapsed,cpu->pc,actual.pc,cpu->a,actual.a,
                    interp816_getFlags(cpu),interp816_getFlags(&actual),ram[cpu->dp+0xc7],expected_ram[cpu->dp+0xc7]);abort();
            }
            ++cases;if(cost>100) ++fused;
        }
    }
    /* The complete driver must stop on every original instruction boundary,
     * including setup and the native/emulation stack return. */
    unsigned driver_cases=0,edges=0,fallbacks=0;
    const unsigned driver_starts[]={0x930d,0x930f,0x9311,0x9313,0x9315,0x9317};
    for(unsigned stage=0;stage<6;++stage) for(unsigned sample=0;sample<256;++sample)
    for(unsigned emulation=0;emulation<2;++emulation) {
        memset(ram,0x5a,sizeof ram);interp816_reset(cpu);
        cpu->k=0;cpu->db=(uint8_t)sample;cpu->pc=driver_starts[stage];
        cpu->dp=pages[(sample>>2)&3];
        cpu->sp=emulation?(uint16_t)(0x100+(sample&255)):(sample&1?0x1ffd:0x1fd);
        cpu->a=(uint16_t)(0x7300+sample);cpu->x=sample*197;cpu->y=sample*313;
        cpu->e=emulation;cpu->mf=stage!=0 || emulation || (sample&1);
        cpu->xf=emulation || (sample&2);cpu->d=sample&4;
        cpu->i=sample&8;cpu->irqWanted=cpu->i && (sample&16);
        cpu->c=sample&32;cpu->v=sample&64;cpu->z=sample&128;cpu->n=!cpu->z;
        ram[cpu->dp+0xb9]=sample%4==0?0:sample%4==1?1:sample%4==2?128:255;
        ram[cpu->dp+0xc7]=(uint8_t)sample;
        unsigned lo=emulation?0x100|((cpu->sp+1)&255):cpu->sp+1;
        unsigned hi=emulation?0x100|((cpu->sp+2)&255):cpu->sp+2;
        ram[lo]=0xff;ram[hi]=0x6f;
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        for(unsigned b=0;b<=sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
            unsigned cost=b==sizeof budgets/sizeof *budgets?ScWaitInstructionStep(cpu,ram):
                ScWaitDriverStep(cpu,ram,budgets[b]);
            if(b<sizeof budgets/sizeof *budgets) assert(cost<=budgets[b]);
            else assert(cost);
            Interp816 actual=*cpu;memcpy(expected_ram,ram,sizeof ram);
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned elapsed=0;
            while(elapsed<cost) elapsed+=interp816_runOpcode(cpu);
            if(elapsed!=cost || memcmp(cpu,&actual,sizeof actual) || memcmp(ram,expected_ram,sizeof ram)) {
                fprintf(stderr,"wait driver stage=%u sample=%u E=%u budget-index=%u clocks=%u/%u pc=%x/%x SP=%x/%x A=%x/%x flags=%x/%x\n",
                    stage,sample,emulation,b,cost,elapsed,cpu->pc,actual.pc,cpu->sp,actual.sp,cpu->a,actual.a,
                    interp816_getFlags(cpu),interp816_getFlags(&actual));abort();
            }
            if(b<sizeof budgets/sizeof *budgets) ++driver_cases;else ++edges;
        }
        *cpu=initial;cpu->nmiWanted=true;memcpy(ram,initial_ram,sizeof ram);
        initial=*cpu;
        assert(!ScWaitDriverStep(cpu,ram,4096) && !ScWaitInstructionStep(cpu,ram));
        assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,initial_ram,sizeof ram));++fallbacks;
    }
    /* Invalid execution layouts and pending interrupts must remain immutable. */
    for(unsigned guard=0;guard<7;++guard) {
        interp816_reset(cpu);cpu->k=0;cpu->pc=0x9311;cpu->dp=0x1e00;cpu->mf=true;cpu->i=false;
        if(guard==0) cpu->k=1;else if(guard==1) cpu->mf=false;
        else if(guard==2) cpu->dp=0x1f39;else if(guard==3) cpu->waiting=true;
        else if(guard==4) cpu->stopped=true;else if(guard==5) cpu->nmiWanted=true;
        else cpu->irqWanted=true;
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        assert(!ScWaitStep(cpu,ram,4096));assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,initial_ram,sizeof ram));
        assert(!ScWaitDriverStep(cpu,ram,4096) && !ScWaitInstructionStep(cpu,ram));
        assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,initial_ram,sizeof ram));
    }
    assert(fused>100);interp816_free(cpu);
    printf("PASS: %u native frame-wait spans (%u fused), original ROM registers/RAM/clocks, byte wraps, ready exits, interrupt/unsafe fallback\n",cases,fused);
    printf("PASS: %u complete wait-driver spans, %u original instruction edges, %u immutable interrupt fallbacks, setup and both stack modes\n",driver_cases,edges,fallbacks);
    return 0;
}
