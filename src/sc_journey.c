#include "sc_journey.h"
#include <stdlib.h>
#include <string.h>

static unsigned word(const uint8_t *r,unsigned p) {return r[p]|r[p+1]<<8;}
static void put(uint8_t *r,unsigned p,unsigned v) {r[p]=(uint8_t)v;r[p+1]=(uint8_t)(v>>8);}
void ScJourneyObservePopulation(ScWorld *w,uint64_t population) {
    if(!w->journey) return;
    unsigned target=population>=UINT64_C(1000000)?2:population>=UINT64_C(100000)?1:0;
    if(target>w->journey_target) w->journey_target=(uint8_t)target;
}
unsigned ScJourneyExpand(ScWorld *w,uint8_t *r,uint64_t population) {
    ScJourneyObservePopulation(w,population);
    if(!w->journey || w->huge || w->journey_notice ||
        w->journey_target<(w->active?2:1)) return 0;
    unsigned old_width=w->active?240:120,old_height=w->active?200:100;
    /* Whole eight-cell blocks keep every spatial field aligned with its city.
     * New land surrounds all four sides; the camera translates with the city. */
    unsigned dx=w->active?120:64,dy=w->active?104:48,stage=w->active?2:1;
    ScWorld *next=malloc(sizeof *next);if(!next) return 0;
    ScMapGenPrng seed={word(r,0x59),word(r,0x5b),word(r,0x5d)};
    seed.s0^=(uint16_t)(0x4a31+stage*0x137);seed.s1^=0x7939;
    if(stage==2) ScWorldGenerateHuge(next,&seed);else ScWorldGenerate(next,&seed);
    unsigned width=ScWorldWidth(next);
    const uint8_t *old=w->active?w->tiles:r+0x10200;
    for(unsigned y=0;y<old_height;++y)
        memcpy(next->tiles+2*((y+dy)*width+dx),old+2*y*old_width,old_width*2);
    for(unsigned f=0;f<17;++f) {
        const ScWorldField *layout=&ScWorldFields[f];
        unsigned ow=w->active?ScWorldFieldWidth(w,f):layout->stock_width;
        unsigned oh=w->active?ScWorldFieldHeight(w,f):layout->stock_height;
        unsigned divx=120/layout->stock_width,divy=f==5?1:divx;
        unsigned nw=ScWorldFieldWidth(next,f),bytes=layout->element_bytes;
        const uint8_t *src=w->active?w->fields[f]:r+0x10000+layout->base;
        for(unsigned y=0;y<oh;++y)
            memcpy(next->fields[f]+((y+dy/divy)*nw+dx/divx)*bytes,src+y*ow*bytes,ow*bytes);
    }
    next->journey=true;next->journey_notice=(uint8_t)stage;
    next->journey_target=w->journey_target;
    next->center_valid=true;
    next->center_x=(w->center_valid?w->center_x:r[0xba9])+dx;
    next->center_y=(w->center_valid?w->center_y:r[0xbaa])+dy;
    /* These are absolute city coordinates, not screen-space cursors. */
    const unsigned pairs[]={0x1bd,0x205,0x219,0x233,0x400};
    for(unsigned i=0;i<sizeof pairs/sizeof *pairs;++i) {
        put(r,pairs[i],word(r,pairs[i])+dx);put(r,pairs[i]+2,word(r,pairs[i]+2)+dy);
    }
    /* Seven native moving objects store full cell positions in four-byte rows. */
    for(unsigned i=0;i<7;++i) {
        put(r,0xa51+4*i,word(r,0xa51+4*i)+dx);
        put(r,0xa4f+4*i,word(r,0xa4f+4*i)+dy);
    }
    put(r,0xae1,word(r,0xae1)+dx*8);put(r,0xae3,word(r,0xae3)+dy*8);
    put(r,0x1c5,width-(word(r,0x1d7)?25:30));
    put(r,0x1c9,ScWorldHeight(next)-(word(r,0x1d7)?22:26));
    r[0xba9]=(uint8_t)next->center_x;r[0xbaa]=(uint8_t)next->center_y;
    memcpy(w,next,sizeof *w);free(next);ScWorldMirror(w,r);
    return stage;
}

enum {MENU_ADDRESS=0xfb4c,MENU_POINTER=0xa182,MENU_SPRITES=32};
static uint8_t menu[140],pairs[MENU_SPRITES][2];
static unsigned menu_size,sprite_count;
static unsigned tile(unsigned slot) {
    const unsigned bands[]={0x1e0,0x1c0,0x140,0x160};
    return bands[slot/8]+2*(slot%8);
}
unsigned ScJourneyMenuY(bool saved,unsigned selection) {return (saved?100:88)+24*selection;}
void ScJourneyMenuInit(const uint8_t *rom,size_t size) {
    menu_size=sprite_count=0;if(!rom || size!=0x80000) return;
    const char *lines[]={"PRACTICE","START NEW CITY","START NEW JOURNEY","SELECT SCENARIO"};
    unsigned attr=rom[0x23d3]&0xfe,flags_at=0,flags=0;
    for(unsigned row=0;row<4;++row) {
        const char *s=lines[row];unsigned x=74;
        while(*s) {
            if(*s==' ') {x+=8;++s;continue;}
            const char *end=s;while(*end && *end!=' ') ++end;
            unsigned letters=(unsigned)(end-s);
            for(unsigned j=0;j<letters;j+=2) {
                unsigned k=sprite_count++,t=tile(k),xb=(x-136)&255;
                pairs[k][0]=(uint8_t)s[j];pairs[k][1]=(uint8_t)(j+1<letters?s[j+1]:' ');
                if(k%8==0) {flags_at=menu_size;menu_size+=2;flags=0;}
                flags|=(2+(xb+136>=256))<<(2*(k%8));
                menu[flags_at]=(uint8_t)flags;menu[flags_at+1]=(uint8_t)(flags>>8);
                menu[menu_size++]=(uint8_t)xb;menu[menu_size++]=(uint8_t)(112+24*row-116);
                menu[menu_size++]=(uint8_t)t;menu[menu_size++]=(uint8_t)(attr|(t>>8));
                x+=16;
            }
            if(letters&1) x-=8;s=end;
        }
    }
    if(sprite_count%8==0) {menu[menu_size++]=1;menu[menu_size++]=0;}
    else {
        flags|=1u<<(2*(sprite_count%8));
        menu[flags_at]=(uint8_t)flags;menu[flags_at+1]=(uint8_t)(flags>>8);
    }
    menu[menu_size++]=0; /* native chain terminator */
}
void ScJourneyMenuFont(uint16_t *vram) {
    if(!vram || !sprite_count) return;
    for(unsigned k=0;k<sprite_count;++k) for(unsigned half=0;half<2;++half) {
        unsigned c=pairs[k][half],dst=tile(k)+half;
        if(c==' ') {memset(vram+dst*16,0,32);memset(vram+(dst+16)*16,0,32);continue;}
        unsigned src=c<='P'?c-'A':32+c-'Q';
        memcpy(vram+dst*16,vram+src*16,32);
        memcpy(vram+(dst+16)*16,vram+(src+16)*16,32);
    }
}
void ScJourneyMenuFrame(uint16_t *vram,unsigned base) {
    /* Extend the original frame by two tile rows so Select Scenario fits
     * below Journey even when Resume adds a fifth choice. Idempotent after
     * the menu packet's DMA; all border and fill tiles come from that frame. */
    if(!vram || base>0x7c00 || (vram[base+25*32+4]&1023)!=0x19c) return;
    memcpy(vram+base+27*32+4,vram+base+25*32+4,24*sizeof *vram);
    for(unsigned row=25;row<27;++row)
        memcpy(vram+base+row*32+4,vram+base+24*32+4,24*sizeof *vram);
}
bool ScJourneyMenuRead(uint32_t a,unsigned screen,uint8_t *v) {
    if(!menu_size || (screen!=2 && screen!=3 && screen!=18)) return false;
    a&=0x7fffff;
    if(a==MENU_POINTER || a==MENU_POINTER+1) {*v=(uint8_t)(MENU_ADDRESS>>(8*(a-MENU_POINTER)));return true;}
    if(a>=MENU_ADDRESS && a<MENU_ADDRESS+menu_size) {*v=menu[a-MENU_ADDRESS];return true;}
    if(a==0x03d34c) {*v=5;return true;} /* native Down wrap */
    if(a==0x03d359) {*v=4;return true;} /* native Up wrap */
    return false;
}
bool ScJourneyMessageRead(unsigned notice,uint32_t a,uint8_t *v) {
    static const char *const big[]={
        "NEW HORIZONS!","", "Congratulations, Mayor!", "100,000 citizens!",
        "We can open the borders", "to BIG: 240 x 200 tiles", "Your city is preserved.",
        "New land surrounds it,", "ready for homes, roads", "and jobs. Reach one",
        "million citizens for", "the HUGE expansion!"};
    static const char *const huge[]={
        "A GRAND JOURNEY!","", "Congratulations, Mayor!", "One million citizens!",
        "The borders are open!", "HUGE: 480 x 400 tiles", "Your city is preserved.",
        "New land surrounds it,", "ready for homes, roads", "and jobs. What a",
        "remarkable city!", "Let*s keep it growing!"};
    a&=0x7fffff;
    if(notice<1 || notice>2 || a<0x0ffd00 || a>=0x0fff00) return false;
    unsigned offset=a-0x0ffd00,row=offset/24,col=offset%24;
    unsigned rows=notice==1?sizeof big/sizeof *big:sizeof huge/sizeof *huge;
    const char *const *lines=notice==1?big:huge;
    *v=row>=rows?0xff:col<strlen(lines[row])?(uint8_t)lines[row][col]:' ';
    return true;
}
