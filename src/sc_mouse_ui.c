#include "sc_mouse_ui.h"

static unsigned word(const uint8_t *r, unsigned a) {
  return r[a] | (r[a + 1] << 8);
}
static void put(uint8_t *r, unsigned a, unsigned v) {
  r[a] = (uint8_t)v; r[a + 1] = (uint8_t)(v >> 8);
}
static bool box(int x, int y, int bx, int by, int w, int h) {
  return x >= bx && x < bx + w && y >= by && y < by + h;
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
    if (r[0x0391] && word(r, 0x0397) == 0x0d && word(r, 0x039b) == 0x0d) {
      for (int i = 0; i < 2; ++i) {
        if (!box(x, y, 30 + i * 40, 132, 28, 28)) continue;
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
    for (int i = 0; i < 3; ++i) {
      if (!box(x, y, 72 + i * 40, 64, 32, 16)) continue;
      result.hit = true;
      if (select) put(r, 0x0b57, i);
    }
    break;
  case 22: /* Difficulty confirmation: 05:9b80 and 9b8a, on BG's second page. */
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
