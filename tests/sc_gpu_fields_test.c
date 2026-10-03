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
    /* Reset while a job is still in flight, then poll it to completion. */
    ScGpuFieldsBegin(g,w,13);ScWorldReset(w);w->active=w->huge=w->giant=w->colossal=true;
    for(unsigned n=0;n<100;++n) {ScGpuFieldsPoll(g);SDL_Delay(1);assert(!ScGpuFieldsData(g,w,13));}
    ScGpuFieldsDestroy(g);free(w);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    printf("PASS: %u real Vulkan whole fields match integer reference on every cell, all expanded maps/byte and word phases; reset invalidates ready and in-flight results\n",cases);
    return 0;
}
