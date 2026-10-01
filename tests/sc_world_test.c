#include "sc_world.h"
#include "sc_world_guest.h"
#include "sc_population.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t ram[0x20000],rom[0x80000];
static ScWorld world,copy;
static ScWorldGuest guest;
static Interp816 *cpu;
static uint16_t product,quotient,dividend;
static uint8_t multiplicand,visits[SC_WORLD_CELLS];
static uint8_t native_sram[0x8000];
static uint8_t *native_cities;
static uint8_t bitmap_ram[0x20000],bitmap[0x2000];
static ScPopulation population;
static bool population_enabled;
static unsigned mini_x,mini_y;
static uint8_t read_bus(void *ctx,uint32_t a) {
    (void)ctx; uint8_t v;
    if (ScWorldGuestRead(&guest,a,&v)) return v;
    unsigned bank=a>>16,p=a&65535;
    if (bank==0x70 && p<0x8000) return native_sram[p];
    if (a==0x0194e2 || a==0x0194a3) return 0x6b; /* presentation only, RTL */
    if (bank==0x7e || bank==0x7f) return ram[a-0x7e0000];
    if (p<0x2000) return ram[p];
    if (p==0x4214) return (uint8_t)quotient;
    if (p==0x4215) return (uint8_t)(quotient>>8);
    if (p==0x4216) return (uint8_t)product;
    if (p==0x4217) return (uint8_t)(product>>8);
    if (p>=0x8000 && bank<16) return rom[bank*32768+p-0x8000];
    return 0;
}
static void write_bus(void *ctx,uint32_t a,uint8_t v) {
    (void)ctx;
    if (ScWorldGuestWrite(&guest,a,v)) return;
    unsigned bank=a>>16,p=a&65535;
    if (bank==0x70 && p<0x8000) { native_sram[p]=v; return; }
    if (bank==0x7e || bank==0x7f) ram[a-0x7e0000]=v;
    else if (p<0x2000) ram[p]=v;
    else if (p==0x4202) multiplicand=v;
    else if (p==0x4203) product=multiplicand*v;
    else if (p==0x4204) dividend=(dividend&0xff00)|v;
    else if (p==0x4205) dividend=(dividend&255)|(v<<8);
    else if (p==0x4206) { quotient=v?dividend/v:65535; product=v?dividend%v:dividend; }
}
static void put(unsigned p,unsigned v) { ram[p]=(uint8_t)v; ram[p+1]=(uint8_t)(v>>8); }
static void routine_bank(unsigned bank,unsigned pc,unsigned a,unsigned y) {
    interp816_reset(cpu); memset(&guest,0,sizeof guest);
    cpu->k=cpu->db=(uint8_t)bank; cpu->pc=(uint16_t)pc; cpu->a=(uint16_t)a; cpu->y=(uint16_t)y;
    cpu->sp=0x1ffd; cpu->dp=0x1e00; cpu->e=cpu->mf=cpu->xf=false;
    put(0x1ffe,0x6fff); unsigned steps=0;
    while (cpu->pc!=0x7000) {
        assert(++steps<20000000);
        if (native_cities && cpu->k==3) {
            if (cpu->pc==0xcf89) ScWorldMirror(&world,ram);
            if (cpu->pc==0xcbe2) {
                assert(native_sram[5]==1);
                assert(ScWorldCitySave(native_cities,native_sram,0,&world));
            }
            if (cpu->pc==0xc8c8) ScWorldReset(&world);
            if (cpu->pc==0xc8dd)
                assert(ScWorldCityLoad(&world,native_cities,ScWorldCitiesSize(),native_sram,0));
        }
        ScWorldGuestStep(&world,cpu,ram);
        if (cpu->k==1 && cpu->pc==0x8ad9) {
            mini_x=ram[0x25d]|(ram[0x25e]<<8);
            mini_y=ram[0x25f]|(ram[0x260]<<8);
        }
        if (population_enabled && cpu->k==3) {
            uint16_t next=ScPopulationStep(&population,ram,cpu->pc,cpu->dp);
            if (next!=cpu->pc) {
                cpu->y=(uint16_t)ScPopulationClass(population.value);
                cpu->a=(uint16_t)((ram[0xba7]|(ram[0xba8]<<8))-7);
                cpu->c=population.value>=500000; cpu->mf=cpu->xf=false;
            }
            cpu->pc=next;
        }
        if (cpu->k==3 && cpu->pc==0x82be) ++visits[ram[0xb86]*240+ram[0xb85]];
        ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
        if (read_bus(NULL,((uint32_t)cpu->k<<16)|cpu->pc)==2) cpu->pc+=2;
        else interp816_runOpcode(cpu);
    }
    assert(cpu->sp==0x1fff && cpu->dp==0x1e00);
}
static void routine(unsigned pc,unsigned a,unsigned y) { routine_bank(3,pc,a,y); }
int main(int argc,char **argv) {
    assert(argc==2); FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom); fclose(f);
    ScMapGenPrng pr={0}; sc_mapgen_prng_seed_from_spin(&pr,0xc7);
    ScWorldGenerate(&world,&pr); assert(world.active);
    for (unsigned i=0;i<SC_WORLD_FIELDS;++i) assert(ScWorldFieldSize(i)<=SC_WORLD_FIELD_BYTES);
    assert(!ScWorldPutCell(&world,-1,0,0xffff) && !ScWorldPutCell(&world,240,199,0xffff));
    for (int y=0;y<200;++y) for (int x=0;x<240;++x)
        assert(ScWorldPutCell(&world,x,y,(uint16_t)(y*240+x)));
    cpu=interp816_init(NULL,read_bus,write_bus); assert(cpu);
    const unsigned points[][2]={{0,0},{119,99},{120,100},{128,128},{239,199},{128,137},{129,137}};
    for (unsigned i=0;i<sizeof points/sizeof *points;++i) {
        unsigned x=points[i][0],y=points[i][1];
        routine(0x849e,x|(y<<8),0);
        assert(cpu->a==y*240+x && cpu->x==(uint16_t)(2*(y*240+x)));
        uint8_t before[sizeof ram]; memcpy(before,ram,sizeof ram);
        routine(0x84c4,x|(y<<8),0x8fff);
        assert(ScWorldCell(&world,x,y)==0x8fff);
        assert(cpu->a==0x8fff && cpu->n && !cpu->z);
        /* The host write never aliases guest tiles or other bank-7f fields. */
        assert(!memcmp(before+0x10000,ram+0x10000,0x10000));
    }
    routine(0xa29a,119|(99<<8),0); assert(cpu->x==11999);
    routine(0xa2b9,59|(49<<8),0); assert(cpu->x==2999);
    routine(0xa2d7,29|(24<<8),0); assert(cpu->x==749);
    /* The native fallback locator must also stay inside its 30x25 map on
     * a 240x200 city, and absolute camera jumps must reach the far half. */
    put(0x1bd,215); put(0x1bf,178); put(0xd7,0);
    routine_bank(1,0x8aa8,0,0);
    assert(mini_x==226 && mini_y==78);
    routine_bank(1,0xa688,239,0); assert(cpu->a==210);
    routine_bank(1,0xa6a1,199,0); assert(cpu->a==172);
    unsigned field; int displacement;
    assert(ScWorldFieldResolve(0xb150,&field,&displacement) && field==10 && displacement== -60);
    assert(ScWorldFieldResolve(0xc17c,&field,&displacement) && field==14 && displacement== -120);
    assert(!ScWorldFieldResolve(0xb151,&field,&displacement));
    assert(!ScWorldFieldResolve(0x5fc0,&field,&displacement)); /* temporal history */
    assert(ScWorldFieldResolve(0xb5c4,&field,&displacement) && field==13 && displacement== -120);
    memset(ram+0x10000,0xa5,0x10000);
    world.map_anchor=2*(190*240+200);
    cpu->k=3; cpu->pc=0x9a6c; cpu->x=(uint16_t)world.map_anchor; cpu->mf=false;
    ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
    uint8_t low,high; assert(ScWorldGuestRead(&guest,guest.address,&low));
    assert(ScWorldGuestRead(&guest,guest.address+1,&high));
    assert((unsigned)(low|(high<<8))==ScWorldCell(&world,200,191));
    assert(!ScWorldGuestRead(&guest,0x03849e,&low)); /* instruction fetch */
    cpu->pc=0x9daf; cpu->x=11999; cpu->mf=true;
    ScWorldGuestBegin(&guest,&world,cpu,rom,sizeof rom);
    assert(ScWorldGuestWrite(&guest,guest.address,0x7b));
    assert(world.fields[0][11999]==0x7b && ram[0x16b00+11999]==0xa5);
    size_t size=ScWorldEncodedSize(); uint8_t *data=malloc(size); assert(data);
    assert(ScWorldEncode(&world,data,size)); assert(ScWorldDecode(&copy,data,size));
    assert(!memcmp(&world,&copy,sizeof world));
    data[10]=100; assert(!ScWorldDecode(&copy,data,size)); assert(!memcmp(&world,&copy,sizeof world));
    assert(!ScWorldDecode(&copy,data,size-1));
    /* Native SRAM slots bind independently to full map/field records. A
     * changed native city or a truncated sidecar must never import another
     * city's large map. Normal cities also replace old large-slot metadata. */
    size_t cities_size=ScWorldCitiesSize(); uint8_t *cities=malloc(cities_size);
    static uint8_t sram[0x8000]; assert(cities);
    ScWorldCitiesInit(cities); assert(ScWorldCitiesValid(cities,cities_size));
    assert(ScWorldCitySave(cities,sram,0,&world));
    assert(ScWorldCityLoad(&copy,cities,cities_size,sram,0) && !memcmp(&world,&copy,sizeof world));
    cities[24+16+48+95999]^=1;
    assert(!ScWorldCityLoad(&copy,cities,cities_size,sram,0) && !memcmp(&world,&copy,sizeof world));
    cities[24+16+48+95999]^=1;
    ScWorldReset(&copy); assert(ScWorldCitySave(cities,sram,1,&copy));
    assert(ScWorldCityLoad(&copy,cities,cities_size,sram,1) && !copy.active);
    sram[0x5000]=1; assert(ScWorldCityLoad(&copy,cities,cities_size,sram,0));
    assert(!ScWorldCityLoad(&copy,cities,cities_size,sram,1));
    sram[0x100]=1; assert(!ScWorldCityLoad(&copy,cities,cities_size,sram,0));
    assert(!ScWorldCityLoad(&copy,cities,cities_size-1,sram,0)); free(cities);
    /* Run the ROM's entire sweep: each of the 48,000 cells, including the
     * far southeast quadrant and the 64-KiB crossing, is visited once. */
    memset(ram,0,sizeof ram); memset(visits,0,sizeof visits);
    memset(world.tiles,0,sizeof world.tiles);
    routine(0x8228,0,0); routine(0x8297,0,0);
    for (unsigned i=0;i<SC_WORLD_CELLS;++i) assert(visits[i]==1);
    assert(ram[0xb85]==240 && ram[0xb86]==200);
    /* Power writeback must neither stop nor alias at native X's wrap. */
    memset(world.fields[5],0,ScWorldFieldSize(5));
    world.fields[5][0]=0x80; world.fields[5][5999]=1;
    routine(0xb152,0,0);
    assert(ScWorldCell(&world,0,0)==0x8000 && ScWorldCell(&world,239,199)==0x8000);
    assert(!ScWorldCell(&world,238,199));
    /* Native bit lookup/set helpers use a 30-byte row across all 200 rows. */
    ram[0xb85]=239; ram[0xb86]=199;
    routine(0xb120,0,0); assert(cpu->x==5999 && cpu->y==7);
    memset(world.fields[5],0,ScWorldFieldSize(5));
    routine(0xb0e5,0,0); assert(world.fields[5][5999]==1 && !world.fields[5][1499]);
    routine(0xb0f8,0,0); assert(cpu->y==1);
    /* Drawing interrupts leave the simulation's full tile cursor intact. */
    world.map_anchor=90000; put(0x1d3,239); put(0x1d5,199);
    cpu->k=1; cpu->pc=0xc7b4; ScWorldGuestStep(&world,cpu,ram);
    assert(world.map_anchor==90000 && world.bank_anchor[1]==95998);
    /* Actual native compression, SRAM writes, decompression and city restore,
     * with the same completion hooks as the host. Presentation calls are
     * replaced by RTL; the saved-city routines and all storage run unchanged. */
    native_cities=malloc(ScWorldCitiesSize()); assert(native_cities);
    ScWorldCitiesInit(native_cities); put(0x423,1); put(0x421,1);
    ScWorldPutCell(&world,200,180,0x8abc); world.fields[0][11999]=123;
    copy=world; routine(0xcafd,0,0);
    assert(native_sram[5]==1 && world.active);
    routine(0xc8a1,0,0);
    assert(world.active && ScWorldCell(&world,200,180)==0x8abc && world.fields[0][11999]==123);
    assert(!memcmp(world.tiles,copy.tiles,sizeof world.tiles));
    free(native_cities); native_cities=NULL;
    /* The real overview bitmap covers the whole large map at half scale.
     * Compare the ROM's bitmap with an independently sampled stock map. */
    memset(ram,0,sizeof ram); put(0x3e,2);
    for (int y=0;y<200;++y) for (int x=0;x<240;++x)
        ScWorldPutCell(&world,x,y,(x/16+y/16)%26);
    for (unsigned y=0;y<100;++y) for(unsigned x=0;x<120;++x)
        put(0x10200+2*(y*120+x),ScWorldCell(&world,x*2,y*2));
    memcpy(bitmap_ram,ram,sizeof ram);
    routine_bank(2,0x899b,0,0); memcpy(bitmap,ram+0xa000,sizeof bitmap);
    world.active=false; memcpy(ram,bitmap_ram,sizeof ram);
    routine_bank(2,0x899b,0,0);
    assert(!memcmp(bitmap,ram+0xa000,sizeof bitmap)); world.active=true;
    /* Real dense-zone capacities across the expanded sweep must exceed the
     * native 16-bit counter safely, then feed the same 64-bit calculation. */
    memset(ram,0,sizeof ram); memset(world.tiles,0,sizeof world.tiles);
    for (unsigned y=0;y<198;y+=3) for(unsigned x=0;x<240;x+=3)
      for (unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx)
        ScWorldPutCell(&world,x+dx,y+dy,0x8000+0x120+dy*3+dx+((dy==2 && dx==2)?0x4000:0));
    population_enabled=true; memset(&population,0,sizeof population); population.valid=true;
    memset(visits,0,sizeof visits);
    routine(0x8228,0,0); routine(0x8297,0,0);
    fprintf(stderr,"large residential capacity=%llu native=%u\n",(unsigned long long)population.capacity[0],ram[0xb8b]|(ram[0xb8c]<<8));
    assert(population.capacity[0]>65535);
    uint64_t expected_population=population.capacity[0]*20;
    routine(0x8196,0,0); assert(population.value==expected_population);
    population_enabled=false;
    interp816_free(cpu); free(data);
    puts("PASS: all 48,000 native sweep visits, full-world power writeback, unsigned coordinate access, 64-KiB crossing, isolated fields, neighbour pitch and portable encoding");
}
