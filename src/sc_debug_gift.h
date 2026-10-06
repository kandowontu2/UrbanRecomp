#ifndef SC_DEBUG_GIFT_H
#define SC_DEBUG_GIFT_H
#include <stdbool.h>
#include <stdint.h>
enum { SC_DEBUG_GIFT_COUNT=15 };
typedef struct { bool replaced; uint8_t slot,original,gift; } ScDebugGift;
extern const char *const ScDebugGiftNames[SC_DEBUG_GIFT_COUNT];
bool ScDebugGiftGrant(ScDebugGift *state,uint8_t *ram,unsigned gift);
void ScDebugGiftRestore(ScDebugGift *state,uint8_t *ram);
void ScDebugGiftTick(ScDebugGift *state,uint8_t *ram);
bool ScDebugGiftButton(int x,int y);
#endif
