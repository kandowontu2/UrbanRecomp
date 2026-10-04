#include "sc_renderer.h"
#include "sc_native_ppu.h"
#include "sc_obj.h"
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
#ifdef SC_TEST_GPU
static ScGpuTerrain *sharp_gpu;
#endif
static void sharp_zoom_test(void) {
    ScRenderer *r=calloc(1,sizeof *r);assert(r);r->view=(ScViewport){16,16,0,0,1,0};r->zoom_frame=true;
    assert(ScTerrainResize(&r->terrain,16,16));assert(ScTerrainSpanWidth(&r->terrain,128));
    ScTerrainFrame *f=&r->terrain;
    f->deferred=256;
    r->pixels=calloc(256,4);assert(r->pixels);
    f->resource_capacity=2048+16384;f->snapshots=1;
    f->resources=calloc(f->resource_capacity,4);f->city_capacity=f->city_words=512;
    f->city=calloc(512,4);assert(f->resources && f->city);
    f->resources[0]=0x23000000; /* alternating original CHR; transparent roof */
    for(unsigned y=0;y<8;++y) f->resources[2048+y/2]|=(y&1?0x55u:0xaau)<<((y&1)*16);
    const unsigned steps[]={262144,327680,131072,73728,58254,32768,16384};
    const int origins[]={-40000,0,40000};
    for(unsigned origin=0;origin<3;++origin)
    for(unsigned test=0;test<sizeof steps/sizeof *steps;++test) {
        unsigned step=steps[test];
        for(unsigned y=0;y<16;++y) {
            ScTerrainRow *row=f->rows+y;memset(row,0,sizeof *row);
            row->math=SC_ROW_CITY_ZOOM|SC_ROW_CITY_SPANS|SC_ROW_RAW_TERRAIN;
            row->main=2;row->zoom_step=step;row->chr_snapshot=2048;
            unsigned vy=y*step>>16;row->world_y=(vy&7)|((uint32_t)(origins[origin]+(int)vy)<<8);
            for(unsigned i=0;i<32;++i) row->brightness[i]=(i<<3)|(i>>2);
            f->palette[y*256+1]=32767;
            for(unsigned x=0;x<16;++x) r->pixels[y*16+x]=SC_TERRAIN_PIXEL;
        }
        /* Opaque UI must retain exactly the same nearest-scaled footprint. */
        r->pixels[0]=0xff123456;
        unsigned detail=0;
        for(unsigned y=0;y<60;++y) for(unsigned x=0;x<60;++x) {
            unsigned vx=(uint64_t)((2*x+1)*16-1)*step/(128*65536),vy=(uint64_t)((2*y+1)*16-1)*step/(128*65536);
            uint32_t expected=x<4 && y<4?0xff123456:((vx+vy)&1?0xff000000:0xffffffff);
            uint32_t actual=ScRendererPresentationPixel(r,x,y,64,64);
            if(actual!=expected) fprintf(stderr,"Sharp zoom step=%u at %u,%u virtual=%u,%u: %08x expected %08x\n",step,x,y,vx,vy,actual,expected);
            assert(actual==expected);
            if(actual!=ScRendererPixel(r,x/4,y/4)) ++detail;
        }
        if(step>65536 || (step&(step-1))) assert(detail>0);
#ifdef SC_TEST_GPU
        ScGpuTerrainDisplaySize(sharp_gpu,64,64);
        assert(ScGpuTerrainDraw(sharp_gpu,r));
#endif
    }
#ifdef SC_TEST_GPU
    ScGpuTerrainDisplaySize(sharp_gpu,0,0);
#endif
    free(r->pixels);ScTerrainDestroy(f);free(r);
    puts("PASS: display-resolution zoom preserves original alternating CHR, fractional scales, tile boundaries and fixed UI");
}
static void large_object_coordinates_test(void) {
    ScRenderer *r=calloc(1,sizeof *r);assert(r);r->view=(ScViewport){16,16,0,0,1,0};r->zoom_frame=true;
    assert(ScTerrainResize(&r->terrain,16,16));ScTerrainFrame *f=&r->terrain;
    f->deferred=256;r->pixels=calloc(256,4);assert(r->pixels);
    f->resource_capacity=2048+16384;f->snapshots=1;f->resources=calloc(f->resource_capacity,4);
    f->city_capacity=f->city_words=64;f->city=calloc(64,4);assert(f->resources && f->city);
    for(unsigned y=0;y<8;++y)f->resources[2048+y/2]|=0xffu<<((y&1)*16);
    f->city[0]=1;for(unsigned bucket=0;bucket<8;++bucket){f->city[1+bucket*2]=17;f->city[2+bucket*2]=1;}
    f->city[17]=0;f->city[18]=8;f->city[20]=0x3000;f->city[21]=0;f->city[22]=0;
    const int origins[]={-40000,40000};
    for(unsigned test=0;test<2;++test) {
        f->city[19]=(uint32_t)origins[test];
        for(unsigned y=0;y<16;++y) {
            ScTerrainRow *row=f->rows+y;row->math=SC_ROW_CITY_ZOOM|SC_ROW_GPU_OBJECTS|SC_ROW_OBJECT_GRID;
            row->world_y=(y&7)|((uint32_t)(origins[test]+(int)y)<<8);row->main=16;
            row->zoom_step=65536;row->chr_snapshot=2048;row->reserved=0;
            for(unsigned i=0;i<32;++i)row->brightness[i]=(i<<3)|(i>>2);
            f->palette[y*256+129]=31<<5;
            for(unsigned x=0;x<16;++x){r->pixels[y*16+x]=SC_TERRAIN_PIXEL;
                assert(ScTerrainPixel(f,x,y)==(x<8 && y<8?0xff00ff00:0xff000000));}
        }
#ifdef SC_TEST_GPU
        assert(ScGpuTerrainDraw(sharp_gpu,r));
#endif
    }
    free(r->pixels);ScTerrainDestroy(f);free(r);
    puts("PASS: signed 24-bit zoom object coordinates beyond both 16-bit boundaries");
}
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
    for(unsigned i=0;i<12000;++i) word(ram,0x10200+2*i,(i*19+71)%958);
    ScRendererInit(cpu,rom,0x80000,true);ScRendererInit(deferred,rom,0x80000,true);
    ScRendererInit(optimized,rom,0x80000,true);
    cpu->world=deferred->world=optimized->world=world;
#ifdef SC_TEST_GPU
    assert(snesrecomp_sdl_init(SDL_INIT_VIDEO));
    SDL_Window *window=snesrecomp_sdl_create_window("Terrain regression",64,64,SDL_WINDOW_HIDDEN);
    assert(window);SDL_Renderer *renderer=ScGpuTerrainRenderer(window);
    if(!renderer) {fprintf(stderr,"SKIP: Vulkan presentation unavailable\n");return 77;}
#ifdef _WIN32
    _putenv("SC_GPU_VALIDATE=1");
#else
    setenv("SC_GPU_VALIDATE","1",1);
#endif
    ScGpuTerrain *gpu=ScGpuTerrainCreate(renderer,false);
    if(!gpu) {fprintf(stderr,"SKIP: Vulkan compute unavailable\n");SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();return 77;}
    sharp_gpu=gpu;
#endif
    sharp_zoom_test();
    large_object_coordinates_test();
    if(getenv("SC_TEST_SHARP_ONLY")) {
#ifdef SC_TEST_GPU
        ScGpuTerrainDestroy(gpu);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
#endif
        ScRendererDestroy(cpu);ScRendererDestroy(deferred);ScRendererDestroy(optimized);
        free(cpu);free(deferred);free(optimized);free(p);free(world);free(rom);free(ram);
        return 0;
    }
    const ScViewport views[]={{448,224,0,0,1,0},{684,448,214,112,1,0},{256,448,0,112,1,0},{684,448,0,0,1,0}};
    uint32_t native[256];unsigned captures=0,city_captures=0,compact_captures=0,sub_bg_rows=0;
    for(unsigned scene=0;scene<3;++scene) for(unsigned map=0;map<5;++map) {
      for(unsigned i=0;i<958;++i) rom[0x184eb+i]=scene && i%7==0?1:0;
      world->active=map!=3;world->huge=map<3;world->giant=map==1 || map==2;world->colossal=map==2;
      word(ram,0x1bd,map==2?1780:map==1?820:map==0?300:map==3?40:100);
      word(ram,0x1bf,map==2?1530:map==1?730:map==0?330:map==3?60:130);
      ScRendererResetHistory(cpu);ScRendererResetHistory(deferred);
      ScRendererResetHistory(optimized);
      for(unsigned v=0;v<4;++v) for(unsigned test=0;test<8;++test) {
        unsigned map_width=world->active?ScWorldWidth(world):120;
        unsigned map_height=world->active?ScWorldHeight(world):100;
        word(ram,0x1bd,test==6?0:test==7?map_width-1:map==2?1780:map==1?820:map==0?300:map==3?40:100);
        word(ram,0x1bf,test==6?0:test==7?map_height-1:map==2?1530:map==1?730:map==0?330:map==3?60:130);
        cpu->reference_terrain=true;
        assert(ScRendererResize(cpu,views[v]) && ScRendererResize(deferred,views[v]));
        assert(ScRendererResize(optimized,views[v]));
        assert(ScRendererDeferTerrain(deferred,true));
        if(scene) assert(ScRendererDeferTerrain(optimized,true));
        word(ram,0x1d7,scene==1 && (test&1));
        p->screenEnabled[0]=scene?19:2;p->screenEnabled[1]=test&1?(scene?5:4):(scene?18:2);
        if(scene==2) {p->screenEnabled[0]=20;p->screenEnabled[1]=3;}
        p->screenWindowed[0]=test&2?2:0;p->screenWindowed[1]=test&4?2:0;
        p->windowsel=(test&1?10:2)<<4|(test&2?15:10)<<20;
        p->wbgobjlog=(test%4)<<2|((test+1)%4)<<10;
        p->window1left=0;p->window1right=250;p->window2left=3;p->window2right=255;
        p->cgadsub=(test&1?128:0)|(test&2?64:0)|34;
        p->cgwsel=(test%4)<<6|((test+1)%4)<<4|(test&4?2:0);p->fixedColor=0x3d7;
        p->hScroll[1]=(test*3)&255;p->vScroll[1]=(test*5)&255;
        /* BG3's extended subscreen is decoded on GPU from its immutable VRAM
         * version. Exercise every map layout and 256/512/1024 coordinate
         * seams, with the reference CPU decoder supplying the oracle. */
        p->bgXsc[2]=0x48|(test&3);
        p->hScroll[2]=(test*255+3)&1023;p->vScroll[2]=(test*253+7)&1023;
        if(scene==1) {
            /* Actual edits after history exists must invalidate each renderer
             * independently. Include power-only changes and a live native
             * warning plane over powered owner footprints. */
            unsigned cx=ram[0x1bd]|((unsigned)ram[0x1be]<<8),cy=ram[0x1bf]|((unsigned)ram[0x1c0]<<8);
            ScWorldPutCell(world,cx+10,cy+10,(test&1)?0x813b:0x13b);
            ScWorldPutCell(world,cx+18,cy+14,137+test);
            if(test==4) for(unsigned i=0;i<1024;++i) p->vram[0x4000+i]=0x1376;
        }
        for(int line=0;line<224;++line) {
            if(scene==1 && line>0 && line%8==3) {
                /* Change city bytes between scanlines of the SAME tile row.
                 * Earlier snapshots must retain their old cells. Exercise
                 * invalid IDs and power bits, including neighbour ownership. */
                unsigned width=world->active?ScWorldWidth(world):120;
                unsigned height=world->active?ScWorldHeight(world):100;
                unsigned cx=ram[0x1bd]|((unsigned)ram[0x1be]<<8);
                unsigned cy=ram[0x1bf]|((unsigned)ram[0x1c0]<<8);
                unsigned row=cy+(unsigned)line/8;
                uint8_t *map=world->active?world->tiles:ram+0x10200;
                if(row+1<height) for(unsigned j=0;j<16 && cx+j<width;++j) {
                    unsigned value=j%4==0?1023:j%4==1?958:j%4==2?0x813b:137+line%127;
                    if(world->active) {
                        ScWorldPutCell(world,cx+j,row,value);
                        ScWorldPutCell(world,cx+j,row+1,value^0x8000);
                    } else {
                        word(map,2*(row*width+cx+j),value);
                        word(map,2*((row+1)*width+cx+j),value^0x8000);
                    }
                }
            }
            for(unsigned x=0;x<256;++x) native[x]=0xff000000|((x*37+line*197)&0xffffff);
            for(unsigned x=0;x<256;++x) p->objBuffer.data[x+kPpuExtraLeftRight]=
                scene && x%13<4?(193+x%31)|((2+4*(x%4))<<12):0;
            /* Changed planes, palette and brightness affect this very row. */
            p->vram[0x1000+((unsigned)line%2048)]^=0x1937;
            p->cgram[(line+test)%256]^=0x357;
            for(unsigned i=0;i<32;++i) p->brightnessMult[i]=((i<<3)|(i>>2))*(line%3+1)/3;
            p->inidisp=(test==7 && line>=100 && line<120)?0x8f:15;
            if(scene) {
                unsigned ay=line+views[v].core_y;
                optimized->terrain.rows[ay].core_x=views[v].core_x;
                ScNativePpuCapture(&optimized->terrain,p,ay,line+1);
                for(unsigned x=0;x<256;++x)
                    native[x]=ScNativePixel(&optimized->terrain,views[v].core_x+x,ay);
                ScRendererDeferNativeLine(optimized,p,ram,line);
            }
            ScRendererLine(cpu,p,ram,line,native);ScRendererLine(deferred,p,ram,line,native);
            ScRendererLine(optimized,p,ram,line,native);
        }
        for(int y=0;y<views[v].height;++y) for(int x=0;x<views[v].width;++x)
            if(ScRendererPixel(deferred,x,y)!=cpu->pixels[(size_t)y*views[v].width+x]) {
                ScTerrainRow *row=deferred->terrain.rows+y;
                ScTerrainTile t=ScTerrainRawTile(&deferred->terrain,y,(x+row->phase)/8);
                ScTerrainTile resolved=ScTerrainResolveTile(&deferred->terrain,y,t);
                fprintf(stderr,"capture mismatch scene=%u map=%u view=%u test=%u x=%d y=%d got=%08x expected=%08x math=%x raw=%x,%x,%x resolved=%x,%x,%x,%x snapshot=%u count=%u hold=%d\n",scene,map,v,test,x,y,ScRendererPixel(deferred,x,y),cpu->pixels[(size_t)y*views[v].width+x],row->math,t.base,t.roof,t.reserved,resolved.base,resolved.roof,resolved.attributes,resolved.reserved,row->chr_snapshot,deferred->terrain.snapshots,deferred->map_hold);
                abort();
            }
        assert(deferred->terrain.deferred>0);++captures;
        for(unsigned y=0;y<deferred->terrain.height;++y)
            sub_bg_rows+=(deferred->terrain.rows[y].math&SC_ROW_GPU_SUB_BG)!=0;
        if(deferred->terrain.city_words) {
            ++city_captures;
            assert(deferred->terrain.city_words*2<deferred->terrain.stride*deferred->terrain.height*3);
            bool compact=deferred->terrain.stride>=34;
            for(unsigned y=0;y<deferred->terrain.height && compact;++y)
                compact=(deferred->terrain.rows[y].math&SC_ROW_CITY_SPANS)!=0;
            if(compact) ++compact_captures;
        }
        if(scene) {
            for(int y=0;y<views[v].height;++y) for(int x=0;x<views[v].width;++x)
                if(ScRendererPixel(optimized,x,y)!=cpu->pixels[(size_t)y*views[v].width+x]) {
                    ScTerrainRow *row=optimized->terrain.rows+y;
                    ScTerrainTile *tile=optimized->terrain.tiles+(size_t)y*optimized->terrain.stride+(x+row->phase)/8;
                    fprintf(stderr,"repair mismatch scene=%u map=%u view=%u test=%u x=%d y=%d actual=%08x expected=%08x math=%x tile=%x expectedword=%x staged=%x pixel=%x scroll=%d,%d repair=%d native=%d\n",scene,map,v,test,x,y,ScRendererPixel(optimized,x,y),cpu->pixels[(size_t)y*views[v].width+x],row->math,tile->attributes,tile->expected,tile->staged,optimized->pixels[(size_t)y*views[v].width+x],optimized->scroll_x,optimized->scroll_y,optimized->scroll_repair,optimized->native_line);
                    abort();
                }
#ifdef SC_TEST_GPU
            SDL_Texture *panel=ScGpuTerrainDraw(gpu,optimized);assert(panel);
#if SNESRECOMP_SDL3
            presentation_test(renderer,panel,cpu,false);presentation_test(renderer,panel,cpu,true);
#endif
#endif
            assert(ScRendererDeferTerrain(optimized,false));
        }
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
    unsigned object_frames=0,object_rows=0;
    for(unsigned map=0;map<5;++map) for(unsigned size=0;size<8;++size) for(unsigned v=0;v<3;++v) {
        ScViewport view=views[v==2?3:v];
        world->active=map!=0;world->huge=map>=2;world->giant=map>=3;world->colossal=map==4;
        word(ram,0x1bd,40);word(ram,0x1bf,50);word(ram,0x1d7,size&1);
        word(ram,0xd7,0);word(ram,0x379,0);ram[0x391]=ram[0xe3]=0;
        p->bgmode=1;p->inidisp=15;p->screenEnabled[0]=19;p->screenEnabled[1]=18;
        p->screenWindowed[0]=p->screenWindowed[1]=18;
        p->windowsel=10<<4|10<<16;p->wbgobjlog=(size&3)<<8;
        p->window1left=4;p->window1right=250;p->window2left=91;p->window2right=214;
        p->cgadsub=34;p->cgwsel=2;p->hScroll[1]=size*3;p->vScroll[1]=size*5;
        p->obsel=(size<<5)|3|8;p->oamaddl=(size*18)|1;
        memset(p->oam,0,sizeof p->oam);memset(p->highOam,0,sizeof p->highOam);
        for(unsigned n=0;n<60;++n) {
            p->oam[n*2+1]=(n*11&255)|((n&1)?0x100:0)|((n&7)<<9)|((n&3)<<12)|((size&3)<<14);
            if(n&1) p->highOam[n/4]|=2<<((n%4)*2);
        }
        ScRendererResetHistory(cpu);ScRendererResetHistory(optimized);
        assert(ScRendererResize(cpu,view) && ScRendererResize(optimized,view));
        cpu->reference_terrain=true;assert(ScRendererDeferTerrain(optimized,true));
        for(int line=0;line<224;++line) {
            if(line==1) {
                ScRenderer *targets[]={cpu,optimized};
                for(unsigned k=0;k<2;++k) {
                    ScRenderer *r=targets[k];memset(r->object_grace,0,sizeof r->object_grace);
                    for(unsigned n=0;n<60;++n) {
                        r->object_grace[n]=4;
                        /* Both margin/core seams, overlapping order, canvas
                         * clipping, the 255->0 Y wrap and lower extension. */
                        const int lefts[]={-18,242,250,262,285,-view.core_x-9};
                        r->object_x[n]=lefts[n%6];
                        r->object_y[n]=(n%5==0)?250:16+(n%5)*35;
                    }
                    r->vehicles[0]=(ScVehicleSprite){61,250,55,true};
                    r->vehicles[1]=(ScVehicleSprite){62,282,65,false};r->vehicle_count=2;
                    r->pan_frame=(size&2)!=0;r->pointer_active=(size&4)!=0;
                    r->pointer_x=310;r->pointer_y=95;
                }
                p->oam[123]=0x7321;p->oam[125]=0x952f;
            }
            if(line==80) {
                /* OAM and both sprite CHR banks change after earlier rows
                 * were captured. Those rows must retain their old pixels. */
                for(unsigned n=0;n<60;++n) p->oam[n*2+1]^=0xe500;
                for(unsigned a=0;a<4096;++a) p->vram[(PPU_objTileAdr1(p)+a)&32767]^=0x5739;
                for(unsigned a=0;a<4096;++a) p->vram[(PPU_objTileAdr2(p)+a)&32767]^=0x3957;
            }
            if(line==120) {p->highOam[0]^=0xaa;p->obsel^=0x20;}
            if(line==145) p->oamaddl^=0x32;
            if(line==170) {
                ScRenderer *targets[]={cpu,optimized};
                for(unsigned k=0;k<2;++k) {
                    targets[k]->object_x[10]+=37;targets[k]->object_y[10]=185;
                    targets[k]->object_grace[11]=0;
                    targets[k]->vehicles[0].y=290; /* vehicles do not wrap like OAM */
                    targets[k]->vehicles[1].y=-5;
                }
            }
            for(unsigned x=0;x<256;++x) p->objBuffer.data[x+kPpuExtraLeftRight]=
                x%13<3?(129+x%127)|((2+4*(x%4))<<12):0;
            unsigned ay=line+view.core_y;optimized->terrain.rows[ay].core_x=view.core_x;
            ScNativePpuCapture(&optimized->terrain,p,ay,line+1);
            for(unsigned x=0;x<256;++x) native[x]=ScNativePixel(&optimized->terrain,view.core_x+x,ay);
            assert(ScRendererDeferNativeLine(optimized,p,ram,line));
            ScRendererLine(cpu,p,ram,line,native);ScRendererLine(optimized,p,ram,line,native);
        }
        unsigned grid_headers[8],grid_count=0,grid_rows=0;
        for(unsigned y=0;y<(unsigned)view.height;++y) {
            ScTerrainRow *row=optimized->terrain.rows+y;
            if((row->math&SC_ROW_GPU_OBJECTS) && row->reserved!=UINT32_MAX) ++object_rows;
            if((row->math&SC_ROW_OBJECT_GRID) && row->reserved!=UINT32_MAX) {
                ++grid_rows;unsigned i=0;
                while(i<grid_count && grid_headers[i]!=row->reserved) ++i;
                if(i==grid_count) {assert(grid_count<8);grid_headers[grid_count++]=row->reserved;}
            }
            for(unsigned x=0;x<(unsigned)view.width;++x) {
                uint32_t got=ScRendererPixel(optimized,x,y),expected=cpu->pixels[(size_t)y*view.width+x];
                if(got!=expected) {
                    fprintf(stderr,"OBJ mismatch map=%u size=%u view=%u x=%u y=%u got=%08x expected=%08x policy=%x objects=%u\n",map,size,v,x,y,got,expected,row->math,row->reserved);
                    abort();
                }
            }
        }
        if(!getenv("SC_GPU_OBJECT_GRID_REFERENCE") && grid_rows) {
            /* One immutable grid per live version, rather than a stream per
             * scanline. Mid-frame attribute/size/rotation/position edits must
             * split versions; a CHR edit alone uses the VRAM snapshot. */
            assert(grid_count>=5 && grid_count<=6);assert(grid_rows>grid_count*10);
        }
#ifdef SC_TEST_GPU
        SDL_Texture *objects=ScGpuTerrainDraw(gpu,optimized);assert(objects);
#if SNESRECOMP_SDL3
        presentation_test(renderer,objects,cpu,false);presentation_test(renderer,objects,cpu,true);
#endif
#endif
        assert(ScRendererDeferTerrain(optimized,false));
        assert(!memcmp(cpu->pixels,optimized->pixels,(size_t)view.width*view.height*4));++object_frames;
    }
    if(!getenv("SC_GPU_OBJECT_REFERENCE") && !getenv("SC_TERRAIN_CAPTURE_REFERENCE")) assert(object_rows>1000);
    printf("PASS: %u extended OBJ frames, %u immutable sprite rows, all sizes/CHR banks/flips/rotations/vehicles/core seams, HUD and pan exclusions\n",object_frames,object_rows);
    /* Independent native-PPU oracle lives in sc_ppu_test. Here its captured
     * protocol also crosses the actual GPU upload/dispatch/presentation path. */
    for(unsigned raw=0;raw<2;++raw) for(unsigned v=0;v<3;++v) for(unsigned id=0;id<32;++id) {
        assert(ScRendererResize(cpu,views[v]) && ScRendererResize(deferred,views[v]));
        assert(ScRendererDeferTerrain(deferred,true));
        ScTerrainFrame *f=&deferred->terrain;f->deferred=0;
        if(raw && f->resource_capacity<2048+f->height*16384) {
            f->resource_capacity=2048+f->height*16384;
            f->resources=realloc(f->resources,(size_t)f->resource_capacity*sizeof *f->resources);
            assert(f->resources);
        }
        if(raw) f->snapshots=f->height;
        p->bgmode=1|(id&1?8:0);p->inidisp=15;p->renderFlags=id&2?kPpuRenderFlags_NewRenderer:0;
        for(unsigned layer=0;layer<3;++layer)
            p->bgXsc[layer]=(0x40+layer*0x10)|((id>>(layer*2))&3);
        p->screenEnabled[0]=(id*7)&31;p->screenEnabled[1]=(id*13)&31;
        p->screenWindowed[0]=(id*19)&31;p->screenWindowed[1]=(id*23)&31;
        p->windowsel=(id*0x1b2935)&0xffffff;p->wbgobjlog=id*157;
        p->window1left=id;p->window1right=255-id;p->window2left=128+id;p->window2right=id;
        p->cgadsub=(id*37)&255;p->cgwsel=(id*61)&0xfe;p->fixedColor=(id*197)&32767;
        for(unsigned i=0;i<f->width*f->height;++i) cpu->pixels[i]=deferred->pixels[i]=0xff000000|((i*719)&0xffffff);
        for(unsigned y=0;y<224;++y) {
            unsigned ay=y+views[v].core_y;f->rows[ay].core_x=views[v].core_x;
            for(unsigned layer=0;layer<3;++layer) {
                p->hScroll[layer]=(id*17+y*3+layer*251)&1023;
                p->vScroll[layer]=(id*31+y*5+layer*193)&1023;
            }
            p->vram[0x1000+(y%2048)]^=0x7193;p->cgram[y]^=0x1357;
            for(unsigned i=0;i<32;++i) p->brightnessMult[i]=((i<<3)|(i>>2))*((id+y/32)%16)/15;
            for(unsigned x=0;x<256;++x) p->objBuffer.data[x+kPpuExtraLeftRight]=x%7?
                (129+((x+id)%127))|((2+4*((x+id)%4))<<12):0;
            unsigned slivers=id==31?SC_OBJ_GPU_SLIVERS+1:id*3;
            if(raw) {
                ScObjBegin(p);
                for(unsigned i=0;i<slivers;++i) {
                    int left=(int)((i*17+id*11)%288)-16;
                    unsigned palette=128+16*((i+id)&7),priority=SPRITE_PRIO_TO_PRIO((i+id)&3,palette<192);
                    unsigned at=0x2000+((i*31+id*19)%512)*16+(y&7);
                    unsigned planes=p->vram[at]|(uint32_t)p->vram[at+8]<<16;
                    ScObjAppend((uint16_t)left|((palette+(priority<<8))<<16),
                        at|((i+id)&1?0x8000:0)|((i%4)<<16)|((8-i%3)<<20),planes);
                }
                ScObjMaterialize(p);
            } else ScObjInvalidate(p);
            assert(ScNativePpuSupported(p));ScNativePpuCapture(f,p,ay,y+1);
            /* Fresh terrain can reuse captured native BG1/BG3 candidates
             * without decoding those candidates on the host. Feed BG2's
             * same native planes as terrain, so the complete PPU oracle
             * independently specifies the expected priority result. */
            unsigned phase=f->native[ay].phase[1];
            f->rows[ay].phase=(phase+8-views[v].core_x%8)%8;
            unsigned start=(views[v].core_x+f->rows[ay].phase)/8;
            for(unsigned tile=0;tile<33;++tile) {
                ScNativeTile n=f->native[ay].tiles[1][tile];bool high=(n.attributes&0x2000)!=0;
                unsigned palette=((n.attributes>>10)&7)*16;
                f->tiles[(size_t)ay*f->stride+start+tile]=(ScTerrainTile){high?0:n.planes,high?n.planes:0,
                    high?(palette<<8)|(n.attributes&0x4000?1u<<17:0):palette|(n.attributes&0x4000?1u<<16:0),0,0,0};
            }
            for(unsigned x=0;x<256;++x) {
                unsigned ax=x+views[v].core_x,at=ay*f->width+ax;
                deferred->pixels[at]=SC_NATIVE_PIXEL;cpu->pixels[at]=ScNativePixel(f,ax,ay);++f->deferred;
                if((id&4) && x>=56) {
                    deferred->pixels[at]=SC_TERRAIN_PIXEL;
                    f->overlays[at].background=0x80000000|SC_OVERLAY_NATIVE_BG|(id&8?SC_OVERLAY_SKIP_BG1:0);
                    unsigned flags=f->native[ay].flags,main=f->rows[ay].main,sub=f->rows[ay].sub;
                    f->native[ay].flags&=~2u;
                    if(id&8) {f->rows[ay].main&=~1u;f->rows[ay].sub&=~1u;}
                    cpu->pixels[at]=ScNativePixel(f,ax,ay);
                    f->native[ay].flags=flags;f->rows[ay].main=main;f->rows[ay].sub=sub;
                }
                assert(ScRendererPixel(deferred,ax,ay)==cpu->pixels[at]);
            }
            if(raw) {
                f->rows[ay].chr_snapshot=2048+ay*16384;
                memcpy(f->resources+2048+ay*16384,p->vram,65536);
                ScNativePpuCaptureRaw(f,p,ay,y+1);
                assert(f->native[ay].flags&SC_NATIVE_RAW_BG);
                assert(!!(f->native[ay].flags&SC_NATIVE_RAW_OBJ)==(slivers<=SC_OBJ_GPU_SLIVERS));
                for(unsigned x=0;x<256;++x) {
                    unsigned ax=x+views[v].core_x;
                    assert(ScRendererPixel(deferred,ax,ay)==cpu->pixels[ay*f->width+ax]);
                }
            }
        }
        /* Relocating a native panel must use its source row's live palette,
         * windows, planes and OBJ data even across centered/tall canvases.
         * Background-only host policies must not alter the opaque panel. */
        unsigned dx=(f->width-256)/2,dy=(f->height-224)/2;
        for(unsigned y=0;y<224;++y) for(unsigned x=0;x<256;++x) {
            unsigned sy=y+views[v].core_y,at=(dy+y)*f->width+dx+x;
            if(!(id&4)) f->rows[sy].math|=SC_ROW_ADVISOR_BACKGROUND|SC_ROW_ADVISOR_CORE;
            deferred->pixels[at]=SC_RELOCATED_NATIVE_PIXEL|(sy<<8)|x;
            cpu->pixels[at]=ScNativePixel(f,views[v].core_x+x,sy);
            assert(ScRendererPixel(deferred,dx+x,dy+y)==cpu->pixels[at]);
        }
#ifdef SC_TEST_GPU
        SDL_Texture *computed=ScGpuTerrainDraw(gpu,deferred);assert(computed);
#if SNESRECOMP_SDL3
        presentation_test(renderer,computed,cpu,false);presentation_test(renderer,computed,cpu,true);
#endif
#endif
        assert(ScRendererDeferTerrain(deferred,false));
        assert(!memcmp(cpu->pixels,deferred->pixels,(size_t)f->width*f->height*4));
    }
#ifdef SC_TEST_GPU
    ScGpuTerrainDestroy(gpu);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
#endif
    ScRendererDestroy(cpu);ScRendererDestroy(deferred);
    ScRendererDestroy(optimized);
    ScObjInvalidate(p);free(cpu);free(deferred);free(optimized);free(p);free(world);free(rom);free(ram);
    printf("PASS: %u CPU/deferred frames, Huge/Giant/1920x1600 coordinates, wide/tall views, live planes/palettes, flips, windows, colour math, fades and fallback\n",captures);
    puts("PASS: 192 decoded/raw native PPU captures, immutable VRAM, low/high OBJ palettes, both renderer window contracts, GPU protocol and materialized fallback");
    if(!getenv("SC_GPU_SUB_BG_REFERENCE") && !getenv("SC_TERRAIN_CAPTURE_REFERENCE")) {
        assert(sub_bg_rows>1000);
        printf("PASS: %u BG3 descriptor rows, all map layouts, scroll seams and live VRAM\n",sub_bg_rows);
    }
    if(!getenv("SC_CITY_SPANS_REFERENCE") && !getenv("SC_TERRAIN_CAPTURE_REFERENCE")) {
        assert(city_captures && compact_captures);
        printf("PASS: %u shared city captures, %u compact upload cases, snapshot reuse and all map borders\n",city_captures,compact_captures);
    }
}
