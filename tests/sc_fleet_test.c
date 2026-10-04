#include "sc_fleet.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    ScWorld *w=calloc(1,sizeof *w);ScFleet *fleet=ScFleetCreate();assert(w && fleet);
    uint8_t *rom=calloc(1,0x80000);assert(rom);
    for(unsigned size=1;size<=5;++size) {
        ScWorldReset(w);w->active=true;w->huge=size>=2;w->giant=size>=3;w->colossal=size>=4;w->mega=size==5;
        unsigned width=ScWorldWidth(w),height=ScWorldHeight(w),districts=(width/120)*(height/100);
        for(unsigned y=0;y<height;y+=100)for(unsigned x=0;x<width;x+=120) {
            for(unsigned dx=8;dx<100;++dx)ScWorldPutCell(w,x+dx,y+20,0x6d);
            ScWorldPutCell(w,x+60,y+60,0x82a5);ScWorldPutCell(w,x+60,y+80,0x826c);
            for(unsigned wy=85;wy<99;++wy)for(unsigned wx=20;wx<100;++wx)ScWorldPutCell(w,x+wx,y+wy,1);
        }
        for(unsigned frame=0;frame<400;++frame)ScFleetStep(fleet,w,frame,true);
        for(unsigned type=0;type<4;++type)assert(ScFleetCount(fleet,type)==districts);
        ScVehicleSprite *all=calloc(SC_VEHICLE_SPRITES,sizeof *all);assert(all);
        unsigned total=ScFleetShown(fleet,w,rom,0,0,0,0,width*8,height*8,all,SC_VEHICLE_SPRITES);
        assert(total>=districts*10 && total<=districts*11);
        unsigned train_art=0,ship_art=0;
        for(unsigned i=0;i<total;++i)if(all[i].rom_chr) {
            assert(all[i].rom_chr&0x80000000u);
            if(all[i].rom_chr&0x40000000u)++ship_art;else ++train_art;
        }
        assert(train_art==districts && ship_art==districts*4);free(all);
        ScVehicleSprite sprites[20];unsigned n=ScFleetShown(fleet,w,rom,0,0,0,0,120*8,100*8,sprites,20);
        assert(n>0 && n<=20);
        for(unsigned i=0;i<n;++i)assert(sprites[i].host && sprites[i].slot==-1 && sprites[i].x<1000);
        ScVehicleSprite before[20];memcpy(before,sprites,sizeof sprites);
        for(unsigned frame=400;frame<500;++frame)ScFleetStep(fleet,w,frame,false);
        assert(ScFleetShown(fleet,w,rom,0,0,0,0,120*8,100*8,sprites,20)==n && !memcmp(before,sprites,n*sizeof *sprites));
        printf("PASS: %ux%u fleet: %u trains, planes, ships and helicopters; paused positions and viewport culling\n",width,height,districts);
    }
    ScWorldPutCell(w,60,60,0);ScFleetStep(fleet,w,500,true);
    assert(ScFleetCount(fleet,1)==1023 && ScFleetCount(fleet,3)==1023);
    ScWorldPutCell(w,180,60,0x2a5);ScWorldPutCell(w,180,80,0x26c);
    ScFleetStep(fleet,w,501,true);
    assert(ScFleetCount(fleet,1)==1022 && ScFleetCount(fleet,3)==1022 && ScFleetCount(fleet,2)==1023);
    ScWorldReset(w);ScFleetStep(fleet,w,502,true);w->active=true;ScFleetStep(fleet,w,503,true);
    for(unsigned type=0;type<4;++type)assert(!ScFleetCount(fleet,type));
    ScFleetFree(fleet);free(w);free(rom);return 0;
}
