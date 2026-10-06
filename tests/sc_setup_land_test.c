#include "sc_city_setup.h"
#include "sc_land_type.h"
#include "snes/interp816.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t ram[0x20000],rom[0x80000];
static uint16_t vram[0x8000],original[0x8000],palette[256],original_palette[256];
static unsigned word(unsigned p) {return ram[p]|ram[p+1]<<8;}
static void put(unsigned p,unsigned v) {ram[p]=v;ram[p+1]=v>>8;}
int main(void) {
    Interp816 cpu={0};cpu.k=3;
    for(unsigned size=0;size<6;++size)for(unsigned level=0;level<4;++level) {
        put(0xb57,level);cpu.pc=0xc65c;
        ScCitySetupStep(&cpu,ram,size);
        unsigned expected=(level==0?20000:level==1?10000:level==2?5000:10000)<<size;
        assert((word(0xb9d)|(ram[0xb9f]<<16))==expected && !ram[0xba0]);
        assert(word(0xb57)==level && cpu.pc==(level==3?0xc667:0xc65c));
        for(unsigned keys=0x100;keys<=0x800;keys<<=1) {
            put(0xb57,level);put(0xc9,keys);cpu.pc=0xd978;
            ScCitySetupStep(&cpu,ram,size);
            unsigned next=keys&0x500?(level<3?level+1:3):(level?level-1:0);
            assert(word(0xb57)==next && cpu.pc==0xd9bc);
        }
    }
    /* Only the intended instruction can read beyond a three-entry table. */
    unsigned tables[][2]={{0x038a40,0x038bcb},{0x038e4a,0x038fe8},{0x038ea0,0x038fee},
        {0x03aac3,0x03abf6},{0x03d9fd,0x03daaf},{0x03da04,0x03dab5},
        {0x03da0b,0x03dabb},{0x03da12,0x03dac1},{0x03da19,0x03dac7},{0x03da20,0x03dacd}};
    put(0xb57,3);uint8_t value=0;
    for(unsigned i=0;i<10;++i)for(unsigned b=0;b<2;++b) {
        unsigned at=(tables[i][1]&0x7fff)+(tables[i][1]>>16)*0x8000+4+b;
        rom[at]=0x36+b;
        assert(ScCitySetupRomRead(rom,sizeof rom,ram,tables[i][0]+3,tables[i][1]+6+b,&value));
        assert(value==0x36+b);
        assert(!ScCitySetupRomRead(rom,sizeof rom,ram,tables[i][0]+4,tables[i][1]+6+b,&value));
    }
    assert(ScCitySetupRomRead(rom,sizeof rom,ram,0x03b926,0x03b96f,&value) && value==(600&255));
    assert(ScCitySetupRomRead(rom,sizeof rom,ram,0x03b926,0x03b970,&value) && value==600>>8);
    put(0xb57,2);assert(!ScCitySetupRomRead(rom,sizeof rom,ram,0x03b926,0x03b96f,&value));
    assert(ScCityDisasterThreshold(2)==1200 && ScCityDisasterThreshold(3)==600);

    for(unsigned i=0;i<0x8000;++i)vram[i]=(uint16_t)(i*171+19);
    for(unsigned i=0;i<256;++i)palette[i]=0x4a52;
    palette[17]=palette[113]=24|(21<<5)|(15<<10);
    palette[18]=palette[114]=27|(24<<5)|(18<<10);
    palette[19]=palette[115]=21|(18<<5)|(12<<10);
    palette[20]=2|(7<<5)|(23<<10);palette[21]=3|(9<<5)|(28<<10);
    palette[116]=2|(23<<5)|(3<<10);
    memcpy(original,vram,sizeof vram);memcpy(original_palette,palette,sizeof palette);
    ScLandGraphics graphics={0};unsigned hashes[SC_LAND_TYPES]={0};
    for(unsigned type=1;type<SC_LAND_TYPES;++type) {
        assert(ScLandGraphicsApply(&graphics,type,7,true,0,vram,palette));
        /* Every connected shoreline/forest mask and animated water tile
         * remains byte-exact. Avoid per-tile shapes and noisy replacement dirt. */
        assert(!memcmp(vram,original,sizeof vram));
        for(unsigned i=0;i<256;++i) {
            unsigned slot=i>=16 && i<32?i-16:i>=112 && i<128?i-96:99;
            bool themed=slot==3 || slot==4 || slot==7 || (slot>=10 && slot<=15) ||
                slot==18 || (slot>=28 && slot<=31);
            if(!themed)assert(palette[i]==original_palette[i]);
        }
        /* Water retains a visible main/shadow ramp instead of flat lava. */
        assert(palette[20]!=palette[26]);
        assert(palette[125]!=palette[126] && palette[126]!=palette[127]);
        for(unsigned i=0;i<0x8000;++i)hashes[type]=hashes[type]*33+vram[i];
        for(unsigned i=0;i<256;++i)hashes[type]=hashes[type]*33+palette[i];
        for(unsigned prior=1;prior<type;++prior)assert(hashes[prior]!=hashes[type]);
        assert(!ScLandGraphicsApply(&graphics,type,7,true,0,vram,palette));
        assert(ScLandGraphicsApply(&graphics,0,7,false,0,vram,palette));
        assert(!memcmp(vram,original,sizeof vram) && !memcmp(palette,original_palette,sizeof palette));
        assert(ScLandPreviewColor(type,1,0xff123456,0,7)==0xff000000);
        assert(ScLandPreviewColor(type,1,0,15,7)!=ScLandPreviewColor(type,0,0,15,7));
    }
    assert(ScLandPreviewColor(0,0,0xff123456,15,7)==0xff123456);
    /* A native upload while themed becomes the baseline on restoration. */
    ScLandGraphicsApply(&graphics,1,7,true,0,vram,palette);
    vram[0x2a5*16]=0xbeef;palette[17]=20|(17<<5)|(10<<10);
    ScLandGraphicsApply(&graphics,2,7,true,0,vram,palette);
    ScLandGraphicsApply(&graphics,0,7,false,0,vram,palette);
    assert(vram[0x2a5*16]==0xbeef && palette[17]==(20|(17<<5)|(10<<10)));
    memcpy(original,vram,sizeof original);
    /* All seven themes have four distinct season anchors, twelve smooth
     * monthly steps and a correct December -> January wrap. Retain every
     * original texture/animation byte, unrelated color and live native upload. */
    const unsigned anchors[]={4,7,10,1};
    for(unsigned type=1;type<SC_LAND_TYPES;++type) {
        unsigned season_hash[4]={0};
        for(unsigned season=0;season<4;++season) {
            memcpy(palette,original_palette,sizeof palette);
            memset(&graphics,0,sizeof graphics);
            assert(ScLandGraphicsApply(&graphics,type,anchors[season],true,0,vram,palette));
            for(unsigned i=0;i<256;++i)season_hash[season]=season_hash[season]*33+palette[i];
            for(unsigned earlier=0;earlier<season;++earlier)
                assert(type==SC_LAND_MOON?season_hash[season]==season_hash[earlier]:season_hash[season]!=season_hash[earlier]);
            assert(!memcmp(vram,original,sizeof vram));
            assert(palette[20]!=palette[26] && palette[125]!=palette[126] && palette[126]!=palette[127]);
            for(unsigned tile=0;tile<38;++tile) {
                assert(ScLandPreviewColor(type,tile,0,0,anchors[season])==0xff000000);
                uint32_t ink=ScLandPreviewColor(type,tile,0,15,anchors[season]);
                assert((ink>>24)==255);
            }
            assert(ScLandGraphicsApply(&graphics,0,anchors[season],false,0,vram,palette));
            assert(!memcmp(palette,original_palette,sizeof palette));
        }
        ScLandColors invalid=ScLandTypeColors(type,0),winter=ScLandTypeColors(type,1),
            spring=ScLandTypeColors(type,4),february=ScLandTypeColors(type,2);
        assert(!memcmp(&invalid,&winter,sizeof winter));
        invalid=ScLandTypeColors(type,13);assert(!memcmp(&invalid,&winter,sizeof winter));
        for(unsigned channel=0;channel<3;++channel)
            assert(february.ground[channel]==(winter.ground[channel]*2+spring.ground[channel]+1)/3);
        for(unsigned month=1;month<=12;++month) {
            bool fresh=!graphics.valid;
            assert(ScLandGraphicsApply(&graphics,type,month,true,0,vram,palette)==(fresh || type!=SC_LAND_MOON));
            assert(!ScLandGraphicsApply(&graphics,type,month,true,0,vram,palette));
            uint16_t before[256];memcpy(before,palette,sizeof before);
            ScLandGraphicsApply(&graphics,type,month%12+1,true,0,vram,palette);
            for(unsigned i=0;i<256;++i)for(unsigned channel=0;channel<15;channel+=5)
                assert(abs((int)(before[i]>>channel&31)-(int)(palette[i]>>channel&31))<=9);
            assert(ScLandGraphicsApply(&graphics,0,month,false,0,vram,palette));
            assert(!memcmp(palette,original_palette,sizeof palette));
        }
    }
    /* Basalt lava retains its hot colors through every season. */
    uint16_t hot=0,deep=0;
    for(unsigned month=1;month<=12;++month) {
        ScLandGraphicsApply(&graphics,SC_LAND_BASALT,month,true,0,vram,palette);
        if(month==1) {hot=palette[20];deep=palette[26];}
        assert(palette[20]==hot && palette[26]==deep);
    }
    ScLandGraphicsApply(&graphics,0,1,false,0,vram,palette);
    /* Native seasons are entirely cartridge-owned. */
    for(unsigned month=1;month<=12;++month) {
        assert(!ScLandGraphicsApply(&graphics,0,month,true,0,vram,palette));
        assert(!memcmp(palette,original_palette,sizeof palette));
        assert(ScLandPreviewColor(0,0,0xff123456,15,month)==0xff123456);
    }
    /* A seasonal native palette upload remains the baseline after a theme
     * and month change, rather than baking last season's themed snow into it. */
    ScLandGraphicsApply(&graphics,SC_LAND_SWAMP,1,true,0,vram,palette);
    palette[23]=0x1234;
    ScLandGraphicsApply(&graphics,SC_LAND_DESERT,10,true,0,vram,palette);
    ScLandGraphicsApply(&graphics,0,10,false,0,vram,palette);
    assert(palette[23]==0x1234 && !memcmp(vram,original,sizeof vram));
    /* Dome art touches only stadium-owned CHR; menus, Earth themes and
     * snapshots restore byte-exact native art, including live DMA updates. */
    const unsigned chars[]={0x3b9,0x3ba,0x3bb,0x1e5,0x3bc,0x3bd,0x3be,0x1e6,
        0x3bf,0x3c0,0x3c1,0x1e7,0x3c2,0x3c3,0x3c4,0x1e8,
        0x2f5,0x2f6,0x2f7,0x2f8,0x2f9,0x2fa,0x2fb,0x2fc,
        0x2fd,0x2fe,0x2ff,0x302,0x303,0x304,0x305,0x306,0x1e4};
    ScLandStadiumGraphics stadium={0};
    for(unsigned type=0;type<SC_LAND_TYPES;++type) {
        bool dome=type==SC_LAND_MARS || type==SC_LAND_VENUS || type==SC_LAND_MOON;
        assert(ScLandStadiumApply(&stadium,type,true,0,vram)==dome);
        assert(!ScLandStadiumApply(&stadium,type,true,0,vram));
        for(unsigned word=0;word<32768;++word) {
            bool owned=false;for(unsigned i=0;i<SC_STADIUM_CHARS;++i)owned|=word/16==chars[i];
            if(!owned || !dome)assert(vram[word]==original[word]);
        }
        assert(ScLandStadiumApply(&stadium,0,false,0,vram)==dome);
        assert(!memcmp(vram,original,sizeof original));
    }
    ScLandStadiumApply(&stadium,SC_LAND_MARS,true,0,vram);
    unsigned at=chars[0]*16;vram[at]=0xabcd;original[at]=0xabcd;
    ScLandStadiumApply(&stadium,SC_LAND_MARS,true,0,vram);
    ScLandStadiumApply(&stadium,0,false,0,vram);
    assert(!memcmp(vram,original,sizeof original));
    puts("PASS: city setup, seasonal/lunar palettes, native restoration, preview fades and stadium-only domes");
    return 0;
}
