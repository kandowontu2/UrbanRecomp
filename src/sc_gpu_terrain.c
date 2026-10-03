#include "sc_gpu_terrain.h"
#include "sc_sdl_compat.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if SNESRECOMP_SDL3 && SDL_VERSION_ATLEAST(3,4,0)
#include "sc_gpu_terrain_spirv.h"
/* One Vulkan device/queue owns compute and presentation. Only validation
 * reads an image back; ordinary gameplay keeps the result on the GPU. */
struct ScGpuTerrain {
    SDL_Renderer *renderer;
    SDL_GPUDevice *device;
    SDL_GPUComputePipeline *pipeline;
    SDL_GPUBuffer *buffers[8];
    SDL_GPUTransferBuffer *upload,*download;
    SDL_GPUTexture *output;
    SDL_Texture *texture;
    unsigned width,height,stride,frames,resource_capacity,city_capacity,offsets[8],bytes[8];
    bool validate,linear_filter;
};
_Static_assert(sizeof(ScTerrainRow)==308*4,"shader scanline layout");
_Static_assert(sizeof(ScNativeRow)==202*4,"shader native scanline layout");
_Static_assert(sizeof(ScTerrainTile)==6*4,"shader terrain span layout");
SDL_Renderer *ScGpuTerrainRenderer(SDL_Window *window) {
    const char *driver=getenv("SDL_RENDER_DRIVER");
    if(driver && strcmp(driver,"gpu") && strcmp(driver,"vulkan")) return NULL;
    SDL_PropertiesID props=SDL_CreateProperties();if(!props) return NULL;
    SDL_SetPointerProperty(props,SDL_PROP_RENDERER_CREATE_WINDOW_POINTER,window);
    SDL_SetStringProperty(props,SDL_PROP_RENDERER_CREATE_NAME_STRING,"gpu");
    SDL_SetStringProperty(props,SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,"vulkan");
    SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,false);
    SDL_SetNumberProperty(props,SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER,0);
    SDL_Renderer *renderer=SDL_CreateRendererWithProperties(props);SDL_DestroyProperties(props);
    if(!renderer) fprintf(stderr,"[vulkan] renderer unavailable: %s\n",SDL_GetError());
    else {
        SDL_GPUDevice *device=SDL_GetGPURendererDevice(renderer);
        fprintf(stderr,"[vulkan] shared presentation/compute device: %s (%s)\n",
            SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown GPU"),
            SDL_GetGPUDeviceDriver(device));
    }
    return renderer;
}
static void release_surface(ScGpuTerrain *g) {
    if(g->texture) {SDL_DestroyTexture(g->texture);g->texture=NULL;}
    if(g->output) SDL_ReleaseGPUTexture(g->device,g->output);
    if(g->upload) SDL_ReleaseGPUTransferBuffer(g->device,g->upload);
    if(g->download) SDL_ReleaseGPUTransferBuffer(g->device,g->download);
    for(unsigned i=0;i<8;++i) {if(g->buffers[i]) SDL_ReleaseGPUBuffer(g->device,g->buffers[i]);g->buffers[i]=NULL;}
    g->output=NULL;g->upload=g->download=NULL;g->width=g->height=0;
}
void ScGpuTerrainDestroy(ScGpuTerrain *g) {
    if(!g) return;
    SDL_FlushRenderer(g->renderer);release_surface(g);
    if(g->pipeline) SDL_ReleaseGPUComputePipeline(g->device,g->pipeline);
    free(g);
}
ScGpuTerrain *ScGpuTerrainCreate(SDL_Renderer *renderer,bool linear_filter) {
    SDL_GPUDevice *device=SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),SDL_PROP_RENDERER_GPU_DEVICE_POINTER,NULL);
    if(!device || strcmp(SDL_GetGPUDeviceDriver(device),"vulkan")) return NULL;
    ScGpuTerrain *g=calloc(1,sizeof *g);if(!g) return NULL;
    g->renderer=renderer;g->device=device;g->linear_filter=linear_filter;
    SDL_GPUComputePipelineCreateInfo info={0};
    info.code=(const Uint8 *)sc_gpu_terrain_spirv;info.code_size=sizeof sc_gpu_terrain_spirv;
    info.entrypoint="main";info.format=SDL_GPU_SHADERFORMAT_SPIRV;
    info.num_readonly_storage_buffers=8;info.num_readwrite_storage_textures=1;info.num_uniform_buffers=1;
    info.threadcount_x=info.threadcount_y=8;info.threadcount_z=1;
    g->pipeline=SDL_CreateGPUComputePipeline(device,&info);
    if(!g->pipeline) {fprintf(stderr,"[vulkan] compute pipeline: %s\n",SDL_GetError());free(g);return NULL;}
    const char *validate=getenv("SC_GPU_VALIDATE");
    g->validate=validate && *validate && *validate!='0';
    fprintf(stderr,"[gpu terrain] Vulkan compute ready%s\n",g->validate?" (pixel validation)":"");return g;
}
static bool surface(ScGpuTerrain *g,const ScTerrainFrame *f) {
    if(g->width==f->width && g->height==f->height && g->stride==f->stride && g->resource_capacity==f->resource_capacity && g->city_capacity==f->city_capacity) return true;
    SDL_FlushRenderer(g->renderer);release_surface(g);
    const unsigned counts[]={f->width*f->height,f->stride*f->height,f->height*256,f->height*308,f->width*f->height,f->height*202,f->resource_capacity?f->resource_capacity:1,f->city_capacity?f->city_capacity:1};
    const unsigned elements[]={4,sizeof(ScTerrainTile),4,4,sizeof(ScTerrainOverlay),4,4,4};unsigned total=0;
    for(unsigned i=0;i<8;++i) {
        total=(total+15)&~15u;g->offsets[i]=total;g->bytes[i]=counts[i]*elements[i];total+=g->bytes[i];
        SDL_GPUBufferCreateInfo info={0};info.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;info.size=g->bytes[i];
        g->buffers[i]=SDL_CreateGPUBuffer(g->device,&info);if(!g->buffers[i]) goto failed;
    }
    SDL_GPUTransferBufferCreateInfo transfer={0};transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;transfer.size=total;
    g->upload=SDL_CreateGPUTransferBuffer(g->device,&transfer);if(!g->upload) goto failed;
    SDL_GPUTextureCreateInfo image={0};image.type=SDL_GPU_TEXTURETYPE_2D;image.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    image.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    image.width=f->width;image.height=f->height;image.layer_count_or_depth=1;image.num_levels=1;
    g->output=SDL_CreateGPUTexture(g->device,&image);if(!g->output) goto failed;
    SDL_PropertiesID props=SDL_CreateProperties();if(!props) goto failed;
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_ABGR8888);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,f->width);SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,f->height);
    SDL_SetPointerProperty(props,SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER,g->output);
    g->texture=SDL_CreateTextureWithProperties(g->renderer,props);SDL_DestroyProperties(props);if(!g->texture) goto failed;
    SDL_SetTextureBlendMode(g->texture,SDL_BLENDMODE_NONE);SDL_SetTextureScaleMode(g->texture,g->linear_filter?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST);
    if(g->validate) {transfer.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;transfer.size=f->width*f->height*4;
        g->download=SDL_CreateGPUTransferBuffer(g->device,&transfer);if(!g->download) goto failed;}
    g->width=f->width;g->height=f->height;g->stride=f->stride;g->resource_capacity=f->resource_capacity;g->city_capacity=f->city_capacity;return true;
failed:
    fprintf(stderr,"[vulkan] terrain resources: %s\n",SDL_GetError());release_surface(g);return false;
}
SDL_Texture *ScGpuTerrainDraw(ScGpuTerrain *g,const ScRenderer *r) {
    if(!g || !r->terrain.deferred || !surface(g,&r->terrain)) return NULL;
    const ScTerrainFrame *f=&r->terrain;uint32_t empty=0;
    bool compact=f->stride>=34;
    for(unsigned y=0;y<f->height && compact;++y) compact=(f->rows[y].math&SC_ROW_CITY_SPANS)!=0;
    bool compact_objects=f->width>=256;
    for(unsigned y=0;y<f->height && compact_objects;++y)
        compact_objects=(f->rows[y].math&(SC_ROW_GPU_OBJECTS|SC_ROW_ADVISOR_BACKGROUND))!=0 &&
            f->rows[y].core_x+256<=f->width;
    const void *data[]={r->pixels,f->tiles,f->palette,f->rows,f->overlays,f->native,f->resources?f->resources:&empty,f->city?f->city:&empty};
    unsigned upload_bytes[8];memcpy(upload_bytes,g->bytes,sizeof upload_bytes);
    upload_bytes[6]=(f->snapshots?2048+f->snapshots*16384:1)*4;
    upload_bytes[7]=(f->city_words?f->city_words:1)*4;
    if(compact) upload_bytes[1]=f->height*34*sizeof(ScTerrainTile);
    if(compact_objects) upload_bytes[4]=f->height*256*sizeof(ScTerrainOverlay);
    Uint8 *mapped=SDL_MapGPUTransferBuffer(g->device,g->upload,true);if(!mapped) return NULL;
    for(unsigned i=0;i<8;++i) {
        if(i==1 && compact) {
            for(unsigned y=0;y<f->height;++y) {
                unsigned first=(f->rows[y].core_x+f->rows[y].phase)/8;
                unsigned count=first+34>f->stride?f->stride-first:34;
                Uint8 *to=mapped+g->offsets[1]+(size_t)y*34*sizeof(ScTerrainTile);
                memcpy(to,f->tiles+(size_t)y*f->stride+first,count*sizeof(ScTerrainTile));
                if(count<34) memset(to+count*sizeof(ScTerrainTile),0,(34-count)*sizeof(ScTerrainTile));
            }
        } else if(i==4 && compact_objects) {
            for(unsigned y=0;y<f->height;++y)
                memcpy(mapped+g->offsets[4]+(size_t)y*256*sizeof(ScTerrainOverlay),
                    f->overlays+(size_t)y*f->width+f->rows[y].core_x,256*sizeof(ScTerrainOverlay));
        } else memcpy(mapped+g->offsets[i],data[i],upload_bytes[i]);
    }
    SDL_UnmapGPUTransferBuffer(g->device,g->upload);SDL_FlushRenderer(g->renderer);
    SDL_GPUCommandBuffer *command=SDL_AcquireGPUCommandBuffer(g->device);if(!command) return NULL;
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(command);
    for(unsigned i=0;i<8;++i) {SDL_GPUTransferBufferLocation from={g->upload,g->offsets[i]};
        SDL_GPUBufferRegion to={g->buffers[i],0,upload_bytes[i]};SDL_UploadToGPUBuffer(copy,&from,&to,true);}
    SDL_EndGPUCopyPass(copy);
    SDL_GPUStorageTextureReadWriteBinding output={0};output.texture=g->output;output.cycle=true;
    SDL_GPUComputePass *compute=SDL_BeginGPUComputePass(command,&output,1,NULL,0);
    SDL_BindGPUComputePipeline(compute,g->pipeline);SDL_BindGPUComputeStorageBuffers(compute,0,g->buffers,8);
    unsigned dimensions[]={f->width,f->height,f->stride,(compact?1u:0u)|(compact_objects?2u:0u)};SDL_PushGPUComputeUniformData(command,0,dimensions,sizeof dimensions);
    SDL_DispatchGPUCompute(compute,(f->width+7)/8,(f->height+7)/8,1);SDL_EndGPUComputePass(compute);
    if(g->validate) {
        copy=SDL_BeginGPUCopyPass(command);SDL_GPUTextureRegion source={0};source.texture=g->output;source.w=f->width;source.h=f->height;source.d=1;
        SDL_GPUTextureTransferInfo dest={0};dest.transfer_buffer=g->download;SDL_DownloadFromGPUTexture(copy,&source,&dest);SDL_EndGPUCopyPass(copy);
        SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);if(!fence) return NULL;
        bool ready=SDL_WaitForGPUFences(g->device,true,&fence,1);SDL_ReleaseGPUFence(g->device,fence);if(!ready) return NULL;
        const Uint32 *pixels=SDL_MapGPUTransferBuffer(g->device,g->download,false);if(!pixels) return NULL;
        bool same=true;
        for(unsigned y=0;y<f->height && same;++y) for(unsigned x=0;x<f->width;++x) {
            Uint32 c=pixels[y*f->width+x];c=0xff000000|((c&255)<<16)|(c&0xff00)|((c>>16)&255);
            Uint32 expected=ScRendererPixel(r,x,y)|0xff000000;
            if(c!=expected) {fprintf(stderr,"[gpu terrain] mismatch %u,%u: %08x != %08x\n",x,y,c,expected);same=false;break;}
        }
        SDL_UnmapGPUTransferBuffer(g->device,g->download);if(!same) return NULL;
        if(g->frames%60==0) fprintf(stderr,"[gpu terrain] pixels match %ux%u, %u deferred\n",f->width,f->height,f->deferred);
    } else if(!SDL_SubmitGPUCommandBuffer(command)) return NULL;
    if(!g->frames) fprintf(stderr,"[gpu terrain] composing %ux%u, %u deferred\n",f->width,f->height,f->deferred);
    ++g->frames;return g->texture;
}
#else
SDL_Renderer *ScGpuTerrainRenderer(SDL_Window *window) {(void)window;return NULL;}
ScGpuTerrain *ScGpuTerrainCreate(SDL_Renderer *renderer,bool linear_filter) {(void)renderer;(void)linear_filter;return NULL;}
SDL_Texture *ScGpuTerrainDraw(ScGpuTerrain *g,const ScRenderer *r) {(void)g;(void)r;return NULL;}
void ScGpuTerrainDestroy(ScGpuTerrain *g) {(void)g;}
#endif
