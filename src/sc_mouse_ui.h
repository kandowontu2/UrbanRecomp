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
typedef struct ScMousePan {
  bool active;
  double x,y,pending_x,pending_y;
  int camera_x,camera_y;
} ScMousePan;
typedef struct ScMousePanDirection { int x,y; } ScMousePanDirection;
#define SC_MOUSE_PAN_SPEED 3
/* Grab-and-drag panning: the land follows the mouse right/down; camera moves left/up.
 * Window motion is converted to guest pixels at 3x speed, then fed
 * through the ordinary scroll routine. Camera feedback consumes the motion
 * only when that routine actually runs. A captured drag may leave the view. */
ScMousePanDirection ScMousePanUpdate(ScMousePan *pan,bool allowed,bool held,bool on_land,
    double x,double y,double scale_x,double scale_y,int camera_x,int camera_y);
#endif
