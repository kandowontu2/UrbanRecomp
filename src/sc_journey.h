#pragma once
#include "sc_world.h"

/* Call only between complete native simulation cycles. Zero means no change. */
void ScJourneyObservePopulation(ScWorld *world,uint64_t population);
unsigned ScJourneyExpand(ScWorld *world,uint8_t *ram,uint64_t population);
/* Native 16-pixel menu lettering, assembled from the player's live ROM font. */
void ScJourneyMenuInit(const uint8_t *rom,size_t size);
void ScJourneyMenuFont(uint16_t *vram);
/* Publish glyphs only when the displayed sprite list belongs to this page. */
bool ScJourneyMenuPresent(uint16_t *vram,const uint16_t *oam,bool saved);
void ScJourneyMenuFrame(uint16_t *vram,unsigned map_base);
bool ScJourneyMenuRead(uint32_t address,unsigned screen,uint8_t *value);
unsigned ScJourneyMenuY(bool saved,unsigned selection);
void ScMapSizeMenuSet(bool active);
bool ScMapSizeMenuActive(void);
void ScDevelopmentMenuSet(bool active);
bool ScDevelopmentMenuActive(void);
/* Native adviser text is 24 columns, read through the original typewriter. */
bool ScJourneyMessageRead(unsigned notice,uint32_t address,uint8_t *value);
/* Original 16-pixel font, emitted into unused native OAM slots on Load City. */
unsigned ScSavedCityMenuY(unsigned slot);
void ScSavedCityMenuFont(uint16_t *vram,uint16_t *oam,uint8_t *high_oam,
    unsigned saved_slots,bool visible,bool test_visible);
