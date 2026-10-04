#include "sc_gpu_fields.h"
#include "sc_gpu_terrain.h"
#include "sc_sdl_compat.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const uint32_t *await_result(ScGpuFields *g,const ScWorld *w,unsigned field) {
    Uint64 start=SDL_GetTicks();const uint32_t *data=NULL;
    do {if(field>=13) ScGpuFieldsPoll(g);data=ScGpuFieldsData(g,w,field);if(!data) SDL_Delay(1);} while(!data && SDL_GetTicks()-start<5000);
    assert(data);return data;
}
static const ScCrimeSample *await_crime(ScGpuFields *g,const ScWorld *w,unsigned bias) {
    Uint64 start=SDL_GetTicks();const ScCrimeSample *data=NULL;
    do {ScGpuFieldsPoll(g);data=ScGpuFieldsCrimeData(g,w,bias);if(!data) SDL_Delay(1);} while(!data && SDL_GetTicks()-start<5000);
    assert(data);return data;
}
static const ScLandSummary *await_land(ScGpuFields *g,const ScWorld *w) {
    Uint64 start=SDL_GetTicks();const ScLandSummary *data=NULL;
    do {ScGpuFieldsPoll(g);data=ScGpuFieldsLandData(g,w);if(!data) SDL_Delay(1);} while(!data && SDL_GetTicks()-start<5000);
    assert(data);return data;
}
int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window *window=SDL_CreateWindow("Vulkan field test",128,128,SDL_WINDOW_HIDDEN);assert(window);
    SDL_Renderer *renderer=ScGpuTerrainRenderer(window);assert(renderer);
    ScGpuFields *g=ScGpuFieldsCreate(renderer);assert(g);
    ScWorld *w=malloc(sizeof *w);assert(w);unsigned cases=0;
    const unsigned sources[]={13,14,10,11};
    for(unsigned map=0;map<4;++map) for(unsigned which=0;which<4;++which)
    for(unsigned pattern=0;pattern<3;++pattern) {
        unsigned field=sources[which];ScWorldReset(w);w->active=true;w->huge=map>0;w->giant=map>1;w->colossal=map==3;
        unsigned width=ScWorldFieldWidth(w,field),height=ScWorldFieldHeight(w,field);
        for(unsigned n=0;n<ScWorldFieldSizeWorld(w,field);++n) w->fields[field][n]=pattern==0?0:pattern==1?255:(n*173+71)&255;
        ScGpuFieldsBegin(g,w,field);const uint32_t *data=await_result(g,w,field);
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x)
            assert(data[y*width+x]==(field<13?ScServicePack(w->fields[field],width,height,x,y):ScStencilPack(w->fields[field],width,height,x,y)));
        ++cases;
        /* A size transition must not expose a completed smaller field. */
        bool saved_huge=w->huge,saved_giant=w->giant,saved_colossal=w->colossal;
        if(map==3) w->colossal=false;else if(map==2) w->colossal=true;else if(map==1) w->giant=true;else w->huge=true;
        assert(!ScGpuFieldsData(g,w,field));
        w->huge=saved_huge;w->giant=saved_giant;w->colossal=saved_colossal;
        /* Same address/shape can hold a different loaded city. Old async
         * results may never survive the reset/load epoch. */
        uint64_t epoch=ScWorldStateEpoch();ScWorldReset(w);assert(ScWorldStateEpoch()!=epoch);
        w->active=true;w->huge=map>0;w->giant=map>1;w->colossal=map==3;
        assert(!ScGpuFieldsData(g,w,field));
    }
    unsigned crime_cases=0;uint64_t crime_cells=0;
    const unsigned biases[]={0,10,65535,65286,300,32767,32768,65000};
    for(unsigned map=0;map<4;++map) for(unsigned b=0;b<sizeof biases/sizeof *biases;++b) {
        ScWorldReset(w);w->active=true;w->huge=map>0;w->giant=map>1;w->colossal=map==3;
        unsigned width=ScWorldWidth(w)/2,height=ScWorldHeight(w)/2;
        for(unsigned n=0;n<width*height;++n) {w->fields[0][n]=(uint8_t)(n>>8);w->fields[3][n]=(uint8_t)n;}
        for(unsigned n=0;n<width*height/16;++n) {
            unsigned coverage=(n*271+71)&65535;w->fields[11][2*n]=(uint8_t)coverage;w->fields[11][2*n+1]=(uint8_t)(coverage>>8);
        }
        ScGpuFieldsCrimeBegin(g,w,biases[b]);const ScCrimeSample *data=await_crime(g,w,biases[b]);
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
            unsigned at=y*width+x,coarse=(y/4)*(width/4)+x/4;
            unsigned coverage=w->fields[11][2*coarse]|((unsigned)w->fields[11][2*coarse+1]<<8);
            ScCrimeSample expected=ScCrimePack(w->fields[0][at],w->fields[3][at],coverage,biases[b]);
            assert(!memcmp(data+at,&expected,sizeof expected));++crime_cells;
        }
        ++crime_cases;
        assert(!ScGpuFieldsCrimeData(g,w,biases[b]^1));
        if(map==3) w->colossal=false;else if(map==2) w->colossal=true;else if(map==1) w->giant=true;else w->huge=true;
        assert(!ScGpuFieldsCrimeData(g,w,biases[b]));
        w->huge=map>0;w->giant=map>1;w->colossal=map==3;
        ScWorldReset(w);w->active=true;w->huge=map>0;w->giant=map>1;w->colossal=map==3;
        assert(!ScGpuFieldsCrimeData(g,w,biases[b]));
    }
    unsigned land_cases=0;uint64_t land_cells=0;
    for(unsigned map=0;map<4;++map) for(unsigned pattern=0;pattern<4;++pattern) {
        ScWorldReset(w);w->active=true;w->huge=map>0;w->giant=map>1;w->colossal=map==3;
        unsigned width=ScWorldWidth(w),half=width/2,height=ScWorldHeight(w)/2;
        for(unsigned n=0;n<ScWorldCells(w);++n) {
            unsigned tile=pattern==0?0:pattern==1?0x15:pattern==2?(n*31+57)%1024:
                n%4==0?0x2bf:n%4==1?0x15:n%4==2?0x364:0x7f;
            if(pattern>1 && (n&3)==1) tile|=0xc000;
            w->tiles[2*n]=(uint8_t)tile;w->tiles[2*n+1]=(uint8_t)(tile>>8);
        }
        ScGpuFieldsLandBegin(g,w);const ScLandSummary *data=await_land(g,w);
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<half;++x) {
            unsigned first=2*y*width+2*x,positions[]={first,first+1,first+width,first+width+1};uint16_t tiles[4];
            for(unsigned n=0;n<4;++n) tiles[n]=(uint16_t)(w->tiles[2*positions[n]]|((unsigned)w->tiles[2*positions[n]+1]<<8));
            ScLandSummary expected=ScLandSummaryPack(tiles);assert(!memcmp(data+y*half+x,&expected,sizeof expected));++land_cells;
        }
        ++land_cases;
        if(map==3) w->colossal=false;else if(map==2) w->colossal=true;else if(map==1) w->giant=true;else w->huge=true;
        assert(!ScGpuFieldsLandData(g,w));w->huge=map>0;w->giant=map>1;w->colossal=map==3;
        ScWorldReset(w);w->active=true;w->huge=map>0;w->giant=map>1;w->colossal=map==3;
        assert(!ScGpuFieldsLandData(g,w));
    }
    /* Reset while a job is still in flight, then poll it to completion. */
    ScGpuFieldsLandBegin(g,w);ScWorldReset(w);w->active=w->huge=w->giant=w->colossal=true;
    for(unsigned n=0;n<100;++n) {ScGpuFieldsPoll(g);SDL_Delay(1);assert(!ScGpuFieldsLandData(g,w));}
    ScGpuFieldsCrimeBegin(g,w,0);ScWorldReset(w);w->active=w->huge=w->giant=w->colossal=true;
    for(unsigned n=0;n<100;++n) {ScGpuFieldsPoll(g);SDL_Delay(1);assert(!ScGpuFieldsCrimeData(g,w,0));}
    ScGpuFieldsBegin(g,w,13);ScWorldReset(w);w->active=w->huge=w->giant=w->colossal=true;
    for(unsigned n=0;n<100;++n) {ScGpuFieldsPoll(g);SDL_Delay(1);assert(!ScGpuFieldsData(g,w,13));}
    ScGpuFieldsDestroy(g);free(w);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    printf("PASS: %u real Vulkan whole fields match integer reference on every cell, all expanded maps/byte and word phases; reset invalidates ready and in-flight results\n",cases);
    printf("PASS: %u real Vulkan crime fields, %llu cells, eight signed biases; values, scratch, aligned/unaligned clocks, source identity, shape/bias/reset invalidation\n",
        crime_cases,(unsigned long long)crime_cells);
    printf("PASS: %u real Vulkan land fields, %llu summaries; all tile categories, raw flags, density/pollution/occupancy/clocks/tail, shape/reset invalidation\n",
        land_cases,(unsigned long long)land_cells);
    return 0;
}
