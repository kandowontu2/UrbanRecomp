#include "sc_gpu_terrain.h"
#include "sc_sdl_compat.h"
#include <stdlib.h>
#include <stdio.h>
#if defined(_WIN32) && SNESRECOMP_SDL3
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include "sc_gpu_shader.h"
struct ScGpuTerrain {
    SDL_Renderer *renderer;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    ID3D11ComputeShader *shader;
    ID3D11Buffer *buffers[4],*constants;
    ID3D11ShaderResourceView *views[4];
    ID3D11Texture2D *output,*staging;
    ID3D11UnorderedAccessView *uav;
    SDL_Texture *texture;
    unsigned width,height,stride,frames;
    bool validate,linear_filter;
};
_Static_assert(sizeof(ScTerrainRow)==300*4,"shader scanline layout");
#define RELEASE(p) do {if(p) {(p)->lpVtbl->Release(p);(p)=NULL;}} while(0)
static void release_surface(ScGpuTerrain *g) {
    if(g->texture) {SDL_DestroyTexture(g->texture);g->texture=NULL;}
    RELEASE(g->uav);RELEASE(g->output);RELEASE(g->staging);
    for(unsigned i=0;i<4;++i) {RELEASE(g->views[i]);RELEASE(g->buffers[i]);}
    g->width=g->height=0;
}
void ScGpuTerrainDestroy(ScGpuTerrain *g) {
    if(!g) return;
    release_surface(g);RELEASE(g->constants);RELEASE(g->shader);
    RELEASE(g->context);RELEASE(g->device);free(g);
}
ScGpuTerrain *ScGpuTerrainCreate(SDL_Renderer *renderer,bool linear_filter) {
    ID3D11Device *device=SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),
        SDL_PROP_RENDERER_D3D11_DEVICE_POINTER,NULL);
    if(!device || ID3D11Device_GetFeatureLevel(device)<D3D_FEATURE_LEVEL_11_0) return NULL;
    ScGpuTerrain *g=calloc(1,sizeof *g);if(!g) return NULL;
    g->renderer=renderer;g->device=device;g->linear_filter=linear_filter;ID3D11Device_AddRef(device);
    ID3D11Device_GetImmediateContext(device,&g->context);
    HMODULE compiler=LoadLibraryExW(L"d3dcompiler_47.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    typedef HRESULT (WINAPI *CompileFn)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,
        ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
    CompileFn compile=compiler?(CompileFn)GetProcAddress(compiler,"D3DCompile"):NULL;
    ID3DBlob *code=NULL,*error=NULL;
    HRESULT hr=compile?compile(sc_gpu_shader,sizeof sc_gpu_shader-1,"sc_gpu_terrain.hlsl",
        NULL,NULL,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error):E_FAIL;
    if(FAILED(hr) && error) fprintf(stderr,"[gpu terrain] shader: %s\n",
        (const char *)ID3D10Blob_GetBufferPointer(error));
    if(SUCCEEDED(hr)) hr=ID3D11Device_CreateComputeShader(device,
        ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&g->shader);
    RELEASE(code);RELEASE(error);if(compiler) FreeLibrary(compiler);
    D3D11_BUFFER_DESC desc={0};desc.ByteWidth=16;desc.Usage=D3D11_USAGE_DYNAMIC;
    desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    if(SUCCEEDED(hr)) hr=ID3D11Device_CreateBuffer(device,&desc,NULL,&g->constants);
    if(FAILED(hr)) {ScGpuTerrainDestroy(g);return NULL;}
    g->validate=getenv("SC_GPU_VALIDATE")!=NULL;
    fprintf(stderr,"[gpu terrain] Direct3D 11 compute ready%s\n",g->validate?" (pixel validation)":"");
    return g;
}
static bool surface(ScGpuTerrain *g,const ScTerrainFrame *f) {
    if(g->width==f->width && g->height==f->height) return true;
    SDL_FlushRenderer(g->renderer);release_surface(g);
    const unsigned count[]={f->width*f->height,f->stride*f->height,f->height*256,f->height*300};
    const unsigned element[]={4,sizeof(ScTerrainTile),4,4};
    for(unsigned i=0;i<4;++i) {
        D3D11_BUFFER_DESC desc={0};desc.ByteWidth=count[i]*element[i];desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=element[i];
        if(FAILED(ID3D11Device_CreateBuffer(g->device,&desc,NULL,&g->buffers[i]))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC view={0};view.Format=DXGI_FORMAT_UNKNOWN;
        view.ViewDimension=D3D11_SRV_DIMENSION_BUFFER;view.Buffer.NumElements=count[i];
        if(FAILED(ID3D11Device_CreateShaderResourceView(g->device,(ID3D11Resource *)g->buffers[i],&view,&g->views[i]))) return false;
    }
    D3D11_TEXTURE2D_DESC desc={0};desc.Width=f->width;desc.Height=f->height;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    if(FAILED(ID3D11Device_CreateTexture2D(g->device,&desc,NULL,&g->output)) ||
       FAILED(ID3D11Device_CreateUnorderedAccessView(g->device,(ID3D11Resource *)g->output,NULL,&g->uav))) return false;
    SDL_PropertiesID props=SDL_CreateProperties();if(!props) return false;
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_ABGR8888);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,f->width);
    SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,f->height);
    SDL_SetPointerProperty(props,SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER,g->output);
    g->texture=SDL_CreateTextureWithProperties(g->renderer,props);SDL_DestroyProperties(props);
    if(!g->texture) return false;
    SDL_SetTextureBlendMode(g->texture,SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(g->texture,g->linear_filter?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST);
    if(g->validate) {
        desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(ID3D11Device_CreateTexture2D(g->device,&desc,NULL,&g->staging))) return false;
    }
    g->width=f->width;g->height=f->height;g->stride=f->stride;return true;
}
static bool upload(ScGpuTerrain *g,ID3D11Buffer *buffer,const void *data,size_t bytes) {
    D3D11_MAPPED_SUBRESOURCE map={0};
    if(FAILED(ID3D11DeviceContext_Map(g->context,(ID3D11Resource *)buffer,0,D3D11_MAP_WRITE_DISCARD,0,&map))) return false;
    memcpy(map.pData,data,bytes);ID3D11DeviceContext_Unmap(g->context,(ID3D11Resource *)buffer,0);return true;
}
SDL_Texture *ScGpuTerrainDraw(ScGpuTerrain *g,const ScRenderer *r) {
    if(!g || !r->terrain.deferred || !surface(g,&r->terrain)) return NULL;
    const ScTerrainFrame *f=&r->terrain;
    unsigned dims[]={f->width,f->height,f->stride,0};
    const void *data[]={r->pixels,f->tiles,f->palette,f->rows};
    const size_t bytes[]={(size_t)f->width*f->height*4,(size_t)f->stride*f->height*sizeof(ScTerrainTile),
        (size_t)f->height*256*4,(size_t)f->height*sizeof(ScTerrainRow)};
    SDL_FlushRenderer(g->renderer);
    for(unsigned i=0;i<4;++i) if(!upload(g,g->buffers[i],data[i],bytes[i])) return NULL;
    if(!upload(g,g->constants,dims,sizeof dims)) return NULL;
    /* SDL caches graphics bindings. Preserve its PS views while compute
     * writes a texture that SDL may have sampled in the previous frame. */
    ID3D11ShaderResourceView *saved[3]={0},*empty[4]={0};
    ID3D11DeviceContext_PSGetShaderResources(g->context,0,3,saved);
    ID3D11DeviceContext_PSSetShaderResources(g->context,0,3,empty);
    ID3D11DeviceContext_CSSetShader(g->context,g->shader,NULL,0);
    ID3D11DeviceContext_CSSetConstantBuffers(g->context,0,1,&g->constants);
    ID3D11DeviceContext_CSSetShaderResources(g->context,0,4,g->views);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(g->context,0,1,&g->uav,NULL);
    ID3D11DeviceContext_Dispatch(g->context,(f->width+7)/8,(f->height+7)/8,1);
    ID3D11UnorderedAccessView *no_uav=NULL;ID3D11Buffer *no_buffer=NULL;
    ID3D11DeviceContext_CSSetUnorderedAccessViews(g->context,0,1,&no_uav,NULL);
    ID3D11DeviceContext_CSSetShaderResources(g->context,0,4,empty);
    ID3D11DeviceContext_CSSetConstantBuffers(g->context,0,1,&no_buffer);
    ID3D11DeviceContext_CSSetShader(g->context,NULL,NULL,0);
    ID3D11DeviceContext_PSSetShaderResources(g->context,0,3,saved);
    for(unsigned i=0;i<3;++i) RELEASE(saved[i]);
    if(FAILED(ID3D11Device_GetDeviceRemovedReason(g->device))) return NULL;
    if(g->validate) {
        ID3D11DeviceContext_CopyResource(g->context,(ID3D11Resource *)g->staging,(ID3D11Resource *)g->output);
        D3D11_MAPPED_SUBRESOURCE map={0};
        if(FAILED(ID3D11DeviceContext_Map(g->context,(ID3D11Resource *)g->staging,0,D3D11_MAP_READ,0,&map))) return NULL;
        bool same=true;
        for(unsigned y=0;y<f->height && same;++y) for(unsigned x=0;x<f->width;++x) {
            uint32_t value=((const uint32_t *)((const uint8_t *)map.pData+y*map.RowPitch))[x];
            value=0xff000000|((value&255)<<16)|(value&0xff00)|((value>>16)&255);
            uint32_t expected=ScRendererPixel(r,x,y)|0xff000000;
            if(value!=expected) {fprintf(stderr,"[gpu terrain] mismatch %u,%u: %08x != %08x\n",x,y,value,expected);same=false;break;}
        }
        ID3D11DeviceContext_Unmap(g->context,(ID3D11Resource *)g->staging,0);
        if(!same) return NULL;
        if(g->frames%60==0) fprintf(stderr,"[gpu terrain] pixels match %ux%u, %u deferred\n",f->width,f->height,f->deferred);
    }
    if(!g->frames) fprintf(stderr,"[gpu terrain] composing %ux%u, %u deferred\n",f->width,f->height,f->deferred);
    ++g->frames;return g->texture;
}
#else
ScGpuTerrain *ScGpuTerrainCreate(SDL_Renderer *renderer,bool linear_filter) {(void)renderer;(void)linear_filter;return NULL;}
SDL_Texture *ScGpuTerrainDraw(ScGpuTerrain *g,const ScRenderer *r) {(void)g;(void)r;return NULL;}
void ScGpuTerrainDestroy(ScGpuTerrain *g) {(void)g;}
#endif
