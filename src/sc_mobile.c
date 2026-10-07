#include "sc_mobile.h"
#include "sc_mobile_input.h"
#include "sc_sram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdatomic.h>

static ScTouchInput touch;
static SDL_Gamepad *gamepad;
static bool background;
static atomic_int picked;
bool ScMobilePrepare(void) {
#ifdef __ANDROID__
    const char *data=SDL_GetAndroidInternalStoragePath();
#else
    extern const char *ScIosDataPath(void);
    const char *data=ScIosDataPath();
#endif
    if(!data||chdir(data))return false;
    fprintf(stderr,"[mobile] saves/settings: %s\n",data);
    setenv("SC_RESTORED_MUSIC","music/restored",0);
#ifndef __ANDROID__
    const char *base=SDL_GetBasePath();char music[4096];
    snprintf(music,sizeof music,"%smusic/restored",base?base:"");setenv("SC_RESTORED_MUSIC",music,1);
    if(base) {char path[4096];snprintf(path,sizeof path,"%ssylt_graphics",base);setenv("SC_SYLT_DIR",path,0);}
#endif
    return true;
}
bool ScMobileImportRom(const void *data,unsigned bytes) {
    unsigned offset;
    if(!ScMobileRomValid(data,bytes,&offset))return false;
    FILE *f=fopen("simcity-us.sfc.tmp","wb");if(!f)return false;
    bool ok=fwrite((const unsigned char *)data+offset,1,0x80000,f)==0x80000;
    if(fclose(f))ok=false;
    if(ok)ok=rename("simcity-us.sfc.tmp","simcity-us.sfc")==0;
    if(!ok)remove("simcity-us.sfc.tmp");return ok;
}
#ifdef __ANDROID__
static void picked_file(void *unused,const char *const *files,int filter) {
    (void)unused;(void)filter;
    if(!files||!files[0]) {atomic_store(&picked,-1);return;}
    SDL_IOStream *stream=SDL_IOFromFile(files[0],"rb");
    Sint64 size=stream?SDL_GetIOSize(stream):-1;
    void *data=(size==0x80000||size==0x80200)?SDL_malloc((size_t)size):NULL;
    bool ok=data&&SDL_ReadIO(stream,data,(size_t)size)==(size_t)size&&ScMobileImportRom(data,(unsigned)size);
    if(stream)SDL_CloseIO(stream);
    SDL_free(data);atomic_store(&picked,ok?1:-2);
}
#else
extern void ScIosPickRom(SDL_Window *window,atomic_int *result);
#endif
bool ScMobileChooseRom(char *out,size_t n) {
    if(!SDL_Init(SDL_INIT_VIDEO))return false;
    SDL_Window *w=SDL_CreateWindow("UrbanRecomp Enhanced",800,450,SDL_WINDOW_FULLSCREEN);
    SDL_Renderer *r=w?SDL_CreateRenderer(w,NULL):NULL;
    if(!r) {if(w)SDL_DestroyWindow(w);return false;}
    bool quit=false,open=true;
    while(!quit) {
        if(open) {open=false;atomic_store(&picked,0);
#ifdef __ANDROID__
            SDL_ShowOpenFileDialog(picked_file,NULL,w,NULL,0,NULL,false);
#else
            ScIosPickRom(w,&picked);
#endif
        }
        SDL_Event event;while(SDL_PollEvent(&event)) {
            if(event.type==SDL_EVENT_QUIT)quit=true;
            if(event.type==SDL_EVENT_MOUSE_BUTTON_UP||event.type==SDL_EVENT_FINGER_UP)open=true;
        }
        int state=atomic_load(&picked);
        if(state==1) {snprintf(out,n,"simcity-us.sfc");break;}
        int width,height;SDL_GetRenderOutputSize(r,&width,&height);
        SDL_SetRenderDrawColor(r,30,23,8,255);SDL_RenderClear(r);
        float scale=width>=800?2:1;SDL_SetRenderScale(r,scale,scale);
        SDL_SetRenderDrawColor(r,255,239,177,255);
        SDL_RenderDebugText(r,24,32,"UrbanRecomp Enhanced");
        SDL_RenderDebugText(r,24,60,"Select your own clean US SimCity SNES ROM.");
        SDL_RenderDebugText(r,24,90,state==-2?"ROM not recognized. Tap to choose another file.":"Tap to open Files. No ROM is included.");
        SDL_SetRenderScale(r,1,1);SDL_RenderPresent(r);SDL_Delay(16);
    }
    bool ok=atomic_load(&picked)==1;SDL_DestroyRenderer(r);SDL_DestroyWindow(w);
    return ok;
}
static void layout(SDL_Window *window) {
    int w,h;SDL_GetWindowSize(window,&w,&h);
    SDL_Rect safe={0,0,w,h};SDL_GetWindowSafeArea(window,&safe);
    if(safe.w<=0||safe.h<=0)safe=(SDL_Rect){0,0,w,h};
    ScTouchLayout(&touch,safe.w,safe.h);
    for(unsigned i=0;i<SC_TOUCH_BUTTONS;++i) {touch.buttons[i].x+=safe.x;touch.buttons[i].y+=safe.y;}
}
void ScMobileInit(SDL_Window *window) {
    touch.visible=true;layout(window);
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"1");
    SDL_SetWindowFullscreen(window,true);
}
static void key(SDL_Window *w,SDL_Scancode sc) {
    SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.windowID=SDL_GetWindowID(w);e.key.scancode=sc;e.key.down=true;SDL_PushEvent(&e);
    e.type=SDL_EVENT_KEY_UP;e.key.down=false;SDL_PushEvent(&e);
}
bool ScMobileEvent(SDL_Window *window,const SDL_Event *e) {
    if(e->type==SDL_EVENT_KEY_DOWN&&e->key.scancode==SDL_SCANCODE_AC_BACK) {key(window,SDL_SCANCODE_ESCAPE);return true;}
    if(e->type==SDL_EVENT_WILL_ENTER_BACKGROUND||e->type==SDL_EVENT_DID_ENTER_BACKGROUND) {
        background=true;ScTouchClear(&touch);ScSram_Flush();return true;
    }
    if(e->type==SDL_EVENT_DID_ENTER_FOREGROUND) {background=false;ScTouchClear(&touch);return true;}
    if(e->type==SDL_EVENT_GAMEPAD_REMOVED&&gamepad&&SDL_GetGamepadID(gamepad)==e->gdevice.which) {SDL_CloseGamepad(gamepad);gamepad=NULL;}
    if(e->type==SDL_EVENT_FINGER_DOWN||e->type==SDL_EVENT_FINGER_MOTION||e->type==SDL_EVENT_FINGER_UP||e->type==SDL_EVENT_FINGER_CANCELED) {
        int w,h;SDL_GetWindowSize(window,&w,&h);layout(window);
        ScTouchUpdate(&touch,e->tfinger.fingerID,e->tfinger.x*w,e->tfinger.y*h,
            e->type!=SDL_EVENT_FINGER_UP&&e->type!=SDL_EVENT_FINGER_CANCELED,e->type==SDL_EVENT_FINGER_DOWN);
        if(touch.action) {
            int action=touch.action;touch.action=0;
            if(action==1)key(window,SDL_SCANCODE_F12);
            else if(action==4)key(window,SDL_SCANCODE_ESCAPE);
            else if(action==2||action==3) {SDL_Event zoom={0};zoom.type=SDL_EVENT_PINCH_UPDATE;
                zoom.pinch.windowID=SDL_GetWindowID(window);zoom.pinch.scale=action==2?0.8f:1.25f;SDL_PushEvent(&zoom);}
        }
        return true;
    }
    // The game samples touch positions below; discard SDL's duplicate click
    // events for control-owned fingers so they cannot activate HUD tools.
    if((e->type==SDL_EVENT_MOUSE_BUTTON_DOWN||e->type==SDL_EVENT_MOUSE_BUTTON_UP)&&e->button.which==SDL_TOUCH_MOUSEID&&ScTouchActive(&touch)&&!touch.mouse_buttons)return true;
    return false;
}
void ScMobileMouse(SDL_Window *window,double *x,double *y,uint32_t *buttons) {
    (void)window;
    // Also clear the generated left button on a control release frame.
    if(ScTouchActive(&touch)||SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK) {
        if(ScTouchActive(&touch)) {
            *buttons=touch.mouse_buttons==2?SDL_BUTTON_MMASK:touch.mouse_buttons==1?SDL_BUTTON_LMASK:0;
            if(touch.mouse_buttons) {*x=touch.mouse_x;*y=touch.mouse_y;}
            else {*x=-1000;*y=-1000;}
        }
    }
}
bool ScMobileTouchActive(void) {return ScTouchActive(&touch);}
bool ScMobileBackground(void) {return background;}
unsigned ScMobilePad(void) {
    if(!gamepad) {int count;SDL_JoystickID *ids=SDL_GetGamepads(&count);if(count)gamepad=SDL_OpenGamepad(ids[0]);SDL_free(ids);}
    unsigned pad=touch.pad;
    if(gamepad) {
        const SDL_GamepadButton b[]={SDL_GAMEPAD_BUTTON_SOUTH,SDL_GAMEPAD_BUTTON_EAST,SDL_GAMEPAD_BUTTON_WEST,SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,SDL_GAMEPAD_BUTTON_START,SDL_GAMEPAD_BUTTON_BACK,
            SDL_GAMEPAD_BUTTON_DPAD_UP,SDL_GAMEPAD_BUTTON_DPAD_DOWN,SDL_GAMEPAD_BUTTON_DPAD_LEFT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
        const unsigned mask[]={1,0x100,2,0x200,0x400,0x800,8,4,0x10,0x20,0x40,0x80};
        for(unsigned i=0;i<12;++i)if(SDL_GetGamepadButton(gamepad,b[i]))pad|=mask[i];
        int x=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTX),y=SDL_GetGamepadAxis(gamepad,SDL_GAMEPAD_AXIS_LEFTY);
        if(x<-16000)pad|=0x40;if(x>16000)pad|=0x80;if(y<-16000)pad|=0x10;if(y>16000)pad|=0x20;
    }
    return pad;
}
void ScMobileDraw(SDL_Renderer *renderer,SDL_Window *window,ScMobileText text) {
    int w,h,rw,rh;SDL_GetWindowSize(window,&w,&h);SDL_GetRenderOutputSize(renderer,&rw,&rh);layout(window);
    if(w<=0||h<=0)return;
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    for(int i=0;i<SC_TOUCH_BUTTONS;++i) {
        if(!touch.visible&&i!=17)continue;ScTouchRect b=touch.buttons[i];
        SDL_FRect r={b.x*rw/w,b.y*rh/h,b.w*rw/w,b.h*rh/h};
        SDL_SetRenderDrawColor(renderer,24,20,8,(touch.pad&b.pad)||(i==16&&touch.pan)?220:150);SDL_RenderFillRect(renderer,&r);
        SDL_SetRenderDrawColor(renderer,244,227,164,220);SDL_RenderRect(renderer,&r);
        int px=(int)(r.w/30);if(px<1)px=1;int tw=(int)strlen(b.label)*6*px-px;
        text(renderer,(int)(r.x+(r.w-tw)/2),(int)(r.y+(r.h-5*px)/2),px,b.label);
    }
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
}
