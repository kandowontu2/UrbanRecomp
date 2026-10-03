#pragma once
#include "sc_stencil.h"
typedef struct SDL_Renderer SDL_Renderer;
typedef struct ScGpuFields ScGpuFields;
ScGpuFields *ScGpuFieldsCreate(SDL_Renderer *renderer);
void ScGpuFieldsDestroy(ScGpuFields *fields);
void ScGpuFieldsBegin(void *fields,const ScWorld *world,unsigned source);
/* One nonblocking completion query per publication span while pending. */
const uint32_t *ScGpuFieldsData(void *fields,const ScWorld *world,unsigned source);
/* Called once per host frame. Never waits for an in-flight GPU fence. */
void ScGpuFieldsPoll(ScGpuFields *fields);
