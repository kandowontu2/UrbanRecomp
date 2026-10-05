#include "sc_program.h"
#include "snes/interp816.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
static uint8_t rom[524288];
extern uint32_t g_interp816_cur_pc;
int interp816_opcode_hook(uint32_t pc) {(void)pc;return 0;}
typedef struct {uint32_t address;unsigned value,kind,pc,cycles,site;} Access;
typedef struct {Access trace[64];unsigned count,seed;bool word;Interp816 *cpu;
    Access *history;unsigned history_count;} Bus;
static uint8_t read_bus(void *ctx,uint32_t address) {
    Bus *b=ctx;unsigned bank=(address>>16)&0x7f,p=address&65535;
    unsigned value=(address>>16)!=0x7e && (address>>16)!=0x7f &&
        (p>=0x8000 || (bank>=0x40 && bank<0x70))?rom[(bank&15)*32768+(p&32767)]:
        ((address*73u)^(address>>8)^(b->seed*197u))&255;
    for(unsigned i=b->history_count;i;--i) if(b->history[i-1].address==address) {
        value=b->history[i-1].value;break;
    }
    for(unsigned i=b->count;i;--i) if(b->trace[i-1].kind==1 && b->trace[i-1].address==address) {
        value=b->trace[i-1].value;break;
    }
    assert(b->count<64);b->trace[b->count++]=(Access){address,value,0,b->cpu->pc,b->cpu->cyclesUsed,g_interp816_cur_pc};return value;
}
static void write_bus(void *ctx,uint32_t address,uint8_t value) {
    Bus *b=ctx;assert(b->count<64);b->trace[b->count++]=(Access){address,value,1,b->cpu->pc,b->cpu->cyclesUsed,g_interp816_cur_pc};
}
static bool read_word(void *ctx,uint32_t low,uint32_t high,uint16_t *out) {
    Bus *b=ctx;if(!b->word)return false;
    unsigned lo=read_bus(ctx,low);*out=(uint16_t)(lo|(read_bus(ctx,high)<<8));return true;
}
static bool write_word(void *ctx,uint32_t low,uint32_t high,uint16_t value,bool reversed) {
    Bus *b=ctx;if(!b->word)return false;
    if(reversed) {write_bus(ctx,high,value>>8);write_bus(ctx,low,(uint8_t)value);}
    else {write_bus(ctx,low,(uint8_t)value);write_bus(ctx,high,value>>8);}
    return true;
}
static Interp816 initial(unsigned bank,unsigned pc,unsigned sample,Bus *bus) {
    Interp816 c={0};c.mem=bus;c.read=read_bus;c.write=write_bus;
    if(sample&16) {c.read_word=read_word;c.write_word=write_word;}
    c.k=(uint8_t)bank;c.pc=(uint16_t)pc;c.db=(uint8_t)(sample&1?0x7f:bank);
    c.e=(sample&64)!=0;c.mf=c.e || (sample&1);c.xf=c.e || (sample&2);
    c.d=(sample&4)!=0;c.c=(sample&8)!=0;c.n=(sample&16)!=0;c.v=(sample&32)!=0;
    c.z=!c.n;c.i=sample%3!=0;c.irqWanted=c.i && sample%7==0;
    c.a=(uint16_t)(0xf8b3+sample*337);c.x=(uint16_t)(0xffff-sample*313);c.y=(uint16_t)(sample*977);
    if(c.xf) {c.x&=255;c.y&=255;}
    c.dp=(uint16_t)(sample%4==0?0:sample%4==1?0xff:sample%4==2?0x1fff:0xffff);
    c.sp=c.e?(sample&1?0x100:0x1ff):(sample&1?0xffff:0);
    c.brkHookEnabled=(sample&128)==0;c.cyclesUsed=233;return c;
}
static unsigned check(unsigned bank,unsigned pc,unsigned sample) {
    Bus a={0},b={0};a.seed=b.seed=sample;a.word=b.word=(sample&32)!=0;
    Interp816 actual=initial(bank,pc,sample,&a),reference=initial(bank,pc,sample,&b),old=actual;
    a.cpu=&actual;b.cpu=&reference;
    g_interp816_cur_pc=0x123456;
    bool available=ScProgramAvailable(&actual);
    assert(a.count==0 && !memcmp(&actual,&old,sizeof old) && g_interp816_cur_pc==0x123456);
    unsigned cost=ScProgramStep(&actual);uint32_t site=g_interp816_cur_pc;
    assert(available==(cost!=0));
    if(!cost) {
        assert(!memcmp(&actual,&old,sizeof old));return 0;
    }
    g_interp816_cur_pc=0x123456;
    unsigned expected=interp816_runOpcode(&reference);actual.mem=&b;
    /* Struct-return padding has no CPU meaning; compare every live field
     * through cyclesUsed, then every ordered bus callback separately. */
    if(cost!=expected || memcmp(&actual,&reference,offsetof(Interp816,cyclesUsed)+sizeof actual.cyclesUsed) || site!=g_interp816_cur_pc ||
       a.count!=b.count || memcmp(a.trace,b.trace,a.count*sizeof *a.trace)) {
        fprintf(stderr,"program %02x:%04x sample=%u clocks=%u/%u pc=%x/%x A=%x/%x flags=%x/%x accesses=%u/%u\n",
            bank,pc,sample,cost,expected,actual.pc,reference.pc,actual.a,reference.a,
            interp816_getFlags(&actual),interp816_getFlags(&reference),a.count,b.count);
        for(unsigned j=0;j<sizeof actual;++j) if(((uint8_t*)&actual)[j]!=((uint8_t*)&reference)[j])
            fprintf(stderr," cpu byte %u: %u/%u\n",j,((uint8_t*)&actual)[j],((uint8_t*)&reference)[j]);
        for(unsigned j=0;j<a.count && j<b.count;++j) if(memcmp(a.trace+j,b.trace+j,sizeof *a.trace)) {
            Access *u=a.trace+j,*v=b.trace+j;
            fprintf(stderr," access %u: addr=%x/%x value=%u/%u kind=%u/%u pc=%x/%x clocks=%u/%u site=%x/%x\n",
                j,u->address,v->address,u->value,v->value,u->kind,v->kind,u->pc,v->pc,u->cycles,v->cycles,u->site,v->site);
        }
        fprintf(stderr," site %x/%x\n",site,g_interp816_cur_pc);abort();
    }
    return cost;
}
typedef struct {
    Bus actual_bus,oracle_bus;
    Access actual_writes[256],oracle_writes[256];
    Interp816 oracle;
    unsigned edges,limit,mode;
    uint32_t prior_site;
} FlowOracle;
static void retain_writes(Bus *bus) {
    for(unsigned i=0;i<bus->count;++i) {
        Access a=bus->trace[i];unsigned bank=(a.address>>16)&0x7f;
        if(a.kind!=1 || ((a.address&65535)>=0x8000 && bank<16)) continue;
        unsigned at=0;while(at<bus->history_count && bus->history[at].address!=a.address) ++at;
        assert(at<256);bus->history[at]=a;
        if(at==bus->history_count) ++bus->history_count;
    }
}
static bool flow_before(void *context,Interp816 *cpu) {
    FlowOracle *o=context;
    if(!ScProgramAvailable(cpu)) return false;
    o->actual_bus.count=o->oracle_bus.count=0;o->prior_site=g_interp816_cur_pc;
    /* A preparation hook can redirect execution across banks/pages. It must
     * execute once, rather than re-entering preparation at the new C label. */
    if(o->mode==5 && o->edges==0) {cpu->k=o->oracle.k=0;cpu->pc=o->oracle.pc=0x929b;}
    return true;
}
static bool flow_after(void *context,Interp816 *cpu,unsigned clocks) {
    FlowOracle *o=context;uint32_t site=g_interp816_cur_pc;
    g_interp816_cur_pc=o->prior_site;
    unsigned expected=interp816_runOpcode(&o->oracle);
    Interp816 actual=*cpu;actual.mem=&o->oracle_bus;
    unsigned prefix=0;
    if(o->mode==6 && o->actual_bus.count==o->oracle_bus.count+1) {
        /* A changed live opcode retains the preceding scheduler's declined
         * ROM fetch before the original CPU fetch. It is a pure ROM read;
         * every CPU field and every subsequent access still match directly. */
        Access *probe=o->actual_bus.trace;
        assert(probe->kind==0 && probe->address==site && probe->value==0xea &&
            probe->pc==(uint16_t)(site+1) && !probe->cycles && probe->site==o->prior_site);
        prefix=1;
    }
    if(clocks!=expected || memcmp(&actual,&o->oracle,offsetof(Interp816,cyclesUsed)+sizeof actual.cyclesUsed) ||
        site!=g_interp816_cur_pc || o->actual_bus.count!=o->oracle_bus.count+prefix ||
        memcmp(o->actual_bus.trace+prefix,o->oracle_bus.trace,o->oracle_bus.count*sizeof(Access))) {
        fprintf(stderr,"C block edge %u site=%x/%x clocks=%u/%u pc=%x/%x accesses=%u/%u\n",
            o->edges,site,g_interp816_cur_pc,clocks,expected,cpu->pc,o->oracle.pc,
            o->actual_bus.count,o->oracle_bus.count);abort();
    }
    g_interp816_cur_pc=site;retain_writes(&o->actual_bus);retain_writes(&o->oracle_bus);
    ++o->edges;
    if(o->edges==1) {
        if(o->mode==1) cpu->nmiWanted=o->oracle.nmiWanted=true;
        if(o->mode==2) {cpu->irqWanted=o->oracle.irqWanted=true;cpu->i=o->oracle.i=false;}
        if(o->mode==3) cpu->waiting=o->oracle.waiting=true;
        if(o->mode==4) cpu->stopped=o->oracle.stopped=true;
    }
    return o->edges<o->limit;
}
static unsigned flow_fallback(void *context,Interp816 *cpu) {
    (void)context;return interp816_runOpcode(cpu);
}
static unsigned check_flow(unsigned bank,unsigned pc,unsigned sample,unsigned mode,unsigned limit) {
    FlowOracle o={0};o.limit=limit;o.mode=mode;
    o.actual_bus.seed=o.oracle_bus.seed=sample;o.actual_bus.word=o.oracle_bus.word=(sample&32)!=0;
    o.actual_bus.history=o.actual_writes;o.oracle_bus.history=o.oracle_writes;
    Interp816 actual=initial(bank,pc,sample,&o.actual_bus);
    o.oracle=initial(bank,pc,sample,&o.oracle_bus);
    o.actual_bus.cpu=&actual;o.oracle_bus.cpu=&o.oracle;
    g_interp816_cur_pc=0x123456;
    unsigned count=ScProgramRun(&actual,&o,flow_before,flow_after,flow_fallback);
    assert(count==o.edges);
    actual.mem=&o.oracle_bus;
    assert(!memcmp(&actual,&o.oracle,offsetof(Interp816,cyclesUsed)+sizeof actual.cyclesUsed));
    return count;
}
static unsigned check_control(uint32_t fingerprint) {
    unsigned checked=0;
    for(unsigned flags=0;flags<256;++flags) for(unsigned pending=0;pending<4;++pending)
    for(unsigned state=0;state<4;++state) for(unsigned emulation=0;emulation<2;++emulation)
    for(unsigned stack=0;stack<4;++stack) for(unsigned word=0;word<3;++word) {
        Bus a={0},b={0};a.seed=b.seed=flags;a.word=b.word=word==2;
        Interp816 actual=initial(1,0xa591,flags&127,&a);
        actual.e=emulation;
        interp816_setFlags(&actual,(uint8_t)flags);
        actual.k=stack&1?0x7e:1;
        const uint16_t stacks[]={0,0x100,0x1ff,0xffff};actual.sp=stacks[stack];
        actual.nmiWanted=(pending&1)!=0;actual.irqWanted=(pending&2)!=0;
        actual.waiting=(state&1)!=0;actual.stopped=(state&2)!=0;
        actual.read_word=word?read_word:NULL;
        Interp816 reference=actual,old=actual;reference.mem=&b;a.cpu=&actual;b.cpu=&reference;
        g_interp816_cur_pc=0x123456;
        unsigned cost=ScProgramControlStep(&actual);
        uint32_t site=g_interp816_cur_pc;
        g_interp816_cur_pc=0x123456;
        unsigned expected=interp816_runOpcode(&reference);
        /* The independent CPU updates this marker only when it fetches an
         * opcode. Native control must admit every no-opcode retirement and
         * decline every edge that also requires an instruction. */
        assert((cost!=0)==(g_interp816_cur_pc==0x123456));
        if(!cost) {
            assert(!memcmp(&actual,&old,sizeof old) && !a.count && site==0x123456);
        } else {
            actual.mem=&b;
            assert(cost==expected && site==g_interp816_cur_pc &&
                !memcmp(&actual,&reference,offsetof(Interp816,cyclesUsed)+sizeof actual.cyclesUsed) &&
                a.count==b.count && !memcmp(a.trace,b.trace,a.count*sizeof *a.trace));
        }
        /* Private CPUs use the same combined control/instruction entry. In
         * particular, masked IRQ must wake WAI and execute the next opcode,
         * rather than returning an idle edge or losing the pending IRQ. */
        Bus ea={0},eb={0};ea.seed=eb.seed=flags;ea.word=eb.word=word==2;
        Interp816 execute=old,oracle=old;
        execute.mem=&ea;oracle.mem=&eb;ea.cpu=&execute;eb.cpu=&oracle;
        if(execute.k==0x7e) execute.k=oracle.k=1;
        g_interp816_cur_pc=0x123456;
        unsigned execution=ScProgramExecute(&execute);site=g_interp816_cur_pc;
        g_interp816_cur_pc=0x123456;
        unsigned original=interp816_runOpcode(&oracle);execute.mem=&eb;
        assert(execution==original && site==g_interp816_cur_pc &&
            !memcmp(&execute,&oracle,offsetof(Interp816,cyclesUsed)+sizeof execute.cyclesUsed) &&
            ea.count==eb.count && !memcmp(ea.trace,eb.trace,ea.count*sizeof *ea.trace));
        ++checked;
    }
    ScProgramEnable(false);Bus bus={0};Interp816 c=initial(1,0xa591,0,&bus);c.nmiWanted=true;
    Interp816 before=c;assert(!ScProgramControlStep(&c) && !memcmp(&c,&before,sizeof c) && !bus.count);
    assert(ScProgramSelectRom(fingerprint));return checked;
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    uint32_t hash=2166136261u;for(unsigned i=0;i<sizeof rom;++i)hash=(hash^rom[i])*16777619u;
    assert(ScProgramSelectRom(hash));
    unsigned sites=0;uint64_t edges=0,fallbacks=0;
    for(unsigned bank=0;bank<16;++bank) for(unsigned pc=0x8000;pc<=0xffff;++pc) {
        if(!check(bank,pc,0)) {++fallbacks;continue;}
        ++sites;++edges;
        for(unsigned sample=1;sample<128;++sample) {assert(check(bank,pc,sample));++edges;}
    }
    assert(sites==524288 && edges==(uint64_t)sites*128);
    printf("Program control: %u exact IRQ/NMI/WAI/STP and immutable-decline states\n",check_control(hash));
    for(unsigned sample=0;sample<128;++sample) for(unsigned kind=0;kind<7;++kind) {
        Bus bus={0};Interp816 c=initial(0,0x930d,sample,&bus);
        if(kind==0)c.nmiWanted=true;
        if(kind==1) {c.irqWanted=true;c.i=false;}
        if(kind==2)c.stopped=true;
        if(kind==3)c.waiting=true;
        if(kind==4)c.k=0x7e;
        if(kind==5)c.pc=0x100;
        if(kind==6)ScProgramEnable(false);
        Interp816 before=c;assert(!ScProgramStep(&c));assert(!memcmp(&c,&before,sizeof c));assert(!bus.count);
        assert(ScProgramSelectRom(hash));++fallbacks;
    }
    unsigned original=rom[0x130d];rom[0x130d]=original==0xea?0xff:0xea;
    Bus bus={0};Interp816 c=initial(0,0x930d,0,&bus),before=c;bus.cpu=&c;
    assert(!ScProgramStep(&c));assert(!memcmp(&c,&before,sizeof c));assert(bus.count==1);
    rom[0x130d]=(uint8_t)original;++fallbacks;
    printf("Program C: %u ROM sites, %llu exact CPU/bus/cycle edges, %llu immutable fallbacks\n",
        sites,(unsigned long long)edges,(unsigned long long)fallbacks);
    unsigned aliases=0;
    const unsigned mirror_banks[]={0x10,0x80,0x90,0x40,0xc0,0x70,0xf0};
    for(unsigned i=0;i<sizeof mirror_banks/sizeof *mirror_banks;++i)
        for(unsigned pc=0x8000;pc<=0xffff;pc+=113)for(unsigned sample=0;sample<128;sample+=13) {
            assert(check(mirror_banks[i],pc,sample));++aliases;
            if((mirror_banks[i]&127)>=0x40 && (mirror_banks[i]&127)<0x70) {
                assert(check(mirror_banks[i],pc&32767,sample));++aliases;
            }
        }
    printf("Program ROM mirrors: %u exact upper-bank and low-ROM-window CPU/bus/cycle edges\n",aliases);
    unsigned architectural_brk=0;
    for(unsigned bank=0;bank<16;++bank)for(unsigned pc=0x8000;pc<=0xffff;++pc)
        if(!rom[bank*32768+pc-0x8000]) {
            for(unsigned sample=128;sample<256;++sample) {assert(check(bank,pc,sample));++architectural_brk;}
            break;
        }
    printf("Program BRK vectors: %u exact native/emulation signature, stack and vector cases\n",architectural_brk);
    if(hash!=0xec01686a) {
        printf("Regional program %08x: every ROM byte executes compiled C with exact CPU/bus/cycles\n",hash);
        return 0;
    }
    uint8_t money_entry=rom[0x42e];rom[0x42e]=0x6b;
    for(unsigned sample=0;sample<128;++sample) {
        assert(check(0,0x842e,sample));
        assert(check_flow(0,0x842e,sample,0,1)==1);
    }
    rom[0x42e]=money_entry;
    puts("Program construction HUD variant: 128 exact RTL edges and connected spans");
    uint64_t connected=0;unsigned spans=0;
    for(unsigned bank=0;bank<2;++bank) for(unsigned pc=0x8000;pc<=0xffff;++pc) {
        Bus b={0};Interp816 probe=initial(bank,pc,0,&b);
        if(!ScProgramBlockAvailable(&probe)) continue;
        for(unsigned sample=0;sample<128;sample+=7) {
            unsigned count=check_flow(bank,pc,sample,0,16);
            assert(count);connected+=count;++spans;
        }
    }
    for(unsigned sample=0;sample<128;++sample) for(unsigned mode=1;mode<=5;++mode) {
        unsigned count=check_flow(0,0x928f,sample,mode,16);
        assert(count && (mode==5 || count==1));connected+=count;++spans;
    }
    /* Every reachable call to the verified inline-operand families must
     * include its real resume instruction. Operand bytes remain data; the
     * emitted call and callee still manipulate the original stack/bus. */
    unsigned inline_calls=0;
    for(unsigned bank=0;bank<6;++bank)for(unsigned pc=0x8000;pc<0xfffa;++pc) {
        Bus b={0};Interp816 probe=initial(bank,pc,0,&b);
        if(!ScProgramAvailable(&probe))continue;
        unsigned at=bank*32768+pc-0x8000,op=rom[at],skip=0,length=0;
        if(op==0x20 && bank==3) {
            unsigned target=rom[at+1]|(unsigned)rom[at+2]<<8;
            if(target==0xa2f5 || target==0xa350 || target==0xa3cf || target==0xa421) {skip=3;length=3;}
        } else if(op==0x22 && rom[at+1]==0xa0 && rom[at+2]==0x98 && rom[at+3]==0) {skip=2;length=4;}
        if(!skip)continue;
        probe.pc=(uint16_t)(pc+length+skip);assert(ScProgramAvailable(&probe));++inline_calls;
    }
    assert(inline_calls>=125);printf("Program inline operands: %u reachable calls retain their actual caller resume sites\n",inline_calls);
    uint8_t patch[4];memcpy(patch,rom+0x40fb,4);memset(rom+0x40fb,0xea,4);
    for(unsigned pc=0xc0fb;pc<=0xc0fe;++pc) for(unsigned sample=0;sample<128;++sample)
        assert(check(0,pc,sample)==2);
    for(unsigned sample=0;sample<128;++sample) {
        unsigned count=check_flow(0,0xc0fb,sample,6,16);
        assert(count);connected+=count;++spans;
    }
    memcpy(rom+0x40fb,patch,4);
    puts("Program live view patch: 512 native NOP edges preserve every CPU field, bus fetch and clock");
    printf("Program blocks: %u independent spans, %llu exact retired edges including live control yields and preparation redirects\n",
        spans,(unsigned long long)connected);
    return 0;
}
