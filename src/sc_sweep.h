#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
bool ScSweepOwns(unsigned pc);
unsigned ScSweepStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,const uint8_t *rom,unsigned max_cycles);
unsigned ScSweepInstructionStep(ScWorld *world,Interp816 *cpu,uint8_t *ram,const uint8_t *rom);
