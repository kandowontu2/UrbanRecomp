#include "sc_postpass.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t ram[0x20000],rom[0x80000],before_ram[0x20000],actual_ram[0x20000];
static ScWorld world,before_world,actual_world;
static ScWorldGuest guest;
static uint8_t read_bus(void *ctx,uint32_t at) {
    (void)ctx;uint8_t value;if(ScWorldGuestRead(&guest,at,&value)) return value;
    unsigned bank=at>>16,p=at&65535;
    if(bank==0x7e || bank==0x7f) return ram[at&0x1ffff];
    if((bank&0x7f)<0x40 && p<0x2000) return ram[p];
    if(p>=0x8000) return rom[(bank&15)*0x8000+(p&0x7fff)];
    assert(!"unexpected device read");return 0;
}
static void write_bus(void *ctx,uint32_t at,uint8_t value) {
    (void)ctx;if(ScWorldGuestWrite(&guest,at,value)) return;
    unsigned bank=at>>16,p=at&65535;
    if(bank==0x7e || bank==0x7f) {ram[at&0x1ffff]=value;return;}
    if((bank&0x7f)<0x40 && p<0x2000) {ram[p]=value;return;}
    assert(!"unexpected device write");
}
static void put(unsigned at,unsigned value) {ram[at]=(uint8_t)value;ram[at+1]=(uint8_t)(value>>8);}
static void original(Interp816 *c) {
    ScWorldGuestStep(&world,c,ram);ScWorldGuestBegin(&guest,&world,c,rom,sizeof rom);interp816_runOpcode(c);
}
static void check(const Interp816 *actual,const Interp816 *expected,unsigned cost,unsigned elapsed,unsigned start) {
    if(cost!=elapsed || memcmp(actual,expected,sizeof *actual) || memcmp(ram,actual_ram,sizeof ram) || memcmp(&world,&actual_world,sizeof world)) {
        fprintf(stderr,"postpass start=%x clocks=%u/%u pc=%x/%x A=%x/%x X=%x/%x Y=%x/%x flags=%x/%x scan=%u/%u\n",
            start,cost,elapsed,actual->pc,expected->pc,actual->a,expected->a,actual->x,expected->x,actual->y,expected->y,
            interp816_getFlags((Interp816 *)actual),interp816_getFlags((Interp816 *)expected),actual_world.field_scan,world.field_scan);
        abort();
    }
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    assert(fread(rom,1,sizeof rom,file)==sizeof rom);assert(fgetc(file)==EOF);fclose(file);
    Interp816 *c=interp816_init(NULL,read_bus,write_bus);assert(c);
    const unsigned entries[]={0x88fa,0x8924,0xb676},prefix[]={0x88f3,0x891f,0xb66a};
    const unsigned budgets[]={0,1,2,3,5,12,16,31,64,128,512,8192};
    unsigned spans=0,edges=0,unavailable=0;uint8_t seen[65536]={0};
    for(unsigned map=0;map<5;++map) for(unsigned kind=0;kind<3;++kind)
    for(unsigned pattern=0;pattern<6;++pattern) for(unsigned setup=0;setup<2;++setup) {
        ScWorldReset(&world);world.active=map!=0;world.huge=map>=2;world.giant=map>=3;world.colossal=map==4;
        unsigned field_id=kind==1?7:0,end=world.active?ScWorldFieldSizeWorld(&world,field_id):kind==1?390:3000;
        unsigned first=end-(kind==1?64:32);
        for(unsigned n=0;n<64;++n) {
            unsigned at=first+n;
            if(at>=end) break;
            unsigned value=(n*71+pattern*41)&255;
            uint8_t *land=world.active?world.fields[0]:ram+0x16b00;
            uint8_t *traffic=world.active?world.fields[4]:ram+0x199e0;
            uint8_t *growth=world.active?world.fields[7]:ram+0x1ae62;
            if(kind==1) {
                value=pattern==0?0:pattern==1?10:pattern==2?201:pattern==3?0xff39:pattern==4?0xff37:0xffff;
                if(!(at&1)) {growth[at]=(uint8_t)value;growth[at+1]=(uint8_t)(value>>8);}
            } else {land[at]=pattern==0?0:pattern==1?1:n&1;traffic[at]=(uint8_t)value;}
        }
        /* Stock fields live in the original WRAM. Preserve them while the
         * low direct-page/stack area is prepared independently. */
        memset(ram,0x5a,0x10000);memset(&guest,0,sizeof guest);interp816_reset(c);
        c->k=c->db=3;c->pc=(uint16_t)(setup?prefix[kind]:entries[kind]);
        c->dp=(uint16_t)(pattern&1?0x1eef:0x1e00);c->sp=0x1f75;
        c->e=c->xf=c->d=false;c->mf=kind!=1;c->i=true;c->c=c->v=true;c->a=0xab12;c->y=65535;c->x=(uint16_t)first;
        world.field_scan=first;put(c->dp,pattern&1?65535:32767);put(c->dp+2,65535);put(c->sp+1,0x6fff);
        ScWorldGuestStep(&world,c,ram);Interp816 initial=*c;before_world=world;memcpy(before_ram,ram,sizeof ram);
        for(unsigned b=0;b<sizeof budgets/sizeof *budgets;++b) {
            *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);
            unsigned cost=ScPostpassStep(&world,c,ram,budgets[b]);Interp816 actual=*c;
            actual_world=world;memcpy(actual_ram,ram,sizeof ram);
            *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);unsigned elapsed=0,guard=0;
            while(elapsed<cost) {assert(++guard<10000);original(c);elapsed+=c->cyclesUsed;}
            check(&actual,c,cost,elapsed,initial.pc);assert(cost<=budgets[b]);++spans;
        }
        *c=initial;world=before_world;memcpy(ram,before_ram,sizeof ram);
        for(unsigned guard=0;c->pc!=0x7000 && guard<(setup?200:10000);++guard) {
            ScWorldGuestStep(&world,c,ram);
            if(ScPostpassOwns(c->pc) && !(seen[c->pc]&(1u<<map))) {
                seen[c->pc]|=1u<<map;Interp816 old=*c;before_world=world;memcpy(before_ram,ram,sizeof ram);
                unsigned cost=ScPostpassInstructionStep(&world,c,ram);assert(cost);
                Interp816 actual=*c;actual_world=world;memcpy(actual_ram,ram,sizeof ram);
                *c=old;world=before_world;memcpy(ram,before_ram,sizeof ram);original(c);
                check(&actual,c,cost,c->cyclesUsed,old.pc);++edges;
            } else original(c);
        }
    }
    c->k=c->db=3;c->pc=0xb676;c->dp=0x1eef;c->sp=0x1f75;c->e=c->d=c->xf=false;c->mf=true;c->i=false;
    for(unsigned flag=0;flag<5;++flag) {
        c->nmiWanted=flag==0;c->irqWanted=flag==1;c->waiting=flag==2;c->stopped=flag==3;if(flag==4)c->sp=c->dp;
        Interp816 initial=*c;before_world=world;memcpy(before_ram,ram,sizeof ram);
        assert(!ScPostpassStep(&world,c,ram,8192) && !ScPostpassInstructionStep(&world,c,ram));
        assert(!memcmp(c,&initial,sizeof initial) && !memcmp(ram,before_ram,sizeof ram) && !memcmp(&world,&before_world,sizeof world));++unavailable;
    }
    assert(edges>200);printf("PASS: %u bounded postpass spans, %u independent native instruction edges, %u immutable unsafe/interrupt fallbacks, all five map sizes against original ROM\n",spans,edges,unavailable);
    interp816_free(c);
}
