#ifndef SC_PROGRAM_FLOW_H
#define SC_PROGRAM_FLOW_H
#include "sc_program.h"
typedef struct ScProgramFlow {
    void *context;
    ScProgramBefore before;
    ScProgramAfter after;
    ScProgramFallback fallback;
    unsigned edges;
    uint64_t compiled_edges,compiled_cycles;
    bool keep_running;
} ScProgramFlow;
static inline bool ScProgramFlowRetire(ScProgramFlow *flow,Interp816 *cpu,
    unsigned clocks,bool compiled) {
    ++flow->edges;
    if(compiled) {++flow->compiled_edges;flow->compiled_cycles+=clocks;}
    flow->keep_running=flow->after(flow->context,cpu,clocks);
    return flow->keep_running;
}
void ScProgramFlowChanged(Interp816 *cpu,ScProgramFlow *flow);
#endif
