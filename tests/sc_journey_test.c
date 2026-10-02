#include "sc_journey.h"
#include "sc_population.h"
#include "sc_construction.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static ScWorld world,before,decoded;
static uint8_t ram[0x20000],original[0x20000],rom[0x80000],sram[0x8000];
static uint16_t vram[0x8000],font[0x8000];
static unsigned word(const uint8_t *r,unsigned a) {return r[a]|r[a+1]<<8;}
static void put(unsigned a,unsigned v) {ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static uint8_t read_bus(void *ctx,uint32_t a) {
    (void)ctx;uint8_t v;if(ScJourneyMenuRead(a,3,&v)) return v;
    unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f) return ram[a-0x7e0000];
    if(p<0x2000) return ram[p];
    if(p>=0x8000 && bank<16) return rom[bank*32768+p-0x8000];
    return 0;
}
static void write_bus(void *ctx,uint32_t a,uint8_t v) {
    (void)ctx;unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f) ram[a-0x7e0000]=v;else if(p<0x2000) ram[p]=v;
}
static void menu_test(void) {
    ScJourneyMenuInit(rom,sizeof rom);
    for(unsigned i=0;i<0x8000;++i) vram[i]=(uint16_t)(i*197+57);
    memcpy(font,vram,sizeof font);ScJourneyMenuFont(vram);
    /* First pair PR uses the original 8x16 P and R, not another font. */
    assert(!memcmp(vram+0x1e0*16,font+15*16,32));
    assert(!memcmp(vram+0x1f0*16,font+31*16,32));
    assert(!memcmp(vram+0x1e1*16,font+33*16,32));
    /* Original panel grows to contain the fifth choice without replacing art. */
    unsigned base=0x3000;vram[base+25*32+4]=0x199c;
    uint16_t bottom[24],side[24];
    memcpy(bottom,vram+base+25*32+4,sizeof bottom);memcpy(side,vram+base+24*32+4,sizeof side);
    ScJourneyMenuFrame(vram,base);
    assert(!memcmp(bottom,vram+base+27*32+4,sizeof bottom));
    assert(!memcmp(side,vram+base+25*32+4,sizeof side));
    assert(!memcmp(side,vram+base+26*32+4,sizeof side));
    memcpy(font,vram,sizeof font);ScJourneyMenuFrame(vram,base);assert(!memcmp(font,vram,sizeof font));
    for(unsigned saved=0;saved<2;++saved) {
        memset(ram,0,sizeof ram);put(0x261,15);put(0x25d,136);put(0x25f,saved?128:116);
        Interp816 *c=interp816_init(NULL,read_bus,write_bus);assert(c);interp816_reset(c);
        c->k=c->db=0;c->pc=0x8ea9;c->sp=0x1ffd;c->dp=0;c->e=c->mf=c->xf=false;
        put(0x1ffe,0x6fff);unsigned steps=0;
        while(c->pc!=0x7000) {assert(++steps<20000);interp816_runOpcode(c);}
        /* Four native lines, 27 sprites, terminating before the next OAM slot. */
        assert(word(ram,0x253)==27*4);
        const unsigned counts[]={4,7,9,7};unsigned k=0;
        for(unsigned row=0;row<4;++row) for(unsigned i=0;i<counts[row];++i,++k) {
            assert(ram[0x2001+4*k]==ScJourneyMenuY(saved,row+1));
            assert(ram[0x2000+4*k]>=74 && ram[0x2000+4*k]<224);
        }
        interp816_free(c);
    }
    /* Dispatch changes the screen first; the native menu emitter then runs
     * again during the fade on every destination screen. */
    const unsigned fades[]={4,10,14,16};
    for(unsigned i=0;i<sizeof fades/sizeof fades[0];++i) {
        uint8_t value;assert(ScJourneyMenuRead(0xa182,fades[i],&value) && value==0x4c);
        assert(ScJourneyMenuRead(0xfb4c,fades[i],&value));
        assert(!ScJourneyMenuRead(0x03d34c,fades[i],&value));
    }
}
static void saved(void) {
    size_t n=ScWorldEncodedSize();uint8_t *p=malloc(n);assert(p);
    assert(ScWorldEncode(&world,p,n));assert(ScWorldDecode(&decoded,p,n));
    assert(!memcmp(&world,&decoded,sizeof world));free(p);
    n=ScWorldCitiesSize();p=malloc(n);assert(p);ScWorldCitiesInit(p);
    for(unsigned slot=0;slot<2;++slot) {
        assert(ScWorldCitySave(p,sram,slot,&world));assert(ScWorldCityLoad(&decoded,p,n,sram,slot));
        assert(!memcmp(&world,&decoded,sizeof world));
    }
    free(p);
}
static void preserved(unsigned ow,unsigned oh,unsigned dx,unsigned dy,bool host) {
    for(unsigned y=0;y<oh;++y) for(unsigned x=0;x<ow;++x)
        assert(ScWorldCell(&world,x+dx,y+dy)==(host?ScWorldCell(&before,x,y):word(original,0x10200+2*(y*ow+x))));
    for(unsigned f=0;f<17;++f) {
        const ScWorldField *layout=&ScWorldFields[f];unsigned bytes=layout->element_bytes;
        unsigned width=host?ScWorldFieldWidth(&before,f):layout->stock_width;
        unsigned height=host?ScWorldFieldHeight(&before,f):layout->stock_height;
        unsigned divx=120/layout->stock_width,divy=f==5?1:divx,nw=ScWorldFieldWidth(&world,f);
        const uint8_t *src=host?before.fields[f]:original+0x10000+layout->base;
        for(unsigned y=0;y<height;++y)
            assert(!memcmp(world.fields[f]+((y+dy/divy)*nw+dx/divx)*bytes,src+y*width*bytes,width*bytes));
    }
    assert(!memcmp(ram+0xb9d,original+0xb9d,3)); /* funds */
    assert(!memcmp(ram+0xb51,original+0xb51,6)); /* calendar */
    assert(!memcmp(ram+0x59,original+0x59,6)); /* live random stream */
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);menu_test();
    memset(ram,0,sizeof ram);ScWorldReset(&world);
    for(unsigned i=0;i<sizeof ram;++i) ram[i]=(uint8_t)(i*13+9);
    put(0x1bd,20);put(0x1bf,15);put(0x205,37);put(0x207,32);
    ram[0xba9]=60;ram[0xbaa]=50;
    memcpy(original,ram,sizeof ram);
    assert(!ScJourneyExpand(&world,ram,SC_POPULATION_MAX)); /* ordinary city */
    world.journey=true;assert(!ScJourneyExpand(&world,ram,99999));saved();
    ScJourneyObservePopulation(&world,100000); /* crossing is latched before a safe boundary */
    assert(ScJourneyExpand(&world,ram,99980)==1 && world.active && !world.huge);
    preserved(120,100,64,48,false);assert(word(ram,0x205)==101 && word(ram,0x207)==80);
    world.journey_announcing=true;saved();
    assert(!ScJourneyExpand(&world,ram,1000000)); /* finish Big celebration first */
    assert(world.journey_target==2);
    world.journey_notice=0;world.journey_announcing=false;
    memcpy(&before,&world,sizeof world);memcpy(original,ram,sizeof ram);
    assert(ScJourneyExpand(&world,ram,900000)==2 && world.huge);
    preserved(240,200,120,104,true);saved();
    world.journey_notice=0;assert(!ScJourneyExpand(&world,ram,SC_POPULATION_MAX));
    /* Actual construction and original flood fill on newly unlocked land. */
    memset(world.tiles+2*(380*480+450),0,60);for(unsigned y=379;y<386;++y)
        for(unsigned x=450;x<475;++x) ScWorldPutCell(&world,x,y,0);
    put(0xb9d,60000);ram[0xb9f]=0;ScBuildPlan plan;unsigned cost;
    assert(ScConstructionPlanWorld(&plan,&world,14,460,380,460,380));
    assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
    assert(ScConstructionPlanWorld(&plan,&world,3,463,381,470,381));
    assert(ScConstructionCommitWorld(ram,&world,rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
    assert(ScConstructionRefreshPower(ram,&world,rom,sizeof rom));
    assert(ScWorldCell(&world,470,381)&0x8000);
    for(unsigned notice=1;notice<=2;++notice) {
        uint8_t v;unsigned n=0;while(ScJourneyMessageRead(notice,0x0ffd00+n,&v) && v!=255) {
            assert(v>=32 && v<=126);assert(++n<=24*13);
            if((n-1)%24>=23) assert(v==' '); /* keep the clipped rightmost column empty */
        }
        assert(n%24==0 && n>=24*10);
    }
    puts("PASS: native font and menu emitter, thresholds, all tiles/fields preserved, camera, economy, PRNG, Journey saves, new-land construction/power and adviser records");
}
