#include "sc_population.h"
#include <limits.h>
#include <string.h>
#include <stdio.h>

static unsigned word(const uint8_t *r, unsigned p) { return r[p] | ((unsigned)r[p+1]<<8); }
static uint32_t dword(const uint8_t *r, unsigned p) { return word(r,p) | ((uint32_t)word(r,p+2)<<16); }
static void put(uint8_t *r, unsigned p, unsigned v) { r[p]=(uint8_t)v; r[p+1]=(uint8_t)(v>>8); }
static void put32(uint8_t *r, unsigned p, uint32_t v) { put(r,p,v); put(r,p+2,v>>16); }
static uint64_t cap(uint64_t v) { return v>SC_POPULATION_MAX ? SC_POPULATION_MAX : v; }
unsigned ScPopulationClass(uint64_t v) {
    const unsigned limits[]={2000,10000,50000,100000,500000};
    unsigned n=0; while (n<5 && v>=limits[n]) ++n; return n;
}
void ScPopulationImport(ScPopulation *s, const uint8_t *r) {
    memset(s,0,sizeof *s);
    s->value=dword(r,0xba5); s->previous=dword(r,0xbcd);
    s->change=(int32_t)dword(r,0xde3);
    s->capacity[0]=word(r,0xb8b); s->capacity[1]=word(r,0xb93); s->capacity[2]=word(r,0xb8f);
    s->valid=true;
}
void ScPopulationMirror(const ScPopulation *s, uint8_t *r) {
    /* Every US population gate is <=500,000; the guest's six-digit HUD and
     * 24-bit report formatter cannot represent the new value. This monotonic
     * compatibility value preserves all gates and prevents formatter overruns.
     * The full value is retained by the host display, history and saves. */
    put32(r,0xba5,(uint32_t)(s->value>999999 ? 999999 : s->value));
    put32(r,0xbcd,(uint32_t)(s->previous>999999 ? 999999 : s->previous));
    int64_t d=s->change;
    if (d>999999) d=999999;
    if (d< -999999) d= -999999;
    put32(r,0xde3,(uint32_t)(int32_t)d);
    put(r,0xdeb,ScPopulationClass(s->value));
}
void ScPopulationReport(const ScPopulation *s, uint8_t *r, bool change) {
    if (!s->valid) return;
    int64_t value=change?s->change:(int64_t)s->value;
    if (value>=-999999 && value<=999999) return;
    char digits[32]; snprintf(digits,sizeof digits,"%lld",(long long)value);
    unsigned columns=change?11:10, end=change?0x19c:0x13c;
    unsigned start=end+1-columns, count=(unsigned)strlen(digits);
    /* The population row has a label immediately before its six digits.
     * Place the expanded value on the spare row below, instead of erasing
     * part of that label. Migration already has a separate value row. */
    if (!change) for (unsigned i=0x117;i<=0x11c;++i) put(r,0x2840+i*2,0x3ff);
    for (unsigned i=0;i<columns;++i) {
        unsigned tile=0x3ff;
        if (i+count>=columns) {
            char c=digits[i+count-columns];
            tile=(change?0x1420:0x0420)|(c=='-'?0x0f:(unsigned)(c-'0'));
        }
        put(r,0x2840+(start+i)*2,tile);
    }
}
void ScPopulationSet(ScPopulation *s, uint8_t *r, uint64_t v) {
    if (!s->valid) ScPopulationImport(s,r);
    s->value=cap(v); s->change=(int64_t)s->value-(int64_t)s->previous;
    ScPopulationMirror(s,r);
}
uint16_t ScPopulationStep(ScPopulation *s, uint8_t *r, uint16_t pc, uint16_t dp) {
    if (!s->valid) ScPopulationImport(s,r);
    const unsigned reset[]={0x8228,0x822b,0x822e};
    const unsigned tally[]={0x93b7,0x9301,0x9255};
    for (unsigned i=0;i<3;++i) {
        if (pc==reset[i]) { s->capacity[i]=0; s->tally_active[i]=1; }
        if (pc==tally[i] && s->tally_active[i]) {
            /* Add the capacity BEFORE the native 16-bit STA wraps. The extra
             * development attempts skip this instruction, so each zone counts
             * exactly once. The local is the ROM's own capacity calculation. */
            s->capacity[i]=cap(s->capacity[i]+word(r,dp));
        }
    }
    if (pc==0xb564) {
        s->previous=s->value;
        s->history[s->history_head]=s->value;
        s->history_head=(s->history_head+1)%SC_POPULATION_HISTORY;
        if (s->history_count<SC_POPULATION_HISTORY) ++s->history_count;
    }
    if (pc==0x81a3) {
        uint64_t res=s->tally_active[0]?s->capacity[0]:word(r,0xb8b);
        uint64_t com=s->tally_active[1]?s->capacity[1]:word(r,0xb93);
        uint64_t ind=s->tally_active[2]?s->capacity[2]:word(r,0xb8f);
        uint64_t units=res+(com+ind)*8;
        s->value=cap(units*20);
        s->change=(int64_t)s->value-(int64_t)s->previous;
        /* Small cities continue through the original calculation, including
         * its exact register/flag/scratch behavior. At overflow bypass only
         * the calculation and class ladder, returning through its PLD/RTS. */
        s->calculation_wide=units>65535 || s->value>999999 || s->previous>999999;
        if (s->calculation_wide) {
            ScPopulationMirror(s,r);
            return 0x821b;
        }
    }
    if (pc==0x821b && !s->calculation_wide) {
        s->value=dword(r,0xba5); s->change=(int32_t)dword(r,0xde3);
    }
    return pc;
}
static void encode64(uint8_t *p,uint64_t v) { for (unsigned i=0;i<8;++i) p[i]=(uint8_t)(v>>(i*8)); }
static uint64_t decode64(const uint8_t *p) { uint64_t v=0; for (unsigned i=0;i<8;++i) v|=(uint64_t)p[i]<<(i*8); return v; }
void ScPopulationEncode(const ScPopulation *s, uint8_t out[SC_POPULATION_BYTES]) {
    memset(out,0,SC_POPULATION_BYTES);
    memcpy(out,"SCPOP64",7); out[7]=1;
    encode64(out+8,s->value); encode64(out+16,s->previous); encode64(out+24,(uint64_t)s->change);
    for (unsigned i=0;i<3;++i) encode64(out+32+i*8,s->capacity[i]);
    encode64(out+56,s->history_head); encode64(out+64,s->history_count);
    memcpy(out+72,s->tally_active,3); out[75]=s->valid; out[76]=s->calculation_wide;
    for (unsigned i=0;i<SC_POPULATION_HISTORY;++i) encode64(out+80+i*8,s->history[i]);
}
bool ScPopulationDecode(ScPopulation *s, const uint8_t *p, size_t size) {
    if (size!=SC_POPULATION_BYTES || memcmp(p,"SCPOP64",7) || p[7]!=1) return false;
    ScPopulation t={0};
    t.value=decode64(p+8); t.previous=decode64(p+16); t.change=(int64_t)decode64(p+24);
    if (t.value>SC_POPULATION_MAX || t.previous>SC_POPULATION_MAX ||
        t.change<-(int64_t)SC_POPULATION_MAX || t.change>(int64_t)SC_POPULATION_MAX) return false;
    for (unsigned i=0;i<3;++i) {
        t.capacity[i]=decode64(p+32+i*8); t.tally_active[i]=p[72+i];
        if (t.capacity[i]>SC_POPULATION_MAX || t.tally_active[i]>1) return false;
    }
    uint64_t head=decode64(p+56), count=decode64(p+64);
    if (head>=SC_POPULATION_HISTORY || count>SC_POPULATION_HISTORY || p[75]>1 || p[76]>1) return false;
    t.history_head=(uint32_t)head; t.history_count=(uint32_t)count; t.valid=p[75]!=0; t.calculation_wide=p[76]!=0;
    for (unsigned i=0;i<SC_POPULATION_HISTORY;++i) {
        t.history[i]=decode64(p+80+i*8);
        if (t.history[i]>SC_POPULATION_MAX) return false;
    }
    *s=t; return true;
}
static uint64_t city_hash(const uint8_t *sram,unsigned slot) {
    unsigned begin=0x10+slot*0x3ff0;
    uint64_t h=UINT64_C(14695981039346656037);
    for (unsigned i=begin;i<begin+0x3ff0;++i) h=(h^sram[i])*UINT64_C(1099511628211);
    return h;
}
void ScPopulationCitiesInit(uint8_t data[SC_POPULATION_CITIES_BYTES]) {
    memset(data,0,SC_POPULATION_CITIES_BYTES); memcpy(data,"SCPCITY",7); data[7]=1;
}
bool ScPopulationCitiesValid(const uint8_t *data,size_t size) {
    return size==SC_POPULATION_CITIES_BYTES && !memcmp(data,"SCPCITY",7) && data[7]==1;
}
void ScPopulationCitySave(uint8_t data[SC_POPULATION_CITIES_BYTES], const uint8_t *sram,
                          unsigned slot, const ScPopulation *s) {
    if (slot>=2) return;
    if (!ScPopulationCitiesValid(data,SC_POPULATION_CITIES_BYTES)) ScPopulationCitiesInit(data);
    encode64(data+8+slot*8,city_hash(sram,slot));
    ScPopulationEncode(s,data+24+slot*SC_POPULATION_BYTES);
}
bool ScPopulationCityLoad(ScPopulation *s,const uint8_t *data,size_t size,
                          const uint8_t *sram,unsigned slot) {
    return slot<2 && ScPopulationCitiesValid(data,size) &&
        decode64(data+8+slot*8)==city_hash(sram,slot) &&
        ScPopulationDecode(s,data+24+slot*SC_POPULATION_BYTES,SC_POPULATION_BYTES);
}
