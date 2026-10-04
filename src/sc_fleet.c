#include "sc_fleet.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
enum { DISTRICTS=1024,TYPES=4 }; /* train, plane, ship, helicopter */
typedef struct {unsigned anchor;double x,y,phase;unsigned direction;bool active;} Vehicle;
struct ScFleet {
    const ScWorld *world;uint64_t epoch,last_frame;
    unsigned width,scan;uint64_t revisions[SC_WORLD_TILE_CHUNKS];
    Vehicle vehicles[DISTRICTS][TYPES];
};
static unsigned tile(const ScWorld *w,int x,int y) {return ScWorldContains(w,x,y)?ScWorldCell(w,x,y)&1023:1023;}
static bool rail(unsigned t) {return (t>=0x6d && t<0x7f && t!=0x6f) || (t>=0x34b && t<0x354);}
static bool sea(const ScWorld *w,int x,int y) {
    /* The 32px ship needs a water footprint, not just a wet centre cell. */
    for(int dy=-2;dy<2;++dy)for(int dx=-2;dx<2;++dx) {
        unsigned t=tile(w,x+dx,y+dy);if(!t || t>=20)return false;
    }
    return true;
}
ScFleet *ScFleetCreate(void) {return calloc(1,sizeof(ScFleet));}
void ScFleetFree(ScFleet *f) {free(f);}
static void start(ScFleet *f,const ScWorld *w,unsigned district,unsigned type,unsigned anchor) {
    Vehicle *v=&f->vehicles[district][type];if(v->active)return;
    int x=anchor%f->width,y=anchor/f->width;
    if(type==2) {
        bool found=false;
        for(int r=4;r<=48 && !found;r+=2)for(int d=0;d<8 && !found;++d) {
            static const int dx[]={1,1,0,-1,-1,-1,0,1},dy[]={0,1,1,1,0,-1,-1,-1};
            int px=x+r*dx[d],py=y+r*dy[d];if(sea(w,px,py)) {x=px;y=py;found=true;}
        }
        if(!found)return;
    }
    *v=(Vehicle){.anchor=anchor,.x=x*8+4,.y=y*8+4,.active=true,
        .phase=(district*37%360)*.0174532925199433,.direction=district&3};
}
void ScFleetStep(ScFleet *f,const ScWorld *w,uint64_t frame,bool moving) {
    if(!f || !w || !w->active)return;
    uint64_t epoch=ScWorldStateEpoch();unsigned width=ScWorldWidth(w),cells=ScWorldCells(w);
    if(f->world!=w || f->epoch!=epoch || f->width!=width) {
        memset(f,0,sizeof *f);f->world=w;f->epoch=epoch;f->width=width;f->last_frame=frame;
    }
    unsigned districts=width/120*(ScWorldHeight(w)/100);
    bool invalidated=false;
    for(unsigned i=0;i<districts;++i)for(unsigned type=0;type<TYPES;++type) {
        Vehicle *v=&f->vehicles[i][type];if(!v->active)continue;
        unsigned cell=ScWorldCell(w,v->anchor%width,v->anchor/width),t=cell&1023;
        bool valid=type==0?rail(t):(cell&0x8000) && (type==2?t==0x26c:t==0x2a5);
        if(!valid) {v->active=false;invalidated=true;}
    }
    if(invalidated)memset(f->revisions,0,sizeof f->revisions);
    const uint64_t *revisions=ScWorldTileRevisions(w);unsigned chunks=(cells+255)/256,budget=65536;
    for(unsigned visits=0;visits<chunks && budget;visits++) {
        unsigned chunk=f->scan++%chunks;if(f->revisions[chunk]==revisions[chunk] && revisions[chunk])continue;
        f->revisions[chunk]=revisions[chunk];unsigned begin=chunk*256,end=begin+256;
        if(end>cells)end=cells;budget-=end-begin;
        for(unsigned at=begin;at<end;++at) {
            unsigned t=(w->tiles[2*at]|w->tiles[2*at+1]<<8)&1023;
            if(!rail(t) && t!=0x2a5 && t!=0x26c)continue;
            unsigned district=(at/width/100)*(width/120)+(at%width/120);
            if(rail(t))start(f,w,district,0,at);
            else if(w->tiles[2*at+1]&128) {
                if(t==0x26c)start(f,w,district,2,at);
                else {start(f,w,district,1,at);start(f,w,district,3,at);}
            }
        }
    }
    unsigned steps=moving && frame>f->last_frame?(unsigned)(frame-f->last_frame):0;
    if(steps>64)steps=64;f->last_frame=frame;
    static const int dx[]={1,0,-1,0},dy[]={0,1,0,-1};
    for(unsigned step=0;step<steps;++step)for(unsigned i=0;i<districts;++i)for(unsigned type=0;type<TYPES;++type) {
        Vehicle *v=&f->vehicles[i][type];if(!v->active)continue;
        if(type==1 || type==3) {
            v->phase+=type==1?.007:.011;
            double radius=type==1?160:52;
            v->x=(v->anchor%width)*8+4+cos(v->phase)*radius;
            v->y=(v->anchor/width)*8+4+sin(v->phase)*radius*.6;
            if(v->x<16)v->x=16;if(v->x>width*8-16)v->x=width*8-16;
            if(v->y<16)v->y=16;if(v->y>ScWorldHeight(w)*8-16)v->y=ScWorldHeight(w)*8-16;
            v->direction=((unsigned)(v->phase*4/3.141592653589793)+2)&7;continue;
        }
        double speed=type==0?.5:.25;
        int x=(int)floor(v->x/8),y=(int)floor(v->y/8);
        if(fmod(v->x,8)==4 && fmod(v->y,8)==4) {
            bool found=false;
            const unsigned turns[]={0,1,3,2};
            for(unsigned turn=0;turn<4;++turn) {
                unsigned d=(v->direction+turns[turn])&3;
                if(type==0?rail(tile(w,x+dx[d],y+dy[d])):sea(w,x+dx[d],y+dy[d])) {v->direction=d;found=true;break;}
            }
            if(!found)continue;
        }
        double nx=v->x+dx[v->direction]*speed,ny=v->y+dy[v->direction]*speed;
        int px=(int)floor(nx/8),py=(int)floor(ny/8);
        if(type==0?!rail(tile(w,px,py)):!sea(w,px,py)) {
            bool found=false;
            for(unsigned turn=1;turn<=4;++turn) {
                unsigned d=(v->direction+turn)&3;
                if(type==0?rail(tile(w,x+dx[d],y+dy[d])):sea(w,x+dx[d],y+dy[d])) {v->direction=d;found=true;break;}
            }
            if(!found) {v->active=false;memset(f->revisions,0,sizeof f->revisions);}
        } else {v->x=nx;v->y=ny;}
    }
}
unsigned ScFleetCount(const ScFleet *f,unsigned type) {
    unsigned n=0;if(f && type<TYPES)for(unsigned i=0;i<DISTRICTS;++i)n+=f->vehicles[i][type].active;return n;
}
unsigned ScFleetShown(const ScFleet *f,const ScWorld *w,const uint8_t *rom,
    int native_x,int native_y,int left,int top,int right,int bottom,ScVehicleSprite *out,unsigned capacity) {
    if(!f || !w || !w->active || !rom)return 0;unsigned count=0;
    for(unsigned i=0;i<DISTRICTS;++i)for(unsigned type=0;type<TYPES;++type) {
        const Vehicle *v=&f->vehicles[i][type];if(!v->active)continue;
        int size=type==0?16:type==3?16:32,x=(int)v->x-size/2,y=(int)v->y-size/2;
        if(x+size<left || x>right || y+size<top || y>bottom)continue;
        unsigned attr=0x284e,offset=0;
        if(type==1) {unsigned dir=v->direction&7;attr=0x3008|rom[0x3bd3+dir]<<8;offset=rom[0x3bdb+dir];}
        else if(type==2) {unsigned dir=(v->direction*2)&7,index=rom[0x3c2f+dir];attr=rom[0x3c2b+index]<<8;offset=rom[0x3c27+index];}
        else if(type==3) {unsigned dir=v->direction&7;attr=(rom[0x3c1f+dir]<<8)|rom[0x3c0f+dir];}
        else if(v->direction==2)attr|=0x4000;
        unsigned parts=size==32?4:1;
        for(unsigned part=0;part<parts && count<capacity;++part) {
            unsigned word=attr;if(parts==4) {unsigned at=0x3bb3+offset+part*2;word+=rom[at]|rom[at+1]<<8;}
            uint32_t chr=0;
            /* Train and ship CHR is normally replaced by guest DMA for its
             * single vehicle. Each added vehicle needs its own heading, even
             * when the guest vehicle is absent. Read the same cartridge art. */
            if(type==0)chr=0x80000000u|((v->direction&1?0x1e80:0x1e00)/2);
            if(type==2) {
                unsigned heading=(v->direction*2)&7,at=0x3a9a+heading*2;
                unsigned source=(rom[at]|rom[at+1]<<8)-0x8000;
                unsigned tile=word&255;
                chr=0xc0000000u|(source+(tile&3)*32+(tile/4)*256)/2;
            }
            out[count++]=(ScVehicleSprite){.slot=-1,.x=x+(part&1)*16-native_x,
                .y=y+(part>>1)*16-native_y-1,.large=true,.host=true,.attr=(uint16_t)word,.rom_chr=chr};
        }
        if(type==3 && !(v->direction&1) && count<capacity) {
            unsigned dir=v->direction&6,rotor=rom[0x3c17+dir]+((unsigned)(v->phase*100)&1);
            int rx=(int16_t)(rom[0x3bff+dir]|rom[0x3c00+dir]<<8)*8;
            int ry=(int16_t)(rom[0x3c07+dir]|rom[0x3c08+dir]<<8)*8;
            out[count++]=(ScVehicleSprite){.slot=-1,.x=x+rx-native_x,.y=y+ry-native_y-1,
                .large=false,.host=true,.attr=(uint16_t)((rom[0x3c1f+dir]<<8)|rotor)};
        }
    }
    return count;
}
