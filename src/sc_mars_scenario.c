#include "sc_mars_scenario.h"
#include "sc_construction.h"
#include "sc_land_type.h"
#include "snes/interp816.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static unsigned word(const uint8_t *r,unsigned a){return r[a]|(unsigned)r[a+1]<<8;}
static void put(uint8_t *r,unsigned a,unsigned v){r[a]=(uint8_t)v;r[a+1]=(uint8_t)(v>>8);}
static unsigned minimum(unsigned a,unsigned b){return a<b?a:b;}
static void stamp(uint8_t *map,unsigned x,unsigned y,unsigned side,unsigned first,const uint8_t *rom){
    for(unsigned dy=0;dy<side;++dy)for(unsigned dx=0;dx<side;++dx){
        unsigned tile=first+dy*side+dx;
        /* The same owner/conductor flags used by native placement/unpacking. */
        put(map,2*((y+dy)*120+x+dx),tile|((rom[0x184eb+tile]&1)?0x400:0));
    }
}
bool ScMarsScenarioGenerate(ScWorld *w,ScPopulation *p,uint8_t *r,const uint8_t *rom,size_t size){
    if(!w || !p || !r || !rom || size!=0x80000)return false;
    /* This is a deliberately pre-developed challenge city, not Test City 3.
     * Its pollution is produced by real coal/factory tiles and native scans. */
    ScWorldReset(w);w->land_type=SC_LAND_MARS;w->development_speed=1;
    w->calendar_year=SC_MARS_START_YEAR;w->mars_status=1;
    uint8_t *map=r+0x10200;memset(map,0,24000);
    /* Factory districts dominate the east; the western colony has jobs,
     * residences and services. Roads every seven cells keep commutes local. */
    for(unsigned y=2;y+7<100;y+=7)for(unsigned x=2;x+7<120;x+=7){
        unsigned bx=(x-2)/7,by=(y-2)/7;
        bool coal=bx%4==0 && by%3==0;
        if(coal)stamp(map,x+1,y+1,4,0x287,rom);
        else for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){
            unsigned first=x>42?0x218:dx?0x17f:0x11c;
            stamp(map,x+1+dx*3,y+1+dy*3,3,first,rom);
        }
    }
    /* Service parcels replace complete six-cell plots, never part of a zone. */
    const unsigned civic[][3]={{10,17,0x245},{31,17,0x24e},{10,59,0x245},{31,59,0x24e},
        {66,17,0x245},{87,17,0x24e},{66,59,0x245},{87,59,0x24e}};
    for(unsigned n=0;n<sizeof civic/sizeof *civic;++n){
        unsigned x=civic[n][0],y=civic[n][1];
        for(unsigned yy=0;yy<6;++yy)for(unsigned xx=0;xx<6;++xx)put(map,2*((y+yy)*120+x+xx),0);
        stamp(map,x,y,3,civic[n][2],rom);
        stamp(map,x+3,y,3,0x11c,rom);stamp(map,x,y+3,3,0x17f,rom);
    }
    /* A native placement transaction joins the road/power network correctly. */
    ScBuildPlan *plan=calloc(1,sizeof *plan);uint8_t *building=malloc(0x20000);
    if(!plan || !building){free(plan);free(building);return false;}
    memcpy(building,r,0x20000);put(building,0xb9d,0xffff);building[0xb9f]=0xff;
    for(unsigned tool=1;tool<=3;tool+=2){
        plan->tool=tool;plan->count=0;
        for(unsigned y=0;y<100;++y)for(unsigned x=0;x<120;++x){
            bool road=x%7==2 || y%7==2;
            bool wire=(x%7==2 && y%7==3) || (y%7==2 && x%7==3);
            if(tool==1?road:wire)plan->cells[plan->count++]=(ScBuildCell){(int)x,(int)y};
        }
        unsigned cost;
        if(ScConstructionCommit(building,rom,size,plan,&cost)!=SC_BUILD_OK){free(plan);free(building);return false;}
    }
    /* Keep the native footprints/joins, while the live loader retains its
     * own screen, renderer staging and scheduler. Placement UI side effects
     * are inappropriate before a city's HUD has been initialized. */
    memcpy(map,building+0x10200,24000);free(building);free(plan);
    put(r,0xb53,SC_MARS_START_YEAR);put(r,0xb55,4); /* red spring, not winter frost */
    put(r,0xb57,1);put(r,0xb59,31337);
    put(r,0xb9d,60000);put(r,0xb9f,0);put(r,0xdc5,5);
    put(r,0xbc5,32);put(r,0xbc7,1000);put(r,0xbc9,1000);
    put(r,0x3e,3);put(r,0x40,SC_MARS_SCENARIO);put(r,0xc0d,0);put(r,0xccb,0);put(r,0xd87,0);
    static const char name[]="MARS";r[0xb5b]=4;
    memset(r+0xb5c,0x27,8);for(unsigned i=0;i<4;++i)r[0xb5c+i]=name[i]-'A'+10;
    put(r,0x1bd,8);put(r,0x1bf,12);put(r,0x1c5,95);put(r,0x1c9,78);
    if(!ScConstructionRefreshPower(r,w,rom,size) || !ScConstructionPrimeFields(r,rom,size))return false;
    memset(p,0,sizeof *p);ScPopulationImport(p,r);
    if(!ScPopulationRefreshLive(p,r,w,rom,size))return false;
    p->previous=p->value;p->change=0;ScPopulationMirror(p,r);
    put(r,0xdeb,ScPopulationClass(p->value));put(r,0xca5,ScPopulationClass(p->value));
    ScMarsStats s;ScMarsScenarioInspect(w,p,r,rom,&s);
    fprintf(stderr,"[Mars] colony created: population=%llu parks=%u pollution=%u hot=%u/%u power=%u/%u\n",
        (unsigned long long)s.population,s.parks,s.pollution,s.hot_cells,s.pollution_cells,s.powered,s.zones);
    return s.zones && s.powered==s.zones && s.pollution>SC_MARS_POLLUTION;
}
static unsigned elapsed(const ScWorld *w,const uint8_t *r){
    unsigned year=ScWorldYear(w,r),month=word(r,0xb55);
    /* The scenario begins April 2050, ends April 2055. */
    int64_t n=((int64_t)year-SC_MARS_START_YEAR)*12+(int)month-4;
    return n<0?0:n>SC_MARS_MONTHS?SC_MARS_MONTHS:(unsigned)n;
}
void ScMarsScenarioInspect(const ScWorld *w,const ScPopulation *p,const uint8_t *r,const uint8_t *rom,ScMarsStats *s){
    memset(s,0,sizeof *s);s->population=p?p->value:word(r,0xba5)|((uint64_t)word(r,0xba7)<<16);
    const uint8_t *map=w->active?w->tiles:r+0x10200;
    unsigned cells=w->active?ScWorldCells(w):12000;
    for(unsigned i=0;i<cells;++i){
        unsigned raw=word(map,2*i),t=raw&1023;
        /* Built parks use forest IDs. The blueprint contains no natural
         * foliage, so every surviving green tile here was placed by the mayor. */
        if(t>=20 && t<=39)++s->parks;
        if(t>=958 || !(rom[0x184eb+t]&1))continue;
        if((t>=0x80 && t<0x129) || (t>=0x137 && t<0x249) || t>=0x376){
            ++s->zones;if(raw&0x8000)++s->powered;
        }
    }
    const uint8_t *pollution=w->active?w->fields[2]:r+0x18270;
    s->pollution_cells=cells/4;unsigned sum=0;
    for(unsigned i=0;i<s->pollution_cells;++i){sum+=pollution[i];s->hot_cells+=pollution[i]>=80;}
    s->pollution=(sum+s->pollution_cells-1)/s->pollution_cells;
    s->months_left=SC_MARS_MONTHS-elapsed(w,r);
    s->clean=s->parks>=SC_MARS_PARKS && sum<=s->pollution_cells*SC_MARS_POLLUTION &&
        s->hot_cells*100<=s->pollution_cells*SC_MARS_HOT_PERCENT && s->population>=SC_MARS_POPULATION &&
        s->zones && s->powered*100>=s->zones*SC_MARS_POWER_PERCENT;
}
void ScMarsScenarioObserve(ScWorld *w,const ScPopulation *p,const uint8_t *r,const uint8_t *rom){
    if(!w || w->mars_status!=1 || word(r,0x3e)!=3 || word(r,0x40)!=SC_MARS_SCENARIO)return;
    unsigned month=elapsed(w,r),sample=month+1;
    ScMarsStats s;ScMarsScenarioInspect(w,p,r,rom,&s);
    /* A skipped/backward clock never gives unobserved clean months credit.
     * A full year of consecutive samples requires the city to stay clean. */
    if(w->mars_sample!=sample) {
        bool consecutive=w->mars_sample && (unsigned)w->mars_sample+1==sample;
        w->mars_clean_months=s.clean?(consecutive?minimum(w->mars_clean_months+1,SC_MARS_CLEAN_MONTHS):0):0;
        w->mars_sample=(uint8_t)sample;
    } else if(!s.clean)w->mars_clean_months=0;
    unsigned parks=minimum(1000,s.parks*1000/SC_MARS_PARKS);
    unsigned clean=s.pollution<=SC_MARS_POLLUTION?1000:s.pollution>=100?0:
        (100-s.pollution)*1000/(100-SC_MARS_POLLUTION);
    unsigned hot=s.hot_cells*100/s.pollution_cells;
    unsigned hotspots=hot<=SC_MARS_HOT_PERCENT?1000:hot>=40?0:
        (40-hot)*1000/(40-SC_MARS_HOT_PERCENT);
    unsigned people=s.population>=SC_MARS_POPULATION?1000:(unsigned)s.population*1000/SC_MARS_POPULATION;
    unsigned power=s.zones?minimum(1000,s.powered*100000/(s.zones*SC_MARS_POWER_PERCENT)):0;
    /* Every park and reduction in pollution visibly helps the landscape.
     * A populated, powered colony is still required for the clean streak. */
    unsigned score=minimum((parks*50+clean*35+hotspots*15)/100,minimum(people,power));
    w->mars_progress=(uint16_t)(score*94/100+w->mars_clean_months*60/SC_MARS_CLEAN_MONTHS);
    if(w->mars_clean_months>=SC_MARS_CLEAN_MONTHS){
        w->mars_status=2;w->mars_progress=1000;w->mars_notice=2;w->land_type=SC_LAND_NATIVE;
    }else if(month>=SC_MARS_MONTHS){w->mars_status=3;w->mars_notice=3;}
}
void ScMarsScenarioStep(ScWorld *w,const ScPopulation *p,Interp816 *c,uint8_t *r,const uint8_t *rom){
    if(!w || !w->mars_status || !c)return;
    if(c->k==3 && c->pc==0xc500 && word(r,0x40)==SC_MARS_SCENARIO && word(r,0x3e)==3){
        c->mf=c->xf=false; /* skipped native REP #$30 */
        ScMarsScenarioObserve(w,p,r,rom);c->pc=0xc545; /* native REP/RTS, no table overread */
    }
    if(c->k==3 && c->pc==0xb96f && word(r,0x40)==SC_MARS_SCENARIO)c->pc=0xb9cc;
    if(c->k==3 && c->pc==0xe30a && c->y==SC_MARS_SCENARIO*2 && word(r,0x3e)==3){
        c->a|=0x200;c->pc=0xe30d; /* only Mars's new win bit */
    }
    if(c->k==1 && c->pc==0x8b9f && w->mars_notice && !w->mars_announcing &&
       !word(r,0x391) && !word(r,0xd7)){
        put(r,0x397,4);put(r,0x395,1);
    }
    if(c->k==1 && c->pc==0xe59b && w->mars_notice && word(r,0x397)==4){
        c->a=0xfc00;w->mars_announcing=true;
    }
    if(c->k==1 && c->pc==0xa63c && w->mars_announcing){
        w->mars_notice=0;w->mars_announcing=false;
        put(r,0xd87,w->mars_status==2?2:w->mars_status==3?1:0);
    }
}
bool ScMarsScenarioMessageRead(const ScWorld *w,uint32_t a,uint8_t *value){
    a&=0x7fffff;
    if(!w || !w->mars_announcing || a<0x0ffc00 || a>=0x0ffd00)return false;
    static const char *const win[]={"TERRAFORMING COMPLETE!","","Mayor, Mars is green!","Our colony stayed alive",
        "and clean for a year.","The parks have brought","new life to our world.","Welcome to a new Earth!"};
    static const char *const lose[]={"TERRAFORMING MARS","","Our five years are over.","The colony is not ready.",
        "We needed 2400 parks","and pollution of 8 or","less for a full year.","We can try again, Mayor!"};
    unsigned row=(a-0x0ffc00)/24,col=(a-0x0ffc00)%24;
    const char *const *lines=w->mars_status==2?win:lose;
    *value=row>=8?255:col<strlen(lines[row])?(uint8_t)lines[row][col]:' ';return true;
}
void ScMarsScenarioSelectorStep(Interp816 *c,uint8_t *r,bool sylt){
    if(c->k!=3)return;
    bool unlocked=word(r,0x42)&0x8000;
    if(c->pc==0xddc3)r[0x79]=4;
    if(c->pc==0xde1c){
        unsigned col=word(r,0x52),row=word(r,0x54)&1;
        if(col==3 && !unlocked){col=(r[0xca]&2)?2:4;put(r,0x52,col);}
        if(col==4){if(!sylt || !unlocked)row=1;put(r,0x54,row);put(r,0x40,8+row);}
        else put(r,0x40,col==3?6+row:row*3+col);
    }
    if(c->pc==0xde31 && word(r,0x52)==4)put(r,0x22,160);
    if(c->pc==0xdebb && word(r,0x40)==SC_MARS_SCENARIO){
        put(r,0x25d,336-word(r,0x16));put(r,0x25f,0x7f);
    }
}
static void card_text(uint8_t pixels[72][64],const uint8_t font[40][8],unsigned y,const char *text){
    unsigned n=(unsigned)strlen(text),left=(64-n*4)/2;
    for(unsigned i=0;i<n;++i){
        unsigned ch=text[i]>='A' && text[i]<='Z'?text[i]-'A'+10:text[i]>='0' && text[i]<='9'?text[i]-'0':39;
        for(unsigned yy=0;yy<8;++yy)for(unsigned xx=0;xx<4;++xx)
            if(font[ch][yy]&(8u>>xx))pixels[y+yy][left+i*4+xx]=3;
    }
}
void ScMarsScenarioCard(uint16_t *vram,unsigned map,const uint8_t font[40][8]){
    if(!vram || !font || map+0x800>32768)return;
    uint8_t pixels[72][64];memset(pixels,2,sizeof pixels);
    for(unsigned y=0;y<36;++y)for(unsigned x=0;x<64;++x){
        pixels[y][x]=3;
        int dx=(int)x-32,dy=(int)y-18;
        if(dx*dx+dy*dy*2<420){
            pixels[y][x]=(x+y)%11==0?3:1;
        }
    }
    /* Same cream label area, gray scenery and four inks as the native cards. */
    for(unsigned y=13;y<29;++y)for(unsigned x=23;x<43;++x)
        if(y>=23 || (x>=25 && x<28) || (x>=35 && x<38))pixels[y][x]=0;
    for(unsigned i=0;i<4;++i){pixels[10-i][26+i]=1;pixels[10-i][36+i]=1;}
    card_text(pixels,font,38,"TERRAFORMING");card_text(pixels,font,47,"MARS");card_text(pixels,font,62,"2050");
    const unsigned base=0x330;
    for(unsigned ty=0;ty<9;++ty)for(unsigned tx=0;tx<8;++tx){
        unsigned tile=base+ty*8+tx;
        for(unsigned y=0;y<8;++y){unsigned lo=0,hi=0;
            for(unsigned x=0;x<8;++x){unsigned ink=pixels[ty*8+y][tx*8+x];lo|=(ink&1)<<(7-x);hi|=(ink>>1)<<(7-x);}
            vram[tile*8+y]=(uint16_t)(lo|(hi<<8));
        }
        vram[map+0x400+(16+ty)*32+10+tx]=(uint16_t)((2<<10)|tile);
    }
    for(unsigned y=17;y<=25;++y)vram[map+0x400+y*32+18]=0x10;
    for(unsigned x=11;x<=18;++x)vram[map+0x400+25*32+x]=0x10;
}
