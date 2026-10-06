#include "sc_world.h"
#include "sc_land_type.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    ScWorld *w=calloc(1,sizeof *w),*copy=calloc(1,sizeof *copy);
    size_t bytes=ScWorldEncodedSize();uint8_t *encoded=malloc(bytes),*cities=malloc(ScWorldCitiesSize());
    uint8_t sram[0x8000]={0};assert(w && copy && encoded && cities);
    assert(ScWorldEncodedVersionSize(7)==bytes && ScWorldEncodedVersionSize(6)==bytes);
    ScWorldCitiesInit(cities);
    for(unsigned size=0;size<6;++size)for(unsigned type=0;type<SC_LAND_TYPES;++type) {
        ScWorldReset(w);w->active=size!=0;w->huge=size>=2;w->giant=size>=3;
        w->colossal=size>=4;w->mega=size>=5;w->land_type=type;w->development_speed=20;
        w->center_valid=true;w->center_x=44;w->center_y=22;
        assert(ScWorldEncode(w,encoded,bytes) && encoded[7]==7);
        assert(ScWorldDecode(copy,encoded,bytes));
        assert(copy->land_type==type && copy->active==w->active && copy->center_valid);
        assert(copy->center_x==44 && copy->center_y==22 && copy->development_speed==20);
        if(size==0 || size==5) {
            assert(ScWorldCitySave(cities,sram,type&1,w));
            assert(ScWorldCityLoad(copy,cities,ScWorldCitiesSize(),sram,type&1));
            assert(copy->land_type==type);
        }
    }
    encoded[60]=SC_LAND_TYPES<<1;assert(!ScWorldDecode(copy,encoded,bytes));
    encoded[7]=6;encoded[60]=1;
    assert(ScWorldDecode(copy,encoded,bytes) && copy->land_type==0 && copy->center_valid);
    cities[7]=6;assert(ScWorldCitiesValid(cities,ScWorldCitiesSize()));
    free(w);free(copy);free(encoded);free(cities);
    puts("PASS: land types survive all map sizes, normal/expanded city slots, and legacy v6 records; invalid metadata rejected");
}
