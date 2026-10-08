#pragma once
#include <stdbool.h>
#include <stdint.h>
enum {SC_LAND_NATIVE,SC_LAND_BASALT,SC_LAND_AMAZON,SC_LAND_DESERT,
      SC_LAND_MARS,SC_LAND_VENUS,SC_LAND_ARCTIC,SC_LAND_SWAMP,SC_LAND_MOON,SC_LAND_TYPES};
static inline const char *ScLandTypeName(unsigned type) {
    static const char *const names[]={"NATIVE","BASALT","AMAZON","DESERT","MARS","VENUS","ARCTIC","SWAMP","MOON"};
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
    bool relief_valid;
    unsigned relief_base,relief_type;
    uint16_t relief_original[18][16],relief_shown[18][16];
} ScLandGraphics;
/* Mars/Arctic replace only natural forest CHR; parks and shores stay native.
 * Restore native graphics/uploads when changing themes or leaving the city. */
bool ScLandGraphicsApply(ScLandGraphics *state,unsigned type,unsigned month,bool active,
    unsigned chr_base,uint16_t *vram,uint16_t *cgram);

enum {SC_STADIUM_CHARS=33};
typedef struct {
    bool valid;
    unsigned base;
    uint16_t original[SC_STADIUM_CHARS][16],shown[SC_STADIUM_CHARS][16];
} ScLandStadiumGraphics;
/* Only the two stadium CHR sets and their flagpole belong to this skin. */
bool ScLandStadiumApply(ScLandStadiumGraphics *state,unsigned type,bool active,
    unsigned chr_base,uint16_t *vram);

/* Four cardinal neighbours of a newly flooded lava cell. Match the native
 * fire's flammable/non-owner rule; bare ground may also catch lava fire.
 * Never replace water, existing fire, flood or a zone's center marker. */
static inline unsigned ScLandFloodFireMask(unsigned type,const uint8_t *map,
        unsigned width,unsigned height,unsigned cell,const uint8_t *properties) {
    if(type!=SC_LAND_BASALT || !map || !properties || !width || cell>=width*height)return 0;
    unsigned x=cell%width,y=cell/width,mask=0;
    const int dx[]={-1,1,0,0},dy[]={0,0,-1,1};
    for(unsigned d=0;d<4;++d) {
        int px=(int)x+dx[d],py=(int)y+dy[d];
        if(px<0 || py<0 || (unsigned)px>=width || (unsigned)py>=height)continue;
        unsigned at=2*((unsigned)py*width+(unsigned)px);
        unsigned tile=(map[at]|(unsigned)map[at+1]<<8)&1023;
        if(!tile || (tile>=20 && tile!=0x7f && tile!=0x365 &&
                    (properties[tile]&5)==4))mask|=1u<<d;
    }
    return mask;
}
