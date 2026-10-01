#ifndef SC_MOUSE_UI_H
#define SC_MOUSE_UI_H
#include <stdbool.h>
#include <stdint.h>

/* US-ROM menu coordinates, after conversion to the 256x224 guest surface.
 * A handled screen rejects clicks in gaps. Selection changes only when the
 * pointer moves or is pressed, allowing an idle mouse and pad to coexist. */
typedef struct { bool handled, hit; } ScMouseUiResult;
ScMouseUiResult ScMouseUiPoint(uint8_t *ram, int x, int y,
                              bool select, bool ninth_scenario);
bool ScMouseUiScenarioScroll(uint8_t *ram, int direction, bool ninth_scenario);
#endif
