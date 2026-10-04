#pragma once
#include "sc_stencil.h"
typedef struct SDL_Renderer SDL_Renderer;
typedef struct ScGpuFields ScGpuFields;
ScGpuFields *ScGpuFieldsCreate(SDL_Renderer *renderer);
void ScGpuFieldsDestroy(ScGpuFields *fields);
void ScGpuFieldsBegin(void *fields,const ScWorld *world,unsigned source);
/* One nonblocking completion query per publication span while pending. */
const uint32_t *ScGpuFieldsData(void *fields,const ScWorld *world,unsigned source);
void ScGpuFieldsCrimeBegin(void *fields,const ScWorld *world,unsigned bias);
/* Immutable snapshot results. The publisher must match each sample's source
 * identity against current land/density/police values before committing it. */
const ScCrimeSample *ScGpuFieldsCrimeData(void *fields,const ScWorld *world,unsigned bias);
void ScGpuFieldsLandBegin(void *fields,const ScWorld *world);
/* Match all four raw source tiles before native publication. */
const ScLandSummary *ScGpuFieldsLandData(void *fields,const ScWorld *world);
/* Called once per host frame. Never waits for an in-flight GPU fence. */
void ScGpuFieldsPoll(ScGpuFields *fields);
