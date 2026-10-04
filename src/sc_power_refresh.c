#include "sc_power_refresh.h"
#include "sc_construction.h"
#include <string.h>
#include <stdlib.h>

static unsigned normal_initial(int speed) {
    return speed==0?800:speed==1?400:200;
}
void ScPowerRefreshReset(ScPowerRefresh *s) {
    memset(s,0,sizeof *s); s->game_speed=-1;
    s->clock_cells=12000;
    ScRefreshClockReset(&s->clock,200);
}
static void game_speed(ScPowerRefresh *s,int speed) {
    if (s->game_speed==speed) return;
    s->game_speed=speed;
    ScRefreshClockReset(&s->clock,normal_initial(speed));
}
void ScPowerRefreshObserve(ScPowerRefresh *s,uint64_t frame,int speed) {
    game_speed(s,speed); ScRefreshClockObserve(&s->clock,frame);
}
static bool traversal_allowed(const uint8_t *ram) {
    unsigned previous=ram[0xb89]|(unsigned)ram[0xb8a]<<8;
    return previous!=0x27c && previous!=0x28c;
}
static void publish(ScPowerRefresh *s,uint8_t *tiles,unsigned first,unsigned end,ScWorld *world) {
    ScWorldPublishPower(world,tiles,s->bitmap,first,end);
}
static bool tile_ids_differ(const uint8_t *current,const uint8_t *previous,unsigned bytes) {
    uint64_t different=0;
    for(unsigned i=0;i<bytes;i+=8) {
        uint64_t a,b;memcpy(&a,current+i,8);memcpy(&b,previous+i,8);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        different|=(a^b)&UINT64_C(0xff03ff03ff03ff03);
#else
        different|=(a^b)&UINT64_C(0x03ff03ff03ff03ff);
#endif
    }
    return different!=0;
}
static bool electrical_topology_differ(const uint8_t *current,const uint8_t *previous,
    unsigned bytes,const uint8_t *rom) {
    if(!tile_ids_differ(current,previous,bytes)) return false;
    /* The ordered solver reads only conductivity and these two seed IDs.
     * Ordinary building artwork and wire joins can change without changing
     * a single edge, seed, capacity or traversal order in the network. */
    for(unsigned i=0;i<bytes;i+=2) {
        unsigned a=(current[i]|(unsigned)current[i+1]<<8)&1023;
        unsigned b=(previous[i]|(unsigned)previous[i+1]<<8)&1023;
        if(a==b) continue;
        if(a==0x27c || a==0x28c || b==0x27c || b==0x28c ||
           ((rom[0x184eb+a]^rom[0x184eb+b])&128)) return true;
    }
    return false;
}
static void remember_regions(ScPowerRefresh *s,const ScWorld *world,unsigned cells) {
    if(!world) return;
    unsigned chunks=(cells*2+SC_WORLD_TILE_CHUNK_BYTES-1)/SC_WORLD_TILE_CHUNK_BYTES;
    const uint64_t *revisions=ScWorldTileRevisions(world);
    memcpy(s->topology_revisions,revisions,chunks*sizeof *revisions);
    memcpy(s->publication_revisions,revisions,chunks*sizeof *revisions);
    memset(s->topology_different,0,sizeof s->topology_different);s->different_chunks=0;
    memset(s->ids_different,0,sizeof s->ids_different);s->different_id_chunks=0;
}
bool ScPowerRefreshRestore(ScPowerRefresh *s,uint8_t *ram,ScWorld *world,
    const uint8_t *rom,size_t size,uint64_t frame) {
    ScPowerRefreshReset(s);
    bool large=world && world->active;
    unsigned cells=large?ScWorldCells(world):12000;
    if(!ScConstructionPowerBitmap(ram,world,rom,size,s->bitmap,sizeof s->bitmap)) return false;
    uint8_t *tiles=large?world->tiles:ram+0x10200;
    s->ready=true;s->large=large;s->huge=large && world->huge;s->clock_cells=cells;
    s->traversal_allowed=traversal_allowed(ram);
    memcpy(s->policy_bitmap[s->traversal_allowed],s->bitmap,cells/8);
    s->policy_valid[s->traversal_allowed]=true;++s->network_solves;
    memcpy(s->topology_tiles,tiles,2*cells);
    game_speed(s,ram[0x193]);ScRefreshClockObserve(&s->clock,frame);
    publish(s,tiles,0,cells,large?world:NULL);
    remember_regions(s,large?world:NULL,cells);
    memcpy(large?world->fields[5]:ram+0x1a598,s->bitmap,cells/8);
    return true;
}
bool ScPowerRefreshStep(ScPowerRefresh *s,uint8_t *ram,ScWorld *world,
    const uint8_t *rom,size_t size,uint64_t frame,int multiplier,bool bitmap_available) {
    bool large=world && world->active;
    unsigned cells=large?ScWorldCells(world):12000;
    if(s->clock_cells!=cells) {s->clock_cells=cells;s->game_speed=-1;s->ready=false;}
    game_speed(s,ram[0x193]);
    bool due=ScRefreshClockDue(&s->clock,frame,multiplier);
    uint8_t *tiles=large?world->tiles:ram+0x10200;
    bool same=s->ready && s->large==large && s->huge==(large && world->huge);
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_POWER_REGIONS_REFERENCE");reference=e && *e=='1';}
    bool regional=large && !reference;
    static int id_reference=-1;
    if(id_reference<0) {const char *e=getenv("SC_POWER_ID_REFERENCE");id_reference=e && *e=='1';}
    unsigned chunks=(cells*2+SC_WORLD_TILE_CHUNK_BYTES-1)/SC_WORLD_TILE_CHUNK_BYTES;
    const uint64_t *revisions=regional?ScWorldTileRevisions(world):NULL;
    if(same) {
        /* Exact, vectorizable comparison of tile IDs. Ignore native power
         * and metadata bits; a serial full-map hash dominated cached frames. */
        if(regional) {
            for(unsigned chunk=0;chunk<chunks;++chunk) {
                if(revisions[chunk]==s->topology_revisions[chunk]) continue;
                unsigned at=chunk*SC_WORLD_TILE_CHUNK_BYTES;
                unsigned bytes=2*cells-at<SC_WORLD_TILE_CHUNK_BYTES?2*cells-at:SC_WORLD_TILE_CHUNK_BYTES;
                bool ids_different=tile_ids_differ(tiles+at,s->topology_tiles+at,bytes);
                if(ids_different!=s->ids_different[chunk]) {
                    if(ids_different) ++s->different_id_chunks;else --s->different_id_chunks;
                    s->ids_different[chunk]=ids_different;
                }
                bool different=id_reference || !rom || size!=0x80000?
                    ids_different:
                    electrical_topology_differ(tiles+at,s->topology_tiles+at,bytes,rom);
                if(different!=s->topology_different[chunk]) {
                    if(different) ++s->different_chunks;else --s->different_chunks;
                    s->topology_different[chunk]=different;
                }
                s->topology_revisions[chunk]=revisions[chunk];
            }
            same=s->different_id_chunks==0;
        }
        else same=!tile_ids_differ(tiles,s->topology_tiles,2*cells);
    }
    bool solved=false,new_bitmap=false;
    if (multiplier>1 && due && !same) {
        bool graph_same=s->ready && regional && s->different_chunks==0;
        bool allowed=traversal_allowed(ram);
        bool equivalent=graph_same && s->traversal_allowed==allowed;
        if(!equivalent) {
            static int policy_reference=-1;
            if(policy_reference<0) {const char *e=getenv("SC_POWER_POLICY_CACHE_REFERENCE");policy_reference=e && *e=='1';}
            if(!graph_same)memset(s->policy_valid,0,sizeof s->policy_valid);
            if(!policy_reference && graph_same && s->policy_valid[allowed]) {
                /* Reuse the same ordered result, including its capacity
                 * cutoff. Never substitute an unordered flood fill. */
                memcpy(s->bitmap,s->policy_bitmap[allowed],cells/8);
                ++s->policy_reuses;
            } else {
                if (!ScConstructionPowerBitmap(ram,world,rom,size,s->bitmap,sizeof s->bitmap)) return false;
                ++s->network_solves;
                memcpy(s->policy_bitmap[allowed],s->bitmap,cells/8);
                s->policy_valid[allowed]=true;
            }
            new_bitmap=true;s->traversal_allowed=allowed;
        }
        s->ready=true; s->large=large; s->huge=large && world->huge;
        if(equivalent) {
            /* Revalidate at exactly the same cadence as the raw-ID cache.
             * The bitmap is identical only with the same $b89 traversal
             * input. Advance the raw snapshot too, so a later scratch change
             * cannot create a solve that the reference would have skipped. */
            for(unsigned chunk=0;chunk<chunks;++chunk) if(s->ids_different[chunk]) {
                unsigned at=chunk*SC_WORLD_TILE_CHUNK_BYTES;
                unsigned bytes=2*cells-at<SC_WORLD_TILE_CHUNK_BYTES?2*cells-at:SC_WORLD_TILE_CHUNK_BYTES;
                memcpy(s->topology_tiles+at,tiles+at,bytes);
            }
            memset(s->ids_different,0,sizeof s->ids_different);s->different_id_chunks=0;
        } else memcpy(s->topology_tiles,tiles,2*cells);
        same=solved=true;
    }
    if (!same) {if(multiplier<=1) s->ready=false;return false;}
    /* Native bitmap rebuilding must not temporarily depower an unchanged
     * network. Restore its settled flags from the last verified result. */
    if(regional && !new_bitmap) {
        /* Publication has its own history: power bits changed while a
         * topology edit was pending still need repair if the edit is undone.
         * Reading a renderer's revisions never consumes its invalidations. */
        for(unsigned chunk=0;chunk<chunks;++chunk) {
            if(revisions[chunk]==s->publication_revisions[chunk]) continue;
            unsigned first=chunk*(SC_WORLD_TILE_CHUNK_BYTES/2);
            unsigned end=first+SC_WORLD_TILE_CHUNK_BYTES/2;
            publish(s,tiles,first,end<cells?end:cells,world);
            s->topology_revisions[chunk]=s->publication_revisions[chunk]=revisions[chunk];
        }
    } else {
        publish(s,tiles,0,cells,large?world:NULL);
        if(regional) remember_regions(s,world,cells);
    }
    if (bitmap_available)
        memcpy(large?world->fields[5]:ram+0x1a598,s->bitmap,cells/8);
    return solved;
}
