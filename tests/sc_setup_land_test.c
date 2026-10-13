#include "sc_city_setup.h"
#include "sc_land_type.h"
#include "sc_land_summary.h"
#include "sc_mapgen.h"
#include "snes/interp816.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t ram[0x20000],rom[0x80000];
static uint16_t vram[0x8000],original[0x8000],palette[256],original_palette[256];
static unsigned word(unsigned p) {return ram[p]|ram[p+1]<<8;}
static void put(unsigned p,unsigned v) {ram[p]=v;ram[p+1]=v>>8;}
static unsigned chr_pixel(const uint16_t *image,unsigned x,unsigned y) {
    unsigned bit=7-x,lo=image[y],hi=image[y+8];
    return ((lo>>bit)&1)|(((lo>>(bit+8))&1)<<1)|
        (((hi>>bit)&1)<<2)|(((hi>>(bit+8))&1)<<3);
}
static void set_chr_pixel(uint16_t *image,unsigned x,unsigned y,unsigned ci) {
    unsigned bit=7-x,mask=(1u<<bit)|(1u<<(bit+8));
    image[y]=(image[y]&~mask)|((ci&1)<<bit)|(((ci>>1)&1)<<(bit+8));
    image[y+8]=(image[y+8]&~mask)|(((ci>>2)&1)<<bit)|(((ci>>3)&1)<<(bit+8));
}
static void check_relief(unsigned type) {
    bool themed=type==SC_LAND_MARS || type==SC_LAND_MOON || type==SC_LAND_ARCTIC || type==SC_LAND_VENUS;
    for(unsigned i=0;i<32768;++i) {
        bool replaced=(themed && i/16>=0x28e && i/16<=0x29f) ||
                      (type==SC_LAND_DESERT && i/16>=0x29d && i/16<=0x29f) ||
                      (type==SC_LAND_SWAMP && i/16>=0x360 && i/16<=0x370) ||
                      (type==SC_LAND_MOON && i/16==0x370);
        if(!replaced)assert(vram[i]==original[i]);
    }
    if(type==SC_LAND_DESERT) {
        for(unsigned i=0;i<SC_DESERT_CACTUS_VARIANTS;++i) {
            assert(memcmp(vram+(0x29d+i)*16,original+(0x29d+i)*16,32));
            for(unsigned j=0;j<i;++j)assert(memcmp(vram+(0x29d+i)*16,vram+(0x29d+j)*16,32));
        }
    }
    if(themed) {
        unsigned variants=type==SC_LAND_ARCTIC?4:type==SC_LAND_VENUS?9:3;
        for(unsigned i=0;i<variants;++i)for(unsigned j=0;j<i;++j)
            assert(memcmp(vram+(0x28e + i)*16,vram+(0x28e + j)*16,32));
    }
    if(type==SC_LAND_VENUS)for(unsigned t=0;t<9;++t) {
        unsigned trunks=0,highlights=0,leaves=0;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            unsigned ci=chr_pixel(vram+(0x28e + t)*16,x,y);
            assert(ci==chr_pixel(vram+(0x28e + t+9)*16,7-x,y));
            trunks+=ci==2;highlights+=ci==13;leaves+=ci==14;
        }
        assert(trunks>0 && trunks<10 && highlights>0 && leaves>highlights);
    }
    if(type==SC_LAND_MOON)assert(memcmp(vram+0x370*16,original+0x370*16,32));
}
static void check_swamp_water(void) {
    uint16_t image[32768],native[32768],colors[256]={0};
    for(unsigned i=0;i<32768;++i)image[i]=native[i]=(uint16_t)(i*171+19);
    for(unsigned t=0;t<17;++t)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
        set_chr_pixel(native+(0x360+t)*16,x,y,x<2?7:x==7?11:(x+y)%3?4:10);
    memcpy(image,native,sizeof image);
    ScLandGraphics graphics={0};
    assert(ScLandGraphicsApply(&graphics,SC_LAND_SWAMP,7,true,0,image,colors));
    unsigned reeds=0,open_reeds=0,open_water=0;
    for(unsigned t=0;t<17;++t)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        unsigned a=chr_pixel(native+(0x360+t)*16,x,y),b=chr_pixel(image+(0x360+t)*16,x,y);
        if(a!=4 && a!=10)assert(a==b); /* Connected land and bank edges stay exact. */
        else {
            assert(a==b || (b>=13 && b<=15));
            reeds+=a!=b;
            if(t==16) {open_reeds+=a!=b;open_water+=a==b;}
        }
    }
    assert(reeds>100 && open_reeds>0 && open_water>open_reeds*2);
    for(unsigned i=0;i<32768;++i)if(i/16<0x360 || i/16>0x370)assert(image[i]==native[i]);
    assert(!ScLandGraphicsApply(&graphics,SC_LAND_SWAMP,7,true,0,image,colors));
    /* A complete new native water-animation frame keeps visible ripples and
     * becomes the restoration baseline, rather than baking reeds into water. */
    for(unsigned y=0;y<8;++y)for(unsigned x=2;x<7;++x) {
        unsigned ci=chr_pixel(native+0x370*16,x,y);
        set_chr_pixel(native+0x370*16,x,y,ci==4?10:4);
    }
    memcpy(image+0x370*16,native+0x370*16,32);
    assert(ScLandGraphicsApply(&graphics,SC_LAND_SWAMP,7,true,0,image,colors));
    uint16_t shown[16];memcpy(shown,image+0x370*16,sizeof shown);
    assert(ScLandGraphicsApply(&graphics,SC_LAND_MOON,1,true,0,image,colors));
    assert(!memcmp(image+0x360*16,native+0x360*16,16*32));
    assert(ScLandGraphicsApply(&graphics,SC_LAND_SWAMP,7,true,0,image,colors));
    assert(!memcmp(shown,image+0x370*16,sizeof shown));
    ScLandGraphicsApply(&graphics,SC_LAND_SWAMP,7,true,0x4000,image,colors);
    assert(!memcmp(image,native,0x4000*2));
    ScLandGraphicsApply(&graphics,SC_LAND_NATIVE,7,true,0,image,colors);
    assert(!memcmp(image,native,sizeof image));
    puts("PASS Swamp reeds: water-only pixels, open channels, animation, Moon/theme/base restoration");
}
static void check_natural_features(void) {
    ScMapGenState *st=calloc(1,sizeof *st);assert(st);
    const unsigned widths[]={120,240,480,960,1920,3840},heights[]={100,200,400,800,1600,3200};
    for(unsigned size=0;size<6;++size)for(unsigned land=0;land<SC_LAND_TYPES;++land) {
        if(land==SC_LAND_AMAZON || land==SC_LAND_SWAMP)continue; /* Extra-scatter tests below. */
        st->width=widths[size];st->height=heights[size];
        unsigned n=st->width*st->height,natural=0;
        for(unsigned i=0;i<n;++i) {
            st->map[i]=i%97==0?1:i%97==1?38:i%97==2?0x27c:20+i%18;
            natural+=i%97>2;
        }
        ScMapGenPrng pr={73,819,29},before=pr;
        sc_mapgen_land_features(&pr,st,land);
        assert(!memcmp(&pr,&before,sizeof pr));
        unsigned count=0,variants=0;
        for(unsigned i=0;i<n;++i) {
            unsigned tile=st->map[i]&1023;
            if(i%97<=2)assert(tile==(i%97==0?1:i%97==1?38:0x27c));
            else if(tile) {assert(tile>=20 && tile<38);++count;variants|=1u<<(tile-20);}
        }
        if(land==SC_LAND_MARS || land==SC_LAND_MOON)assert(count>natural/5 && count<natural*28/100);
        else if(land==SC_LAND_DESERT)assert(count>0 && count<natural/20);
        else assert(count==natural);
        if(land==SC_LAND_MARS || land==SC_LAND_MOON || land==SC_LAND_ARCTIC || land==SC_LAND_VENUS)assert(variants==((1u<<18)-1));
        if(land==SC_LAND_DESERT) {
            assert((variants>>15)==7);
            for(unsigned y=0;y<st->height;++y)for(unsigned x=0;x<st->width;++x) {
                unsigned tile=st->map[y*st->width+x]&1023;
                if(tile<SC_DESERT_CACTUS_FIRST || tile>=38)continue;
                for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
                    if(!dx && !dy)continue;
                    int nx=(int)x+dx,ny=(int)y+dy;
                    if(nx<0 || ny<0 || nx>=st->width || ny>=st->height)continue;
                    unsigned neighbour=st->map[ny*st->width+nx]&1023;
                    assert(neighbour<20 || neighbour>=40);
                }
            }
        }
        if(land==SC_LAND_MARS || land==SC_LAND_MOON || land==SC_LAND_DESERT)
            printf("natural features size=%u land=%s retained=%.2f%%\n",size,ScLandTypeName(land),100.0*count/natural);
    }
    free(st);
}
static void check_desert_cacti(void) {
    ScMapGenState *st=calloc(1,sizeof *st);assert(st);
    const unsigned widths[]={120,240,480,960,1920,3840},heights[]={100,200,400,800,1600,3200};
    for(unsigned size=0;size<6;++size) {
        st->width=widths[size];st->height=heights[size];
        unsigned sand=0;
        for(unsigned y=0;y<st->height;++y)for(unsigned x=0;x<st->width;++x) {
            unsigned i=y*st->width+x;
            st->map[i]=x<8?1:x<12?4:y<6?38:y<10?0x84:0;
            sand+=!st->map[i];
        }
        ScMapGenPrng pr={0x1234,0xabcd,0x9abc},before=pr;
        sc_mapgen_land_features(&pr,st,SC_LAND_DESERT);
        assert(!memcmp(&pr,&before,sizeof pr));
        unsigned count=0,variants=0;
        for(unsigned y=0;y<st->height;++y)for(unsigned x=0;x<st->width;++x) {
            unsigned i=y*st->width+x,tile=st->map[i];
            if(x<8)assert(tile==1);
            else if(x<12)assert(tile==4);
            else if(y<6)assert(tile==38);
            else if(y<10)assert(tile==0x84);
            else if(tile) {
                assert(tile>=SC_DESERT_CACTUS_FIRST && tile<38);
                ++count;variants|=1u<<(tile-SC_DESERT_CACTUS_FIRST);
                /* Identical natural amenity, pollution and density semantics. */
                assert(ScLandTilePack(tile)==ScLandTilePack(20));
            }
        }
        assert(variants==7 && count>sand/400 && count<sand/150);
        printf("PASS Desert size=%u: %u lone cacti, three variants, ordinary forest effects; protected tiles intact\n",size,count);
    }
    free(st);
}
static void check_swamp_foliage(void) {
    ScMapGenState *st=calloc(1,sizeof *st);assert(st);
    const unsigned widths[]={120,240,480,960,1920,3840},heights[]={100,200,400,800,1600,3200};
    for(unsigned size=0;size<6;++size) {
        st->width=widths[size];st->height=heights[size];
        unsigned old=0,now=0;
        for(unsigned y=0;y<st->height;++y)for(unsigned x=0;x<st->width;++x) {
            unsigned i=y*st->width+x;
            st->map[i]=x<8?1:x<12?4:y<6?38:y<10?0x84:x+12>st->width?24:0;
            old+=st->map[i]>=20 && st->map[i]<38;
        }
        ScMapGenPrng pr={0x1234,0xabcd,0x9abc};
        sc_mapgen_land_features(&pr,st,SC_LAND_SWAMP);
        for(unsigned y=0;y<st->height;++y)for(unsigned x=0;x<st->width;++x) {
            unsigned i=y*st->width+x;
            if(x<8)assert(st->map[i]==1);
            else if(x<12)assert(st->map[i]==4);
            else if(y<6)assert(st->map[i]==38);
            else if(y<10)assert(st->map[i]==0x84);
            now+=st->map[i]>=20 && st->map[i]<38;
        }
        assert(now>old);
        printf("PASS Swamp size=%u foliage=%u -> %u, water/shores/parks/buildings preserved\n",size,old,now);
    }
    free(st);
}
int main(void) {
    check_natural_features();
    check_desert_cacti();
    check_swamp_foliage();
    check_swamp_water();
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
        /* Only owned natural art changes. Swamp's shoreline land pixels,
         * parks, buildings, menus and underlying animation remain exact. */
        check_relief(type);
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
            check_relief(type);
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
    /* Switching relief styles or CHR bases restores native art, including
     * cartridge DMA refreshes while the replacement is visible. */
    ScLandGraphicsApply(&graphics,SC_LAND_MARS,7,true,0,vram,palette);
    unsigned forest_at=0x28e*16;vram[forest_at]=0xbeef;original[forest_at]=0xbeef;
    ScLandGraphicsApply(&graphics,SC_LAND_ARCTIC,1,true,0,vram,palette);
    check_relief(SC_LAND_ARCTIC);
    ScLandGraphicsApply(&graphics,SC_LAND_ARCTIC,1,true,0x4000,vram,palette);
    assert(!memcmp(vram,original,0x4000*2));
    ScLandGraphicsApply(&graphics,0,1,false,0,vram,palette);
    assert(!memcmp(vram,original,sizeof vram));
    /* Moon's ice freezes the open-water texture, including animation uploads,
     * and restores the latest native frame when switching back to Earth. */
    ScLandGraphicsApply(&graphics,SC_LAND_MOON,1,true,0,vram,palette);
    uint16_t ice[16];memcpy(ice,vram+0x370*16,sizeof ice);
    ScLandColors lunar=ScLandTypeColors(SC_LAND_MOON,7);
    assert(lunar.water[0]>=23 && lunar.water[1]>=27 && lunar.water[2]>=30);
    assert(!ScLandGraphicsApply(&graphics,SC_LAND_MOON,7,true,0,vram,palette));
    vram[0x370*16]=0xbeef;original[0x370*16]=0xbeef;
    assert(ScLandGraphicsApply(&graphics,SC_LAND_MOON,7,true,0,vram,palette));
    assert(!memcmp(ice,vram+0x370*16,sizeof ice));
    ScLandGraphicsApply(&graphics,SC_LAND_NATIVE,7,true,0,vram,palette);
    assert(!memcmp(vram,original,sizeof vram));
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
    /* Mars progress keeps all native roof/steel/UI colors, becomes green,
     * restores exact current native palette/CHR, and survives regression. */
    memcpy(original_palette,palette,sizeof palette);
    for(unsigned month=1;month<=12;++month) {
        uint32_t native=0xffc6b58c,green=ScLandTerraformPreviewColor(600,0,native,15,month);
        assert(((green>>8)&255)>((green>>16)&255));
        assert(ScLandTerraformPreviewColor(0,0,native,15,month)==ScLandPreviewColor(SC_LAND_MARS,0,native,15,month));
        assert(ScLandTerraformPreviewColor(1000,0,native,15,month)==native);
        assert(ScLandGraphicsTerraform(&graphics,0,month,true,0,vram,palette));
        uint16_t red=palette[23];
        ScLandGraphicsTerraform(&graphics,600,month,true,0,vram,palette);
        assert(((palette[23]>>5)&31)>(palette[23]&31));
        assert(palette[23]!=red);
        for(unsigned at=0;at<256;++at)
            if(at!=19 && at!=20 && at!=23 && at!=26 && at!=27 && at!=28 && at!=29 && at!=30 && at!=31 &&
               at!=114 && at!=124 && at!=125 && at!=126 && at!=127)assert(palette[at]==original_palette[at]);
        ScLandGraphicsTerraform(&graphics,999,month,true,0,vram,palette);
        ScLandGraphicsTerraform(&graphics,1000,month,true,0,vram,palette);
        assert(!memcmp(palette,original_palette,sizeof palette));
        assert(!memcmp(vram,original,sizeof vram));
        assert(!ScLandGraphicsTerraform(&graphics,1000,month,true,0,vram,palette));
    }
    puts("PASS: seasonal terrain/foliage, water skins, stadium domes, and reversible Mars-to-green-to-native terraforming palettes");
    return 0;
}
