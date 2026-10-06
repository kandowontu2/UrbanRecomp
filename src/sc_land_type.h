#pragma once
#include <stdbool.h>
#include <stdint.h>
enum {SC_LAND_NATIVE,SC_LAND_BASALT,SC_LAND_AMAZON,SC_LAND_DESERT,
      SC_LAND_MARS,SC_LAND_VENUS,SC_LAND_ARCTIC,SC_LAND_SWAMP,SC_LAND_TYPES};
static inline const char *ScLandTypeName(unsigned type) {
    static const char *const names[]={"NATIVE","BASALT","AMAZON","DESERT","MARS","VENUS","ARCTIC","SWAMP"};
    return names[type<SC_LAND_TYPES?type:0];
}
typedef struct {uint8_t ground[3],water[3],forest[3];} ScLandColors;
enum {SC_LAND_SPRING,SC_LAND_SUMMER,SC_LAND_AUTUMN,SC_LAND_WINTER,SC_LAND_SEASONS};
/* Month is the guest's saved 1..12 calendar. Invalid/setup dates use January. */
ScLandColors ScLandTypeColors(unsigned type,unsigned month);
uint32_t ScLandPreviewColor(unsigned type,unsigned tile,uint32_t native,
    unsigned brightness,unsigned month);

typedef struct {
    bool valid;
    unsigned base;
    uint16_t palette[32],shown_palette[32];
} ScLandGraphics;
/* Keep cartridge terrain textures, animation and connected edge masks intact.
 * Theme only designated terrain colors; restore live native uploads on exit. */
bool ScLandGraphicsApply(ScLandGraphics *state,unsigned type,unsigned month,bool active,
    unsigned chr_base,uint16_t *vram,uint16_t *cgram);
