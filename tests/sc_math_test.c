#include "sc_math.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t rom[0x80000],ram[0x20000],initial_ram[0x20000],expected_ram[0x20000];
static uint8_t intermediate_ram[0x20000];
static uint32_t seed=0x4872b19;
static unsigned random_word(void) {
    seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed&65535;
}
static uint8_t read_bus(void *ctx,uint32_t a) {
    (void)ctx;unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f) return ram[a-0x7e0000];
    if(p<0x2000) return ram[p];
    /* The caller's three inline offsets; arithmetic opcodes remain ROM. */
    if(bank==3 && p>=0x7001 && p<=0x7004) return p==0x7004?0:(uint8_t)(0x10+8*(p-0x7001));
    if(p>=0x8000 && bank<16) return rom[bank*32768+p-0x8000];
    fprintf(stderr,"Unexpected arithmetic bus read %06x\n",a);assert(0);return 0;
}
static void write_bus(void *ctx,uint32_t a,uint8_t v) {
    (void)ctx;unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f) ram[a-0x7e0000]=v;
    else {assert(p<0x2000);ram[p]=v;}
}
static void put(unsigned a,unsigned v) {ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static uint32_t wide(unsigned a) {return ram[a]|(uint32_t)ram[a+1]<<8|(uint32_t)ram[a+2]<<16|(uint32_t)ram[a+3]<<24;}
static void same(const Interp816 *a,const Interp816 *b,unsigned entry,unsigned budget,unsigned n) {
    if(memcmp(a,b,sizeof *a) || memcmp(ram,expected_ram,sizeof ram)) {
        fprintf(stderr,"math mismatch entry=%04x budget=%u case=%u pc=%04x/%04x A=%04x/%04x X=%u/%u Y=%u/%u C=%u/%u V=%u/%u cycles=%u/%u\n",
            entry,budget,n,a->pc,b->pc,a->a,b->a,a->x,b->x,a->y,b->y,a->c,b->c,a->v,b->v,a->cyclesUsed,b->cyclesUsed);
        abort();
    }
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);
    const unsigned entries[]={0xa32e,0xa395,0xa406,0xa462};
    const unsigned budgets[]={0,26,31,40,63,81,127,300,4096};
    const unsigned edges[]={0,1,0x7fff,0x8000,0xfffe,0xffff};
    unsigned cases=0;
    for(unsigned kind=0;kind<4;++kind) for(unsigned n=0;n<2048;++n) {
        bool multiply=kind<2;unsigned limit=kind==0 || kind==2?16:32;
        memset(ram,0x59,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=entries[kind];cpu->dp=(uint16_t)(n&1?0x1d07:0x1d00);
        cpu->e=cpu->mf=cpu->xf=cpu->d=false;cpu->i=true;
        cpu->a=(uint16_t)random_word();cpu->x=(uint16_t)random_word();cpu->y=(uint16_t)random_word();
        if(multiply) cpu->x=(uint16_t)(1+n%limit);else cpu->y=(uint16_t)(1+n%limit);
        cpu->c=(n&2)!=0;cpu->v=(n&4)!=0;cpu->z=(n&8)!=0;cpu->n=(n&16)!=0;
        for(unsigned at=0;at<20;at+=2) put(cpu->dp+at,n<36?edges[(n+at)%6]:random_word());
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
            unsigned native=ScMathStep(cpu,ram,budgets[b]);
            assert(native<=budgets[b]);Interp816 expected=*cpu;
            memcpy(expected_ram,ram,sizeof ram);
            if(!native) {assert(!memcmp(cpu,&initial,sizeof initial));assert(!memcmp(ram,initial_ram,sizeof ram));continue;}
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned elapsed=0,guard=0;
            while(elapsed<native) {assert(++guard<3000);elapsed+=interp816_runOpcode(cpu);}
            if(elapsed!=native) {fprintf(stderr,"math clock mismatch entry=%04x budget=%u case=%u native=%u original=%u\n",entries[kind],budgets[b],n,native,elapsed);abort();}
            same(cpu,&expected,entries[kind],budgets[b],n);++cases;
        }
    }
    for(unsigned n=0;n<512;++n) {
        memset(ram,0x69,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=0xa3cf;cpu->dp=n&1?0x1e00:0x1e08;cpu->sp=n&256?0x1e13:n&2?0x1f75:0x1fd;
        cpu->e=cpu->d=false;cpu->mf=n&4;cpu->xf=n&8;cpu->i=true;
        cpu->a=(uint16_t)random_word();cpu->x=(uint16_t)random_word();cpu->y=(uint16_t)random_word();
        cpu->c=n&16;cpu->v=n&32;cpu->z=n&64;cpu->n=n&128;
        for(unsigned at=0;at<36;at+=2) put(cpu->dp+at,n<36?edges[(n+at)%6]:random_word());
        put(cpu->sp+1,0x7000);
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        unsigned native=ScMathStep(cpu,ram,1000);
        if(!native && getenv("SC_DIV16_SETUP_REFERENCE")) continue;
        assert(native);Interp816 expected=*cpu;memcpy(expected_ram,ram,sizeof ram);
        *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
        assert(!ScMathStep(cpu,ram,native-1));
        assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,initial_ram,sizeof ram));
        assert(ScMathStep(cpu,ram,native)==native);
        same(cpu,&expected,0xa3cf,native,n);
        *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned elapsed=0,guard=0;
        while(cpu->pc!=0xa406) {assert(++guard<40);elapsed+=interp816_runOpcode(cpu);}
        assert(elapsed==native);same(cpu,&expected,0xa3cf,native,n);++cases;
    }
    const unsigned multiply_entries[]={0xa32e,0xa330,0xa332,0xa334,0xa336,0xa338,0xa339,0xa33b,0xa33d,0xa33f,0xa341,0xa342};
    const unsigned multiply_budgets[]={0,1,2,3,4,5,6,7,8,26,31,40,63,127,4096};
    for(unsigned entry=0;entry<12;++entry) for(unsigned n=0;n<128;++n) {
        memset(ram,0x59,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=multiply_entries[entry];cpu->dp=n&1?0x1d07:0x1d00;
        cpu->e=cpu->mf=cpu->xf=cpu->d=false;cpu->i=true;
        cpu->a=(uint16_t)random_word();cpu->x=(uint16_t)(1+n%16);cpu->y=(uint16_t)random_word();
        cpu->c=n&1;cpu->v=n&2;cpu->z=n&4;cpu->n=n&8;
        if(entry==11 && n&16) cpu->x=0;
        for(unsigned at=0;at<12;at+=2) put(cpu->dp+at,n<36?edges[(n+at)%6]:random_word());
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof multiply_budgets/sizeof *multiply_budgets;++b) {
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
            unsigned native=ScMathStep(cpu,ram,multiply_budgets[b]);assert(native<=multiply_budgets[b]);
            Interp816 expected=*cpu;memcpy(expected_ram,ram,sizeof ram);
            if(!native) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,initial_ram,sizeof ram));continue;}
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned elapsed=0,guard=0;
            while(elapsed<native) {assert(++guard<3000);elapsed+=interp816_runOpcode(cpu);}
            assert(elapsed==native);same(cpu,&expected,multiply_entries[entry],multiply_budgets[b],n);++cases;
        }
    }
    const unsigned divide_entries[]={0xa406,0xa408,0xa40a,0xa40c,0xa40e,0xa410,0xa412,0xa414,0xa415,0xa417,0xa419,0xa41b,0xa41d,0xa41f,0xa420};
    for(unsigned entry=0;entry<15;++entry) for(unsigned n=0;n<256;++n) {
        memset(ram,0x59,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=divide_entries[entry];cpu->dp=n&1?0x1d07:0x1d00;cpu->sp=0x1f75;
        cpu->e=cpu->mf=cpu->xf=cpu->d=false;cpu->i=true;
        cpu->a=(uint16_t)random_word();cpu->x=n&31;cpu->y=(uint16_t)(1+n%16);
        cpu->c=n&1;cpu->v=n&2;cpu->z=n&4;cpu->n=n&8;
        if(entry==8 && n&16) cpu->y=0;
        for(unsigned at=0;at<20;at+=2) put(cpu->dp+at,n<36?edges[(n+at)%6]:random_word());
        put(cpu->sp+1,0x1e00);put(cpu->sp+3,0x6fff);
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
            unsigned native=ScMathStep(cpu,ram,budgets[b]);assert(native<=budgets[b]);
            Interp816 expected=*cpu;memcpy(expected_ram,ram,sizeof ram);
            if(!native) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,initial_ram,sizeof ram));continue;}
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned elapsed=0,guard=0;
            while(elapsed<native) {assert(++guard<3000);elapsed+=interp816_runOpcode(cpu);}
            assert(elapsed==native);same(cpu,&expected,divide_entries[entry],budgets[b],n);++cases;
        }
    }
    for(unsigned kind=0;kind<2;++kind) for(unsigned n=0;n<2048;++n) {
        memset(ram,0x59,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=kind?0x904d:0x9092;cpu->dp=(uint16_t)(n%4==0?0x1d00:n%4==1?0x1d07:n%4==2?0xccf+n%12:0x20);
        cpu->e=cpu->mf=cpu->xf=cpu->d=false;cpu->i=true;cpu->x=(uint16_t)(2*(1+n%6));
        cpu->a=(uint16_t)random_word();cpu->y=(uint16_t)random_word();cpu->sp=0x1fc;
        cpu->c=n&1;cpu->v=n&2;cpu->z=n&4;cpu->n=n&8;
        for(unsigned at=0xccf;at<0xcdf;at+=2) put(at,n<36?edges[n%6]:random_word());
        put(cpu->dp,n<36?edges[(n/6)%6]:random_word());
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
            unsigned native=ScMathStep(cpu,ram,budgets[b]);assert(native<=budgets[b]);
            Interp816 expected=*cpu;memcpy(expected_ram,ram,sizeof ram);
            if(!native) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,initial_ram,sizeof ram));continue;}
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned elapsed=0,guard=0;
            while(elapsed<native) {assert(++guard<100);elapsed+=interp816_runOpcode(cpu);}
            if(elapsed!=native) fprintf(stderr,"RNG clock case=%u budget=%u native=%u original=%u X=%u/%u pc=%x/%x\n",n,budgets[b],native,elapsed,expected.x,cpu->x,expected.pc,cpu->pc);
            assert(elapsed==native);same(cpu,&expected,0x9092,budgets[b],n);++cases;
        }
    }
    const unsigned rng_spans[]={0x9035,0x907e,0x905b,0x90a0,0x90a6};
    for(unsigned entry=0;entry<5;++entry) for(unsigned n=0;n<256;++n) {
        memset(ram,0x69,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=rng_spans[entry];cpu->dp=n&1?0x1e00:0x1e02;cpu->sp=0x1f75;
        cpu->e=cpu->d=false;cpu->mf=entry<2 && (n&2);cpu->xf=entry<2 && (n&4);cpu->i=true;
        cpu->a=(uint16_t)random_word();cpu->x=(uint16_t)random_word();cpu->y=(uint16_t)random_word();
        cpu->c=n&8;cpu->v=n&16;cpu->z=n&32;cpu->n=n&64;
        put(cpu->sp+1,0x7654);put(cpu->sp+3,0x1e00);ram[cpu->sp+5]=(uint8_t)n;
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);
            unsigned native=ScMathStep(cpu,ram,budgets[b]);assert(native<=budgets[b]);
            Interp816 expected=*cpu;memcpy(expected_ram,ram,sizeof ram);
            if(!native) {assert(!memcmp(cpu,&initial,sizeof initial) && !memcmp(ram,initial_ram,sizeof ram));continue;}
            *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned elapsed=0,guard=0;
            while(elapsed<native) {assert(++guard<32);elapsed+=interp816_runOpcode(cpu);}
            if(elapsed!=native) fprintf(stderr,"RNG span clock entry=%x case=%u budget=%u native=%u original=%u\n",rng_spans[entry],n,budgets[b],native,elapsed);
            assert(elapsed==native);same(cpu,&expected,rng_spans[entry],budgets[b],n);++cases;
        }
    }
    const unsigned range_bounds[]={0,1,7,63,255,1023,32766,32767,32768,65534};
    for(unsigned kind=0;kind<2;++kind) for(unsigned n=0;n<512;++n) {
        memset(ram,0x69,sizeof ram);interp816_reset(cpu);
        unsigned entry=kind?0x9035:0x907e;
        cpu->k=cpu->db=3;cpu->pc=entry;cpu->dp=n&1?0x1e00:0x1e02;cpu->sp=0x1ffd;
        cpu->e=cpu->d=false;cpu->mf=n&2;cpu->xf=n&4;cpu->i=true;
        cpu->a=(uint16_t)random_word();cpu->x=(uint16_t)random_word();cpu->y=(uint16_t)random_word();
        if(kind) cpu->a=(uint16_t)range_bounds[n%10];
        cpu->c=n&8;cpu->v=n&16;cpu->z=n&32;cpu->n=n&64;put(0x1ffe,0x6fff);
        for(unsigned at=0xccf;at<0xcdf;at+=2) put(at,n<36?edges[n%6]:random_word());
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);unsigned elapsed=0,guard=0;
        while(cpu->pc!=0x7000) {assert(++guard<3000);elapsed+=interp816_runOpcode(cpu);}
        Interp816 expected=*cpu;memcpy(expected_ram,ram,sizeof ram);
        *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned native=0;guard=0;
        while(cpu->pc!=0x7000) {
            assert(++guard<3000);Interp816 before=*cpu;memcpy(initial_ram,ram,sizeof ram);
            unsigned fast=ScMathBatchStep(cpu,ram,budgets[6+n%3]);
            assert(fast<=budgets[6+n%3]);
            if(fast) {
                Interp816 after=*cpu;memcpy(intermediate_ram,ram,sizeof ram);
                *cpu=before;memcpy(ram,initial_ram,sizeof ram);unsigned original=0,steps=0;
                while(original<fast) {assert(++steps<3000);original+=interp816_runOpcode(cpu);}
                assert(original==fast && !memcmp(cpu,&after,sizeof after) && !memcmp(ram,intermediate_ram,sizeof ram));
                native+=fast;
            } else {
                assert(!memcmp(cpu,&before,sizeof before) && !memcmp(ram,initial_ram,sizeof ram));
                native+=interp816_runOpcode(cpu);
            }
        }
        assert(native==elapsed);same(cpu,&expected,entry,budgets[6+n%3],n);++cases;
    }
    const unsigned helpers[]={0xa2f5,0xa350,0xa3cf,0xa421};
    for(unsigned kind=0;kind<4;++kind) for(unsigned n=0;n<512;++n) {
        memset(ram,0x69,sizeof ram);interp816_reset(cpu);
        cpu->k=cpu->db=3;cpu->pc=helpers[kind];cpu->dp=0x1e00;cpu->sp=0x1ffd;
        cpu->e=cpu->d=false;cpu->mf=cpu->xf=cpu->i=true;
        cpu->a=(uint16_t)random_word();cpu->x=(uint16_t)random_word();cpu->y=(uint16_t)random_word();
        put(0x1ffe,0x7000); /* JSR's return address points to inline operands. */
        put(0x1e10,n<36?edges[n%6]:random_word());put(0x1e12,n<36?edges[(n/6)%6]:random_word());
        put(0x1e18,n<36?edges[(n/6)%6]:random_word());put(0x1e1a,n<36?edges[n%6]:random_word());
        uint32_t a=wide(0x1e10),b=wide(0x1e18);
        Interp816 initial=*cpu;memcpy(initial_ram,ram,sizeof ram);
        unsigned original_cycles=0,guard=0;
        while(cpu->pc!=0x7004) {assert(++guard<4096);original_cycles+=interp816_runOpcode(cpu);}
        Interp816 expected=*cpu;memcpy(expected_ram,ram,sizeof ram);
        *cpu=initial;memcpy(ram,initial_ram,sizeof ram);unsigned native_cycles=0;guard=0;
        while(cpu->pc!=0x7004) {
            assert(++guard<4096);unsigned fast=ScMathStep(cpu,ram,budgets[6+n%3]);
            native_cycles+=fast?fast:interp816_runOpcode(cpu);
        }
        assert(native_cycles==original_cycles);same(cpu,&expected,helpers[kind],budgets[6+n%3],n);
        assert(cpu->sp==0x1fff && cpu->dp==0x1e00);
        if(kind==0) assert(wide(0x1e20)==(a&65535)*(b&65535));
        if(kind==1) assert((wide(0x1e20)|((uint64_t)wide(0x1e24)<<32))==(uint64_t)a*b);
        if(kind==2) assert((wide(0x1e20)&65535)==((b&65535)?(a&65535)/(b&65535):65535));
        if(kind==3) assert(wide(0x1e20)==(b?a/b:UINT32_MAX));
        ++cases;
    }
    /* Unsupported layouts, suspended CPUs and interrupts stay in the exact
     * compatibility path without touching registers or RAM. */
    for(unsigned kind=0;kind<2;++kind) for(unsigned guard=0;guard<13;++guard) {
        interp816_reset(cpu);cpu->pc=kind?0x9092:0xa32e;cpu->k=cpu->db=3;cpu->dp=0x1d00;cpu->x=kind?12:16;
        cpu->e=cpu->mf=cpu->xf=false;
        switch(guard) {
        case 0:cpu->k=2;break;case 1:cpu->db=2;break;case 2:cpu->e=true;break;
        case 3:cpu->mf=true;break;case 4:cpu->xf=true;break;case 5:cpu->d=true;break;
        case 6:cpu->waiting=true;break;case 7:cpu->stopped=true;break;
        case 8:cpu->nmiWanted=true;break;case 9:cpu->irqWanted=true;cpu->i=false;break;
        case 10:cpu->dp=0x1ff0;break;case 11:cpu->x=0;break;case 12:cpu->x=17;break;
        }
        Interp816 before=*cpu;memcpy(initial_ram,ram,sizeof ram);
        assert(!ScMathStep(cpu,ram,4096));assert(!memcmp(cpu,&before,sizeof before));
        assert(!memcmp(ram,initial_ram,sizeof ram));
        cpu->read=read_bus;
    }
    for(unsigned guard=0;guard<9;++guard) {
        memset(ram,0x69,sizeof ram);cpu->read=read_bus;interp816_reset(cpu);
        cpu->pc=0xa3cf;cpu->k=cpu->db=3;cpu->dp=0x1e08;cpu->sp=0x1f75;
        cpu->e=cpu->d=false;cpu->mf=cpu->xf=cpu->i=true;
        put(cpu->sp+1,0x7000);
        switch(guard) {
        case 0:cpu->sp=0x100;break;case 1:cpu->sp=0x1ffe;break;
        case 2:cpu->dp=0x20;break;case 3:cpu->sp=0x1e06;put(cpu->sp+1,0x7000);break;
        case 4:put(cpu->sp+1,0x6000);break;case 5:put(cpu->sp+1,0xfffa);break;
        case 6:cpu->dp=0x1fe0;break;case 7:cpu->read=NULL;break;case 8:cpu->dp=0x1ff0;break;
        }
        Interp816 before=*cpu;memcpy(initial_ram,ram,sizeof ram);
        assert(!ScMathStep(cpu,ram,4096));assert(!memcmp(cpu,&before,sizeof before));
        assert(!memcmp(ram,initial_ram,sizeof ram));
    }
    interp816_free(cpu);printf("PASS: %u original-ROM math loop comparisons, exact CPU/RAM/clocks, bounded budgets and safe fallback\n",cases);
    return 0;
}
