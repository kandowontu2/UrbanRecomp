#include "sc_mouse_ui.h"
#include "sc_city_setup.h"
#include <math.h>
ScMousePanDelta ScMousePanUpdate(ScMousePan *p,bool allowed,bool held,bool on_land,
    double x,double y,double scale_x,double scale_y) {
  ScMousePanDelta delta={0,0};
  if(!allowed || !held || (!p->active && !on_land) || scale_x<=0 || scale_y<=0 ||
      !isfinite(x) || !isfinite(y) || !isfinite(scale_x) || !isfinite(scale_y)) {
    *p=(ScMousePan){0};return delta;
  }
  if(!p->active) {*p=(ScMousePan){.active=true,.x=x,.y=y};return delta;}
  delta.x=-(x-p->x)*scale_x;delta.y=-(y-p->y)*scale_y;
  p->x=x;p->y=y;return delta;
}

static unsigned word(const uint8_t *r, unsigned a) {
  return r[a] | (r[a + 1] << 8);
}
static void put(uint8_t *r, unsigned a, unsigned v) {
  r[a] = (uint8_t)v; r[a + 1] = (uint8_t)(v >> 8);
}
static bool box(int x, int y, int bx, int by, int w, int h) {
  return x >= bx && x < bx + w && y >= by && y < by + h;
}
void ScMouseUiObserve(ScMouseDialog *dialog, const uint8_t *r,
                      unsigned bank, unsigned pc, unsigned sp) {
  if(bank==0 && pc==0xd19e) {
    switch(word(r,(sp+1)&65535)) {
    case 0xc8fc: case 0xc99d: case 0xca4b: *dialog=SC_MOUSE_DIALOG_SLOTS; break;
    case 0xc963: case 0xca11: *dialog=SC_MOUSE_DIALOG_SAVE_CONFIRM; break;
    default: *dialog=SC_MOUSE_DIALOG_NONE; break;
    }
  }
  if(bank==0 && (pc==0xd1fc || pc==0xd20a)) *dialog=SC_MOUSE_DIALOG_NONE;
  if(bank==1 && pc==0xcc1a) *dialog=SC_MOUSE_DIALOG_GIFTS;
  if(bank==1 && pc==0xcc3a) *dialog=SC_MOUSE_DIALOG_NONE;
}
ScMouseUiResult ScMouseUiDialogPoint(ScMouseDialog dialog, uint8_t *r,
                                    int x, int y, bool select) {
  ScMouseUiResult result={dialog!=SC_MOUSE_DIALOG_NONE,false};
  int choice=-1;
  if(dialog==SC_MOUSE_DIALOG_GIFTS) {
    /* The four 32-pixel images use 40-pixel spacing on both axes.
     * 01:cca0 positions the hand nine pixels below each image's top.
     * Empty inventory slots cannot
     * become a selected/confirmed gift. */
    for(int i=0;i<4;++i)
      if(r[0x3f5+i] && box(x,y,64+(i&1)*40,128+(i/2)*40,32,32)) choice=i;
    if(choice>=0 && select) put(r,0x3f3,choice);
  } else if(dialog==SC_MOUSE_DIALOG_SLOTS || dialog==SC_MOUSE_DIALOG_SAVE_CONFIRM) {
    /* The slot buttons and Save? Yes/No use the same native button layout. */
    if(box(x,y,164,128,24,24)) choice=0;
    if(box(x,y,84,128,24,24)) choice=1;
    if(box(x,y,124,128,24,24)) choice=2;
    if(choice>=0 && select) put(r,0x421,choice);
  }
  result.hit=choice>=0;
  return result;
}
bool ScMouseUiScenarioScroll(uint8_t *r, int direction, bool ninth) {
  if (word(r, 0x14) != 11) return false;
  int maxcol = (word(r, 0x42) & 0x8000) ? (ninth ? 4 : 3) : 2;
  int col = (int)word(r, 0x52) + (direction < 0 ? -1 : 1);
  if (col < 0 || col > maxcol) return false;
  int row = col == 4 ? 0 : (word(r, 0x54) & 1);
  put(r, 0x52, col); put(r, 0x54, row);
  put(r, 0x40, col == 4 ? 8 : col == 3 ? 6 + row : row * 3 + col);
  put(r, 0x22, col >= 3 ? (col - 2) * 80 : col == 0 ? 0 : word(r, 0x22));
  return true;
}
bool ScMouseUiBudgetLive(const uint8_t *r) {
  unsigned mode=word(r,0x14);
  return (mode==0 || mode==0x8000) && word(r,0x3e) &&
      word(r,0x1fb)==2 && r[0xe3]==1 && r[0xc3]==1 && !r[0x391];
}
bool ScMouseUiMapNumberArrows(int x,int y) {
  return box(x,y,SC_MAP_NUMBER_X,SC_MAP_NUMBER_Y,SC_MAP_NUMBER_WIDTH,SC_MAP_NUMBER_HEIGHT);
}
ScMouseUiResult ScMouseUiPoint(uint8_t *r, int x, int y,
                              bool select, bool ninth) {
  ScMouseUiResult result = {true, false};
  const unsigned mode = word(r, 0x14);
  switch (mode) {
  case 0:
  case 0x8000:
    if(ScMouseUiBudgetLive(r)) {
      /* 02:ab6a's eight arrow positions and the Go With Figures button.
       * Absolute mouse coordinates never become D-pad movement. The guest
       * still owns arithmetic, held-button repeat and budget confirmation. */
      const int rows[]={52,96,112,128};int choice=-1;
      for(int row=0;row<4;++row)for(int col=0;col<2;++col)
        if(box(x,y,204+col*12,rows[row]-6,12,14))choice=row*2+col;
      if(box(x,y,72,195,120,17))choice=8;
      result.hit=choice>=0;
      if(result.hit && select)put(r,0xd67,(unsigned)choice);
      break;
    }
    /* 01:a507..a5f8 brackets the adviser dialogue with the byte $0391.
     * Gift 0d is the amusement-park/casino choice, including when it opens
     * automatically outside a toolbar modal. $039b identifies the displayed
     * message so a queued or dismissed gift cannot take over city input. */
    if (r[0x0391] && word(r, 0x0397) == word(r, 0x039b)) {
      if (word(r, 0x039b) != 0x0d) {
        result.hit=box(x,y,0,0,256,224); /* Native B dismisses the message. */
        break;
      }
      for (int i = 0; i < 2; ++i) {
        if (!box(x, y, 32 + i * 40, 135, 32, 32)) continue;
        result.hit = true;
        if (select) put(r, 0x037f, i);
      }
      break;
    }
    /* The city's settings, options, tax/budget and file pages already hit-test
     * $01eb/$01ed in 01:aecc. Do not add D-pad travel to the absolute pointer.
     * 01:9d6b raises these flags for the duration of its modal page. */
    if (word(r, 0x3e) && word(r, 0x0ab5) && word(r, 0x0101) && word(r, 0x0379)) {
      result.hit = true;
    } else result.handled = false;
    break;
  case 5: { /* Map preview: buttons and five pairs of digit arrows. */
    int choice = -1;
    for(unsigned type=0;type<2;++type) {
      int top=(type?SC_MAP_LAND_Y:SC_MAP_GENERATION_Y)-1;
      if(box(x,y,SC_MAP_GENERATION_LEFT_X-4,top,16,10))choice=type?SC_MAP_LAND_LEFT:SC_MAP_GENERATION_LEFT;
      if(box(x,y,SC_MAP_GENERATION_LEFT_X+12,top,
          SC_MAP_GENERATION_RIGHT_X-SC_MAP_GENERATION_LEFT_X-4,10))choice=type?SC_MAP_LAND_RIGHT:SC_MAP_GENERATION_RIGHT;
    }
    if (box(x, y, 192, 88, 32, 16)) choice = 0;
    if (box(x, y, 192, 112, 32, 16)) choice = 1;
    for (int digit = 0; digit < 5; ++digit) {
      if (box(x, y, SC_MAP_NUMBER_X+32-digit*8, SC_MAP_NUMBER_Y, 8, 8)) choice = 2 + digit * 2;
      if (box(x, y, SC_MAP_NUMBER_X+32-digit*8, SC_MAP_NUMBER_Y+8, 8, 8)) choice = 3 + digit * 2;
    }
    if (choice >= 0) {
      result.hit = true;
      if (select && word(r, 0x0b2d) != (unsigned)choice) {
        put(r, 0x0b2d, choice);
        if (word(r, 0x0b31)) put(r, 0x0b31, 0x80);
      }
    }
    break;
  }
  case 3: { /* Game select: 03:d37c's hand positions, with/without saves. */
    const bool saved = word(r, 0x44) != 0;
    const int first = saved ? 0 : 1;
    for (int i = first; i < 5; ++i) {
      int by = saved ? 100 + 24 * i : 112 + 24 * (i - 1);
      if (!box(x, y, 40, by, 192, 16)) continue;
      result.hit = true;
      if (select) put(r, 0x3e, (unsigned)i);
    }
    break;
  }
  case 7: { /* Name keyboard: 03:db5d's staggered key coordinates. */
    for (int row = 0; row < 4; ++row) {
      const int columns = row < 2 ? 11 : 10;
      for (int col = 0; col < columns; ++col) {
        int bx = 40 + row * 8 + col * 16;
        int width = 16;
        if (col == 10 || (row == 3 && col == 9)) {
          bx = row == 0 ? 200 : 208;
          width = row == 0 ? 32 : 24;
        }
        if (!box(x, y, bx, 112 + row * 16, width, 16)) continue;
        result.hit = true;
        if (select) { put(r, 0x4a, col); put(r, 0x4c, row); }
      }
    }
    if (box(x, y, 64, 176, 128, 16)) {
      result.hit = true;
      if (select) { put(r, 0x4a, 0); put(r, 0x4c, 4); }
    }
    break;
  }
  case 9: /* Four vertical choices; clicking a row retains native confirm. */
    result.hit=box(x,y,200,184,24,16); /* Relocated native END key. */
    for (int i = 0; i < SC_DIFFICULTIES; ++i) {
      if (!box(x,y,56,SC_DIFFICULTY_Y+i*SC_DIFFICULTY_SPACING-2,144,12))continue;
      result.hit = true;
      if (select) put(r, 0x0b57, i);
    }
    break;
  case 22: /* Difficulty confirmation: 05:9b80 and 9b8a, on BG's second page. */
    result.hit=box(x,y,200,184,24,16);
    for (int i = 0; i < 2; ++i) {
      if (!box(x, y, 96 + i * 40, 72, 32, 8)) continue;
      result.hit = true;
      if (select) put(r, 0x36, i);
    }
    break;
  case 11: { /* Scenario cards: 03:df10/df00, including the host's Sylt. */
    int maxcol = (word(r, 0x42) & 0x8000) ? (ninth ? 4 : 3) : 2;
    for (int col = 0; col <= maxcol; ++col) {
      for (int row = 0; row < (col == 4 ? 1 : 2); ++row) {
        int bx = 16 + col * 80 - (int)word(r, 0x16);
        if (!box(x, y, bx, 39 + row * 88, 64, 72)) continue;
        result.hit = true;
        if (select) {
          put(r, 0x52, col); put(r, 0x54, row);
          put(r, 0x40, col == 4 ? 8 : col == 3 ? 6 + row : row * 3 + col);
          put(r, 0x22, col >= 3 ? (col - 2) * 80 : col == 0 ? 0 : word(r, 0x22));
        }
      }
    }
    break;
  }
  case 17: /* Resume's two save slots: 03:e2dd's hand positions. */
    for (int i = 0; i < 2; ++i) {
      if (!(word(r, 0x44) & (i + 1)) || !box(x, y, 48, 132 + i * 32, 148, 24)) continue;
      result.hit = true;
      if (select) put(r, 0x0421, i);
    }
    break;
  default: result.handled = false; break;
  }
  return result;
}

bool ScMouseUiPointerScreen(const uint8_t *r) {
    switch(word(r,0x14)) {
    case 2:case 3:case 5:case 6:case 7:case 9:case 10:case 11:case 12:
    case 17:case 18:case 22:return true;
    case 0:case 0x8000:
        return word(r,0xd7) || word(r,0x379) || r[0x391] || r[0xe3];
    default:return false;
    }
}
void ScMouseUiPointerUpdate(ScMouseUiPointer *p,bool inside,bool moved,bool pressed,
                           bool pad_input,int x,int y) {
    if(!inside || pad_input) p->active=false;
    else if(moved || pressed)p->active=true;
    p->x=x;p->y=y;
}

int ScMouseUiCursorPlace(const uint8_t *ram,uint16_t *oam,uint8_t *high,int x,int y) {
    int slot=-1;
    unsigned mode=word(ram,0x14);
    if((mode==0 || mode==0x8000) && word(ram,0x20d)==15 && ram[0xe3]==255 &&
       !word(ram,0xd7) && !ram[0x391]) {
        /* The native gift selection uses four outline pieces. Preserve the
         * selection frame and draw a separate authentic city hand. */
        slot=127;oam[slot*2+1]=0x31ec;
        high[31]=(high[31]&63)|128;
    }
    /* Screen 2 begins before the menu's CHR upload. The visible native
     * option arrow marks menu readiness; title/logo OAM must stay intact. */
    if(mode==2 && (oam[1]!=0x34b9 || (oam[0]>>8)>=224 || (high[0]&1)))return -1;
    if(slot>=0) { /* Dedicated gift hand above. */ }
    else if(ScMouseUiArrowScreen(ram) || (mode>=10 && mode<=12)) {
        /* Scenario OAM begins with pins, followed by the selection frame.
         * Its unused final slot can show the shared native menu hand without
         * changing either the frame or the scroll inferred from those pins. */
        slot=127;oam[slot*2+1]=0x3f9e;
        high[slot/4]=(high[slot/4]&~(3u<<6))|(2u<<6);
        x-=SC_MOUSE_HAND_HOT_X;y-=SC_MOUSE_HAND_HOT_Y;
    } else if((oam[1]==0x31ec || oam[1]==0x3f9e) &&
              (oam[0]>>8)<224 && !(high[0]&1))slot=0;
    /* Gift pictures reuse $30c2. Only the dedicated modal cursor slot may
     * be moved; searching every OBJ can tear a tile out of an icon. */
    else for(int i=0;i<4;++i)
        if(oam[i*2+1]==0x30c2 && (oam[i*2]>>8)<224 &&
           !(high[i/4]&(1u<<(2*(i&3))))) {slot=i;break;}
    if(slot<0)return -1;
    x=x<0?0:x>255?255:x;y=y<0?0:y>223?223:y;
    oam[slot*2]=(uint16_t)(x|(y<<8));high[slot/4]&=~(1u<<(2*(slot&3)));
    return slot;
}
uint16_t ScMouseUiModalInput(uint8_t *r,uint16_t input,bool back) {
  const unsigned b=1,start=8,x=0x200,mode=word(r,0x14);
  if(mode==5 && word(r,0xb2d)>=SC_MAP_GENERATION_LEFT && (input&start))
    return (input&~start)|b;
  if(mode==0 || mode==0x8000) {
    if(r[0x391] && word(r,0x397)==word(r,0x39b)) {
      if(word(r,0x39b)!=0x0d && (back || (input&start)))
        return (input&~start)|b; /* Single adviser message: native B. */
    } else if(word(r,0x3e) && r[0xe3]==1 && r[0xc3]==1 && word(r,0xd7)) {
      unsigned page=word(r,0x1fb);
      if(page==4) {
        /* 02:a713: $0b17=0 accepts a loan, 1 declines it. An existing
         * loan's information page ($0b19!=0) exits with native X. */
        if(back && !r[0xb19]) {put(r,0xb17,1);return b;}
        if(back || (input&(b|start)))
          return (input&~(b|start))|(r[0xb19]?x:b);
      } else if(page==3 || page==5 || page==6) {
        if(back || (input&(b|start)))return (input&~(b|start))|x;
      } else if(page==2 && (input&start)) {
        return (input&~start)|b; /* Confirm the currently selected budget control. */
      }
    }
  }
  /* Existing city tool menus close with A; pre-game screens use X. */
  return back?((mode==0 || mode==0x8000)?0x100:x):input;
}
