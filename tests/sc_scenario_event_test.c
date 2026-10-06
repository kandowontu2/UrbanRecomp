#include "sc_scenario_event.h"
#include "sc_program.h"
#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint8_t ram[0x20000],rom[0x80000];
static ScWorld *world;
static ScWorldGuest guest;
int interp816_opcode_hook(uint32_t pc) {(void)pc;return 0;}
static uint8_t read_bus(void *ctx,uint32_t a) {
    (void)ctx;uint8_t value;if(ScWorldGuestRead(&guest,a,&value))return value;
    unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f)return ram[a-0x7e0000];
    if(p<0x2000)return ram[p];
    if(p>=0x8000)return rom[(bank&15)*32768+(p&32767)];
    return 0;
}
static void write_bus(void *ctx,uint32_t a,uint8_t v) {
    (void)ctx;if(ScWorldGuestWrite(&guest,a,v))return;
    unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f)ram[a-0x7e0000]=v;
    else if(p<0x2000)ram[p]=v;
}
static unsigned word(unsigned p) {return ram[p]|(unsigned)ram[p+1]<<8;}
static void put(unsigned p,unsigned v) {ram[p]=(uint8_t)v;ram[p+1]=(uint8_t)(v>>8);}
static void check(bool compiled,bool manual,bool disabled) {
    memset(ram,0,sizeof ram);memset(&guest,0,sizeof guest);put(0x3e,1);put(0x40,7);put(0xc0d,291);
    put(0x425,disabled?1:0);put(0x395,1); /* no unrelated random event */
    ScScenarioEvent event={0};
    if(manual)assert(ScScenarioEventArm(&event,ram,NULL,6,16));
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);
    cpu->k=cpu->db=3;cpu->pc=0xb84b;cpu->dp=0x1e00;cpu->sp=0x1ffd;
    cpu->e=cpu->mf=cpu->xf=false;put(0x1ffe,0x6fff);
    unsigned steps=0;
    while(cpu->pc!=0x7000 && !word(0xaf1)) {
        assert(++steps<20000);ScScenarioEventStep(&event,cpu,ram,NULL);
        if(compiled)assert(ScProgramStep(cpu));else interp816_runOpcode(cpu);
    }
    assert((word(0xaf1)==0x8000)==manual);
    assert(word(0xba5)==0 && word(0xba7)==0 && word(0x425)==(disabled?1:0));
    assert(rom[0x1b9bf]==0x90 && rom[0x1b9c0]==3);
    if(manual) {
        assert(word(0xaef)==0x8000 && word(0xaf9)>0 && word(0xaf7)>0);
        assert(!ScScenarioEventTick(&event,ram));
        assert(!ScScenarioEventArm(&event,ram,NULL,4,1));
        put(0xc0d,15);assert(ScScenarioEventTick(&event,ram));
        assert(word(0x3e)==1 && word(0x40)==7 && word(0xc0d)==291);
        cpu->pc=0xb9bf;cpu->c=false;ScScenarioEventStep(&event,cpu,ram,NULL);assert(!cpu->c);
        assert(!ScScenarioEventTick(&event,ram));
    }
    interp816_free(cpu);
}
static void meltdown(bool compiled,unsigned size,bool plant) {
    memset(ram,0,sizeof ram);memset(&guest,0,sizeof guest);ScWorldReset(world);
    world->active=size!=0;world->huge=size>=2;world->giant=size>=3;world->colossal=size>=4;world->mega=size>=5;
    unsigned width=size?ScWorldWidth(world):120,height=size?ScWorldHeight(world):100;
    unsigned x=width-35,y=height-35;
    uint8_t *tiles=size?world->tiles:ram+0x10200;
    for(unsigned yy=y-20;yy<y+20;++yy)for(unsigned xx=x-25;xx<x+25;++xx) {
        unsigned at=2*(yy*width+xx);tiles[at]=0x80;tiles[at+1]=0;
    }
    if(plant)for(unsigned dy=0;dy<3;++dy)for(unsigned dx=0;dx<3;++dx) {
        unsigned at=2*((y+dy-1)*width+x+dx-1),value=0x278+dy*3+dx;
        tiles[at]=(uint8_t)value;tiles[at+1]=(uint8_t)(value>>8);
    }
    put(0x3e,2);put(0x40,7);put(0xc0d,291);put(0x425,1);put(0x395,1);
    memset(ram+0xced,255,60); /* native empty dated-news slots */
    put(0x400,x);put(0x402,y);
    ScScenarioEvent event={0};assert(ScScenarioEventArm(&event,ram,world,4,1));
    assert(event.nuclear_x==(plant?(int)x:-1) && event.nuclear_y==(plant?(int)y:-1));
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);
    cpu->k=cpu->db=3;cpu->pc=0xb84b;cpu->dp=0x1e00;cpu->sp=0x1ffd;
    cpu->e=cpu->mf=cpu->xf=false;put(0x1ffe,0x6fff);
    unsigned steps=0;
    while(cpu->pc!=0x7000) {
        assert(++steps<3000000);ScScenarioEventStep(&event,cpu,ram,world);
        ScWorldGuestStep(world,cpu,ram);ScWorldGuestBegin(&guest,world,cpu,rom,sizeof rom);
        if(compiled)assert(ScProgramStep(cpu));else interp816_runOpcode(cpu);
    }
    assert(word(0xc0d)==0 && word(0x425)==1);
    unsigned radiation=0;
    for(unsigned yy=y-20;yy<y+20;++yy)for(unsigned xx=x-25;xx<x+25;++xx) {
        unsigned at=2*(yy*width+xx);radiation+=((tiles[at]|(unsigned)tiles[at+1]<<8)&1023)==0x364;
    }
    assert((radiation>0)==plant);assert((word(0xced)==8)==plant);
    if(plant) {
        assert((tiles[2*(y*width+x)]|((unsigned)tiles[2*(y*width+x)+1]<<8))!=0x27c);
        assert(word(0x397)==0x24); /* Manual disaster replaces queued advice. */
    }
    assert(ScScenarioEventTick(&event,ram));
    assert(word(0x3e)==2 && word(0x40)==7 && word(0xc0d)==291);
    printf("PASS %s meltdown size=%u plant=%u radiation=%u steps=%u\n",compiled?"native":"oracle",size,plant,radiation,steps);
    interp816_free(cpu);memset(&guest,0,sizeof guest);
}
static void menu(void) {
    memset(ram,0,sizeof ram);put(0x14,0x8000);put(0x379,255);put(0xab5,65535);put(0x1df,2);
    assert(ScScenarioMenuLive(ram));Interp816 cpu={0};cpu.k=1;
    for(unsigned button=0;button<2;++button) {
        put(0x1eb,button?100:62);put(0x1ed,124);put(0xc9,0x8000);
        cpu.pc=0xaecc;ScScenarioMenuStep(&cpu,ram);
        assert(cpu.pc==0xaee4 && cpu.a==14+button && !cpu.mf);
    }
    put(0xc9,0x40);cpu.pc=0xaecc;ScScenarioMenuStep(&cpu,ram);assert(cpu.pc==0xaecc);
    put(0xc9,0x8000);put(0x1df,1);ScScenarioMenuStep(&cpu,ram);assert(cpu.pc==0xaecc);
    assert(ScScenarioMenuButton(44,112)==0 && ScScenarioMenuButton(117,135)==1);
    assert(ScScenarioMenuButton(80,124)<0 && ScScenarioMenuButton(62,136)<0);
}
static void ufo_damage(bool compiled,unsigned size) {
    memset(ram,0,sizeof ram);memset(&guest,0,sizeof guest);ScWorldReset(world);
    world->active=true;world->huge=size>=2;world->giant=size>=3;world->colossal=size>=4;world->mega=size>=5;
    unsigned width=ScWorldWidth(world),height=ScWorldHeight(world),x=32,y=36;
    unsigned at=2*(y*width+x);world->tiles[at]=0x62;
    world->coord[2][0]=width-40;world->coord[2][1]=height-40;
    ScScenarioEvent event={0};assert(ScScenarioEventArm(&event,ram,world,6,16));
    put(0xaf9,x);put(0xaf7,y);
    Interp816 *cpu=interp816_init(NULL,read_bus,write_bus);assert(cpu);
    cpu->k=cpu->db=3;cpu->pc=0xa9c2;cpu->dp=0x1e00;cpu->sp=0x1ffc;
    cpu->e=cpu->mf=cpu->xf=false;cpu->a=x|(y<<8);cpu->y=1;
    put(0x1ffd,0x6fff);ram[0x1fff]=3;
    unsigned steps=0;
    while(cpu->pc!=0x7000) {
        assert(++steps<20000);ScScenarioEventStep(&event,cpu,ram,world);
        ScWorldGuestStep(world,cpu,ram);ScWorldGuestBegin(&guest,world,cpu,rom,sizeof rom);
        if(compiled)assert(ScProgramStep(cpu));else interp816_runOpcode(cpu);
    }
    assert((world->tiles[at]|world->tiles[at+1]<<8)!=0x62);
    assert(world->coord[2][0]==width-40 && world->coord[2][1]==height-40 && !event.damage_anchor);
    /* The native UFO announcement must override routine queued advice too. */
    cpu->pc=0xbe04;cpu->sp=0x1ffd;cpu->a=0x30;put(0x1ffe,0x6fff);put(0x395,1);put(0x397,6);
    while(cpu->pc!=0x7000) {
        assert(++steps<20000);ScScenarioEventStep(&event,cpu,ram,world);
        if(compiled)assert(ScProgramStep(cpu));else interp816_runOpcode(cpu);
    }
    assert(word(0x397)==0x30 && word(0x395)==2);
    printf("PASS %s UFO damage size=%u with distant simulation anchor and queued advice\n",compiled?"native":"oracle",size);
    interp816_free(cpu);memset(&guest,0,sizeof guest);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<sizeof rom;++i)hash=(hash^rom[i])*16777619u;
    assert(ScProgramSelectRom(hash));
    for(unsigned compiled=0;compiled<2;++compiled)for(unsigned manual=0;manual<2;++manual)
        for(unsigned disabled=0;disabled<2;++disabled)check(compiled,manual,disabled);
    puts("PASS manual UFO: native/interpreter, zero population, NO DISASTER, untouched ROM/state restoration");
    world=calloc(1,sizeof *world);assert(world);
    for(unsigned compiled=0;compiled<2;++compiled)for(unsigned size=0;size<6;++size) {
        meltdown(compiled,size,true);meltdown(compiled,size,false);
    }
    for(unsigned compiled=0;compiled<2;++compiled)for(unsigned size=1;size<6;++size)ufo_damage(compiled,size);
    menu();free(world);puts("PASS temporary native Disaster menu selection/back/other-page isolation");
}
