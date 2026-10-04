#include "sc_gpu_fields.h"
#include "sc_sdl_compat.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if SNESRECOMP_SDL3 && SDL_VERSION_ATLEAST(3,4,0)
#include "sc_gpu_fields_spirv.h"
typedef struct FieldJob {
    SDL_GPUBuffer *input,*output;
    SDL_GPUTransferBuffer *upload,*download;
    SDL_GPUFence *fence;
    const uint32_t *mapped,*available;
    uint32_t *cached;
    uint8_t *snapshot;
    const ScWorld *world;
    uint64_t epoch;
    unsigned width,height,count,bytes,source,output_words,bias;
    bool eligible;
} FieldJob;
struct ScGpuFields {
    SDL_GPUDevice *device;
    SDL_GPUComputePipeline *pipeline;
    FieldJob job[6];
    bool service_reference,direct_reference,crime_reference,land_reference;
    uint64_t submitted,ready,validated,missed;
    bool validate,profile;
};
enum {CRIME_SOURCE=17,LAND_SOURCE=18};
static int slot(unsigned source) {return source==13?0:source==14?1:source==10?2:source==11?3:source==CRIME_SOURCE?4:source==LAND_SOURCE?5:-1;}
static void release_job(ScGpuFields *g,FieldJob *j) {
    if(j->mapped) SDL_UnmapGPUTransferBuffer(g->device,j->download);
    if(j->fence) SDL_ReleaseGPUFence(g->device,j->fence);
    if(j->input) SDL_ReleaseGPUBuffer(g->device,j->input);
    if(j->output) SDL_ReleaseGPUBuffer(g->device,j->output);
    if(j->upload) SDL_ReleaseGPUTransferBuffer(g->device,j->upload);
    if(j->download) SDL_ReleaseGPUTransferBuffer(g->device,j->download);
    free(j->snapshot);free(j->cached);memset(j,0,sizeof *j);
}
ScGpuFields *ScGpuFieldsCreate(SDL_Renderer *renderer) {
    SDL_GPUDevice *device=SDL_GetGPURendererDevice(renderer);
    if(!device || strcmp(SDL_GetGPUDeviceDriver(device),"vulkan")) return NULL;
    ScGpuFields *g=calloc(1,sizeof *g);if(!g) return NULL;g->device=device;
    SDL_GPUComputePipelineCreateInfo p={0};p.code=(const Uint8 *)sc_gpu_fields_spirv;
    p.code_size=sizeof sc_gpu_fields_spirv;p.entrypoint="main";p.format=SDL_GPU_SHADERFORMAT_SPIRV;
    p.num_readonly_storage_buffers=1;p.num_readwrite_storage_buffers=1;p.num_uniform_buffers=1;
    p.threadcount_x=128;p.threadcount_y=p.threadcount_z=1;
    g->pipeline=SDL_CreateGPUComputePipeline(device,&p);
    if(!g->pipeline) {fprintf(stderr,"[gpu fields] pipeline unavailable: %s\n",SDL_GetError());free(g);return NULL;}
    const char *e=getenv("SC_GPU_FIELDS_VALIDATE");g->validate=e && *e=='1';
    e=getenv("SC_GPU_FIELDS_PROFILE");g->profile=e && *e=='1';
    e=getenv("SC_SERVICE_REFERENCE");g->service_reference=e && *e=='1';
    e=getenv("SC_GPU_SERVICE_REFERENCE");g->service_reference|=e && *e=='1';
    e=getenv("SC_GPU_CRIME_REFERENCE");g->crime_reference=e && *e=='1';
    e=getenv("SC_GPU_LAND_REFERENCE");g->land_reference=e && *e=='1';
    /* The CPU-owned copy is a diagnostic alternative. Same-machine controls
     * did not show a speedup; avoid that allocation and transfer by default. */
    e=getenv("SC_GPU_FIELDS_DIRECT_REFERENCE");g->direct_reference=!e || *e!='0';
    fprintf(stderr,"[gpu fields] asynchronous Vulkan whole-field compute ready%s\n",g->validate?" (validation)":"");return g;
}
static bool resources(ScGpuFields *g,FieldJob *j,unsigned width,unsigned height,unsigned element_bytes,unsigned output_words,unsigned trailing_bytes) {
    if(j->width==width && j->height==height && j->output_words==output_words) return true;
    uint64_t started=g->profile?SDL_GetPerformanceCounter():0;
    release_job(g,j);unsigned count=width*height,bytes=count*element_bytes+trailing_bytes;
    SDL_GPUBufferCreateInfo b={0};b.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;b.size=(bytes+3)&~3u;
    j->input=SDL_CreateGPUBuffer(g->device,&b);b.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;b.size=count*4*output_words;
    j->output=SDL_CreateGPUBuffer(g->device,&b);
    SDL_GPUTransferBufferCreateInfo t={0};t.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;t.size=(bytes+3)&~3u;
    j->upload=SDL_CreateGPUTransferBuffer(g->device,&t);t.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;t.size=count*4*output_words;
    j->download=SDL_CreateGPUTransferBuffer(g->device,&t);
    if(g->validate) j->snapshot=malloc(bytes);
    if(!g->direct_reference) j->cached=malloc(count*sizeof *j->cached*output_words);
    if(!j->input || !j->output || !j->upload || !j->download || (g->validate && !j->snapshot) || (!g->direct_reference && !j->cached)) {
        fprintf(stderr,"[gpu fields] resources unavailable: %s\n",SDL_GetError());release_job(g,j);return false;
    }
    j->width=width;j->height=height;j->count=count;j->bytes=bytes;j->output_words=output_words;
    if(g->profile) fprintf(stderr,"[gpu fields profile] allocate %ux%u bytes=%u ms=%.3f\n",width,height,bytes,
        (SDL_GetPerformanceCounter()-started)*1000.0/SDL_GetPerformanceFrequency());
    return true;
}
static void begin(void *context,const ScWorld *world,unsigned source,unsigned bias) {
    ScGpuFields *g=context;if(!g || !world || !world->active || slot(source)<0) return;
    uint64_t started=g->profile?SDL_GetPerformanceCounter():0;
    FieldJob *j=&g->job[slot(source)];j->eligible=false;j->available=NULL;
    if(source<13 && g->service_reference) return;
    if(source==CRIME_SOURCE && g->crime_reference) return;
    if(source==LAND_SOURCE && g->land_reference) return;
    /* A new pass never waits for an older job. It uses the normal C path
     * if that slot is still in flight; old results cannot become eligible. */
    if(j->fence) {++g->missed;return;}
    if(j->mapped) {SDL_UnmapGPUTransferBuffer(g->device,j->download);j->mapped=NULL;}
    bool crime=source==CRIME_SOURCE,land=source==LAND_SOURCE,half=crime || land;
    unsigned width=half?ScWorldWidth(world)/2:ScWorldFieldWidth(world,source);
    unsigned height=half?ScWorldHeight(world)/2:ScWorldFieldHeight(world,source);
    if(!resources(g,j,width,height,crime?4:land?8:ScWorldFields[source].element_bytes,crime?3:land?5:1,land?4096:0)) return;
    uint8_t *mapped=SDL_MapGPUTransferBuffer(g->device,j->upload,true);if(!mapped) return;
    if(crime) {
        uint32_t *samples=(uint32_t *)mapped;unsigned coarse_width=width/4;
        for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
            unsigned at=y*width+x,coarse=(y/4)*coarse_width+x/4;
            unsigned coverage=world->fields[11][2*coarse]|((unsigned)world->fields[11][2*coarse+1]<<8);
            samples[at]=world->fields[0][at]|((unsigned)world->fields[3][at]<<8)|(coverage<<16);
        }
    } else if(land) {
        unsigned tile_bytes=width*height*8;memcpy(mapped,world->tiles,tile_bytes);
        uint32_t table[1024];for(unsigned n=0;n<1024;++n) table[n]=ScLandTilePack(n);
        memcpy(mapped+tile_bytes,table,sizeof table);
    } else memcpy(mapped,world->fields[source],j->bytes);
    if(j->snapshot) memcpy(j->snapshot,mapped,j->bytes);
    SDL_UnmapGPUTransferBuffer(g->device,j->upload);
    SDL_GPUCommandBuffer *command=SDL_AcquireGPUCommandBuffer(g->device);if(!command) return;
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(command);
    SDL_GPUTransferBufferLocation from={j->upload,0};SDL_GPUBufferRegion to={j->input,0,(j->bytes+3)&~3u};
    SDL_UploadToGPUBuffer(copy,&from,&to,true);SDL_EndGPUCopyPass(copy);
    SDL_GPUStorageBufferReadWriteBinding output={0};output.buffer=j->output;output.cycle=true;
    SDL_GPUComputePass *compute=SDL_BeginGPUComputePass(command,NULL,0,&output,1);
    SDL_BindGPUComputePipeline(compute,g->pipeline);SDL_BindGPUComputeStorageBuffers(compute,0,&j->input,1);
    unsigned shape[]={width,height,j->count,crime?3:land?4:ScWorldFields[source].element_bytes,land?width*height*8:bias,0,0,0};SDL_PushGPUComputeUniformData(command,0,shape,sizeof shape);
    SDL_DispatchGPUCompute(compute,(j->count+127)/128,1,1);SDL_EndGPUComputePass(compute);
    copy=SDL_BeginGPUCopyPass(command);SDL_GPUBufferRegion output_region={j->output,0,j->count*4*j->output_words};
    SDL_GPUTransferBufferLocation destination={j->download,0};SDL_DownloadFromGPUBuffer(copy,&output_region,&destination);
    SDL_EndGPUCopyPass(copy);j->fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);if(!j->fence) return;
    j->source=source;j->bias=bias;j->world=world;j->epoch=ScWorldStateEpoch();j->eligible=true;++g->submitted;
    if(g->profile) fprintf(stderr,"[gpu fields profile] submit field=%u ms=%.3f\n",source,
        (SDL_GetPerformanceCounter()-started)*1000.0/SDL_GetPerformanceFrequency());
}
void ScGpuFieldsBegin(void *context,const ScWorld *world,unsigned source) {begin(context,world,source,0);}
void ScGpuFieldsCrimeBegin(void *context,const ScWorld *world,unsigned bias) {begin(context,world,CRIME_SOURCE,bias);}
void ScGpuFieldsLandBegin(void *context,const ScWorld *world) {begin(context,world,LAND_SOURCE,0);}
static void poll_job(ScGpuFields *g,FieldJob *j) {
    if(!j->fence || !SDL_QueryGPUFence(g->device,j->fence)) return;
    uint64_t started=g->profile?SDL_GetPerformanceCounter():0;
    SDL_ReleaseGPUFence(g->device,j->fence);j->fence=NULL;
    if(!j->eligible || j->epoch!=ScWorldStateEpoch()) {j->eligible=false;return;}
    j->mapped=SDL_MapGPUTransferBuffer(g->device,j->download,false);
    if(!j->mapped) {j->eligible=false;return;}
    if(j->cached) {
        /* GPU download memory can be uncached/write-combined. Read it once
         * sequentially, then publish scalar samples from ordinary cached RAM. */
        memcpy(j->cached,j->mapped,j->count*sizeof *j->cached*j->output_words);
        SDL_UnmapGPUTransferBuffer(g->device,j->download);j->mapped=NULL;
        j->available=j->cached;
    } else j->available=j->mapped;
    ++g->ready;
    if(g->validate) {
        bool same=true;
        for(unsigned y=0;y<j->height && same;++y) for(unsigned x=0;x<j->width;++x) {
            if(j->source==LAND_SOURCE) {
                unsigned at=y*j->width+x,first=4*y*j->width+2*x;
                uint16_t tiles[4];memcpy(tiles,j->snapshot+2*first,4);memcpy(tiles+2,j->snapshot+2*(first+2*j->width),4);
                ScLandSummary expected=ScLandSummaryPack(tiles);
                if(memcmp(j->available+5*at,&expected,sizeof expected)) {
                    fprintf(stderr,"[gpu fields] mismatch field=%u at=%u,%u land metadata\n",j->source,x,y);
                    same=false;break;
                }
                continue;
            }
            if(j->source==CRIME_SOURCE) {
                unsigned at=y*j->width+x,input;memcpy(&input,j->snapshot+4*at,4);
                ScCrimeSample expected=ScCrimePack(input&255,(input>>8)&255,input>>16,j->bias);
                if(memcmp(j->available+3*at,&expected,sizeof expected)) {
                    fprintf(stderr,"[gpu fields] mismatch field=%u at=%u,%u crime metadata\n",j->source,x,y);
                    same=false;break;
                }
                continue;
            }
            uint32_t expected=j->source<13?ScServicePack(j->snapshot,j->width,j->height,x,y):ScStencilPack(j->snapshot,j->width,j->height,x,y);
            if(j->available[y*j->width+x]!=expected) {
                fprintf(stderr,"[gpu fields] mismatch field=%u at=%u,%u GPU=%08x CPU=%08x\n",j->source,x,y,j->available[y*j->width+x],expected);
                same=false;break;
            }
        }
        if(same) {++g->validated;fprintf(stderr,"[gpu fields] validated %ux%u field=%u\n",j->width,j->height,j->source);}
        else {if(j->mapped) SDL_UnmapGPUTransferBuffer(g->device,j->download);j->mapped=NULL;j->available=NULL;j->eligible=false;}
    }
    if(g->profile) fprintf(stderr,"[gpu fields profile] publish field=%u ms=%.3f\n",j->source,
        (SDL_GetPerformanceCounter()-started)*1000.0/SDL_GetPerformanceFrequency());
}
void ScGpuFieldsPoll(ScGpuFields *g) {
    if(g) for(unsigned n=0;n<6;++n) poll_job(g,&g->job[n]);
}
const uint32_t *ScGpuFieldsData(void *context,const ScWorld *world,unsigned source) {
    ScGpuFields *g=context;if(!g || slot(source)<0) return NULL;
    FieldJob *j=&g->job[slot(source)];
    /* A small word pass can finish within one emulated frame. Query only at
     * fused publication boundaries, once per span, so it can use a completed
     * job in that same frame without a wait or per-cell fence calls. */
    if(j->fence && j->eligible && j->world==world && j->epoch==ScWorldStateEpoch()) poll_job(g,j);
    return world && j->eligible && j->available && j->world==world && j->epoch==ScWorldStateEpoch() &&
        j->width==(source>=CRIME_SOURCE?ScWorldWidth(world)/2:ScWorldFieldWidth(world,source)) &&
        j->height==(source>=CRIME_SOURCE?ScWorldHeight(world)/2:ScWorldFieldHeight(world,source))?j->available:NULL;
}
const ScCrimeSample *ScGpuFieldsCrimeData(void *context,const ScWorld *world,unsigned bias) {
    ScGpuFields *g=context;if(!g || g->job[4].bias!=bias) return NULL;
    return (const ScCrimeSample *)ScGpuFieldsData(context,world,CRIME_SOURCE);
}
const ScLandSummary *ScGpuFieldsLandData(void *context,const ScWorld *world) {
    return (const ScLandSummary *)ScGpuFieldsData(context,world,LAND_SOURCE);
}
void ScGpuFieldsDestroy(ScGpuFields *g) {
    if(!g) return;
    /* Only shutdown waits. Ordinary submission, publication and polling do not. */
    for(unsigned n=0;n<6;++n) if(g->job[n].fence) SDL_WaitForGPUFences(g->device,true,&g->job[n].fence,1);
    for(unsigned n=0;n<6;++n) release_job(g,&g->job[n]);
    SDL_ReleaseGPUComputePipeline(g->device,g->pipeline);
    fprintf(stderr,"[gpu fields] submitted=%llu ready=%llu validated=%llu skipped-in-flight=%llu\n",
        (unsigned long long)g->submitted,(unsigned long long)g->ready,(unsigned long long)g->validated,(unsigned long long)g->missed);
    free(g);
}
#else
ScGpuFields *ScGpuFieldsCreate(SDL_Renderer *r) {(void)r;return NULL;}
void ScGpuFieldsBegin(void *g,const ScWorld *w,unsigned s) {(void)g;(void)w;(void)s;}
const uint32_t *ScGpuFieldsData(void *g,const ScWorld *w,unsigned s) {(void)g;(void)w;(void)s;return NULL;}
void ScGpuFieldsCrimeBegin(void *g,const ScWorld *w,unsigned b) {(void)g;(void)w;(void)b;}
const ScCrimeSample *ScGpuFieldsCrimeData(void *g,const ScWorld *w,unsigned b) {(void)g;(void)w;(void)b;return NULL;}
void ScGpuFieldsLandBegin(void *g,const ScWorld *w) {(void)g;(void)w;}
const ScLandSummary *ScGpuFieldsLandData(void *g,const ScWorld *w) {(void)g;(void)w;return NULL;}
void ScGpuFieldsPoll(ScGpuFields *g) {(void)g;}
void ScGpuFieldsDestroy(ScGpuFields *g) {(void)g;}
#endif
