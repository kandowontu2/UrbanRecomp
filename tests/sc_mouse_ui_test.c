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
  ScMouseUiPointer pointer={0};
  ScMouseUiPointerUpdate(&pointer,true,true,false,false,117,83);
  assert(pointer.active && pointer.x==117 && pointer.y==83);
  ScMouseUiPointerUpdate(&pointer,true,false,false,false,117,83);assert(pointer.active);
  ScMouseUiPointerUpdate(&pointer,true,false,false,true,117,83);assert(!pointer.active);
  ScMouseUiPointerUpdate(&pointer,true,false,true,false,118,84);assert(pointer.active);
  ScMouseUiPointerUpdate(&pointer,false,true,false,false,117,83);assert(!pointer.active);
  const unsigned modes[]={2,3,5,6,7,9,11,17,18,22};
  for(unsigned i=0;i<sizeof modes/sizeof *modes;++i) {put(0x14,modes[i]);assert(ScMouseUiPointerScreen(r));}
  put(0x14,0);assert(!ScMouseUiPointerScreen(r));
  r[0x391]=255;assert(ScMouseUiPointerScreen(r));r[0x391]=0;
  put(0x379,255);assert(ScMouseUiPointerScreen(r));put(0x379,0);
  uint16_t oam[256],original[256];uint8_t high[32],high_before[32];
  for(unsigned i=0;i<256;++i)oam[i]=original[i]=(uint16_t)(i*113);
  memset(high,0,sizeof high);oam[0]=0x5080;oam[1]=0x31ec;
  memcpy(original,oam,sizeof oam);memcpy(high_before,high,sizeof high);
  assert(ScMouseUiCursorPlace(r,oam,high,117,83)==0);
  assert(oam[0]==(117|(83<<8)) && !memcmp(oam+1,original+1,sizeof oam-2));
  assert(!memcmp(high,high_before,sizeof high));
  assert(ScMouseUiCursorPlace(r,oam,high,-3,500)==0 && oam[0]==(223<<8));
  /* Title-to-menu screen 2 must not inject a hand into the logo CHR bank. */
  put(0x14,2);memcpy(original,oam,sizeof oam);memcpy(high_before,high,sizeof high);
  assert(ScMouseUiCursorPlace(r,oam,high,100,87)==-1);
  assert(!memcmp(oam,original,sizeof oam) && !memcmp(high,high_before,sizeof high));
  oam[0]=0x702a;oam[1]=0x34b9;
  assert(ScMouseUiCursorPlace(r,oam,high,100,87)==127);
  assert(oam[254]==(99|(86<<8)) && oam[255]==0x3f9e);
  /* Main-menu arrow remains beside its option; mouse gets a separate hand. */
  put(0x14,3);oam[1]=0x34b9;oam[128]=0x7c2a;oam[129]=0x30c2;
  memcpy(original,oam,sizeof oam);
  assert(ScMouseUiCursorPlace(r,oam,high,100,87)==127);
  assert(oam[254]==(99|(86<<8)) && oam[255]==0x3f9e);
  assert(!memcmp(oam,original,254*2));
  /* Scenario pins and frame remain intact; its extra mouse hand is private. */
  put(0x14,11);memcpy(original,oam,sizeof oam);
  assert(ScMouseUiCursorPlace(r,oam,high,99,88)==127);
  assert(oam[254]==(98|(87<<8)) && oam[255]==0x3f9e);
  assert(!memcmp(oam,original,254*2) && (high[31]&192)==128);
  /* No cursor yet: do not reposition an arbitrary native sprite. */
  put(0x14,5);oam[129]=0;oam[255]=0;
  memcpy(original,oam,sizeof oam);
  assert(ScMouseUiCursorPlace(r,oam,high,77,66)==-1 && !memcmp(oam,original,sizeof oam));
  ScMousePan pan={0};ScMousePanDelta d;
  d=ScMousePanUpdate(&pan,true,true,true,100,100,.5,.25);
  assert(pan.active && !d.x && !d.y);
  d=ScMousePanUpdate(&pan,true,true,true,132,132,.5,.25);
  assert(d.x==-16 && d.y==-8); /* direct DPI/zoom conversion; land follows drag */
  for(unsigned i=0;i<1000;++i) {
    d=ScMousePanUpdate(&pan,true,true,false,132,132,.5,.25);
    assert(!d.x && !d.y && pan.active); /* no drift, debt or delayed camera steps */
  }
  d=ScMousePanUpdate(&pan,true,true,false,-100,-100,.5,.25);
  assert(d.x==116 && d.y==58 && pan.active);
  d=ScMousePanUpdate(&pan,true,false,false,-100,-100,.5,.25);
  assert(!pan.active && !d.x && !d.y);
  ScMousePanUpdate(&pan,true,true,false,100,100,1,1);
  assert(!pan.active);
  ScMousePanUpdate(&pan,true,true,true,100,100,1,1);
  d=ScMousePanUpdate(&pan,false,true,true,92,100,1,1);
  assert(!pan.active && !d.x && !d.y);
  for(int i=0;i<4;++i) {
    const double scale=i==0?.25:i==1?1:i==2?2:4;
    pan=(ScMousePan){0};
    ScMousePanUpdate(&pan,true,true,true,100,100,scale,scale);
    d=ScMousePanUpdate(&pan,true,true,true,116,84,scale,scale);
    assert(d.x==-16*scale && d.y==16*scale);
    d=ScMousePanUpdate(&pan,true,true,true,116.25,83.75,scale,scale);
    assert(d.x==-.25*scale && d.y==.25*scale); /* sub-tile motion retained */
    d=ScMousePanUpdate(&pan,true,true,true,116.25,83.75,scale,scale);
    assert(!d.x && !d.y);
  }
  put(0x14,3); put(0x3e,1);
  assert(point(100,142) && word(0x3e)==2);
  assert(point(42,114) && word(0x3e)==1); /* shifted native pointer */
  assert(!point(39,114) && word(0x3e)==1);
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
  assert(point(195,180) && word(0xb2d)==8);
  assert(point(185,189) && word(0xb2d)==11);
  assert(!point(183,180));
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
  /* Annual budget has no toolbar modal flags. Its free mouse must hit the
   * arrows directly and reject gaps without moving or snapping the pointer. */
  put(0xab5,0);put(0x101,0);put(0x379,0);put(0x1fb,2);r[0xe3]=r[0xc3]=1;
  assert(ScMouseUiBudgetLive(r));put(0x1eb,119);put(0x1ed,73);
  const int budget_rows[]={52,96,112,128};
  for(int row=0;row<4;++row)for(int col=0;col<2;++col)
    for(int y=budget_rows[row]-6;y<budget_rows[row]+8;++y)
      for(int x=204+col*12;x<216+col*12;++x) {
        assert(point(x,y) && word(0xd67)==(unsigned)(row*2+col));
        assert(word(0x1eb)==119 && word(0x1ed)==73);
      }
  put(0xd67,3);memcpy(before,r,sizeof r);
  assert(ScMouseUiPoint(r,210,128,false,true).hit && !memcmp(before,r,sizeof r));
  assert(!point(120,73) && !point(232,100) && !point(215,85) && word(0xd67)==3);
  assert(point(72,195) && word(0xd67)==8 && point(191,211));
  assert(!point(71,200) && !point(192,208) && !point(128,194) && !point(128,212));
  put(0x379,255);put(0xab5,65535);put(0x101,65535);
  assert(ScMouseUiBudgetLive(r) && point(220,52) && word(0xd67)==1);
  put(0x1fb,3);assert(!ScMouseUiBudgetLive(r));
  put(0x1fb,2);r[0xe3]=255;assert(!ScMouseUiBudgetLive(r));
  put(0x1fb,0);r[0xe3]=r[0xc3]=0;
  /* The park/casino gift also opens automatically, without toolbar flags. */
  put(0x0ab5,0); put(0x0101,0); put(0x0397,0x0d); put(0x039b,0x0d); r[0x391]=0xff;
  assert(point(84,140) && word(0x037f)==1);
  assert(point(44,140) && word(0x037f)==0);
  assert(!point(20,150) && !point(64,150) && !point(80,167));
  for(int i=0;i<2;++i) for(int yy=135;yy<167;++yy) for(int xx=32+i*40;xx<64+i*40;++xx)
    assert(point(xx,yy) && word(0x37f)==(unsigned)i);
  put(0x397,1);put(0x39b,1);assert(point(240,200)); /* Single gift: dismiss. */
  assert(!point(256,200));put(0x397,0x0d);put(0x39b,0x0d);
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
  assert(ScMouseUiDialogPoint(dialog,r,76,180,true).hit && word(0x3f3)==2);
  assert(ScMouseUiDialogPoint(dialog,r,116,180,true).hit && word(0x3f3)==3);
  assert(!ScMouseUiDialogPoint(dialog,r,100,170,true).hit);
  for(int i=0;i<4;++i) for(int yy=128+(i/2)*40;yy<160+(i/2)*40;++yy)
    for(int xx=64+(i&1)*40;xx<96+(i&1)*40;++xx)
      assert(ScMouseUiDialogPoint(dialog,r,xx,yy,true).hit==(i!=1));
  assert(!ScMouseUiDialogPoint(dialog,r,80,164,true).hit); /* Row gap. */
  memcpy(before,r,sizeof r); ScMouseUiDialogPoint(dialog,r,80,150,false);
  assert(!memcmp(before,r,sizeof r));
  ScMouseUiObserve(&dialog,r,1,0xcc3a,0); assert(dialog==SC_MOUSE_DIALOG_NONE);
  puts("PASS: direct grab-and-drag panning, zoom/DPI and fractional movement, stationary holds, captured exit/release, focus/modal cancellation; free budget pointer/arrow/button regions, gaps, pad coexistence, saved-slot and scenario gates, name keys and map controls");
}
