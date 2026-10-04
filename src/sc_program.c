#include "sc_program.h"
#include "sc_program_flow.h"
#include "snes/interp816.h"
#include <stdlib.h>
#ifdef SC_PROGRAM_TIER
#include "program_gen/sc_program_sites.h"
#endif
#ifdef SC_PROGRAM_BLOCK_TIER
void ScProgramBlocks00(Interp816 *cpu,ScProgramFlow *flow);
void ScProgramBlocks01(Interp816 *cpu,ScProgramFlow *flow);
#endif
static bool enabled;
static uint64_t calls,cycles,control_calls;
void ScProgramEnable(bool clean_us) {enabled=clean_us;calls=cycles=control_calls=0;}
uint64_t ScProgramCalls(void) {return calls;}
uint64_t ScProgramCycles(void) {return cycles;}
uint64_t ScProgramControlCalls(void) {return control_calls;}
static void push_control_byte(Interp816 *cpu,uint8_t value) {
    cpu->write(cpu->mem,cpu->sp,value);
    --cpu->sp;
    if(cpu->e) cpu->sp=(cpu->sp&255)|0x100;
}
unsigned ScProgramControlStep(Interp816 *cpu) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_PROGRAM_CONTROL_REFERENCE");reference=e && *e=='1';}
    if(!enabled || reference || !cpu) return 0;
    /* Admission precedes mutation: a masked IRQ wakes WAI and executes its
     * next opcode in the same original edge. Leave that to the caller. */
    bool interrupt=cpu->nmiWanted || (cpu->irqWanted && !cpu->i);
    if(!cpu->stopped && !interrupt && !(cpu->waiting && !cpu->irqWanted)) return 0;
    ++control_calls;
    cpu->cyclesUsed=0;
    if(cpu->stopped) return 1;
    if(cpu->waiting) {
        if(!cpu->nmiWanted && !cpu->irqWanted) return 1;
        cpu->waiting=false;
    }
    bool irq=!cpu->nmiWanted;
    if(irq) cpu->irqWanted=false;else cpu->nmiWanted=false;
    cpu->cyclesUsed=7;
    push_control_byte(cpu,cpu->k);
    push_control_byte(cpu,(uint8_t)(cpu->pc>>8));
    push_control_byte(cpu,(uint8_t)cpu->pc);
    uint8_t flags=(cpu->n<<7)|(cpu->v<<6)|(cpu->mf<<5)|(cpu->xf<<4)|
                  (cpu->d<<3)|(cpu->i<<2)|(cpu->z<<1)|cpu->c;
    push_control_byte(cpu,flags);
    ++cpu->cyclesUsed;
    cpu->i=true;cpu->d=false;cpu->k=0;
    uint32_t vector=irq?0xffee:0xffea;
    uint16_t value;
    if(!cpu->read_word || !cpu->read_word(cpu->mem,vector,vector+1,&value)) {
        uint8_t low=cpu->read(cpu->mem,vector);
        value=(uint16_t)(low|(cpu->read(cpu->mem,vector+1)<<8));
    }
    cpu->pc=value;
    return cpu->cyclesUsed;
}
bool ScProgramAvailable(const Interp816 *cpu) {
#ifdef SC_PROGRAM_TIER
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_PROGRAM_REFERENCE");reference=e && *e=='1';}
    if(!enabled || reference || !cpu || cpu->pc<0x8000 || cpu->k>=6 ||
       cpu->stopped || cpu->waiting || cpu->nmiWanted || (cpu->irqWanted && !cpu->i)) return false;
    unsigned bit=cpu->pc-0x8000;
    return (sc_program_sites[cpu->k][bit>>3]&(1u<<(bit&7)))!=0;
#else
    (void)cpu;return false;
#endif
}
unsigned ScProgramStep(Interp816 *cpu) {
#ifdef SC_PROGRAM_TIER
    if(!ScProgramAvailable(cpu)) return 0;
    unsigned cost;
    switch(cpu->k) {
    case 0:cost=ScProgramBank00(cpu);break;
    case 1:cost=ScProgramBank01(cpu);break;
    case 2:cost=ScProgramBank02(cpu);break;
    case 3:cost=ScProgramBank03(cpu);break;
    case 4:cost=ScProgramBank04(cpu);break;
    default:cost=ScProgramBank05(cpu);break;
    }
    if(cost) {++calls;cycles+=cost;}
    return cost;
#else
    (void)cpu;return 0;
#endif
}
void ScProgramFlowChanged(Interp816 *cpu,ScProgramFlow *flow) {
    unsigned cost=ScProgramStep(cpu);
    if(!cost) cost=flow->fallback(flow->context,cpu);
    ScProgramFlowRetire(flow,cpu,cost?cost:1,false);
}
unsigned ScProgramRun(Interp816 *cpu,void *context,ScProgramBefore before,
    ScProgramAfter after,ScProgramFallback fallback) {
#ifdef SC_PROGRAM_BLOCK_TIER
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_PROGRAM_BLOCKS_REFERENCE");reference=e && *e=='1';}
    if(reference || !before || !after || !fallback || !ScProgramAvailable(cpu) || cpu->k>1) return 0;
    ScProgramFlow flow={context,before,after,fallback,0,0,0,true};
    while(flow.keep_running && cpu->k<2 && ScProgramAvailable(cpu)) {
        unsigned prior=flow.edges;
        if(cpu->k==0) ScProgramBlocks00(cpu,&flow);else ScProgramBlocks01(cpu,&flow);
        if(flow.edges==prior) break;
    }
    calls+=flow.compiled_edges;cycles+=flow.compiled_cycles;
    return flow.edges;
#else
    (void)cpu;(void)context;(void)before;(void)after;(void)fallback;return 0;
#endif
}
