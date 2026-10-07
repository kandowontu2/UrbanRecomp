#pragma once
#include "sc_sdl_compat.h"
#include <stddef.h>
typedef void (*ScMobileText)(SDL_Renderer *,int,int,int,const char *);
#ifdef SC_MOBILE
bool ScMobilePrepare(void);
bool ScMobileChooseRom(char *out,size_t n);
void ScMobileInit(SDL_Window *window);
bool ScMobileEvent(SDL_Window *window,const SDL_Event *event);
void ScMobileMouse(SDL_Window *window,double *x,double *y,uint32_t *buttons);
bool ScMobileTouchActive(void);
bool ScMobileBackground(void);
unsigned ScMobilePad(void);
void ScMobileDraw(SDL_Renderer *renderer,SDL_Window *window,ScMobileText text);
bool ScMobileImportRom(const void *data,unsigned bytes);
#else
static inline bool ScMobilePrepare(void) {return true;}
static inline bool ScMobileChooseRom(char *out,size_t n) {(void)out;(void)n;return false;}
static inline void ScMobileInit(SDL_Window *w) {(void)w;}
static inline bool ScMobileEvent(SDL_Window *w,const SDL_Event *e) {(void)w;(void)e;return false;}
static inline void ScMobileMouse(SDL_Window *w,double *x,double *y,uint32_t *b) {(void)w;(void)x;(void)y;(void)b;}
static inline bool ScMobileTouchActive(void) {return false;}
static inline bool ScMobileBackground(void) {return false;}
static inline unsigned ScMobilePad(void) {return 0;}
static inline void ScMobileDraw(SDL_Renderer *r,SDL_Window *w,ScMobileText t) {(void)r;(void)w;(void)t;}
#endif
