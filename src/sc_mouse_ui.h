#ifndef SC_MOUSE_UI_H
#define SC_MOUSE_UI_H
#include <stdbool.h>
#include <stdint.h>

/* US-ROM menu coordinates, after conversion to the 256x224 guest surface.
 * A handled screen rejects clicks in gaps. Selection changes only when the
 * pointer moves or is pressed, allowing an idle mouse and pad to coexist. */
typedef struct { bool handled, hit; } ScMouseUiResult;
typedef enum { SC_MOUSE_DIALOG_NONE, SC_MOUSE_DIALOG_SLOTS,
               SC_MOUSE_DIALOG_SAVE_CONFIRM, SC_MOUSE_DIALOG_GIFTS } ScMouseDialog;
/* These modal loops retain their own selection instead of hit-testing the
 * native pointer. Observe entry/exit, including the repeating wait after a
 * state load; the caller's return address distinguishes slots from Yes/No. */
void ScMouseUiObserve(ScMouseDialog *dialog, const uint8_t *ram,
                      unsigned bank, unsigned pc, unsigned sp);
ScMouseUiResult ScMouseUiDialogPoint(ScMouseDialog dialog, uint8_t *ram,
                                    int x, int y, bool select);
/* Both yearly and toolbar budget pages, excluding city/gift/report transitions. */
bool ScMouseUiBudgetLive(const uint8_t *ram);
/* Route confirm/back through the cartridge's modal handlers. Serial pad bits:
 * B=1, Start=8, X=0x200. Escape cancels a loan rather than accepting it. */
uint16_t ScMouseUiModalInput(uint8_t *ram,uint16_t input,bool back);
enum { SC_MAP_NUMBER_X=192, SC_MAP_NUMBER_Y=176, SC_MAP_NUMBER_WIDTH=40,
       SC_MAP_NUMBER_HEIGHT=16 };
/* One boundary covers all five digit pairs, including movement between them. */
bool ScMouseUiMapNumberArrows(int x,int y);
enum { SC_MAP_GENERATION_LEFT=12, SC_MAP_GENERATION_RIGHT=13,
       SC_MAP_LAND_LEFT=14, SC_MAP_LAND_RIGHT=15,
       SC_MAP_GENERATION_LEFT_X=132, SC_MAP_GENERATION_RIGHT_X=228,
       SC_MAP_GENERATION_CENTER_X=184,
       SC_MAP_GENERATION_Y=38, SC_MAP_LAND_Y=58 };
ScMouseUiResult ScMouseUiPoint(uint8_t *ram, int x, int y,
                              bool select, bool ninth_scenario);
bool ScMouseUiScenarioScroll(uint8_t *ram, int direction, bool ninth_scenario);
/* Mouse owns the displayed cursor until real keyboard/pad input takes over.
 * The game retains its selection state and synthesized pad handling. */
typedef struct ScMouseUiPointer { bool active; int x,y; } ScMouseUiPointer;
/* These lists retain the native arrow beside the highlighted option. */
static inline bool ScMouseUiArrowScreen(const uint8_t *ram) {
    unsigned mode=ram[0x14]|((unsigned)ram[0x15]<<8);
    return mode==2 || mode==3 || mode==17 || mode==18;
}
enum { SC_MOUSE_HAND_HOT_X=1, SC_MOUSE_HAND_HOT_Y=1 };
bool ScMouseUiPointerScreen(const uint8_t *ram);
/* Locate the native hand/arrow without moving map digits, title lights or
 * scenario pins. Returns the changed slot, or -1 when no cursor is ready. */
int ScMouseUiCursorPlace(const uint8_t *ram,uint16_t *oam,uint8_t *high,int x,int y);
void ScMouseUiPointerUpdate(ScMouseUiPointer *p,bool inside,bool moved,bool pressed,
                           bool pad_input,int x,int y);
typedef struct ScMousePan { bool active; double x,y; } ScMousePan;
typedef struct ScMousePanDelta { double x,y; } ScMousePanDelta;
/* Immediate world-pixel displacement: no pad input, inertia or queued motion.
 * Land follows the drag; a captured gesture may leave the view. */
ScMousePanDelta ScMousePanUpdate(ScMousePan *pan,bool allowed,bool held,bool on_land,
    double x,double y,double scale_x,double scale_y);
#endif
