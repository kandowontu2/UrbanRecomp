#include "sc_mouse_ui.h"

static double pan_remaining(double pending,int moved) {
  if(!((pending>0 && moved>0) || (pending<0 && moved<0))) return pending;
  double left=pending-moved;
  /* Ctrl's extra native scroll passes can overshoot a small request. Do not
   * reverse the gesture to undo that movement on the following frame. */
  if((pending>0 && moved>0 && left<0) || (pending<0 && moved<0 && left>0)) return 0;
  return left;
}
ScMousePanDirection ScMousePanUpdate(ScMousePan *p,bool allowed,bool held,bool on_land,
    double x,double y,double scale_x,double scale_y,int camera_x,int camera_y) {
  ScMousePanDirection direction={0,0};
  if(!allowed || !held || (!p->active && !on_land) || scale_x<=0 || scale_y<=0) {
    *p=(ScMousePan){0};return direction;
  }
  if(!p->active) {
    *p=(ScMousePan){.active=true,.x=x,.y=y,.camera_x=camera_x,.camera_y=camera_y};
    return direction;
  }
  p->pending_x=pan_remaining(p->pending_x,camera_x-p->camera_x)-(x-p->x)*scale_x*SC_MOUSE_PAN_SPEED;
  p->pending_y=pan_remaining(p->pending_y,camera_y-p->camera_y)-(y-p->y)*scale_y*SC_MOUSE_PAN_SPEED;
  p->x=x;p->y=y;p->camera_x=camera_x;p->camera_y=camera_y;
  direction.x=p->pending_x<=-8?-1:p->pending_x>=8?1:0;
  direction.y=p->pending_y<=-8?-1:p->pending_y>=8?1:0;
  return direction;
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
ScMouseUiResult ScMouseUiPoint(uint8_t *r, int x, int y,
                              bool select, bool ninth) {
  ScMouseUiResult result = {true, false};
  const unsigned mode = word(r, 0x14);
  switch (mode) {
  case 0:
  case 0x8000:
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
  case 5: { /* Map preview: buttons and the three pairs of digit arrows. */
    int choice = -1;
    if (box(x, y, 192, 88, 32, 16)) choice = 0;
    if (box(x, y, 192, 112, 32, 16)) choice = 1;
    for (int digit = 0; digit < 3; ++digit) {
      if (box(x, y, 216 - digit * 8, 176, 8, 8)) choice = 2 + digit * 2;
      if (box(x, y, 216 - digit * 8, 184, 8, 8)) choice = 3 + digit * 2;
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
      if (!box(x, y, 48, by, 184, 16)) continue;
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
  case 9: /* Difficulty tiles: 05:9b4a, 9b5c and 9b6e. */
    result.hit=box(x,y,208,160,24,16); /* Keyboard END confirms the selection. */
    for (int i = 0; i < 3; ++i) {
      if (!box(x, y, 72 + i * 40, 64, 32, 16)) continue;
      result.hit = true;
      if (select) put(r, 0x0b57, i);
    }
    break;
  case 22: /* Difficulty confirmation: 05:9b80 and 9b8a, on BG's second page. */
    result.hit=box(x,y,208,160,24,16);
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
