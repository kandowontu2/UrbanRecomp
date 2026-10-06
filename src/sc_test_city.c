#include "sc_test_city.h"
#include "sc_construction.h"
#include "sc_land_summary.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
static unsigned word(const uint8_t *p,unsigned a) {return p[a]|p[a+1]<<8;}
static void put(uint8_t *p,unsigned a,unsigned v) {p[a]=(uint8_t)v;p[a+1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,unsigned a,uint32_t v) {put(p,a,v);put(p,a+2,v>>16);}
static uint32_t get32(const uint8_t *p,unsigned a) {return word(p,a)|(uint32_t)word(p,a+2)<<16;}
static bool commit(uint8_t *ram,ScWorld *w,const uint8_t *rom,ScBuildPlan *plan) {
    if(!plan->count)return true;
    unsigned price;ScBuildResult result=ScConstructionCommitWorld(ram,w,rom,0x80000,plan,&price);
    if(result!=SC_BUILD_OK)fprintf(stderr,"[test city] construction tool %u failed: %u\n",plan->tool,result);
    return result==SC_BUILD_OK;
}
static bool single(uint8_t *ram,ScWorld *w,const uint8_t *rom,ScBuildPlan *plan,unsigned tool,int x,int y) {
    plan->tool=tool;plan->count=1;plan->cells[0]=(ScBuildCell){x,y};return commit(ram,w,rom,plan);
}
void ScTestCityInspect(const ScWorld *w,const ScPopulation *p,const uint8_t *ram,const uint8_t *rom,ScTestCityStats *s) {
    memset(s,0,sizeof *s);s->population=p->value;
    const uint8_t *tiles=w->active?w->tiles:ram+0x10200;
    for(unsigned i=0;i<(w->active?ScWorldCells(w):12000);++i) {
        unsigned raw=word(tiles,2*i),t=raw&1023;
        if(t>=958 || !(rom[0x184eb+t]&1))continue;
        bool zone=false;
        if((t>=0x80 && t<0x129) || (t>=0x376 && t<0x39a)) {++s->residential;zone=true;}
        else if((t>=0x137 && t<0x1f4) || t>=0x39a) {++s->commercial;zone=true;}
        else if(t>=0x1f4 && t<0x249) {++s->industrial;zone=true;}
        else if(t==0x249)++s->police;
        else if(t==0x252)++s->fire;
        else if(t==0x27c || t==0x28c)++s->plants;
        else if(t==0x25c || t==0x36b)++s->stadiums;
        else if(t==0x26c)++s->seaports;
        else if(t==0x2a5)++s->airports;
        else if(t>=0x2bb && t<0x354)++s->rewards;
        if(zone) {++s->zones;if(raw&0x8000)++s->powered_zones;}
    }
    /* Ordinary fully developed R/C/I capacities, without speculative paired
     * tower upgrades: 40*20, 5*8*20, 4*8*20 (03:842f/8456/847a). */
    s->ordinary_capacity=(uint64_t)(s->residential+s->commercial)*800+(uint64_t)s->industrial*640;
}
/* Footprints are produced by the cartridge placement routine, then copied
 * into the blueprint. This avoids a full-world transaction for every lot. */
static unsigned footprint(unsigned tool) {return tool==12?6:tool>=10 && tool<=14?4:tool==4?1:3;}
static void stamp(uint8_t *tiles,unsigned width,unsigned x,unsigned y,const uint16_t art[36],unsigned side) {
    for(unsigned dy=0;dy<side;++dy)for(unsigned dx=0;dx<side;++dx)
        put(tiles,2*((y+dy)*width+x+dx),art[dy*6+dx]);
}
/* Exact native terrain calculation in a small candidate window: cap each
 * 2x2 accumulator at 255, add four into a byte, then diffuse 4x4 cells.
 * This scores the actual affected neighbours instead of a guessed radius. */
static unsigned terrain_raw(const uint8_t *tiles,unsigned width,unsigned height,int qx,int qy,
    unsigned gx,unsigned gy,const uint16_t *gift) {
    if(qx<0 || qy<0 || qx>=(int)width/4 || qy>=(int)height/4)return 0;
    unsigned sum=0;
    for(unsigned y=0;y<4;y+=2)for(unsigned x=0;x<4;x+=2) {
        uint16_t cell[4];
        for(unsigned n=0;n<4;++n) {
            unsigned xx=qx*4+x+n%2,yy=qy*4+y+n/2;
            cell[n]=(uint16_t)word(tiles,2*(yy*width+xx));
            if(gift && xx>=gx && xx<gx+3 && yy>=gy && yy<gy+3)cell[n]=gift[(yy-gy)*6+xx-gx];
        }
        unsigned v=ScLandSummaryPack(cell).stats&65535;
        sum+=v>255?255:v;
    }
    return sum&255;
}
static unsigned terrain_at(const unsigned q[36],unsigned x,unsigned y) {
    return (q[y*6+x]+(q[y*6+x-1]+q[y*6+x+1]+q[(y-1)*6+x]+q[(y+1)*6+x])/4)/2;
}
void ScTestCityConfigureStart(uint8_t *ram) {
    put(ram,0xbc5,32);put(ram,0xbc7,1000);put(ram,0xbc9,1000);put(ram,0xdc5,3);
    /* Already awarded normal quantities, not unlimited copies. No fountain
     * before 1950. Leave rank notices live so the mayor's house can upgrade. */
    static const unsigned counters[][2]={{0xca7,1},{0xca9,2},{0xcab,0x0203},{0xcad,2},
        {0xcb1,3},{0xcb3,3},{0xcb5,3},{0xcb7,3},{0xcb9,2},{0xcbb,2},{0xcbd,1},{0xcbf,1}};
    for(unsigned i=0;i<sizeof counters/sizeof *counters;++i)put(ram,counters[i][0],counters[i][1]);
}
bool ScTestCityGenerate(ScWorld *w,ScPopulation *population,uint8_t *ram,
    const uint8_t *rom,size_t size,unsigned map_size,unsigned development_speed,ScTestCityStats *stats) {
    if(!w || !ram || !rom || size!=0x80000 || map_size>5 || !ScWorldDevelopmentSpeedValid(development_speed))return false;
    uint8_t *prototype=calloc(1,0x20000);ScBuildPlan *plan=calloc(1,sizeof *plan);
    uint16_t art[16][36]={{0}},gifts[16][36]={{0}};
    if(!prototype || !plan) {free(prototype);free(plan);return false;}
    for(unsigned tool=1;tool<=15;++tool)for(unsigned gift=tool==15?1:0;gift<=(tool==15?15:0);++gift) {
        if(gift==6)continue; /* No water to reclaim on 31337. */
        memcpy(prototype,ram,0x10000);memset(prototype+0x10000,0,0x10000);
        put(prototype,0xb9d,0xffff);prototype[0xb9f]=0xff;prototype[0x3f5]=gift;put(prototype,0x3f3,0);
        if(!single(prototype,NULL,rom,plan,tool,8,8))goto fail;
        unsigned side=footprint(tool);uint16_t *a=gift?gifts[gift]:art[tool];
        for(unsigned dy=0;dy<side;++dy)for(unsigned dx=0;dx<side;++dx)
            a[dy*6+dx]=(uint16_t)word(prototype,0x10200+2*((8+dy)*120+8+dx));
    }
    memcpy(prototype,ram,0x10000);memset(prototype+0x10000,0,0x10000);
    put(prototype,0xb9d,0xffff);prototype[0xb9f]=0xff;
    for(unsigned tool=1;tool<=3;++tool) {
        plan->tool=tool;plan->count=0;
        unsigned pitch=tool==1?56:7;
        for(unsigned line=0;line<97;line+=pitch)for(unsigned at=0;at<97;++at)
            if(tool!=3 || at%7==1) {
                plan->cells[plan->count++]=(ScBuildCell){line,at};
                plan->cells[plan->count++]=(ScBuildCell){at,line};
            }
        if(!commit(prototype,NULL,rom,plan))goto fail;
    }
    ScWorldReset(w);w->active=map_size!=0;w->huge=map_size>=2;w->giant=map_size>=3;
    w->colossal=map_size>=4;w->mega=map_size==5;w->test_city=true;
    w->development_speed=(uint8_t)development_speed;
    unsigned width=w->active?ScWorldWidth(w):120,height=w->active?ScWorldHeight(w):100,scale=width/120;
    uint8_t *tiles=w->active?w->tiles:ram+0x10200;
    memset(tiles,0,width*height*2);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
        put(tiles,2*(y*width+x),word(prototype,0x10200+2*((y%56)*120+x%56)));
    /* Global concentric districts, not an industrial corner repeated beside
     * every housing cluster. Small local shops are still essential: a fully
     * segregated empty residential district cannot bootstrap native density. */
    for(unsigned by=0;by+7<height;by+=7)for(unsigned bx=0;bx+7<width;bx+=7) {
        bool plant=((by/7)%8==0 && (bx/7)%8==0) || ((by/7)%8==4 && (bx/7)%8==4);
        if(plant) {
            stamp(tiles,width,bx+1,by+1,art[13],4);
            for(unsigned at=5;at<7;++at) {
                put(tiles,2*((by+1)*width+bx+at),art[3][0]);
                put(tiles,2*((by+at)*width+bx+1),art[3][0]);
            }
            continue;
        }
        for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx) {
            unsigned x=bx+1+3*dx,y=by+1+3*dy;
            unsigned edge=x+1;if(width-x-2<edge)edge=width-x-2;if(y+1<edge)edge=y+1;if(height-y-2<edge)edge=height-y-2;
            unsigned tool=edge<4*scale?7:edge<12*scale?(dy?4:6):dy?(dx?4:6):5;
            if(!dx && !dy && (by/7)%4==1 && (bx/7)%4==1)tool=8;
            if(!dx && !dy && (by/7)%4==3 && (bx/7)%4==3)tool=9;
            if(!dx && !dy && (by/7)%8==1 && (bx/7)%8==3)tool=9;
            if(tool==4) {
                for(unsigned yy=0;yy<3;++yy)for(unsigned xx=0;xx<3;++xx)
                    stamp(tiles,width,x+xx,y+yy,art[4],1);
                if(!dx && dy)for(unsigned yy=0;yy<3;++yy)
                    put(tiles,2*((y+yy)*width+x),art[3][0]);
            } else stamp(tiles,width,x,y,art[tool],3);
        }
    }
    /* Polluting demand-cap facilities belong on the industrial edge. These
     * all count on dry land; only actual boat spawning requires water. */
    const unsigned facilities[3]={10,11,12};
    for(unsigned n=0;n<3;++n) {
        unsigned x=(n==0?width/2:n==1?width/4:3*width/4)/7*7,y=n==0?0:(height-8)/7*7;
        for(unsigned yy=1;yy<7;++yy)for(unsigned xx=1;xx<7;++xx)put(tiles,2*((y+yy)*width+x+xx),0);
        unsigned side=footprint(facilities[n]);stamp(tiles,width,x+1,y+1,art[facilities[n]],side);
        for(unsigned at=side+1;at<7;++at) {
            put(tiles,2*((y+1)*width+x+at),art[3][0]);
            put(tiles,2*((y+at)*width+x+1),art[3][0]);
        }
    }
    w->center_valid=true;w->center_x=width/2;w->center_y=height/2;
    if(!ScConstructionRefreshPower(ram,w,rom,size) || !ScConstructionPrimeWorldFields(ram,w,rom,size)) {fprintf(stderr,"[test city] initial fields failed\n");goto fail;}
    /* Amenities go to deficient neighbourhoods. HQs also supply doubled
     * services. Score actual native diffusion and capacity thresholds.
     * Previously placed gifts are included, so redundant aura scores less. */
    static const unsigned reward[]={7,7,7,8,8,8,1,2,3,3,3,3,3,4,4,10,11,12,12,13,13,13,14,14,14,15,15};
    const uint8_t *land=w->active?w->fields[0]:ram+0x16b00;
    const uint8_t *pollution=w->active?w->fields[2]:ram+0x18270;
    const uint8_t *terrain=w->active?w->fields[6]:ram+0x1ab74;
    for(unsigned n=0;n<sizeof reward/sizeof *reward;++n) {
        unsigned best_x=0,best_y=0;int best=INT_MIN;
        unsigned stride=scale>2?scale/2+1:1; /* Odd pitch visits all 4x4 alignments. */
        for(unsigned by=0;by+7<height;by+=7*stride)for(unsigned bx=0;bx+7<width;bx+=7*stride) {
          for(unsigned corner=0;corner<4;++corner) {
            unsigned x=bx+1+3*(corner%2),y=by+1+3*(corner/2);
            bool vacant=true;
            for(unsigned yy=0;yy<3;++yy)for(unsigned xx=0;xx<3;++xx) {
                unsigned t=word(tiles,2*((y+yy)*width+x+xx))&1023;
                if(t!=0x26 && t!=0x27 && (t<0x60 || t>=0x6d))vacant=false;
            }
            if(!vacant)continue;
            unsigned before[36],after[36];int qx=(int)x/4-2,qy=(int)y/4-2;
            for(unsigned yy=0;yy<6;++yy)for(unsigned xx=0;xx<6;++xx) {
                before[yy*6+xx]=terrain_raw(tiles,width,height,qx+xx,qy+yy,0,0,NULL);
                after[yy*6+xx]=terrain_raw(tiles,width,height,qx+xx,qy+yy,x,y,gifts[reward[n]]);
            }
            if(!n)for(unsigned yy=1;yy<5;++yy)for(unsigned xx=1;xx<5;++xx) {
                int ax=qx+xx,ay=qy+yy;
                if(ax<0 || ay<0 || ax>=(int)width/4 || ay>=(int)height/4)continue;
                if(terrain_at(before,xx,yy)!=terrain[ay*(width/4)+ax]) {
                    fprintf(stderr,"[test city] native terrain scoring mismatch %d,%d\n",ax,ay);goto fail;
                }
            }
            int score=0;
            for(int yy=(int)y-8;yy<=(int)y+8;++yy)for(int xx=(int)x-8;xx<=(int)x+8;++xx) {
                if(xx<0 || yy<0 || xx>=(int)width || yy>=(int)height)continue;
                unsigned t=word(tiles,2*(yy*width+xx))&1023;
                if(t!=0x84 && t!=0x13b)continue;
                unsigned i=(yy/2)*(width/2)+xx/2;
                unsigned sx=xx/4-qx,sy=yy/4-qy;
                if(sx<1 || sx>4 || sy<1 || sy>4)continue;
                int old=terrain_at(before,sx,sy),delta=(int)terrain_at(after,sx,sy)-old;
                if(delta<=0)continue;
                int value=(int)land[i]-pollution[i]+old-terrain[(yy/4)*(width/4)+xx/4];
                int target=t==0x84?150:160,gain=target-value;
                if(gain>delta)gain=delta;
                if(gain>0)score+=gain*(t==0x84?2:1);
                if(t==0x84 && value<94 && value+delta>=94)score+=200;
                if(t==0x13b && value<128 && value+delta>=128)score+=100;
            }
            if(score>best){best=score;best_x=x;best_y=y;}
          }
        }
        if(best==INT_MIN) {fprintf(stderr,"[test city] no gift parcel %u\n",n);goto fail;}
        stamp(tiles,width,best_x,best_y,gifts[reward[n]],3);
    }
    if(!ScConstructionRefreshPower(ram,w,rom,size) || !ScConstructionPrimeWorldFields(ram,w,rom,size)) {fprintf(stderr,"[test city] final fields failed\n");goto fail;}
    /* Even maximum native R demand (2000) cannot overcome land-pollution
     * below 32: 32*31-3000+2000 is still negative. Keep gift-rescued housing;
     * convert remaining impossible fringe lots to local employment instead.
     * Both empty footprints have zero density/pollution and the same owner
     * center and conductivity, so these initial spatial fields stay valid. */
    for(unsigned y=1;y+1<height;++y)for(unsigned x=1;x+1<width;++x) {
        unsigned at=2*(y*width+x),raw=word(tiles,at),i=(y/2)*(width/2)+x/2;
        if((raw&1023)!=0x84 || (int)land[i]-pollution[i]>=32)continue;
        stamp(tiles,width,x-1,y-1,art[6],3);
        for(unsigned yy=y-1;yy<=y+1;++yy)for(unsigned xx=x-1;xx<=x+1;++xx) {
            unsigned p=2*(yy*width+xx);put(tiles,p,word(tiles,p)|(raw&0x8000));
        }
    }
    put(ram,0x1bd,width/2-32);put(ram,0x1bf,height/2-24);put(ram,0x1c5,width-25);put(ram,0x1c9,height-22);
    put(ram,0xb53,1900);put(ram,0xb55,1);put(ram,0xb57,0);put(ram,0xb59,31337);
    w->calendar_year=1900;
    put(ram,0xb9d,0x1200);ram[0xb9f]=0x7a; /* 8 million ordinary treasury */
    static const char name[]="TEST CITY";ram[0xb5b]=sizeof name-1;
    for(unsigned i=0;i<sizeof name-1;++i)ram[0xb5c+i]=name[i]==' '?0x2f:name[i]-'A'+0x0a;
    put(ram,0x3e,2);put(ram,0x38,0);put(ram,0x425,0);ScTestCityConfigureStart(ram);
    ScWorldMirror(w,ram);ScPopulationImport(population,ram);
    if(!ScPopulationRefreshLive(population,ram,w,rom,size))goto fail;
    population->previous=population->value;population->change=0;ScPopulationMirror(population,ram);
    ScWorldTilesTouch(w,0,SC_WORLD_MAX_TILE_BYTES);ScTestCityInspect(w,population,ram,rom,stats);
    fprintf(stderr,"[test city] generated %ux%u map=31337 speed=%ux population=%llu ordinary-capacity=%llu R/C/I=%u/%u/%u powered=%u/%u plants=%u police=%u fire=%u gifts=%u\n",
        width,height,development_speed,(unsigned long long)population->value,(unsigned long long)stats->ordinary_capacity,
        stats->residential,stats->commercial,stats->industrial,stats->powered_zones,stats->zones,stats->plants,stats->police,stats->fire,stats->rewards);
    free(prototype);free(plan);return stats->zones && stats->powered_zones==stats->zones && stats->police && stats->fire;
fail:free(prototype);free(plan);return false;
}
size_t ScTestCityRecordSize(void) {return 24+0x8000+ScWorldEncodedSize()+SC_POPULATION_BYTES;}
bool ScTestCityRecordValid(const uint8_t *p,size_t n) {
    return p && n>=24+0x8000+48+SC_POPULATION_BYTES && n<=ScTestCityRecordSize() && !memcmp(p,"SCTEST3",7) && p[7]==1 &&
        get32(p,8)==n && get32(p,12)==ScWorldEncodedVersionSize(p[24+0x8000+7]) && n==24+0x8000+get32(p,12)+SC_POPULATION_BYTES && get32(p,16)==SC_POPULATION_BYTES &&
        !get32(p,20) && p[24+5]==1;
}
const uint8_t *ScTestCityNativeSave(const uint8_t *p,size_t n) {return ScTestCityRecordValid(p,n)?p+24:NULL;}
bool ScTestCityEncode(uint8_t *p,size_t n,const uint8_t *sram,const ScWorld *w,const ScPopulation *pop) {
    if(n!=ScTestCityRecordSize() || !w->test_city || !sram || sram[5]!=1)return false;
    memset(p,0,24);memcpy(p,"SCTEST3",7);p[7]=1;put32(p,8,n);put32(p,12,ScWorldEncodedSize());put32(p,16,SC_POPULATION_BYTES);
    memcpy(p+24,sram,0x8000);
    if(!ScWorldEncode(w,p+24+0x8000,ScWorldEncodedSize()))return false;
    ScPopulationEncode(pop,p+24+0x8000+ScWorldEncodedSize());return true;
}
bool ScTestCityDecode(const uint8_t *p,size_t n,ScWorld *w,ScPopulation *pop) {
    return ScTestCityRecordValid(p,n) && ScWorldDecode(w,p+24+0x8000,get32(p,12)) && w->test_city &&
        ScPopulationDecode(pop,p+24+0x8000+get32(p,12),SC_POPULATION_BYTES);
}
