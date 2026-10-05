#include "sc_mapgen.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
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
static unsigned geographic_check(ScMapGenState *st,unsigned seed,unsigned size) {
    unsigned w=st->width,h=st->height,n=w*h,counts[3]={0},quadrants[4]={0};
    uint8_t *seen=calloc(n,1);unsigned *queue=malloc(n*sizeof *queue);assert(seen && queue);
    bool crossing=false;
    for(unsigned i=0;i<n;++i) {
        unsigned v=st->map[i];assert(v<=0x25);
        unsigned type=!v?0:v<0x14?1:2;++counts[type];
        if(v)++quadrants[((i/w)>=h/2)*2+((i%w)>=w/2)];
        if(type!=1 || seen[i])continue;
        unsigned head=0,tail=0,edges=0;queue[tail++]=i;seen[i]=1;
        while(head<tail) {
            unsigned at=queue[head++],x=at%w,y=at/w;
            edges|=(x==0?1:0)|(x==w-1?2:0)|(y==0?4:0)|(y==h-1?8:0);
            const int dx[]={-1,1,0,0},dy[]={0,0,-1,1};
            for(unsigned d=0;d<4;++d) {
                int px=x+dx[d],py=y+dy[d];
                if(px<0 || py<0 || px>=(int)w || py>=(int)h)continue;
                unsigned next=py*w+px,v=st->map[next];
                if(!seen[next] && v && v<0x14) {seen[next]=1;queue[tail++]=next;}
            }
        }
        crossing|=tail>=w && edges && (edges&(edges-1));
    }
    if((size && !crossing) || counts[0]<=n/20 || counts[1]<=n/20 || counts[2]<=n/100) fprintf(stderr,"failed seed %u size %u: crossing %d counts %u,%u,%u\n",seed,size,crossing,counts[0],counts[1],counts[2]);
    assert((!size || crossing) && counts[0]>n/20 && counts[1]>n/20 && counts[2]>n/100);
    for(unsigned i=0;i<4;++i)assert(quadrants[i]>0);
    /* Use the native fitter's tile vocabulary, including its protected
     * centre markers. Do not demand noise-generator topology from the ROM. */
    memset(seen,0,n);unsigned islands=0;
    for(unsigned i=0;i<n;++i) {
        unsigned v=st->map[i];if((v && v<20) || seen[i])continue;
        unsigned head=0,tail=0;bool edge=false;queue[tail++]=i;seen[i]=1;
        while(head<tail) {
            unsigned at=queue[head++],x=at%w,y=at/w;
            edge|=!x || !y || x==w-1 || y==h-1;
            const int dx[]={-1,1,0,0},dy[]={0,0,-1,1};
            for(unsigned d=0;d<4;++d) {
                int px=x+dx[d],py=y+dy[d];if(px<0 || py<0 || px>=(int)w || py>=(int)h)continue;
                unsigned next=py*w+px,v=st->map[next];
                if(!seen[next] && (!v || v>=20)) {seen[next]=1;queue[tail++]=next;}
            }
        }
        islands+=!edge && tail>=16 && tail<=1200;
    }
    ScMapPreview preview;sc_mapgen_preview_build(&preview,st->map,w,h,seed);
    preview.frame=0;
    for(unsigned y=0;y<100;++y)for(unsigned x=0;x<120;++x)assert(!sc_mapgen_preview_cell(&preview,x,y));
    preview.frame=45;
    for(unsigned y=0;y<100;++y)for(unsigned x=0;x<120;++x)assert(sc_mapgen_preview_cell(&preview,x,y)<0x14);
    preview.frame=90;
    for(unsigned y=0;y<100;++y)for(unsigned x=0;x<120;++x) {
        unsigned v=sc_mapgen_preview_cell(&preview,x,y);assert(v<38);
        if(!size)assert(v==st->map[y*w+x]);
    }
    sc_mapgen_preview_zoom(&preview,8,.25,.75);
    assert(preview.zoom==8 && preview.center_x>=w/16.0 && preview.center_y<=h-h/16.0);
    double cx=preview.center_x,cy=preview.center_y;
    sc_mapgen_preview_pan(&preview,-.1,-.1);
    assert(preview.center_x<cx && preview.center_y<cy);
    cx=preview.center_x;cy=preview.center_y;
    sc_mapgen_preview_pan(&preview,0,0);
    assert(preview.center_x==cx && preview.center_y==cy);
    sc_mapgen_preview_zoom(&preview,.001,.25,.75);
    assert(preview.zoom==1 && preview.center_x==w*.5 && preview.center_y==h*.5);
    sc_mapgen_preview_pan(&preview,10,-10);
    assert(preview.center_x==w*.5 && preview.center_y==h*.5);
    free(queue);free(seen);
    printf("geography seed %u size %u: land %.1f%% water %.1f%% trees %.1f%%\n",seed,size,
        counts[0]*100.0/n,counts[1]*100.0/n,counts[2]*100.0/n);
    return islands;
}
static int compare_keys(const void *a,const void *b) {
    uint32_t x=*(const uint32_t *)a,y=*(const uint32_t *)b;return (x>y)-(x<y);
}
int main(void) {
    uint32_t *keys=malloc(100000*sizeof *keys);assert(keys);
    for(unsigned n=0;n<100000;++n)keys[n]=sc_mapgen_number_key(n);
    qsort(keys,100000,sizeof *keys,compare_keys);
    for(unsigned n=1;n<100000;++n)assert(keys[n]!=keys[n-1]);free(keys);
    assert(sc_mapgen_number_digit(0,4,-1)==90000);
    assert(sc_mapgen_number_digit(99999,4,1)==9999);
    assert(sc_mapgen_number_digit(99999,0,1)==99990);
    assert(sc_mapgen_number_nav(9,2)==11 && sc_mapgen_number_nav(11,1)==9);
    assert(sc_mapgen_number_nav(10,4)==11 && sc_mapgen_number_nav(10,8)==1);
    assert(sc_mapgen_number_nav(0,4)==1 && sc_mapgen_number_nav(1,4)==2);
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
        ScMapGenPrng native=p;
        p=q;sc_mapgen_generate_geographic(&p,&g->state,0);
        assert(hash(g->state.map,12000)==stock[seed]);
        assert(p.s0==native.s0 && p.s1==native.s1 && p.t==native.t);
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
    uint64_t last=0;unsigned island_counts[6]={0};
    for(unsigned size=0;size<6;++size)for(unsigned seed=0;seed<(size<2?16:2);++seed) {
        ScMapGenPrng p,q;sc_mapgen_seed(&p,0x5c00,seed,0,0,2,0);q=p;
        sc_mapgen_generate_geographic(&p,&g->state,size);
        sc_mapgen_generate_geographic(&q,again,size);
        unsigned cells=g->state.width*g->state.height;
        assert(!memcmp(g->state.map,again->map,cells*2));
        uint64_t current=hash(g->state.map,cells);assert(current!=last);last=current;
        assert(g->before==g->after && g->after==UINT64_C(0xfacedeed98765432));
        island_counts[size]+=geographic_check(&g->state,seed,size);
        /* Growing the map adds native-scale reaches, not map-sized channels.
         * Most horizontal/vertical water spans remain under 40 cells wide. */
        if(size) {
            unsigned small=0,total=0,w=g->state.width,h=g->state.height;
            for(unsigned y=24;y<h-24;y+=7) {
                unsigned run=0;
                for(unsigned x=24;x<w-24;++x) {
                    unsigned v=g->state.map[y*w+x];
                    if(v && v<20)++run;
                    else if(run) {++total;small+=run<40;run=0;}
                }
            }
            assert(total && small*4>total*3);
        }
    }
    for(unsigned size=1;size<6;++size)assert(island_counts[size]>0);
    const unsigned numbers[]={0,1,999,1000,10000,99999};last=0;
    for(unsigned i=0;i<6;++i) {
        sc_mapgen_generate_numbered(&g->state,0,numbers[i]);sc_mapgen_generate_numbered(again,0,numbers[i]);
        assert(!memcmp(g->state.map,again->map,12000*2));
        uint64_t current=hash(g->state.map,12000);assert(current!=last);last=current;
    }
    /* A narrow river between the old point samples remains visible in both
     * the native overview and the sharper display-sized preview. */
    memset(again->map,0,240*200*2);
    for(unsigned y=0;y<200;++y)again->map[y*240+1]=1;
    ScMapPreview preview;sc_mapgen_preview_build(&preview,again->map,240,200,123);
    preview.frame=90;
    for(unsigned y=0;y<100;++y)assert(sc_mapgen_preview_cell(&preview,0,y)==1);
    uint8_t cells[240*200],reveal[240*200];
    sc_mapgen_preview_raster(&preview,cells,reveal,240,200);
    assert(cells[1]==1 && !cells[0] && !cells[2]);
    free(g); free(again);
    puts("PASS: 16 unchanged cartridge fingerprints/PRNG endpoints, 40 deterministic native river/forest/coast seeds across all six sizes through 3840x3200, connected water, feature coverage, preview phases, far-bank cells and bounds guards");
}
