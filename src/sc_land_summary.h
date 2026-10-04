#pragma once
#include <stdint.h>
#include <stdbool.h>
/* Integer tile summaries shared by GPU validation and native preflight.
 * Ordered city-field and CPU publications remain in sc_world_guest.c. */
static inline unsigned ScLandTilePollution(unsigned tile,unsigned *cost) {
    unsigned cycles=9,value;
    if(tile==0x7f) {value=60;cycles+=3;}
    else {
        cycles+=2+3+3;
        if(tile==0x364) {value=65536-40;cycles+=3;}
        else {
            cycles+=2+3;
            if(tile<0x60) {
                cycles+=2+3+3;
                if(tile>=0x50) {value=25;cycles+=3;}
                else {
                    cycles+=2+3+3;
                    if(tile>=0x40) {value=10;cycles+=3;}
                    else {value=0;cycles+=2+3+3;}
                }
            } else {
                cycles+=3+3+3;
                if(tile<0x1fd) {value=0;cycles+=3;}
                else {
                    cycles+=2+3+3;
                    if(tile<0x245) {value=50;cycles+=3;}
                    else {
                        cycles+=2+3+3;
                        if(tile<0x267) {value=0;cycles+=3+3;}
                        else {
                            cycles+=2+3;
                            if(tile<0x277) {value=60;cycles+=3;}
                            else {
                                cycles+=2+3;
                                if(tile<0x287) {value=0;cycles+=3+3;}
                                else {
                                    cycles+=2+3;
                                    if(tile<0x2ba) {value=60;cycles+=3;}
                                    else {value=0;cycles+=2+3;}
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    *cost=cycles+2+6;return value;
}
static inline unsigned ScLandTileCycles(unsigned tile,unsigned dp) {
    unsigned cycles=tile?27+2*dp:9;
    if(tile<0x28) return cycles;
    cycles=14+3+(tile<0x2bf?3:2);
    if(tile>=0x2bf) {
        cycles+=3+(tile>=0x354?3:2);
        if(tile<0x354) {
            cycles+=3+(tile==0x307?3:2);
            if(tile!=0x307) {
                cycles+=3+(tile==0x310?3:2);
                if(tile!=0x310) cycles+=16+dp;
            }
        }
    }
    unsigned helper=0;ScLandTilePollution(tile,&helper);
    return cycles+4+6+helper+2+4+dp+4+dp+5+3+(tile<0x30?3:2)+(tile>=0x30?7+dp:0);
}

typedef struct ScLandSummary {
    uint32_t tiles[2],stats,tail,clocks;
} ScLandSummary;
static inline uint32_t ScLandTilePack(unsigned tile) {
    unsigned helper=0,score=tile>=0x28?ScLandTilePollution(tile,&helper):0;
    unsigned cost=ScLandTileCycles(tile,0),extra=ScLandTileCycles(tile,1)-cost;
    bool force=tile>=0x2bf && tile<0x354 && tile!=0x307 && tile!=0x310;
    return score|(cost<<16)|(extra<<24)|((unsigned)(tile!=0 && tile<0x28)<<28)|
        ((unsigned)force<<29)|((unsigned)(tile>=0x30)<<30)|((unsigned)(tile>=0x28)<<31);
}
static inline ScLandSummary ScLandSummaryPack(const uint16_t tiles[4]) {
    ScLandSummary p={{tiles[0]|((uint32_t)tiles[1]<<16),tiles[2]|((uint32_t)tiles[3]<<16)},0,0,0};
    unsigned density=0,pollution=0,cost=0,extra=0,occupied=0,tail=0;
    for(unsigned n=0;n<4;++n) {
        unsigned tile=tiles[n]&1023,t=ScLandTilePack(tile);
        if(t&(1u<<28)) density+=15;
        if(t&(1u<<29)) density=255;
        if(t&(1u<<31)) {pollution=(pollution+(t&65535))&65535;tail=(t&65535)|(tile<<16)|(1u<<29);}
        occupied+=(t>>30)&1;cost+=(t>>16)&255;extra+=(t>>24)&15;
    }
    p.stats=density|(pollution<<16);p.tail=tail|(occupied<<26);p.clocks=cost|(extra<<16);return p;
}
