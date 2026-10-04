#pragma once
#include "sc_world.h"
#include "sc_vehicles.h"
typedef struct ScFleet ScFleet;
ScFleet *ScFleetCreate(void);
void ScFleetFree(ScFleet *fleet);
void ScFleetStep(ScFleet *fleet,const ScWorld *world,uint64_t frame,bool moving);
/* Original sprite artwork, world placements independent of native OAM slots. */
unsigned ScFleetShown(const ScFleet *fleet,const ScWorld *world,const uint8_t *rom,
    int native_x,int native_y,int left,int top,int right,int bottom,
    ScVehicleSprite *sprites,unsigned capacity);
unsigned ScFleetCount(const ScFleet *fleet,unsigned type);
