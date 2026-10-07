#include "sc_population.h"
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned word(const uint8_t *r, unsigned p) { return r[p] | ((unsigned)r[p+1]<<8); }
static uint32_t dword(const uint8_t *r, unsigned p) { return word(r,p) | ((uint32_t)word(r,p+2)<<16); }
static void put(uint8_t *r, unsigned p, unsigned v) { r[p]=(uint8_t)v; r[p+1]=(uint8_t)(v>>8); }
static void put32(uint8_t *r, unsigned p, uint32_t v) { put(r,p,v); put(r,p+2,v>>16); }
static uint64_t cap(uint64_t v) { return v>SC_POPULATION_MAX ? SC_POPULATION_MAX : v; }
static unsigned map_cell(const uint8_t *map,int width,int height,int x,int y) {
    if (x<0 || y<0 || x>=width || y>=height) return 0;
    return word(map,2*(y*width+x))&1023;
}
bool ScPopulationRefreshLive(ScPopulation *s,const uint8_t *ram,const ScWorld *world,
                             const uint8_t *rom,size_t size) {
    if (!rom || size!=0x80000) return false;
    bool large=world && world->active;
    int width=large?ScWorldWidth(world):120,height=large?ScWorldHeight(world):100;
    const uint8_t *map=large?world->tiles:ram+0x10200;
    uint64_t capacity[3]={0,0,0};
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
        unsigned tile=map_cell(map,width,height,x,y);
        /* US 03:84eb bit 0 is the same once-per-object dispatch used by the
         * native sweep. Ignore the other eight tiles, flags, and non-RCI art. */
        if (tile>=958 || !(rom[0x184eb+tile]&1)) continue;
        if ((tile>=0x80 && tile<0x129) || (tile>=0x376 && tile<0x39a)) {
            unsigned n=0;
            if (tile>=0x376) n=48;
            else if (tile==0x84) {
                /* 03:9a3e counts occupied houses surrounding a free zone. */
                for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
                    if (!dx && !dy) continue;
                    unsigned house=map_cell(map,width,height,x+dx,y+dy);
                    n+=house>=0x89 && house<0x95;
                }
            } else if (tile>=0x99) n=(2+(tile-0x99)%36/9)*8;
            capacity[0]+=n;
        } else if ((tile>=0x137 && tile<0x1f4) || tile>=0x39a) {
            capacity[1]+=tile>=0x39a?6:tile>=0x144?1+(tile-0x144)%45/9:0;
        } else if (tile>=0x1f4 && tile<0x249) {
            capacity[2]+=tile>=0x201?1+(tile-0x201)%36/9:0;
        }
    }
    if (!s->valid) ScPopulationImport(s,ram);
    s->value=cap((capacity[0]+(capacity[1]+capacity[2])*8)*20);
    s->change=(int64_t)s->value-(int64_t)s->previous;
    s->live=true;
    return true;
}

struct ScPopulationCensus {
    const ScWorld *world;
    const uint8_t *rom;
    uint64_t epoch, evaluated;
    unsigned width, height;
    bool ready;
    uint16_t ids[SC_WORLD_MAX_CELLS];
    uint64_t revisions[SC_WORLD_TILE_CHUNKS];
    uint64_t total[3];
    bool dirty[SC_WORLD_TILE_CHUNKS];
    uint8_t affected[SC_WORLD_MAX_CELLS/8];
    uint32_t queue[SC_WORLD_MAX_CELLS];
    unsigned queued;
};
ScPopulationCensus *ScPopulationCensusCreate(void) {return calloc(1,sizeof(ScPopulationCensus));}
void ScPopulationCensusDestroy(ScPopulationCensus *c) {free(c);}
uint64_t ScPopulationCensusEvaluatedCells(const ScPopulationCensus *c) {return c?c->evaluated:0;}

static unsigned census_cell(const uint8_t *map,const uint16_t *ids,int width,int height,int x,int y) {
    if(x<0 || y<0 || x>=width || y>=height)return 0;
    return ids?ids[y*width+x]:word(map,2*(y*width+x))&1023;
}
static void census_capacity(const uint8_t *map,const uint16_t *ids,int width,int height,const uint8_t *rom,
                            int x,int y,uint32_t capacity[3]) {
    unsigned tile=census_cell(map,ids,width,height,x,y);
    if(tile>=958 || !(rom[0x184eb+tile]&1)) return;
    if((tile>=0x80 && tile<0x129) || (tile>=0x376 && tile<0x39a)) {
        unsigned n=0;
        if(tile>=0x376) n=48;
        else if(tile==0x84) {
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                if(!dx && !dy) continue;
                unsigned house=census_cell(map,ids,width,height,x+dx,y+dy);
                n+=house>=0x89 && house<0x95;
            }
        } else if(tile>=0x99) n=(2+(tile-0x99)%36/9)*8;
        capacity[0]+=n;
    } else if((tile>=0x137 && tile<0x1f4) || tile>=0x39a) {
        capacity[1]+=tile>=0x39a?6:tile>=0x144?1+(tile-0x144)%45/9:0;
    } else if(tile>=0x1f4 && tile<0x249) {
        capacity[2]+=tile>=0x201?1+(tile-0x201)%36/9:0;
    }
}
static bool census_center(unsigned tile,const uint8_t *rom) {
    return tile<958 && (rom[0x184eb+tile]&1) &&
        ((tile>=0x80 && tile<0x129) || (tile>=0x137 && tile<0x249) || tile>=0x376);
}
static void census_affect(ScPopulationCensus *c,unsigned cell) {
    unsigned byte=cell/8,bit=1u<<(cell&7);
    if(c->affected[byte]&bit)return;
    c->affected[byte]|=(uint8_t)bit;c->queue[c->queued++]=cell;
}
bool ScPopulationRefreshCached(ScPopulationCensus *c,ScPopulation *s,const uint8_t *ram,
    const ScWorld *w,const uint8_t *rom,size_t size) {
    if(!s || !ram || !rom || size!=0x80000) return false;
    if(c) c->evaluated=0;
    if(!c || !w || !w->active) {
        if(c) c->ready=false;
        return ScPopulationRefreshLive(s,ram,w,rom,size);
    }
    unsigned width=ScWorldWidth(w),height=ScWorldHeight(w),cells=width*height;
    const unsigned span=SC_WORLD_TILE_CHUNK_BYTES/2,chunks=(cells+span-1)/span;
    const uint64_t *revisions=ScWorldTileRevisions(w);
    uint64_t epoch=ScWorldStateEpoch();
    bool full=!c->ready || c->world!=w || c->rom!=rom || c->epoch!=epoch ||
        c->width!=width || c->height!=height;
    if(full) {
        c->world=w;c->rom=rom;c->epoch=epoch;c->width=width;c->height=height;
        memset(c->total,0,sizeof c->total);memset(c->dirty,0,sizeof c->dirty);
        memset(c->affected,0,sizeof c->affected);c->queued=0;
    }
    for(unsigned chunk=0;chunk<chunks;++chunk) {
        if(!full && c->revisions[chunk]==revisions[chunk]) continue;
        unsigned first=chunk*span,end=first+span;if(end>cells) end=cells;
        bool changed=false;
        for(unsigned i=first;i<end;++i) {
            unsigned tile=word(w->tiles,2*i)&1023;
            if(full) {c->ids[i]=(uint16_t)tile;continue;}
            unsigned old=c->ids[i];if(old==tile)continue;
            changed=true;
            if(census_center(old,rom) || census_center(tile,rom))census_affect(c,i);
            /* Only a change in occupied-house membership can affect another
             * center's population. Keep the complete old snapshot until all
             * deltas have been computed, including simultaneous neighbor edits. */
            if((old>=0x89 && old<0x95)!=(tile>=0x89 && tile<0x95)) {
                int x=(int)(i%width),y=(int)(i/width);
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    if((!dx && !dy) || x+dx<0 || y+dy<0 || x+dx>=(int)width || y+dy>=(int)height)continue;
                    unsigned neighbor=(unsigned)(y+dy)*width+(unsigned)(x+dx);
                    if(c->ids[neighbor]==0x84 || (word(w->tiles,2*neighbor)&1023)==0x84)census_affect(c,neighbor);
                }
            }
        }
        c->revisions[chunk]=revisions[chunk];
        c->dirty[chunk]=changed;
        if(full) {
            uint32_t capacity[3]={0};unsigned x=first%width,y=first/width;
            for(unsigned i=first;i<end;++i) {
                census_capacity(w->tiles,NULL,(int)width,(int)height,rom,(int)x,(int)y,capacity);
                if(++x==width) {x=0;++y;}
            }
            for(unsigned kind=0;kind<3;++kind)c->total[kind]+=capacity[kind];
            c->evaluated+=end-first;
        }
    }
    for(unsigned n=0;n<c->queued;++n) {
        unsigned i=c->queue[n];int x=(int)(i%width),y=(int)(i/width);
        uint32_t old[3]={0},next[3]={0};
        census_capacity(NULL,c->ids,(int)width,(int)height,rom,x,y,old);
        census_capacity(w->tiles,NULL,(int)width,(int)height,rom,x,y,next);
        for(unsigned kind=0;kind<3;++kind) {c->total[kind]-=old[kind];c->total[kind]+=next[kind];}
        c->affected[i/8]&=(uint8_t)~(1u<<(i&7));++c->evaluated;
    }
    c->queued=0;
    /* Commit IDs only after all old/new neighborhood deltas are resolved. */
    for(unsigned chunk=0;chunk<chunks;++chunk)if(c->dirty[chunk]) {
        unsigned first=chunk*span,end=first+span;if(end>cells)end=cells;
        for(unsigned i=first;i<end;++i)c->ids[i]=(uint16_t)(word(w->tiles,2*i)&1023);
        c->dirty[chunk]=false;
    }
    c->ready=true;
    if(!s->valid) ScPopulationImport(s,ram);
    s->value=cap((c->total[0]+(c->total[1]+c->total[2])*8)*20);
    s->change=(int64_t)s->value-(int64_t)s->previous;s->live=true;
    return true;
}
unsigned ScPopulationClass(uint64_t v) {
    const unsigned limits[]={2000,10000,50000,100000,500000};
    unsigned n=0; while (n<5 && v>=limits[n]) ++n; return n;
}
bool ScPopulationQueueMilestone(ScPopulation *s,uint8_t *ram) {
    if(!s->valid || s->value<SC_MEGAGOPOLOS_POPULATION ||
       s->megagopolos_announcing || s->gigagopolois_announcing ||
       ram[0x14] || !word(ram,0x3e) || word(ram,0xca5)<5 || word(ram,0xd7) || word(ram,0x379) ||
       ram[0xe3] || ram[0x391] || word(ram,0x395) || word(ram,0x397) || word(ram,0x3fe))return false;
    /* Message 4 has the original happy Wright/fanfare, with no gift or
     * scenario side effect. Keep the guest's class within its six entries. */
    if(!s->megagopolos_unlocked)s->megagopolos_unlocked=s->megagopolos_announcing=true;
    else if(!s->gigagopolois_unlocked && s->value>=SC_GIGAGOPOLOIS_POPULATION)
        s->gigagopolois_unlocked=s->gigagopolois_announcing=true;
    else return false;
    put(ram,0x397,4);put(ram,0x395,1);return true;
}
unsigned ScPopulationMusicMilestone(const ScPopulation *s) {
    if(s->gigagopolois_unlocked && !s->gigagopolois_announcing)return 2;
    return s->megagopolos_unlocked && !s->megagopolos_announcing;
}
bool ScPopulationMilestoneRead(const ScPopulation *s,uint32_t address,uint8_t *value) {
    static const char *const mega[]={
        "MEGAGOPOLOS!","","Congratulations, Mayor!","Ten million citizens!",
        "Your city has reached a","new level of greatness.","Keep our people happy,",
        "and let*s keep growing!"};
    static const char *const giga[]={
        "GIGAGOPOLOIS!","","Congratulations, Mayor!","One hundred million!",
        "An incredible city of","one hundred million",
        "citizens! Keep building","a bright future for all!"};
    const char *const *lines=s->gigagopolois_announcing?giga:mega;
    address&=0x7fffff;
    if(address<0x0ffd00 || address>=0x0fff00)return false;
    unsigned row=(address-0x0ffd00)/24,col=(address-0x0ffd00)%24;
    *value=row>=8?0xff:
        col<strlen(lines[row])?(uint8_t)lines[row][col]:' ';
    return true;
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
    bool small=value>=-999999 && value<=999999;
    if (small && !s->live) return;
    char digits[32]; snprintf(digits,sizeof digits,"%lld",(long long)value);
    unsigned columns=change?14:small?6:13, end=change?0x19c:small?0x11c:0x13c;
    unsigned start=end+1-columns, count=(unsigned)strlen(digits);
    /* The population row has a label immediately before its six digits.
     * Place the expanded value on the spare row below, instead of erasing
     * part of that label. Migration already has a separate value row. */
    if (!change && !small) for (unsigned i=0x117;i<=0x11c;++i) put(r,0x2840+i*2,0x3ff);
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
        if (!s->live) s->value=cap(units*20);
        s->change=(int64_t)s->value-(int64_t)s->previous;
        /* Small cities continue through the original calculation, including
         * its exact register/flag/scratch behavior. At overflow bypass only
         * the calculation and class ladder, returning through its PLD/RTS. */
        s->calculation_wide=s->live || units>65535 || s->value>999999 || s->previous>999999;
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
    out[77]=s->live;
    out[78]=s->megagopolos_unlocked | s->megagopolos_announcing<<1 |
        s->gigagopolois_unlocked<<2 | s->gigagopolois_announcing<<3;
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
    if (head>=SC_POPULATION_HISTORY || count>SC_POPULATION_HISTORY || p[75]>1 || p[76]>1 || p[77]>1) return false;
    t.history_head=(uint32_t)head; t.history_count=(uint32_t)count; t.valid=p[75]!=0; t.calculation_wide=p[76]!=0;
    /* Valid states: no rewards, Mega earned/announcing, both earned, or
     * Giga announcing after Mega. The two native visits cannot overlap. */
    if(p[78]!=0 && p[78]!=1 && p[78]!=3 && p[78]!=5 && p[78]!=13)return false;
    t.live=p[77]!=0;t.megagopolos_unlocked=(p[78]&1)!=0;t.megagopolos_announcing=(p[78]&2)!=0;
    t.gigagopolois_unlocked=(p[78]&4)!=0;t.gigagopolois_announcing=(p[78]&8)!=0;
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
