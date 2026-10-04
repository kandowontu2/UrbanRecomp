#include "sc_obj.h"
#include "snes/ppu.h"
#include <assert.h>
#include <string.h>

static Ppu *owner;
static ScObjSliver slivers[SC_OBJ_MAX_SLIVERS];
static unsigned count;
static bool complete,blocks[(kPpuBufWidth+7)/8];
void ScObjBegin(Ppu *p) {owner=p;count=0;complete=false;memset(blocks,0,sizeof blocks);}
void ScObjAppend(uint32_t position,uint32_t source,uint32_t planes) {
    assert(count<SC_OBJ_MAX_SLIVERS);slivers[count++]=(ScObjSliver){position,source,planes};
}
void ScObjInvalidate(const Ppu *p) {if(owner==p) {owner=NULL;count=0;}}
bool ScObjSnapshot(const Ppu *p,const ScObjSliver **data,unsigned *size) {
    if(owner!=p) return false;
    if(data) *data=slivers;if(size) *size=count;return true;
}
static unsigned pixel(const ScObjSliver *s,unsigned dx) {
    unsigned bits=s->planes>>((s->source&0x8000)?dx:7-dx);
    return (bits&1)|((bits>>7)&2)|((bits>>14)&4)|((bits>>21)&8);
}
static void render(int left,int right) {
    for(int x=left;x<right;++x) owner->objBuffer.data[x+kPpuExtraLeftRight]=0x0500;
    for(unsigned i=0;i<count;++i) {
        const ScObjSliver *s=slivers+i;int x=(int16_t)s->position;
        int begin=x+((s->source>>16)&15),end=x+((s->source>>20)&15);
        if(begin<left) begin=left;if(end>right) end=right;
        for(int at=begin;at<end;++at) {
            unsigned value=pixel(s,at-x);
            if(value) owner->objBuffer.data[at+kPpuExtraLeftRight]=(s->position>>16)+value;
        }
    }
}
unsigned ScObjPixel(const Ppu *p,int x) {
    assert(x>=-kPpuExtraLeftRight && x<256+kPpuExtraLeftRight);
    if(owner==p && !complete) {
        unsigned block=(x+kPpuExtraLeftRight)/8;
        if(!blocks[block]) {
            int left=(int)(block*8)-kPpuExtraLeftRight;
            render(left,left+8);blocks[block]=true;
        }
    }
    return p->objBuffer.data[x+kPpuExtraLeftRight];
}
void ScObjMaterialize(const Ppu *p) {
    if(owner==p && !complete) {render(-kPpuExtraLeftRight,256+kPpuExtraLeftRight);complete=true;}
}
void ScObjCopyBuffer(Ppu *destination,const Ppu *source) {
    ScObjMaterialize(source);memcpy(&destination->objBuffer,&source->objBuffer,sizeof source->objBuffer);
}
