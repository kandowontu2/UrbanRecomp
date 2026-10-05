/* Register/bus/save ABI for generated native game code. No opcode decoder.
 * Lifecycle and flags adapted from the pinned LakeSnes/snesrecomp core.
 * Copyright (c) 2021-2023 angelo_wf and contributors, MIT.
 * See snesrecomp/THIRD_PARTY_ATTRIBUTION.md for the full notice. */
#include "snes/interp816.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

/* Keep the existing save layout and store-site hook ABI. Their historical
 * names do not imply instruction interpretation. */
uint32_t g_interp816_cur_pc;
Interp816 *interp816_init(void *mem,Interp816ReadHandler read,Interp816WriteHandler write) {
    Interp816 *cpu=calloc(1,sizeof *cpu);
    if(!cpu) return NULL;
    cpu->mem=mem;cpu->read=read;cpu->write=write;cpu->brkHookEnabled=true;
    return cpu;
}
void interp816_free(Interp816 *cpu) {free(cpu);}
void interp816_reset(Interp816 *cpu) {
    cpu->a=cpu->x=cpu->y=0;cpu->sp=0x100;
    uint8_t low=cpu->read(cpu->mem,0xfffc);
    cpu->pc=(uint16_t)(low|(cpu->read(cpu->mem,0xfffd)<<8));
    cpu->dp=0;cpu->k=cpu->db=0;
    cpu->c=cpu->z=cpu->v=cpu->n=false;cpu->i=true;cpu->d=false;
    cpu->xf=cpu->mf=cpu->e=true;
    cpu->irqWanted=cpu->nmiWanted=cpu->waiting=cpu->stopped=false;cpu->cyclesUsed=0;
}
void interp816_set_brk_hook_enabled(Interp816 *cpu,bool enabled) {
    if(cpu) cpu->brkHookEnabled=enabled;
}
void interp816_saveload(Interp816 *cpu,SaveLoadInfo *sli) {
    sli->func(sli,&cpu->a,offsetof(Interp816,cyclesUsed)-offsetof(Interp816,a));
}
uint8_t interp816_getFlags(Interp816 *cpu) {
    return (cpu->n<<7)|(cpu->v<<6)|(cpu->mf<<5)|(cpu->xf<<4)|
        (cpu->d<<3)|(cpu->i<<2)|(cpu->z<<1)|cpu->c;
}
void interp816_setFlags(Interp816 *cpu,uint8_t val) {
    cpu->n=val&128;cpu->v=val&64;cpu->mf=val&32;cpu->xf=val&16;
    cpu->d=val&8;cpu->i=val&4;cpu->z=val&2;cpu->c=val&1;
    if(cpu->e) {cpu->mf=cpu->xf=true;cpu->sp=(cpu->sp&255)|0x100;}
    if(cpu->xf) {cpu->x&=255;cpu->y&=255;}
}
uint64_t interp816_insns_total(void) {return 0;}
uint64_t interp816_cycles_total(void) {return 0;}

/* An uncovered instruction must be diagnosed, never silently interpreted.
 * The independently built oracle executable retains the real decoder. */
int interp816_runOpcode(Interp816 *cpu) {
    fprintf(stderr,"[native coverage missing] %02x:%04x M=%u X=%u "
        "waiting=%u stopped=%u NMI=%u IRQ=%u\n",cpu->k,cpu->pc,cpu->mf,cpu->xf,
        cpu->waiting,cpu->stopped,cpu->nmiWanted,cpu->irqWanted);
    fflush(stderr);exit(86);
}
