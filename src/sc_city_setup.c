#include "sc_city_setup.h"
#include "snes/interp816.h"
#include <string.h>
static unsigned word(const uint8_t *r,unsigned p) {return r[p]|r[p+1]<<8;}
static void put(uint8_t *r,unsigned p,unsigned v) {r[p]=(uint8_t)v;r[p+1]=(uint8_t)(v>>8);}
bool ScCitySetupRomRead(const uint8_t *rom,size_t size,const uint8_t *ram,
        uint32_t pc,uint32_t address,uint8_t *value) {
    if(word(ram,0xb57)!=3 || !rom)return false;
    pc&=0x7fffff;address&=0x7fffff;
    static const struct {unsigned pc,table;} inherited[]={
        {0x038a40,0x038bcb},{0x038e4a,0x038fe8},{0x038ea0,0x038fee},
        {0x03aac3,0x03abf6},
        {0x03d9fd,0x03daaf},{0x03da04,0x03dab5},{0x03da0b,0x03dabb},
        {0x03da12,0x03dac1},{0x03da19,0x03dac7},{0x03da20,0x03dacd}};
    for(unsigned i=0;i<sizeof inherited/sizeof *inherited;++i)
        if(pc>=inherited[i].pc && pc<=inherited[i].pc+3 &&
            address>=inherited[i].table+6 && address<inherited[i].table+8) {
            unsigned source=(inherited[i].table&0x7fff)+(inherited[i].table>>16)*0x8000+4+
                address-inherited[i].table-6;
            if(source>=size)return false;
            *value=rom[source];return true;
        }
    if(pc>=0x03b923 && pc<=0x03b926 && address>=0x03b96f && address<=0x03b970) {
        *value=(uint8_t)(ScCityDisasterThreshold(3)>>(8*(address-0x03b96f)));return true;
    }
    /* The information report has three text IDs. Keep its safe Hard report. */
    if(pc>=0x02adea && pc<=0x02aded && address==0xb57) {*value=2;return true;}
    return false;
}
void ScCitySetupStep(Interp816 *c,uint8_t *r,unsigned size) {
    if(c->k!=3)return;
    if(c->pc==0xc65c) {
        unsigned difficulty=word(r,0xb57),funds=ScCityStartingFunds(size,difficulty);
        put(r,0xb9d,funds);r[0xb9f]=(uint8_t)(funds>>16);r[0xba0]=0;
        if(difficulty==3)c->pc=0xc667; /* preserve Super Hard instead of STZ $0b57 */
    }
    if(c->pc==0xd978) {
        unsigned selection=word(r,0xb57),keys=word(r,0xc9),next=selection;
        if(keys&0x0500)next=selection<3?selection+1:3; /* Right/Down */
        else if(keys&0x0a00)next=selection?selection-1:0; /* Left/Up */
        if(next!=selection) {put(r,0xb57,next);r[6]=7;}
        c->mf=c->xf=false;c->pc=0xd9bc;
    }
}
bool ScCitySetupFont(uint8_t out[128][8],const uint8_t *tiles,size_t size) {
    if(!tiles || size<0x1f7*16)return false;
    memset(out,0,128*8);
    /* The ROM's labels are proportional, prepacked text strips. Glyphs
     * cross eight-pixel tile boundaries; extract by pixel offset, not halves. */
    static const struct {unsigned tile,x,width;char ch;} glyphs[]={
        {0x1a0,0,3,'1'},{0x1a1,0,3,'2'},{0x1a2,0,3,'3'},
        {0x1a4,0,3,'0'},{0x1a6,0,3,'5'},{0x1a8,2,3,'$'},
        {0x1d3,0,5,'E'},{0x1d3,6,4,'a'},{0x1d3,11,4,'s'},{0x1d3,16,4,'y'},
        {0x1d6,0,5,'M'},{0x1d6,6,3,'e'},{0x1d6,10,3,'d'},
        {0x1d6,14,1,'i'},{0x1d6,16,3,'u'},{0x1d6,20,5,'m'},
        {0x1da,0,5,'H'},{0x1da,11,4,'r'},{0x1a9,3,6,'S'},
        {0x1ed,0,6,'N'}};
    for(unsigned i=0;i<sizeof glyphs/sizeof *glyphs;++i)for(unsigned y=0;y<8;++y)
        for(unsigned x=0;x<glyphs[i].width;++x) {
            unsigned px=glyphs[i].x+x,at=(glyphs[i].tile+px/8)*16+y*2,bit=7-px%8;
            unsigned ci=((tiles[at]>>bit)&1)|(((tiles[at+1]>>bit)&1)<<1);
            if(ci==1)out[(unsigned char)glyphs[i].ch][y]|=1u<<(7-x);
        }
    static const uint8_t four[]={2,6,10,14,2},six[]={6,8,14,10,14},eight[]={14,10,14,10,14},p[]={0,12,10,12,8,8},D[]={30,17,17,17,17,17,30};
    for(unsigned y=0;y<5;++y) {out['4'][y+2]=four[y]<<4;out['6'][y+2]=six[y]<<4;out['8'][y+2]=eight[y]<<4;}
    for(unsigned y=0;y<6;++y)out['p'][y+1]=p[y]<<4;
    for(unsigned y=0;y<7;++y)out['D'][y]=D[y]<<3;
    return true;
}
