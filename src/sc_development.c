#include "sc_development.h"
#include <string.h>

static unsigned word(const uint8_t *r, unsigned p) {
    return r[p] | ((unsigned)r[p+1]<<8);
}
static void put(uint8_t *r, unsigned p, unsigned v) {
    r[p]=(uint8_t)v; r[p+1]=(uint8_t)(v>>8);
}
void ScDevelopmentReset(ScDevelopment *s) { memset(s,0,sizeof *s); }

static uint16_t zone(unsigned tile) {
    if ((tile>=0x80 && tile<0x129) || (tile>=0x376 && tile<0x39a)) return 0x937a;
    if ((tile>=0x137 && tile<0x1f4) || tile>=0x39a) return 0x92ce;
    if (tile>=0x1f4 && tile<0x249) return 0x922f;
    return 0;
}
uint16_t ScDevelopmentStep(ScDevelopment *s, uint8_t *ram,
                           uint16_t pc, uint16_t dp, uint16_t sp, int speed) {
    return ScDevelopmentStepWorld(s,NULL,ram,pc,dp,sp,speed);
}
uint16_t ScDevelopmentStepWorld(ScDevelopment *s, const ScWorld *w, uint8_t *ram,
                           uint16_t pc, uint16_t dp, uint16_t sp, int speed) {
    bool large=w && w->active;
    if (speed<=1 && !s->remaining) return pc;
    if (!s->remaining && (pc==0x922f || pc==0x92ce || pc==0x937a)) {
        s->entry=pc; s->dp=dp; s->sp=sp;
        s->cell=(uint16_t)word(ram,0x0b49);
        if ((s->cell&1) || (large?!ScWorldBounds(ram[0xb85],ram[0xb86]):s->cell>=24000)) return pc;
        s->remaining=speed;
        s->repeating=false;
        if (pc==0x922f) {
            s->end=0x92cc; s->capacity=0x923d; s->skip=0x9242; s->attempt=0x926f;
        } else if (pc==0x92ce) {
            s->end=0x9378; s->capacity=0x92dc; s->skip=0x92ee; s->attempt=0x931b;
        } else {
            s->end=0x9447; s->capacity=0x9388; s->skip=0x93a4; s->attempt=0x93d1;
        }
        ++s->attempts;
        return pc;
    }
    /* Skip the object/population tally and transport probe on extra attempts.
     * Keep their result at local $04, but recalculate local $00 (capacity)
     * through the ROM helper after each change of the zone's tile. */
    if (s->remaining && s->repeating && pc==s->skip) return s->attempt;
    if (s->remaining && pc==s->end) {
        /* Only redirect the live zone frame, never a nested/interrupt frame. */
        unsigned locals=s->entry==0x937a?10:8;
        if (dp!=(uint16_t)(s->dp-locals) || sp!=(uint16_t)(s->sp-2)) {
            s->remaining=0; s->repeating=false; return pc;
        }
        if (--s->remaining>0) {
            unsigned raw=large?ScWorldCell(w,ram[0xb85],ram[0xb86]):word(ram,0x10200+s->cell), tile=raw&0x3ff;
            if (zone(tile)!=s->entry) {
                s->remaining=0; s->repeating=false; return pc;
            }
            put(ram,0x0b87,raw); put(ram,0x0b89,tile);
            s->repeating=true;
            ++s->attempts; ++s->extra_attempts;
            return s->capacity;
        }
        s->repeating=false;
    }
    return pc;
}
