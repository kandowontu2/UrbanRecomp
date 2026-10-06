#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct Interp816 Interp816;
enum { SC_DIFFICULTIES=4, SC_DIFFICULTY_Y=110, SC_DIFFICULTY_SPACING=20 };
static inline unsigned ScCityStartingFunds(unsigned size,unsigned difficulty) {
    static const unsigned base[]={20000,10000,5000,10000};
    return base[difficulty<4?difficulty:0]<<(size<=5?size:0);
}
static inline const char *ScCityDifficultyName(unsigned difficulty) {
    static const char *const names[]={"Easy","Medium","Hard","Super Hard"};
    return names[difficulty<4?difficulty:0];
}
static inline unsigned ScCityDisasterThreshold(unsigned difficulty) {
    return 4800u>>(difficulty<4?difficulty:0);
}
/* Extend only the actual difficulty-indexed reads. Adjacent tables overlap
 * the fourth entry, so an address-only ROM patch would corrupt Easy rules. */
bool ScCitySetupRomRead(const uint8_t *rom,size_t size,const uint8_t *ram,
    uint32_t pc,uint32_t address,uint8_t *value);
void ScCitySetupStep(Interp816 *cpu,uint8_t *ram,unsigned size);
/* Original compact setup lettering; add only glyphs absent from its set. */
bool ScCitySetupFont(uint8_t out[128][8],const uint8_t *tiles,size_t size);
static inline unsigned ScCitySetupGlyphAdvance(const uint8_t font[128][8],unsigned ch) {
    if(ch==' ')return 4;
    unsigned bits=0,width=0;
    for(unsigned y=0;y<8;++y)bits|=font[ch&127][y];
    for(unsigned x=0;x<8;++x)if(bits&(128u>>x))width=x+1;
    return width+1;
}
static inline unsigned ScCitySetupTextWidth(const uint8_t font[128][8],const char *text) {
    unsigned width=0;
    for(;*text;++text)width+=ScCitySetupGlyphAdvance(font,(unsigned char)*text);
    return width;
}
