#pragma once
#include "sc_world.h"
typedef struct Interp816 Interp816;
static inline bool ScPostpassOwns(uint16_t pc) {
    return (pc>=0x88f3 && pc<=0x894b) || (pc>=0xb66a && pc<=0xb6bc);
}
/* Complete traffic decay and transport-total control in C. Whole-field
 * spans and interrupted instructions share the same immutable deadline. */
unsigned ScPostpassStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget);
unsigned ScPostpassInstructionStep(ScWorld *w,Interp816 *c,uint8_t *r);
