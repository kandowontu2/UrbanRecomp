#include "sc_mapgen.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Keep failures in the test log instead of a Windows CRT assertion dialog. */
#undef assert
#define assert(condition) do {if(!(condition)) {fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#condition);fflush(stderr);exit(1);}} while(0)

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
static void island_building_check(const ScMapGenState *st,unsigned style,unsigned seed) {
    unsigned w=st->width,h=st->height,land=0,districts=0;
    unsigned *sums=calloc((size_t)(w+1)*(h+1),sizeof *sums);assert(sums);
    for(unsigned y=0;y<h;++y) {
        unsigned row=0;
        for(unsigned x=0;x<w;++x) {
            unsigned tile=st->map[y*w+x];bool water=tile>0 && tile<20;
            row+=water;land+=!water;
            sums[(y+1)*(w+1)+x+1]=sums[y*(w+1)+x+1]+row;
        }
    }
    /* Twelve-cell squares fit several zones, transport and services. Count
     * complete district footprints, not merely a high total of narrow land. */
    for(unsigned y=12;y<=h;++y)for(unsigned x=12;x<=w;++x) {
        unsigned water=sums[y*(w+1)+x]-sums[(y-12)*(w+1)+x]-
            sums[y*(w+1)+x-12]+sums[(y-12)*(w+1)+x-12];
        districts+=water==0;
    }
    free(sums);
    printf("%s seed %u %ux%u: land %.1f%% district footprints %.1f%%\n",
        sc_mapgen_style_name(style),seed,w,h,land*100.0/(w*h),districts*100.0/(w*h));
    assert(land*100>(uint64_t)w*h*35 && land*100<(uint64_t)w*h*90);
    assert(districts*100>(uint64_t)w*h*(style==SC_TERRAIN_ISLANDS?25:20));
}
static void amazon_forests(void) {
    ScMapGenState *st=calloc(1,sizeof *st);assert(st);
    const unsigned sizes[]={0,2,5};
    for(unsigned k=0;k<3;++k) {
        sc_mapgen_generate_numbered(st,sizes[k],42);
        unsigned cells=(st->width?st->width:120)*(st->height?st->height:100);
        uint16_t *before=malloc(cells*2);assert(before);memcpy(before,st->map,cells*2);
        unsigned old=0,now=0;ScMapGenPrng pr={0x1234,0xabcd,0x9abc};
        sc_mapgen_extra_forests(&pr,st);
        for(unsigned i=0;i<cells;++i) {
            unsigned a=before[i]&1023,b=st->map[i]&1023;
            if(a && a<20)assert(st->map[i]==before[i]);
            old+=a>=20;now+=b>=20;
        }
        assert(now>old);printf("PASS: Amazon size %u forests %u -> %u, water and shores preserved\n",sizes[k],old,now);
        free(before);
    }
    free(st);
}
int main(void) {
    amazon_forests();
    setvbuf(stdout,NULL,_IONBF,0);
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
    assert(sc_mapgen_number_nav(0,8)==15 && sc_mapgen_number_nav(1,8)==0);
    assert(sc_mapgen_number_nav(14,8)==12 && sc_mapgen_number_nav(15,8)==13);
    assert(sc_mapgen_number_nav(14,4)==0 && sc_mapgen_number_nav(15,4)==0);
    assert(sc_mapgen_number_nav(12,1)==13 && sc_mapgen_number_nav(13,2)==12);
    assert(sc_mapgen_number_nav(12,4)==14 && sc_mapgen_number_nav(13,4)==15);
    assert(!strcmp(sc_mapgen_style_name(SC_TERRAIN_NATIVE),"NATIVE"));
    assert(!strcmp(sc_mapgen_style_name(SC_TERRAIN_PROCEDURAL),"PROCEDURAL"));
    assert(!strcmp(sc_mapgen_style_name(SC_TERRAIN_FRACTAL),"FRACTAL"));
    assert(!strcmp(sc_mapgen_style_name(SC_TERRAIN_CONTINENT),"CONTINENT"));
    assert(!strcmp(sc_mapgen_style_name(SC_TERRAIN_DELTA),"DELTA"));
    assert(!strcmp(sc_mapgen_style_name(SC_TERRAIN_ATOLLS),"ATOLLS"));
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
    /* Small Fractal maps must offer useful building space for every seed,
     * rather than allowing a low noise patch to become almost all ocean. */
    for(unsigned size=0;size<2;++size)for(unsigned seed=0;seed<16;++seed) {
        ScMapGenPrng p={0x1234^seed,0xabcd+seed*137,0};
        sc_mapgen_generate_style(&p,&g->state,size,SC_TERRAIN_FRACTAL);
        unsigned w=g->state.width,h=g->state.height,water=0,buildable=0;
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            unsigned tile=g->state.map[y*w+x];water+=tile>0 && tile<20;
            if(x+2>=w || y+2>=h)continue;
            bool land=true;
            for(unsigned dy=0;dy<3;++dy)for(unsigned dx=0;dx<3;++dx) {
                unsigned t=g->state.map[(y+dy)*w+x+dx];if(t>0 && t<20)land=false;
            }
            buildable+=land;
        }
        assert(water*100>w*h*18 && water*100<w*h*52 && buildable*100>w*h*35);
    }
    for(unsigned size=0;size<3;++size)for(unsigned seed=0;seed<16;++seed)
        for(unsigned type=0;type<2;++type) {
            unsigned style=type?SC_TERRAIN_ATOLLS:SC_TERRAIN_ISLANDS;
            ScMapGenPrng p={0x1234^seed,0xabcd+seed*137,0};
            sc_mapgen_generate_style(&p,&g->state,size,style);
            island_building_check(&g->state,style,seed);
        }
    /* Each opt-in style remains deterministic and valid at every selectable
     * size; the default's cartridge fingerprints above are unchanged. */
    for(unsigned size=0;size<6;++size)for(unsigned style=1;style<SC_TERRAIN_STYLES;++style) {
        ScMapGenPrng p={0x1234,0xabcd,0},q=p;
        sc_mapgen_generate_style(&p,&g->state,size,style);
        sc_mapgen_generate_style(&q,again,size,style);
        unsigned cells=g->state.width*g->state.height,land=0,water=0,shore=0;
        assert(g->state.width==(120u<<size) && g->state.height==(100u<<size));
        assert(!memcmp(g->state.map,again->map,cells*2) && !memcmp(&p,&q,sizeof p));
        for(unsigned i=0;i<cells;++i) {
            unsigned tile=g->state.map[i];assert(tile<=0x25);
            land+=!tile || tile>=20;water+=tile>0 && tile<20;shore+=tile>=4 && tile<20;
        }
        assert(land && water && shore && g->before==g->after);
        if(style==SC_TERRAIN_ISLANDS || style==SC_TERRAIN_ATOLLS)
            island_building_check(&g->state,style,0);
        if(style>=SC_TERRAIN_FRACTAL || style==SC_TERRAIN_ISLANDS) {
            unsigned w=g->state.width,h=g->state.height;
            for(unsigned y=1;y+1<h;++y)for(unsigned x=1;x+1<w;++x) {
                unsigned at=y*w+x;if(g->state.map[at]!=1)continue;
                const unsigned neighbors[]={at-1,at+1,at-w,at+w};
                for(unsigned n=0;n<4;++n) {
                    unsigned tile=g->state.map[neighbors[n]];assert(tile>0 && tile<20);
                }
            }
        }
        sc_mapgen_apply_number(&g->state,31337);
        for(unsigned i=0;i<cells;++i)assert(!g->state.map[i] || g->state.map[i]>=20);
        printf("PASS: style %u size %u, %u land, %u water, %u shore cells\n",style,size,land,water,shore);
    }
    /* A narrow river between the old point samples remains visible in both
     * the native overview and the sharper display-sized preview. */
    for(unsigned size=0;size<6;++size) {
        sc_mapgen_generate_numbered(&g->state,size,31337);
        unsigned trees=0;
        for(unsigned i=0;i<(unsigned)g->state.width*g->state.height;++i) {
            unsigned tile=g->state.map[i]&0x3ff;
            assert(!tile || tile>=20);trees+=tile>=20;
        }
        assert(trees);
    }
    /* Applying the special number preserves tree IDs and does not alter
     * neighbouring numbered maps or consume extra randomness. */
    again->width=20;again->height=1;
    for(unsigned i=0;i<20;++i)again->map[i]=i+1;
    sc_mapgen_apply_number(again,31336);
    for(unsigned i=0;i<20;++i)assert(again->map[i]==i+1);
    sc_mapgen_apply_number(again,31337);
    for(unsigned i=0;i<19;++i)assert(!again->map[i]);
    assert(again->map[19]==20);
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
