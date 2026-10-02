#include "sc_terrain.h"
#include <stdlib.h>
#include <string.h>
bool ScTerrainResize(ScTerrainFrame *f,unsigned width,unsigned height) {
    if(!width || !height || width>4096 || height>4096) return false;
    if(f->width==width && f->height==height && f->tiles) return true;
    unsigned stride=(width+14)/8;
    ScTerrainTile *tiles=calloc((size_t)stride*height,sizeof *tiles);
    uint32_t *palette=calloc((size_t)height*256,sizeof *palette);
    ScTerrainRow *rows=calloc(height,sizeof *rows);
    if(!tiles || !palette || !rows) {free(tiles);free(palette);free(rows);return false;}
    ScTerrainDestroy(f);
    *f=(ScTerrainFrame){width,height,stride,tiles,palette,rows,0};return true;
}
void ScTerrainDestroy(ScTerrainFrame *f) {
    free(f->tiles);free(f->palette);free(f->rows);memset(f,0,sizeof *f);
}
static unsigned decode(uint32_t planes,unsigned bit) {
    return ((planes>>bit)&1)|(((planes>>(bit+8))&1)<<1)|
        (((planes>>(bit+16))&1)<<2)|(((planes>>(bit+24))&1)<<3);
}
uint32_t ScTerrainPixel(const ScTerrainFrame *f,unsigned x,unsigned y) {
    if(!f || !f->tiles || x>=f->width || y>=f->height) return 0xff000000;
    const ScTerrainRow *row=f->rows+y;
    unsigned px=x+row->phase;
    const ScTerrainTile *t=f->tiles+(size_t)y*f->stride+px/8;
    unsigned b=decode(t->base,t->attributes&(1<<16)?px&7:7-(px&7));
    unsigned roof=decode(t->roof,t->attributes&(1<<17)?px&7:7-(px&7));
    unsigned index=roof?roof+((t->attributes>>8)&127):b?b+(t->attributes&127):0;
    int edge=(int)x-(int)row->core_x;if(edge<0) edge=0;if(edge>255) edge=255;
    /* Same window truth table and integer colour arithmetic as the CPU
     * compositor, applied to captured data only. Used for fallback/captures. */
    bool windows[2];const unsigned layers[]={1,5};
    for(unsigned i=0;i<2;++i) {
        unsigned flags=(row->windows>>(layers[i]*4))&15;
        bool a=(edge>=(int)(row->bounds&255) && edge<=(int)((row->bounds>>8)&255))!=((flags&1)!=0);
        bool b=(edge>=(int)((row->bounds>>16)&255) && edge<=(int)(row->bounds>>24))!=((flags&4)!=0);
        if(!(flags&2)) windows[i]=(flags&8)?b:false;
        else if(!(flags&8)) windows[i]=a;
        else switch((row->logic>>(layers[i]*2))&3) {
            case 0:windows[i]=a||b;break;case 1:windows[i]=a&&b;break;
            case 2:windows[i]=a!=b;break;default:windows[i]=a==b;break;
        }
    }
    unsigned main=(row->main&2) && (!(row->window_main&2) || !windows[0])?index:0;
    unsigned sub=(row->sub&2) && (!(row->window_sub&2) || !windows[0])?index:
        row->sub&4?row->sub_bg[edge]:0;
    const uint32_t *palette=f->palette+(size_t)y*256;
    unsigned c=palette[main],clip=(row->control>>6)&3,prevent=(row->control>>4)&3;
    if(clip==3 || (clip==2 && windows[1]) || (clip==1 && !windows[1])) c=0;
    bool math=(row->math&(1<<(main?1:5))) &&
        !(prevent==3 || (prevent==2 && windows[1]) || (prevent==1 && !windows[1]));
    unsigned second=(row->control&2) && sub?palette[sub]:row->fixed;
    uint32_t result=0xff000000;
    for(unsigned channel=0;channel<3;++channel) {
        int value=(c>>(channel*5))&31;
        if(math) {
            int operand=(second>>(channel*5))&31;
            value+=row->math&128?-operand:operand;
            if((row->math&64) && (sub || !(row->control&2))) value>>=1;
            if(value<0) value=0;
            if(value>31) value=31;
        }
        result|=row->brightness[value]<<(16-channel*8);
    }
    return result;
}
