#include "sc_refresh_clock.h"
#include <string.h>

void ScRefreshClockReset(ScRefreshClock *c,unsigned frames) {
    memset(c,0,sizeof *c); c->budget=(uint64_t)(frames?frames:1)*2;
}
void ScRefreshClockObserve(ScRefreshClock *c,uint64_t frame) {
    if (c->observed && frame>c->native_frame) {
        uint64_t gap=frame-c->native_frame;
        /* The pipeline alternates long and short work phases. Average the
         * last two gaps instead of treating each phase as a speed change. */
        c->budget=gap+(c->previous_gap?c->previous_gap:gap);
        c->previous_gap=gap;
    }
    c->observed=true; c->native_frame=frame;
}
bool ScRefreshClockDue(ScRefreshClock *c,uint64_t frame,int speed) {
    if (speed<=1) { c->started=false; c->credit=0; return false; }
    if (!c->budget) c->budget=400; /* measured initial Normal tick: 200 frames */
    if (!c->started || c->speed!=speed || frame<c->frame) {
        c->started=true; c->frame=frame; c->speed=speed; c->credit=0;
        return true; /* synchronise immediately when enabling/changing speed */
    }
    c->credit+=(frame-c->frame)*(unsigned)speed*2;
    c->frame=frame;
    if (c->credit<c->budget) return false;
    c->credit%=c->budget;
    return true;
}
