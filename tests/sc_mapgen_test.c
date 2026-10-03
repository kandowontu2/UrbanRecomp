#include "sc_mapgen.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t hash(const uint16_t *map,unsigned cells) {
    uint64_t h=UINT64_C(14695981039346656037);
    for (unsigned i=0;i<cells;++i) {
        h=(h^(map[i]&255))*UINT64_C(1099511628211);
        h=(h^(map[i]>>8))*UINT64_C(1099511628211);
    }
    return h;
}
int main(void) {
    /* Captured before making geometry configurable: both generator branches. */
    const uint64_t stock[]={
        UINT64_C(0x10f44bf362cfb8d0), UINT64_C(0xc28eaef5a4198d7a), UINT64_C(0xfacbce6031f24840), UINT64_C(0xd9c432a61de84d0c),
        UINT64_C(0xce9d3592f519a00e), UINT64_C(0x4538ef7b7e41ea9a), UINT64_C(0x4a3a85fade638d2a), UINT64_C(0xf61d552b616977a2),
        UINT64_C(0x6eaed2041d4d8c97), UINT64_C(0x5a491345552cc72e), UINT64_C(0x165c7e910f28082d), UINT64_C(0x43e41c1a5ae05f23),
        UINT64_C(0xe0c9a747261d2311), UINT64_C(0x986b5fe45611bfaf), UINT64_C(0xbf4c547eed37f249), UINT64_C(0xdda5932c68b65280)};
    struct Guard { uint64_t before; ScMapGenState state; uint64_t after; } *g=malloc(sizeof *g);
    ScMapGenState *again=malloc(sizeof *again); assert(g && again);
    g->before=g->after=UINT64_C(0xfacedeed98765432);
    for (unsigned seed=0;seed<16;++seed) {
        ScMapGenPrng p,q; sc_mapgen_seed(&p,0x5c00,(uint8_t)seed,0,0,2,0); q=p;
        memset(&g->state,0xa5,sizeof g->state);
        sc_mapgen_generate(&p,&g->state);
        assert(g->state.width==120 && g->state.height==100);
        assert(hash(g->state.map,12000)==stock[seed]);
        p=q; sc_mapgen_generate_large(&p,&g->state);
        assert(g->state.width==240 && g->state.height==200);
        sc_mapgen_generate_large(&q,again);
        assert(!memcmp(g->state.map,again->map,sizeof again->map));
        assert(p.s0==q.s0 && p.s1==q.s1 && p.t==q.t);
        unsigned counts[4]={0};
        for (unsigned y=0;y<200;++y) for (unsigned x=0;x<240;++x) {
            unsigned v=sc_mapgen_read_cell(&g->state,x,y);
            assert(v<=0x25); if (v) ++counts[(y>=100)*2+(x>=120)];
        }
        for (unsigned i=0;i<4;++i) assert(counts[i]>0);
        assert(g->before==UINT64_C(0xfacedeed98765432) && g->after==g->before);
        /* A far cell is beyond the old guest bank. It must not alias the
         * first 120x100 map or lose its native upper flag bits. */
        unsigned old=g->state.map[0];
        sc_mapgen_write_cell(&g->state,239,199,0xc123);
        assert(g->state.map[47999]==0xc123 && sc_mapgen_read_cell(&g->state,239,199)==0x123);
        assert(g->state.map[0]==old);
        sc_mapgen_seed(&p,0x5c00,(uint8_t)seed,0,0,2,0);
        sc_mapgen_generate(&p,&g->state); assert(hash(g->state.map,12000)==stock[seed]);
        sc_mapgen_seed(&p,0x5c00,(uint8_t)seed,0,0,2,0);q=p;
        sc_mapgen_generate_huge(&p,&g->state);sc_mapgen_generate_huge(&q,again);
        assert(g->state.width==480 && g->state.height==400);
        assert(!memcmp(g->state.map,again->map,sizeof again->map));
        memset(counts,0,sizeof counts);
        for(unsigned y=0;y<400;++y) for(unsigned x=0;x<480;++x) {
            unsigned v=sc_mapgen_read_cell(&g->state,x,y);assert(v<=0x25);
            if(v) ++counts[(y>=200)*2+(x>=240)];
        }
        for(unsigned i=0;i<4;++i) assert(counts[i]>0);
        unsigned first=g->state.map[0];sc_mapgen_write_cell(&g->state,479,399,0xc123);
        assert(g->state.map[191999]==0xc123 && g->state.map[0]==first);
        assert(g->before==g->after && g->after==UINT64_C(0xfacedeed98765432));
    }
    for(unsigned seed=0;seed<4;++seed) {
      ScMapGenPrng p,q;sc_mapgen_seed(&p,0x5c00,seed,0,0,2,0);q=p;
      sc_mapgen_generate_giant(&p,&g->state);sc_mapgen_generate_giant(&q,again);
      assert(g->state.width==960 && g->state.height==800);
      assert(!memcmp(g->state.map,again->map,sizeof again->map));
      unsigned counts[4]={0};
      for(unsigned y=0;y<800;++y) for(unsigned x=0;x<960;++x) {
        unsigned v=sc_mapgen_read_cell(&g->state,x,y);assert(v<=0x25);
        if(v) ++counts[(y>=400)*2+(x>=480)];
      }
      for(unsigned i=0;i<4;++i) assert(counts[i]>0);
      unsigned first=g->state.map[0];sc_mapgen_write_cell(&g->state,959,799,0xc123);
      assert(g->state.map[767999]==0xc123 && g->state.map[0]==first && g->before==g->after);
    }
    for(unsigned seed=0;seed<2;++seed) {
      ScMapGenPrng p,q;sc_mapgen_seed(&p,0x5c00,seed,0,0,2,0);q=p;
      sc_mapgen_generate_colossal(&p,&g->state);sc_mapgen_generate_colossal(&q,again);
      assert(g->state.width==1920 && g->state.height==1600);
      assert(!memcmp(g->state.map,again->map,sizeof again->map));
      unsigned counts[4]={0};
      for(unsigned y=0;y<1600;++y) for(unsigned x=0;x<1920;++x) {
        unsigned v=sc_mapgen_read_cell(&g->state,x,y);assert(v<=0x25);
        if(v) ++counts[(y>=800)*2+(x>=960)];
      }
      for(unsigned i=0;i<4;++i) assert(counts[i]>0);
      unsigned first=g->state.map[0];sc_mapgen_write_cell(&g->state,1919,1599,0xc123);
      assert(g->state.map[3071999]==0xc123 && g->state.map[0]==first && g->before==g->after);
    }
    free(g); free(again);
    puts("PASS: 16 stock fingerprints, deterministic continuous Big/Huge/960x800/1920x1600 terrain, far-bank cells and bounds guards");
}
