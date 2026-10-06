#ifndef SC_SCENARIO_EVENT_H
#define SC_SCENARIO_EVENT_H
#include <stdbool.h>
#include <stdint.h>
struct Interp816;
#include "sc_world.h"
typedef struct {
    bool armed;
    uint16_t mode, scenario, countdown, armed_countdown;
    unsigned event;
    int nuclear_x,nuclear_y;
} ScScenarioEvent;
bool ScScenarioEventArm(ScScenarioEvent *event,uint8_t *ram,const ScWorld *world,unsigned scenario,uint16_t countdown);
void ScScenarioEventStep(const ScScenarioEvent *event,struct Interp816 *cpu,const uint8_t *ram,ScWorld *world);
bool ScScenarioEventTick(ScScenarioEvent *event,uint8_t *ram);
enum { SC_DISASTER_BUTTON_Y=112,SC_DISASTER_BUTTON_H=24,SC_DISASTER_BUTTON_W=36 };
bool ScScenarioMenuLive(const uint8_t *ram);
int ScScenarioMenuButton(int x,int y);
void ScScenarioMenuStep(struct Interp816 *cpu,const uint8_t *ram);
#endif
