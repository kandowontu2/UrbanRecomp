#pragma once
#include "sc_world.h"
/* Whole-field GPU results include the original sum, aligned stencil clock,
 * unaligned direct-page surcharge and final ADC overflow. No floating point. */
static inline uint32_t ScStencilPack(const uint8_t *s,unsigned width,unsigned height,unsigned x,unsigned y) {
    unsigned at=y*width+x,sum=0,cost=0,extra=0,old;
    unsigned values[5]={x?s[at-1]:0,x+1<width?s[at+1]:0,y?s[at-width]:0,y+1<height?s[at+width]:0,s[at]};
    bool present[5]={x!=0,x+1<width,y!=0,y+1<height,true};
    for(unsigned n=0;n<5;++n) if(present[n]) {
        old=sum&255;sum+=values[n];cost+=7;
        if(n) {bool carry=old+values[n]>255;cost+=carry?7:3;extra+=carry;}
    }
    unsigned before=(sum-values[4])&255;
    bool overflow=((before^(sum&255))&(values[4]^(sum&255))&128)!=0;
    cost+=3+2+4+(x?2:3)+3+(x+1<width?2:3)+4+(y?2:3)+3+(y+1<height?2:3);
    cost+=3+3+4+4+3+(sum/4<250?3:5)+3+5;
    extra+=5; /* STZ, two LDYs, STA scratch, LDA scratch. */
    return sum|(cost<<11)|(extra<<20)|((unsigned)overflow<<25);
}
typedef struct ScStencilBackend {
    void *context;
    void (*begin)(void *context,const ScWorld *world,unsigned source);
    const uint32_t *(*data)(void *context,const ScWorld *world,unsigned source);
} ScStencilBackend;

/* Police/fire use wrapped 16-bit neighbours and rounding carry from LSR.
 * Pack value, exact cell clocks, final LSR carry and ADC overflow. */
static inline uint32_t ScServicePack(const uint8_t *s,unsigned width,unsigned height,unsigned x,unsigned y) {
    unsigned at=y*width+x,sum=0,cost=66;
    const unsigned offsets[4]={x?at-1:at,x+1<width?at+1:at,y?at-width:at,y+1<height?at+width:at};
    const bool present[4]={x!=0,x+1<width,y!=0,y+1<height};
    for(unsigned n=0;n<4;++n) {
        cost+=present[n]?10:3;
        if(present[n]) sum=(sum+s[2*offsets[n]]+((unsigned)s[2*offsets[n]+1]<<8))&65535;
    }
    unsigned average=sum/4,center=s[2*at]+((unsigned)s[2*at+1]<<8),total=average+center+((sum>>1)&1);
    unsigned wrapped=total&65535;
    bool overflow=((average^total)&(center^total)&32768)!=0;
    return wrapped/2|(cost<<16)|((wrapped&1)<<30)|((unsigned)overflow<<31);
}
