#include "sc_mouse_ui.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t r[0x20000], before[0x20000];
static unsigned word(unsigned a) { return r[a] | (r[a+1]<<8); }
static void put(unsigned a,unsigned v) { r[a]=(uint8_t)v; r[a+1]=(uint8_t)(v>>8); }
static bool point(int x,int y) {
  ScMouseUiResult s=ScMouseUiPoint(r,x,y,true,true); assert(s.handled); return s.hit;
}
int main(void) {
  put(0x14,3); put(0x3e,1);
  assert(point(100,142) && word(0x3e)==2);
  assert(!point(100,100) && word(0x3e)==2);
  assert(!point(100,134)); /* gap between rows */
  assert(point(180,164) && word(0x3e)==3); /* Journey below New City */
  assert(point(180,188) && word(0x3e)==4); /* Scenario moved down */
  put(0x44,1); assert(point(100,108) && word(0x3e)==0);
  assert(point(180,178) && word(0x3e)==3);
  assert(point(180,202) && word(0x3e)==4);
  memcpy(before,r,sizeof r); ScMouseUiPoint(r,100,156,false,true);
  assert(!memcmp(before,r,sizeof r)); /* stationary pointer permits pad use */
  put(0x14,5);
  assert(point(210,96) && word(0xb2d)==0);
  put(0xb31,0x81); assert(point(210,120) && word(0xb2d)==1 && word(0xb31)==0x80);
  assert(point(218,180) && word(0xb2d)==2);
  assert(point(202,189) && word(0xb2d)==7);
  assert(!point(195,180));
  put(0x14,7);
  assert(point(41,112) && word(0x4a)==0 && word(0x4c)==0);
  assert(point(201,113) && word(0x4a)==10 && word(0x4c)==0); /* CLR */
  assert(point(230,143) && word(0x4a)==10 && word(0x4c)==1); /* backspace */
  assert(point(209,161) && word(0x4a)==9 && word(0x4c)==3); /* END */
  assert(point(150,180) && word(0x4c)==4); /* SPACE */
  assert(!point(48,180) && !point(232,160));
  put(0x14,9); assert(point(160,70) && word(0xb57)==2);
  assert(point(220,168) && word(0xb57)==2); /* END keeps the selected difficulty. */
  assert(!point(110,70));
  put(0x14,22); assert(point(140,75) && word(0x36)==1);
  assert(point(100,75) && word(0x36)==0); assert(!point(100,80));
  assert(point(220,168) && word(0x36)==0);
  put(0x36,1); assert(point(220,168) && word(0x36)==1); /* END also confirms No. */
  put(0x14,11); put(0x42,0); put(0x16,0);
  assert(point(100,130) && word(0x52)==1 && word(0x54)==1 && word(0x40)==4);
  assert(!point(82,80));
  put(0x52,2); assert(!ScMouseUiScenarioScroll(r,1,true)); /* locked */
  put(0x42,0x8000); assert(ScMouseUiScenarioScroll(r,1,true));
  assert(word(0x40)==7 && word(0x22)==80);
  assert(ScMouseUiScenarioScroll(r,1,true)); assert(word(0x40)==8 && word(0x54)==0);
  put(0x16,160); assert(point(180,50) && word(0x40)==8);
  assert(!point(180,130)); /* Sylt has no second row */
  put(0x14,17); put(0x44,2);
  assert(!point(100,140)); assert(point(100,170) && word(0x421)==1);
  put(0x14,0x8000); assert(!ScMouseUiPoint(r,100,100,true,true).handled);
  put(0x3e,2); put(0x0ab5,0xffff); put(0x0101,0xffff); put(0x0379,0xff);
  assert(ScMouseUiPoint(r,100,100,true,true).handled); /* native modal cursor */
  /* The park/casino gift also opens automatically, without toolbar flags. */
  put(0x0ab5,0); put(0x0101,0); put(0x0397,0x0d); put(0x039b,0x0d); r[0x391]=0xff;
  assert(point(84,140) && word(0x037f)==1);
  assert(point(44,140) && word(0x037f)==0);
  assert(!point(20,150) && !point(64,150) && !point(80,160));
  r[0x391]=0; assert(!ScMouseUiPoint(r,84,140,true,true).handled);
  r[0x391]=0xff; put(0x039b,1);
  assert(!ScMouseUiPoint(r,84,140,true,true).handled); /* queued message */
  ScMouseDialog dialog=SC_MOUSE_DIALOG_NONE;
  put(0x1ffe,0xc8fc); ScMouseUiObserve(&dialog,r,0,0xd19e,0x1ffd);
  assert(dialog==SC_MOUSE_DIALOG_SLOTS);
  assert(ScMouseUiDialogPoint(dialog,r,96,140,true).hit && word(0x421)==1);
  assert(ScMouseUiDialogPoint(dialog,r,136,140,true).hit && word(0x421)==2);
  assert(!ScMouseUiDialogPoint(dialog,r,116,140,true).hit);
  assert(ScMouseUiDialogPoint(dialog,r,176,140,true).hit && word(0x421)==0);
  ScMouseUiObserve(&dialog,r,0,0xd1fc,0x1ffd); assert(dialog==SC_MOUSE_DIALOG_NONE);
  put(0x1ffe,0xc963); ScMouseUiObserve(&dialog,r,0,0xd19e,0x1ffd);
  assert(dialog==SC_MOUSE_DIALOG_SAVE_CONFIRM);
  assert(ScMouseUiDialogPoint(dialog,r,96,140,true).hit && word(0x421)==1);
  assert(ScMouseUiDialogPoint(dialog,r,136,140,true).hit && word(0x421)==2);
  ScMouseUiObserve(&dialog,r,0,0xd20a,0x1ffd); assert(dialog==SC_MOUSE_DIALOG_NONE);
  put(0x1ffe,0x1234); ScMouseUiObserve(&dialog,r,0,0xd19e,0x1ffd);
  assert(dialog==SC_MOUSE_DIALOG_NONE);
  ScMouseUiObserve(&dialog,r,1,0xcc1a,0);
  r[0x3f5]=1; r[0x3f6]=0; r[0x3f7]=4; r[0x3f8]=6;
  assert(ScMouseUiDialogPoint(dialog,r,80,150,true).hit && word(0x3f3)==0);
  assert(!ScMouseUiDialogPoint(dialog,r,120,150,true).hit); /* Empty gift. */
  assert(ScMouseUiDialogPoint(dialog,r,76,170,true).hit && word(0x3f3)==2);
  assert(ScMouseUiDialogPoint(dialog,r,116,170,true).hit && word(0x3f3)==3);
  assert(!ScMouseUiDialogPoint(dialog,r,100,170,true).hit);
  memcpy(before,r,sizeof r); ScMouseUiDialogPoint(dialog,r,80,150,false);
  assert(!memcmp(before,r,sizeof r));
  ScMouseUiObserve(&dialog,r,1,0xcc3a,0); assert(dialog==SC_MOUSE_DIALOG_NONE);
  puts("PASS: menu hit regions, gaps, pad coexistence, saved-slot and scenario gates, name keys and map controls");
}
