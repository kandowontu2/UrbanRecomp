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
ScMouseUiResult ScMouseUiPoint(uint8_t *ram, int x, int y,
                              bool select, bool ninth_scenario);
bool ScMouseUiScenarioScroll(uint8_t *ram, int direction, bool ninth_scenario);
typedef struct ScMousePan { bool active; double x,y; } ScMousePan;
typedef struct ScMousePanDelta { double x,y; } ScMousePanDelta;
/* Immediate world-pixel displacement: no pad input, inertia or queued motion.
 * Land follows the drag; a captured gesture may leave the view. */
ScMousePanDelta ScMousePanUpdate(ScMousePan *pan,bool allowed,bool held,bool on_land,
    double x,double y,double scale_x,double scale_y);
#endif
