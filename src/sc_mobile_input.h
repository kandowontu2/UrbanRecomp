#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { SC_TOUCH_BUTTONS=18, SC_TOUCH_FINGERS=10 };
typedef struct ScTouchRect { float x,y,w,h; const char *label; unsigned pad; int action; } ScTouchRect;
typedef struct ScTouchFinger { int64_t id; float x,y; int button; bool active; } ScTouchFinger;
typedef struct ScTouchInput {
    ScTouchRect buttons[SC_TOUCH_BUTTONS];
    ScTouchFinger fingers[SC_TOUCH_FINGERS];
    bool visible,pan,multi_pan; int width,height,action;
    float mouse_x,mouse_y; unsigned mouse_buttons,pad;
} ScTouchInput;
void ScTouchLayout(ScTouchInput *input,int width,int height);
void ScTouchUpdate(ScTouchInput *input,int64_t id,float x,float y,bool down,bool begin);
void ScTouchClear(ScTouchInput *input);
bool ScTouchActive(const ScTouchInput *input);
bool ScMobileRomValid(const void *data,unsigned bytes,unsigned *offset);
