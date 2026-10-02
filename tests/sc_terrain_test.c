#include "sc_renderer.h"
#include "snes/ppu.h"
#ifdef SC_TEST_GPU
#include "sc_gpu_terrain.h"
#include "sc_sdl_compat.h"
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void word(uint8_t *data,unsigned at,unsigned v) {data[at]=v;data[at+1]=v>>8;}
#if defined(SC_TEST_GPU) && SNESRECOMP_SDL3
static void presentation_test(SDL_Renderer *renderer,SDL_Texture *computed,const ScRenderer *cpu,bool linear) {
    SDL_Texture *reference=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,cpu->view.width,cpu->view.height);assert(reference);
    assert(SDL_SetTextureBlendMode(reference,SDL_BLENDMODE_NONE));
    assert(SDL_SetTextureScaleMode(reference,linear?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST));
    assert(SDL_SetTextureScaleMode(computed,linear?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST));
    assert(SDL_UpdateTexture(reference,NULL,cpu->pixels,cpu->view.width*4));
    SDL_Surface *shots[2];SDL_Texture *textures[]={reference,computed};
    for(unsigned i=0;i<2;++i) {
        assert(SDL_RenderClear(renderer));assert(SDL_RenderTexture(renderer,textures[i],NULL,NULL));
        SDL_Surface *raw=SDL_RenderReadPixels(renderer,NULL);assert(raw);
        shots[i]=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_ARGB8888);assert(shots[i]);
        SDL_DestroySurface(raw);
    }
    assert(shots[0]->w==shots[1]->w && shots[0]->h==shots[1]->h);
    for(int y=0;y<shots[0]->h;++y)
        assert(!memcmp((const uint8_t *)shots[0]->pixels+y*shots[0]->pitch,
            (const uint8_t *)shots[1]->pixels+y*shots[1]->pitch,shots[0]->w*4));
    SDL_DestroySurface(shots[0]);SDL_DestroySurface(shots[1]);SDL_DestroyTexture(reference);
}
#endif
int main(void) {
    ScRenderer *cpu=calloc(1,sizeof *cpu),*deferred=calloc(1,sizeof *deferred),*optimized=calloc(1,sizeof *optimized);
    Ppu *p=calloc(1,sizeof *p);ScWorld *world=calloc(1,sizeof *world);
    uint8_t *rom=calloc(1,0x80000),*ram=calloc(1,0x20000);
    assert(cpu && deferred && optimized && p && world && rom && ram);
    for(unsigned i=0;i<958;++i) {
        word(rom,0x156a9+2*i,(i%127)|((i%8)<<10)|((i%4)<<14));
        word(rom,0x14f2d+2*i,i%5?((i+17)%127)|(((i+3)%8)<<10)|(((i+1)%4)<<14):0x300);
    }
    for(unsigned i=0;i<0x8000;++i) p->vram[i]=(uint16_t)(i*197+57);
    for(unsigned i=0;i<256;++i) p->cgram[i]=(uint16_t)(i*313+41)&32767;
    for(unsigned i=0;i<32;++i) p->brightnessMult[i]=(i<<3)|(i>>2);
    p->inidisp=15;p->bgmode=1;p->bgTileAdr=0x321;
    p->bgXsc[0]=0x40;p->bgXsc[1]=0x48;p->bgXsc[2]=0x50;
    ram[0x3e]=1;word(ram,0x1bd,300);word(ram,0x1bf,330);
    world->active=world->huge=true;
    for(unsigned i=0;i<SC_WORLD_MAX_CELLS;++i) word(world->tiles,2*i,(i*19+71)%958);
    ScRendererInit(cpu,rom,0x80000,true);ScRendererInit(deferred,rom,0x80000,true);
    ScRendererInit(optimized,rom,0x80000,true);
    cpu->world=deferred->world=optimized->world=world;
#ifdef SC_TEST_GPU
    assert(snesrecomp_sdl_init(SDL_INIT_VIDEO));
    SDL_Window *window=snesrecomp_sdl_create_window("Terrain regression",64,64,SDL_WINDOW_HIDDEN);
    assert(window);SDL_Renderer *renderer=snesrecomp_sdl_create_renderer(window,false,false);assert(renderer);
#ifdef _WIN32
    _putenv("SC_GPU_VALIDATE=1");
#else
    setenv("SC_GPU_VALIDATE","1",1);
#endif
    ScGpuTerrain *gpu=ScGpuTerrainCreate(renderer,false);
    if(!gpu) {fprintf(stderr,"SKIP: Direct3D 11 compute unavailable\n");return 77;}
#endif
    const ScViewport views[]={{448,224,0,0,1,0},{684,448,214,112,1,0},{256,448,0,112,1,0}};
    uint32_t native[256];unsigned captures=0;
    for(unsigned map=0;map<2;++map) {
      world->giant=map!=0;
      word(ram,0x1bd,map?820:300);word(ram,0x1bf,map?730:330);
      ScRendererResetHistory(cpu);ScRendererResetHistory(deferred);
      ScRendererResetHistory(optimized);
      for(unsigned v=0;v<3;++v) for(unsigned test=0;test<8;++test) {
        cpu->reference_terrain=true;
        assert(ScRendererResize(cpu,views[v]) && ScRendererResize(deferred,views[v]));
        assert(ScRendererResize(optimized,views[v]));
        assert(ScRendererDeferTerrain(deferred,true));
        p->screenEnabled[0]=2;p->screenEnabled[1]=test&1?4:2;
        p->screenWindowed[0]=test&2?2:0;p->screenWindowed[1]=test&4?2:0;
        p->windowsel=(test&1?10:2)<<4|(test&2?15:10)<<20;
        p->wbgobjlog=(test%4)<<2|((test+1)%4)<<10;
        p->window1left=0;p->window1right=250;p->window2left=3;p->window2right=255;
        p->cgadsub=(test&1?128:0)|(test&2?64:0)|34;
        p->cgwsel=(test%4)<<6|((test+1)%4)<<4|(test&4?2:0);p->fixedColor=0x3d7;
        p->hScroll[1]=(test*3)&255;p->vScroll[1]=(test*5)&255;
        for(int line=0;line<224;++line) {
            for(unsigned x=0;x<256;++x) native[x]=0xff000000|((x*37+line*197)&0xffffff);
            /* Changed planes, palette and brightness affect this very row. */
            p->vram[0x1000+((unsigned)line%2048)]^=0x1937;
            p->cgram[(line+test)%256]^=0x357;
            for(unsigned i=0;i<32;++i) p->brightnessMult[i]=((i<<3)|(i>>2))*(line%3+1)/3;
            p->inidisp=(test==7 && line>=100 && line<120)?0x8f:15;
            ScRendererLine(cpu,p,ram,line,native);ScRendererLine(deferred,p,ram,line,native);
            ScRendererLine(optimized,p,ram,line,native);
        }
        for(int y=0;y<views[v].height;++y) for(int x=0;x<views[v].width;++x)
            assert(ScRendererPixel(deferred,x,y)==cpu->pixels[(size_t)y*views[v].width+x]);
        assert(deferred->terrain.deferred>0);++captures;
        assert(!memcmp(cpu->pixels,optimized->pixels,(size_t)views[v].width*views[v].height*4));
#ifdef SC_TEST_GPU
        SDL_Texture *computed=ScGpuTerrainDraw(gpu,deferred);assert(computed);
#if SNESRECOMP_SDL3
        presentation_test(renderer,computed,cpu,false);
        presentation_test(renderer,computed,cpu,true);
#endif
#endif
        assert(ScRendererDeferTerrain(deferred,false));
        assert(!memcmp(cpu->pixels,deferred->pixels,(size_t)views[v].width*views[v].height*4));
      }
    }
#ifdef SC_TEST_GPU
    ScGpuTerrainDestroy(gpu);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
#endif
    ScRendererDestroy(cpu);ScRendererDestroy(deferred);
    ScRendererDestroy(optimized);
    free(cpu);free(deferred);free(optimized);free(p);free(world);free(rom);free(ram);
    printf("PASS: %u CPU/deferred frames, Huge/Giant coordinates, wide/tall views, live planes/palettes, flips, windows, colour math, fades and fallback\n",captures);
}
