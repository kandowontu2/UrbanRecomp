/* Whole-executable migration check. Linker wrapping covers every call site,
 * including private CPUs and reference routines outside the main scheduler.
 * This is a development gate, not a replacement opcode decoder. */
#include "snes/interp816.h"
#include <stdio.h>
#include <stdlib.h>

int __real_interp816_runOpcode(Interp816 *cpu);
int __wrap_interp816_runOpcode(Interp816 *cpu) {
    static int required=-1;
    if(required<0) {
        const char *value=getenv("SC_REQUIRE_NATIVE");
        required=value && *value=='1';
    }
    if(required) {
        fprintf(stderr,"[native execution violation] interpreter requested at %02x:%04x "
            "M=%u X=%u waiting=%u stopped=%u NMI=%u IRQ=%u\n",
            cpu->k,cpu->pc,cpu->mf,cpu->xf,cpu->waiting,cpu->stopped,cpu->nmiWanted,cpu->irqWanted);
        fflush(stderr);
        exit(86);
    }
    return __real_interp816_runOpcode(cpu);
}
