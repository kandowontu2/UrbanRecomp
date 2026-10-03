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
    unsigned width,height,count,bytes,source;
    bool eligible;
} FieldJob;
struct ScGpuFields {
    SDL_GPUDevice *device;
    SDL_GPUComputePipeline *pipeline;
    FieldJob job[4];
    bool service_reference,direct_reference;
    uint64_t submitted,ready,validated,missed;
    bool validate;
};
static int slot(unsigned source) {return source==13?0:source==14?1:source==10?2:source==11?3:-1;}
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
    e=getenv("SC_SERVICE_REFERENCE");g->service_reference=e && *e=='1';
    e=getenv("SC_GPU_SERVICE_REFERENCE");g->service_reference|=e && *e=='1';
    /* The CPU-owned copy is a diagnostic alternative. Same-machine controls
     * did not show a speedup; avoid that allocation and transfer by default. */
    e=getenv("SC_GPU_FIELDS_DIRECT_REFERENCE");g->direct_reference=!e || *e!='0';
    fprintf(stderr,"[gpu fields] asynchronous Vulkan whole-field compute ready%s\n",g->validate?" (validation)":"");return g;
}
static bool resources(ScGpuFields *g,FieldJob *j,unsigned width,unsigned height,unsigned element_bytes) {
    if(j->width==width && j->height==height) return true;
    release_job(g,j);unsigned count=width*height,bytes=count*element_bytes;
    SDL_GPUBufferCreateInfo b={0};b.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;b.size=(bytes+3)&~3u;
    j->input=SDL_CreateGPUBuffer(g->device,&b);b.usage=SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;b.size=count*4;
    j->output=SDL_CreateGPUBuffer(g->device,&b);
    SDL_GPUTransferBufferCreateInfo t={0};t.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;t.size=(bytes+3)&~3u;
    j->upload=SDL_CreateGPUTransferBuffer(g->device,&t);t.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;t.size=count*4;
    j->download=SDL_CreateGPUTransferBuffer(g->device,&t);
    if(g->validate) j->snapshot=malloc(bytes);
    if(!g->direct_reference) j->cached=malloc(count*sizeof *j->cached);
    if(!j->input || !j->output || !j->upload || !j->download || (g->validate && !j->snapshot) || (!g->direct_reference && !j->cached)) {
        fprintf(stderr,"[gpu fields] resources unavailable: %s\n",SDL_GetError());release_job(g,j);return false;
    }
    j->width=width;j->height=height;j->count=count;j->bytes=bytes;return true;
}
void ScGpuFieldsBegin(void *context,const ScWorld *world,unsigned source) {
    ScGpuFields *g=context;if(!g || !world || !world->active || slot(source)<0) return;
    FieldJob *j=&g->job[slot(source)];j->eligible=false;j->available=NULL;
    if(source<13 && g->service_reference) return;
    /* A new pass never waits for an older job. It uses the normal C path
     * if that slot is still in flight; old results cannot become eligible. */
    if(j->fence) {++g->missed;return;}
    if(j->mapped) {SDL_UnmapGPUTransferBuffer(g->device,j->download);j->mapped=NULL;}
    unsigned width=ScWorldFieldWidth(world,source),height=ScWorldFieldHeight(world,source);
    if(!resources(g,j,width,height,ScWorldFields[source].element_bytes)) return;
    uint8_t *mapped=SDL_MapGPUTransferBuffer(g->device,j->upload,true);if(!mapped) return;
    memcpy(mapped,world->fields[source],j->bytes);
    if(j->snapshot) memcpy(j->snapshot,mapped,j->bytes);
    SDL_UnmapGPUTransferBuffer(g->device,j->upload);
    SDL_GPUCommandBuffer *command=SDL_AcquireGPUCommandBuffer(g->device);if(!command) return;
    SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(command);
    SDL_GPUTransferBufferLocation from={j->upload,0};SDL_GPUBufferRegion to={j->input,0,(j->bytes+3)&~3u};
    SDL_UploadToGPUBuffer(copy,&from,&to,true);SDL_EndGPUCopyPass(copy);
    SDL_GPUStorageBufferReadWriteBinding output={0};output.buffer=j->output;output.cycle=true;
    SDL_GPUComputePass *compute=SDL_BeginGPUComputePass(command,NULL,0,&output,1);
    SDL_BindGPUComputePipeline(compute,g->pipeline);SDL_BindGPUComputeStorageBuffers(compute,0,&j->input,1);
    unsigned shape[]={width,height,j->count,ScWorldFields[source].element_bytes};SDL_PushGPUComputeUniformData(command,0,shape,sizeof shape);
    SDL_DispatchGPUCompute(compute,(j->count+127)/128,1,1);SDL_EndGPUComputePass(compute);
    copy=SDL_BeginGPUCopyPass(command);SDL_GPUBufferRegion output_region={j->output,0,j->count*4};
    SDL_GPUTransferBufferLocation destination={j->download,0};SDL_DownloadFromGPUBuffer(copy,&output_region,&destination);
    SDL_EndGPUCopyPass(copy);j->fence=SDL_SubmitGPUCommandBufferAndAcquireFence(command);if(!j->fence) return;
    j->source=source;j->world=world;j->epoch=ScWorldStateEpoch();j->eligible=true;++g->submitted;
}
static void poll_job(ScGpuFields *g,FieldJob *j) {
    if(!j->fence || !SDL_QueryGPUFence(g->device,j->fence)) return;
    SDL_ReleaseGPUFence(g->device,j->fence);j->fence=NULL;
    if(!j->eligible || j->epoch!=ScWorldStateEpoch()) {j->eligible=false;return;}
    j->mapped=SDL_MapGPUTransferBuffer(g->device,j->download,false);
    if(!j->mapped) {j->eligible=false;return;}
    if(j->cached) {
        /* GPU download memory can be uncached/write-combined. Read it once
         * sequentially, then publish scalar samples from ordinary cached RAM. */
        memcpy(j->cached,j->mapped,j->count*sizeof *j->cached);
        SDL_UnmapGPUTransferBuffer(g->device,j->download);j->mapped=NULL;
        j->available=j->cached;
    } else j->available=j->mapped;
    ++g->ready;
    if(g->validate) {
        bool same=true;
        for(unsigned y=0;y<j->height && same;++y) for(unsigned x=0;x<j->width;++x) {
            uint32_t expected=j->source<13?ScServicePack(j->snapshot,j->width,j->height,x,y):ScStencilPack(j->snapshot,j->width,j->height,x,y);
            if(j->available[y*j->width+x]!=expected) {
                fprintf(stderr,"[gpu fields] mismatch field=%u at=%u,%u GPU=%08x CPU=%08x\n",j->source,x,y,j->available[y*j->width+x],expected);
                same=false;break;
            }
        }
        if(same) {++g->validated;fprintf(stderr,"[gpu fields] validated %ux%u field=%u\n",j->width,j->height,j->source);}
        else {if(j->mapped) SDL_UnmapGPUTransferBuffer(g->device,j->download);j->mapped=NULL;j->available=NULL;j->eligible=false;}
    }
}
void ScGpuFieldsPoll(ScGpuFields *g) {
    if(g) for(unsigned n=0;n<4;++n) poll_job(g,&g->job[n]);
}
const uint32_t *ScGpuFieldsData(void *context,const ScWorld *world,unsigned source) {
    ScGpuFields *g=context;if(!g || slot(source)<0) return NULL;
    FieldJob *j=&g->job[slot(source)];
    /* A small word pass can finish within one emulated frame. Query only at
     * fused publication boundaries, once per span, so it can use a completed
     * job in that same frame without a wait or per-cell fence calls. */
    if(j->fence && j->eligible && j->world==world && j->epoch==ScWorldStateEpoch()) poll_job(g,j);
    return world && j->eligible && j->available && j->world==world && j->epoch==ScWorldStateEpoch() &&
        j->width==ScWorldFieldWidth(world,source) && j->height==ScWorldFieldHeight(world,source)?j->available:NULL;
}
void ScGpuFieldsDestroy(ScGpuFields *g) {
    if(!g) return;
    /* Only shutdown waits. Ordinary submission, publication and polling do not. */
    for(unsigned n=0;n<4;++n) if(g->job[n].fence) SDL_WaitForGPUFences(g->device,true,&g->job[n].fence,1);
    for(unsigned n=0;n<4;++n) release_job(g,&g->job[n]);
    SDL_ReleaseGPUComputePipeline(g->device,g->pipeline);
    fprintf(stderr,"[gpu fields] submitted=%llu ready=%llu validated=%llu skipped-in-flight=%llu\n",
        (unsigned long long)g->submitted,(unsigned long long)g->ready,(unsigned long long)g->validated,(unsigned long long)g->missed);
    free(g);
}
#else
ScGpuFields *ScGpuFieldsCreate(SDL_Renderer *r) {(void)r;return NULL;}
void ScGpuFieldsBegin(void *g,const ScWorld *w,unsigned s) {(void)g;(void)w;(void)s;}
const uint32_t *ScGpuFieldsData(void *g,const ScWorld *w,unsigned s) {(void)g;(void)w;(void)s;return NULL;}
void ScGpuFieldsPoll(ScGpuFields *g) {(void)g;}
void ScGpuFieldsDestroy(ScGpuFields *g) {(void)g;}
#endif
