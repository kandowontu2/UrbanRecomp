#ifndef SC_DEBUG_GIFT_H
#define SC_DEBUG_GIFT_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
enum { SC_DEBUG_GIFT_COUNT=15, SC_DEBUG_GIFT_COLUMNS=4,
       SC_DEBUG_GIFT_ROWS=(SC_DEBUG_GIFT_COUNT+SC_DEBUG_GIFT_COLUMNS-1)/SC_DEBUG_GIFT_COLUMNS,
       SC_DEBUG_GIFT_PANEL_WIDTH=8+40*SC_DEBUG_GIFT_COLUMNS,
       SC_DEBUG_GIFT_PANEL_HEIGHT=40+40*SC_DEBUG_GIFT_ROWS };
typedef struct { bool replaced; uint8_t slot,original,gift; } ScDebugGift;
extern const char *const ScDebugGiftNames[SC_DEBUG_GIFT_COUNT];
bool ScDebugGiftGrant(ScDebugGift *state,uint8_t *ram,unsigned gift);
void ScDebugGiftRestore(ScDebugGift *state,uint8_t *ram);
void ScDebugGiftTick(ScDebugGift *state,uint8_t *ram);
bool ScDebugGiftButton(int x,int y);
/* The cartridge's 4x4 tile compositions, decoded without touching live WRAM. */
bool ScDebugGiftIcons(uint8_t icons[SC_DEBUG_GIFT_COUNT][32*32],const uint8_t *rom,size_t size);
typedef struct { int x,y,w,h; } ScGiftBounds;
/* Transparent padding is not part of the visible selection outline. */
ScGiftBounds ScDebugGiftIconBounds(const uint8_t icon[32*32]);
typedef struct { int x,y,w,h,rows,cols; } ScGiftLayout;
ScGiftLayout ScDebugGiftLayout(int width,int height);
int ScDebugGiftHit(ScGiftLayout layout,int x,int y);
int ScDebugGiftMove(int selected,int dx,int dy);
#endif
