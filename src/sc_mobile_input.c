#include "sc_mobile_input.h"
#include <math.h>
#include <string.h>

void ScTouchLayout(ScTouchInput *s,int w,int h) {
    s->width=w;s->height=h;
    float size=fmaxf(44,fminf(54,h/7.0f)),gap=5,step=size+gap;
    float left=12,bottom=h-size-12,right=w-size-12;
    const char *labels[]={"UP","LEFT","DOWN","RIGHT","B","A","Y","X","L","R","START","SEL","F12","-","+","SAVE","PAN","PAD"};
    const unsigned pads[]={0x10,0x40,0x20,0x80,1,0x100,2,0x200,0x400,0x800,8,4,0,0,0,0,0,0};
    const int col[]={1,0,1,2,2,1,0,1,0,2,4,3,0,1,2,3,4,0};
    const int row[]={2,1,0,1,1,0,1,2,3,3,0,0,0,0,0,0,0,0};
    for(int b=0;b<SC_TOUCH_BUTTONS;++b) {
        float x,y;
        if(b<4) {x=left+col[b]*step;y=bottom-row[b]*step;}
        else if(b<10) {x=right-(2-col[b])*step;y=bottom-row[b]*step;}
        else if(b<12) {x=w/2.0f+(col[b]-4)*step;y=bottom;}
        else if(b<17) {x=w/2.0f+(col[b]-2)*step;y=12;}
        else {x=right;y=12;}
        s->buttons[b]=(ScTouchRect){x,y,size,size,labels[b],pads[b],b>=12?b-11:0};
    }
}
static void aggregate(ScTouchInput *s) {
    s->pad=0;s->mouse_buttons=0;float x=0,y=0;unsigned n=0;
    for(unsigned i=0;i<SC_TOUCH_FINGERS;++i) {
        ScTouchFinger *f=s->fingers+i;if(!f->active)continue;
        if(f->button>=0) {s->pad|=s->buttons[f->button].pad;continue;}
        x+=f->x;y+=f->y;++n;
    }
    if(n>1)s->multi_pan=true;
    if(n) {s->mouse_x=x/n;s->mouse_y=y/n;s->mouse_buttons=n>1||s->pan?2:s->multi_pan?0:1;}
    else s->multi_pan=false;
}
void ScTouchUpdate(ScTouchInput *s,int64_t id,float x,float y,bool down,bool begin) {
    if(!isfinite(x)||!isfinite(y))return;
    ScTouchFinger *f=NULL;
    for(unsigned i=0;i<SC_TOUCH_FINGERS;++i)if(s->fingers[i].active&&s->fingers[i].id==id)f=s->fingers+i;
    if(begin&&!f)for(unsigned i=0;i<SC_TOUCH_FINGERS;++i)if(!s->fingers[i].active) {f=s->fingers+i;break;}
    if(!f)return;
    if(begin) {
        *f=(ScTouchFinger){id,x,y,-1,true};
        for(int b=0;b<SC_TOUCH_BUTTONS;++b) {
            if(!s->visible&&b!=17)continue;
            ScTouchRect r=s->buttons[b];
            if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h) {
                f->button=b;
                if(b==17)s->visible=!s->visible;
                else if(b==16)s->pan=!s->pan;
                else if(r.action)s->action=r.action;
                break;
            }
        }
    }
    f->x=x;f->y=y;f->active=down;
    if(down&&f->button>=0&&s->buttons[f->button].pad) {
        // Sliding between D-pad directions is allowed; a UI-owned contact
        // never becomes a construction tap when it leaves its button.
        int next=-1;
        for(int b=0;b<4;++b) {ScTouchRect r=s->buttons[b];if(x>=r.x&&x<r.x+r.w&&y>=r.y&&y<r.y+r.h)next=b;}
        if(f->button<4&&next>=0)f->button=next;
    }
    aggregate(s);
}
void ScTouchClear(ScTouchInput *s) {memset(s->fingers,0,sizeof s->fingers);s->pad=s->mouse_buttons=0;s->action=0;s->multi_pan=false;}
bool ScTouchActive(const ScTouchInput *s) {for(unsigned i=0;i<SC_TOUCH_FINGERS;++i)if(s->fingers[i].active)return true;return false;}
bool ScMobileRomValid(const void *data,unsigned bytes,unsigned *offset) {
    if(!data||(bytes!=0x80000&&bytes!=0x80200))return false;
    unsigned skip=bytes-0x80000;const unsigned char *rom=(const unsigned char *)data+skip;
    uint32_t hash=2166136261u;for(unsigned i=0;i<0x80000;++i)hash=(hash^rom[i])*16777619u;
    if(hash!=0xec01686au)return false;
    if(offset)*offset=skip;return true;
}
