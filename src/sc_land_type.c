#include "sc_land_type.h"
#include <stdlib.h>

#define RGB(r,g,b) ((r)|((g)<<5)|((b)<<10))
#define MOON_PALETTE {RGB(19,19,20),RGB(22,22,23),RGB(12,12,14),RGB(26,26,27), \
    RGB(10,13,17),RGB(5,7,10),RGB(20,20,21),RGB(13,13,15),RGB(7,7,9),RGB(12,12,13)}
typedef struct {
    uint16_t ground,grain,bank,edge,fluid,deep,leaves,shade,outline,trunk;
} LandPalette;
/* Keep native textures, connected shore/forest masks and animation intact.
 * Coordinated seasonal ramps avoid tiled noise and isolated vegetation cuts.
 * Order: spring, summer, autumn, winter; native colors remain cartridge-owned. */
static const LandPalette palettes[SC_LAND_TYPES][SC_LAND_SEASONS]={
    {{0}},
    /* Basalt: Near-black rock, subtle seasonal ash/frost; lava never freezes. */
    {
        {RGB(4,5,6),RGB(6,7,8),RGB(2,3,4),RGB(12,13,14),
         RGB(28,9,1),RGB(21,5,0),RGB(15,19,12),RGB(9,13,7),RGB(5,8,4),RGB(7,6,5)},
        {RGB(5,5,6),RGB(7,7,8),RGB(3,3,4),RGB(13,13,14),
         RGB(28,9,1),RGB(21,5,0),RGB(14,17,11),RGB(9,12,7),RGB(5,8,4),RGB(7,6,5)},
        {RGB(6,5,5),RGB(8,7,7),RGB(4,3,3),RGB(14,13,13),
         RGB(28,9,1),RGB(21,5,0),RGB(18,17,10),RGB(12,11,6),RGB(7,7,4),RGB(7,6,5)},
        {RGB(8,9,10),RGB(10,11,12),RGB(4,5,6),RGB(16,17,18),
         RGB(28,9,1),RGB(21,5,0),RGB(18,20,17),RGB(11,14,11),RGB(6,9,7),RGB(7,6,5)}
    },
    /* Amazon: Wet winter/spring and warmer, drier autumn; rainforest stays evergreen. */
    {
        {RGB(18,20,12),RGB(20,22,14),RGB(13,15,9),RGB(23,25,17),
         RGB(4,13,16),RGB(2,7,11),RGB(12,25,7),RGB(6,18,5),RGB(2,10,3),RGB(10,7,3)},
        {RGB(18,19,11),RGB(20,21,13),RGB(13,14,8),RGB(23,24,16),
         RGB(3,11,15),RGB(2,6,10),RGB(10,24,6),RGB(5,17,4),RGB(2,9,3),RGB(10,7,3)},
        {RGB(21,19,11),RGB(23,21,13),RGB(15,13,8),RGB(26,24,17),
         RGB(5,12,14),RGB(3,7,9),RGB(15,23,6),RGB(9,16,4),RGB(4,9,3),RGB(10,7,3)},
        {RGB(15,17,10),RGB(17,19,12),RGB(10,12,7),RGB(20,22,15),
         RGB(3,9,13),RGB(2,5,9),RGB(8,23,7),RGB(3,16,5),RGB(1,9,3),RGB(10,7,3)}
    },
    /* Desert: Spring scrub, sun-baked summer, dry autumn and cool winter sand. */
    {
        {RGB(25,22,14),RGB(27,24,16),RGB(20,17,10),RGB(29,27,20),
         RGB(4,16,22),RGB(2,10,16),RGB(18,24,12),RGB(11,18,7),RGB(6,11,4),RGB(16,11,5)},
        {RGB(25,21,13),RGB(27,23,15),RGB(20,16,9),RGB(29,26,19),
         RGB(4,15,21),RGB(2,10,15),RGB(18,23,12),RGB(12,17,7),RGB(7,11,4),RGB(16,11,5)},
        {RGB(26,20,12),RGB(28,22,14),RGB(21,15,8),RGB(30,25,18),
         RGB(4,14,20),RGB(2,9,14),RGB(23,21,10),RGB(16,15,6),RGB(9,10,4),RGB(16,11,5)},
        {RGB(24,23,17),RGB(26,25,19),RGB(18,17,11),RGB(29,28,23),
         RGB(5,16,22),RGB(3,11,16),RGB(16,20,12),RGB(10,15,8),RGB(5,9,4),RGB(16,11,5)}
    },
    /* Mars: Dusty summers and pale winter frost on ground and rock. */
    {
        {RGB(25,12,8),RGB(27,14,10),RGB(19,8,5),RGB(30,19,14),
         RGB(6,12,17),RGB(3,7,11),RGB(27,15,10),RGB(21,9,6),RGB(14,5,3),RGB(15,5,3)},
        {RGB(26,10,6),RGB(28,12,8),RGB(20,6,3),RGB(30,16,11),
         RGB(5,10,15),RGB(3,6,10),RGB(27,13,7),RGB(21,7,4),RGB(14,4,2),RGB(15,5,3)},
        {RGB(24,9,6),RGB(26,11,8),RGB(18,5,3),RGB(29,15,11),
         RGB(4,9,14),RGB(2,5,9),RGB(25,11,7),RGB(19,6,3),RGB(12,3,2),RGB(15,5,3)},
        {RGB(27,21,19),RGB(29,23,21),RGB(21,15,13),RGB(31,26,23),
         RGB(11,17,21),RGB(6,11,16),RGB(30,23,19),RGB(23,16,13),RGB(15,9,8),RGB(15,5,3)}
    },
    /* Venus: Subtle sulfur/acid color shifts; no temperate snow or ice. */
    {
        {RGB(22,24,14),RGB(24,26,16),RGB(16,18,9),RGB(27,29,20),
         RGB(11,18,8),RGB(7,12,5),RGB(22,25,15),RGB(15,18,9),RGB(9,12,6),RGB(14,12,6)},
        {RGB(23,22,13),RGB(25,24,15),RGB(17,16,8),RGB(28,27,19),
         RGB(13,18,7),RGB(8,12,4),RGB(24,25,14),RGB(17,18,8),RGB(10,12,5),RGB(14,12,6)},
        {RGB(24,20,11),RGB(26,22,13),RGB(18,14,7),RGB(29,25,17),
         RGB(16,18,6),RGB(10,12,3),RGB(26,22,12),RGB(19,15,7),RGB(12,9,4),RGB(14,12,6)},
        {RGB(21,21,16),RGB(23,23,18),RGB(15,15,10),RGB(27,27,22),
         RGB(12,16,9),RGB(7,10,5),RGB(22,23,17),RGB(15,16,11),RGB(9,10,7),RGB(14,12,6)}
    },
    /* Arctic: Spring melt, short summer thaw, autumn cooling and deep winter snow. */
    {
        {RGB(24,26,25),RGB(26,28,27),RGB(15,18,18),RGB(29,31,30),
         RGB(5,15,22),RGB(3,9,15),RGB(21,26,24),RGB(11,20,17),RGB(5,13,10),RGB(9,10,8)},
        {RGB(22,25,23),RGB(24,27,25),RGB(15,18,15),RGB(28,30,29),
         RGB(4,12,21),RGB(2,7,13),RGB(16,23,19),RGB(8,18,13),RGB(4,11,8),RGB(9,10,8)},
        {RGB(23,25,25),RGB(25,27,27),RGB(16,18,19),RGB(29,30,30),
         RGB(6,14,23),RGB(3,9,15),RGB(20,23,17),RGB(12,18,11),RGB(6,12,7),RGB(9,10,8)},
        {RGB(29,30,31),RGB(30,31,31),RGB(21,24,27),RGB(31,31,31),
         RGB(14,22,28),RGB(8,16,23),RGB(29,31,31),RGB(20,25,26),RGB(10,17,17),RGB(9,10,8)}
    },
    /* Swamp: Fresh spring, mossy summer, amber autumn and frosted winter wetland. */
    {
        {RGB(15,18,11),RGB(17,20,13),RGB(11,14,8),RGB(21,23,16),
         RGB(6,14,10),RGB(3,9,6),RGB(15,25,8),RGB(8,18,5),RGB(3,10,3),RGB(9,7,3)},
        {RGB(15,17,10),RGB(17,19,12),RGB(11,13,7),RGB(21,22,15),
         RGB(7,13,10),RGB(4,8,6),RGB(13,23,9),RGB(7,16,5),RGB(3,9,4),RGB(9,7,3)},
        {RGB(18,16,10),RGB(20,18,12),RGB(13,11,7),RGB(24,21,15),
         RGB(9,12,8),RGB(5,7,5),RGB(26,19,6),RGB(18,12,4),RGB(9,7,3),RGB(9,7,3)},
        {RGB(25,26,24),RGB(27,28,26),RGB(17,19,16),RGB(30,31,28),
         RGB(12,18,19),RGB(6,11,13),RGB(27,27,22),RGB(17,19,15),RGB(8,12,9),RGB(9,7,3)}
    },
    /* Airless lunar dust and dark ice seas; no Earth-like vegetation/seasons. */
    {MOON_PALETTE,MOON_PALETTE,MOON_PALETTE,MOON_PALETTE}
};
#undef MOON_PALETTE
#undef RGB

/* Anchors are January (winter), April, July and October. The intervening
 * months blend toward the next anchor, including December -> January.
 * Read the saved guest calendar, never wall time or development-speed ticks. */
static uint16_t mix_color(uint16_t a,uint16_t b,unsigned part) {
    unsigned out=0;
    for(unsigned shift=0;shift<15;shift+=5)
        out|=((((a>>shift)&31)*(3-part)+((b>>shift)&31)*part+1)/3)<<shift;
    return (uint16_t)out;
}
static LandPalette land_palette(unsigned type,unsigned month) {
    if(type>=SC_LAND_TYPES)type=SC_LAND_NATIVE;
    if(month<1 || month>12)month=1;
    unsigned season=((month-1)/3+3)%4,part=(month-1)%3;
    const LandPalette *a=&palettes[type][season],*b=&palettes[type][(season+1)%4];
#define MIX(field) mix_color(a->field,b->field,part)
    return (LandPalette){MIX(ground),MIX(grain),MIX(bank),MIX(edge),MIX(fluid),
        MIX(deep),MIX(leaves),MIX(shade),MIX(outline),MIX(trunk)};
#undef MIX
}
ScLandColors ScLandTypeColors(unsigned type,unsigned month) {
    if(!type || type>=SC_LAND_TYPES)
        return (ScLandColors){{24,21,15},{2,7,23},{2,23,3}};
    LandPalette p=land_palette(type,month);ScLandColors colors;
    for(unsigned i=0;i<3;++i) {
        colors.ground[i]=(uint8_t)((p.ground>>(i*5))&31);
        colors.water[i]=(uint8_t)((p.fluid>>(i*5))&31);
        colors.forest[i]=(uint8_t)((((p.leaves>>(i*5))&31)+((p.shade>>(i*5))&31)+1)/2);
    }
    return colors;
}
uint32_t ScLandPreviewColor(unsigned type,unsigned tile,uint32_t native,
        unsigned brightness,unsigned month) {
    if(!type || type>=SC_LAND_TYPES)return native;
    ScLandColors c=ScLandTypeColors(type,month);
    const uint8_t *rgb=tile>0 && tile<20?c.water:tile>=20?c.forest:c.ground;
    unsigned bright=tile>=4 && tile<20?36:31;
    unsigned r=rgb[0]*bright*255/(31*31),g=rgb[1]*bright*255/(31*31),b=rgb[2]*bright*255/(31*31);
    r=(r>255?255:r)*brightness/15;g=(g>255?255:g)*brightness/15;b=(b>255?255:b)*brightness/15;
    return 0xff000000u|(r<<16)|(g<<8)|b;
}
/* Use actual terrain roles, leaving unrelated roofs/walls/metal colors native. */
static uint16_t land_color(const LandPalette *p,unsigned index,uint16_t native) {
    switch(index) {
    case 3:return p->edge;
    case 4:return p->fluid;
    case 7:return p->ground;
    case 10:return p->deep;
    case 11:return p->bank;
    case 12:return p->grain;
    case 13:case 29:return p->leaves;
    case 14:case 30:return p->shade;
    case 15:case 31:return p->outline;
    case 18:return p->trunk;
    case 28:return p->ground;
    default:return native;
    }
}
bool ScLandGraphicsApply(ScLandGraphics *s,unsigned type,unsigned month,bool active,
        unsigned base,uint16_t *vram,uint16_t *cgram) {
    (void)vram; /* Native CHR is deliberately never rewritten. */
    if(type>=SC_LAND_TYPES)type=0;
    bool changed=false;
    if(!active || !type) {
        if(s->valid)for(unsigned i=0;i<32;++i) {
            unsigned at=i<16?16+i:112+i-16;
            if(cgram[at]==s->shown_palette[i] && cgram[at]!=s->palette[i]) {
                cgram[at]=s->palette[i];changed=true;
            }
        }
        s->valid=false;return changed;
    }
    bool fresh=!s->valid;s->valid=true;s->base=base;
    LandPalette p=land_palette(type,month);
    for(unsigned i=0;i<32;++i) {
        unsigned at=i<16?16+i:112+i-16;
        if(fresh || cgram[at]!=s->shown_palette[i])s->palette[i]=cgram[at];
        uint16_t result=land_color(&p,i,s->palette[i]);
        changed|=cgram[at]!=result;cgram[at]=s->shown_palette[i]=result;
    }
    return changed;
}

static const unsigned stadium_chars[SC_STADIUM_CHARS]={
    0x3b9,0x3ba,0x3bb,0x1e5,0x3bc,0x3bd,0x3be,0x1e6,
    0x3bf,0x3c0,0x3c1,0x1e7,0x3c2,0x3c3,0x3c4,0x1e8,
    0x2f5,0x2f6,0x2f7,0x2f8,0x2f9,0x2fa,0x2fb,0x2fc,
    0x2fd,0x2fe,0x2ff,0x302,0x303,0x304,0x305,0x306,0x1e4
};
static unsigned dome_pixel(unsigned x,unsigned y,unsigned native) {
    int dx=(int)x*2-31,dy=(int)y*2-27;
    int ellipse=dx*dx*121+dy*dy*196;
    if(ellipse>196*121*4 || y>26)return native;
    if(ellipse>196*121*7/2 || y==26)return 2; /* dark perimeter and sealed base */
    if(y>=23)return y==23?6:3;
    /* Curved steel ribs, a central spine and two narrow reflected skylights.
     * Use the cartridge's existing stadium gray/blue ramp, without touching
     * the shared palette or texture of any other building. */
    int width=28,rib=dx<0?-dx:dx;
    while(width>0 && width*width*121+dy*dy*196>196*121*4)--width;
    if(x==15 || x==16 || abs(rib*3-width)<3 || abs(rib*3-width*2)<3)return 3;
    if(y==9 || y==17)return 3;
    if(x+y>=15 && x+y<=18 && y>=5 && y<=13)return 7;
    return x<15?6:5;
}
bool ScLandStadiumApply(ScLandStadiumGraphics *s,unsigned type,bool active,
                        unsigned base,uint16_t *vram) {
    bool dome=active && (type==SC_LAND_MARS || type==SC_LAND_VENUS || type==SC_LAND_MOON);
    bool changed=false;
    if(s->valid && (!dome || s->base!=base)) {
        for(unsigned tile=0;tile<SC_STADIUM_CHARS;++tile)for(unsigned i=0;i<16;++i) {
            unsigned at=(s->base+stadium_chars[tile]*16+i)&32767;
            if(vram[at]==s->shown[tile][i] && vram[at]!=s->original[tile][i]) {
                vram[at]=s->original[tile][i];changed=true;
            }
        }
        s->valid=false;
    }
    if(!dome)return changed;
    bool fresh=!s->valid,dirty=fresh;s->valid=true;s->base=base;
    for(unsigned tile=0;tile<SC_STADIUM_CHARS;++tile)for(unsigned i=0;i<16;++i) {
        unsigned at=(base+stadium_chars[tile]*16+i)&32767;
        if(fresh || vram[at]!=s->shown[tile][i]) {
            s->original[tile][i]=vram[at];dirty=true;
        }
    }
    if(!dirty)return changed;
    for(unsigned tile=0;tile<SC_STADIUM_CHARS;++tile) {
        uint16_t image[16]={0};
        if(tile<32)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            unsigned bit=7-x,lo=s->original[tile][y],hi=s->original[tile][y+8];
            unsigned native=((lo>>bit)&1)|(((lo>>(bit+8))&1)<<1)|
                (((hi>>bit)&1)<<2)|(((hi>>(bit+8))&1)<<3);
            unsigned ci=dome_pixel((tile%4)*8+x,((tile%16)/4)*8+y,native);
            image[y]|=(ci&1)<<bit|((ci>>1)&1)<<(bit+8);
            image[y+8]|=((ci>>2)&1)<<bit|((ci>>3)&1)<<(bit+8);
        }
        for(unsigned i=0;i<16;++i) {
            unsigned at=(base+stadium_chars[tile]*16+i)&32767;
            changed|=vram[at]!=image[i];vram[at]=s->shown[tile][i]=image[i];
        }
    }
    return changed;
}
