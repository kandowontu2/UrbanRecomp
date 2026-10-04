#pragma once
#include <stdint.h>

/* Independent crime-cell result, native scratch shadow, and exact branch
 * clocks. Aggregate overflow is ordered and remains with the publisher. */
typedef struct ScCrimeSample {
    uint32_t value_scratch,clocks,source;
} ScCrimeSample;
static inline ScCrimeSample ScCrimePack(unsigned land,unsigned density,
                                        unsigned coverage,unsigned bias) {
    ScCrimeSample p={0,0,land|(density<<8)|(coverage<<16)};
    if(!land) return p;
    unsigned cost=31,extra=2,value=(land<128?128-land:0)+density;
    cost+=3+3+4+7+3+2+4+(land<=128?3:5);extra+=3;
    cost+=3+2+5+3+(value>255?7:3);extra+=1+(value>255);
    value=(value+bias)&65535;
    cost+=3+4+2+5+4+4;extra+=3;
    if(value&32768) {cost+=3+3+4;extra+=1;value=0;}
    else {cost+=2+3;if(value>=300) {cost+=2+3+3+4;extra+=1;value=300;}else cost+=3;}
    cost+=36;extra+=2;
    unsigned scratch=(value-coverage)&65535;
    cost+=3+2+2+4+2+6+4;extra+=2;
    if(value<coverage) {cost+=2+3+3;value=0;}
    else {value-=coverage;cost+=3+4+3;extra+=1;if(value>=250) {cost+=2+3;value=250;}else cost+=3;}
    cost+=3+5+5+3+2+4+4;extra+=2;
    p.value_scratch=value|(scratch<<8);p.clocks=cost|(extra<<16);return p;
}
