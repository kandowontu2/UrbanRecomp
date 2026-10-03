#include "sc_clipboard.h"
#include "sc_population.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t rom[0x80000],ram[0x20000],before[0x20000];
static ScWorld world,world_before;
static ScBuildPlan plan;
static unsigned word(unsigned a) {return ram[a]|(unsigned)ram[a+1]<<8;}
static void put(unsigned a,unsigned v) {ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static void reset(unsigned cash) {
    memset(ram,0,sizeof ram);put(0xb9d,cash);ram[0xb9f]=(uint8_t)(cash>>16);
    put(0x1bd,40);put(0x1bf,40);memset(ram+0x1c00,0x69,0x400);
}

static ScClipboard clip;
static void map(unsigned size,unsigned cash) {
    reset(cash);ScWorldReset(&world);
    world.active=size>0;world.huge=size>1;world.giant=size>2;world.colossal=size>3;
}
static ScWorld *live(void) {return world.active?&world:NULL;}
static unsigned raw(int x,int y) {return world.active?ScWorldCell(&world,x,y):word(0x10200+2*(y*120+x));}
static void write_cell(int x,int y,unsigned t) {
    if(world.active) ScWorldPutCell(&world,x,y,t);
    else {assert(x>=0 && x<120 && y>=0 && y<100);put(0x10200+2*(y*120+x),t);}
}
static unsigned place(unsigned tool,int x,int y) {
    unsigned cost;
    assert(ScConstructionPlanWorld(&plan,live(),tool,x,y,x,y));
    assert(ScConstructionCommitWorld(ram,live(),rom,sizeof rom,&plan,&cost)==SC_BUILD_OK);
    return cost;
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==3);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(rom,1,sizeof rom,f)==sizeof rom);fclose(f);
    for(unsigned size=0;size<5;++size) for(unsigned tool=1;tool<=14;++tool) {
        map(size,1000000);int mw=world.active?ScWorldWidth(&world):120;
        int mh=world.active?ScWorldHeight(&world):100,x=mw-20,y=mh-20;
        unsigned cost=place(tool,x,y),n=tool==12?6:tool>=10?4:tool>=5?3:1;
        /* Touch only the southeast corner: the complete building is copied. */
        assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,x+n-1,y+n-1,x+n-1,y+n-1));
        assert(clip.width==n && clip.height==n && clip.count==n*n && clip.price==cost);
        unsigned funds_before=word(0xb9d)|ram[0xb9f]<<16;
        unsigned chunk=2*(y*mw+x-20)/SC_WORLD_TILE_CHUNK_BYTES;
        uint64_t revision=size?ScWorldTileRevisions(&world)[chunk]:0;
        assert(ScClipboardPaste(&clip,ram,live(),rom,sizeof rom,x-20,y)==SC_BUILD_OK);
        if(size) assert(ScWorldTileRevisions(&world)[chunk]>revision);
        assert((word(0xb9d)|ram[0xb9f]<<16)==funds_before-cost);
        for(unsigned dy=0;dy<n;++dy) for(unsigned dx=0;dx<n;++dx)
            assert(raw(x-20+dx,y+dy)==(raw(x+dx,y+dy)&0x7fff));
        assert(!raw(x-21,y)); /* no modulo-coordinate duplicates */
        if(tool!=4) {
            memcpy(before,ram,sizeof ram);memcpy(&world_before,&world,sizeof world);
            assert(ScClipboardPaste(&clip,ram,live(),rom,sizeof rom,x-20,y)==SC_BUILD_INVALID);
            assert(!memcmp(before,ram,sizeof ram) && !memcmp(&world_before,&world,sizeof world));
        }
        assert(ScClipboardPaste(&clip,ram,live(),rom,sizeof rom,mw-1,mh-1)==(n==1?SC_BUILD_OK:SC_BUILD_INVALID));
    }
    /* Exact joins against original placement, including the neighbour outside
     * the copied selection, road/rail crossing and road/wire crossing. */
    for(unsigned size=0;size<5;++size) for(unsigned tool=1;tool<=3;++tool) {
        map(size,100000);place(tool,20,20);
        assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,20,20,20,20));
        place(tool,30,30);memcpy(before,ram,sizeof ram);memcpy(&world_before,&world,sizeof world);
        place(tool,31,30);unsigned expected0=raw(30,30),expected1=raw(31,30);
        memcpy(ram,before,sizeof ram);memcpy(&world,&world_before,sizeof world);
        assert(ScClipboardPaste(&clip,ram,live(),rom,sizeof rom,31,30)==SC_BUILD_OK);
        assert(raw(30,30)==expected0 && raw(31,30)==expected1);
    }
    for(unsigned size=0;size<5;size+=4) for(unsigned tool=1;tool<=3;++tool)
        for(unsigned mask=0;mask<16;++mask) {
            map(size,100000);int x=size?1800:30,y=size?1500:30;
            place(tool,20,20);
            assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,20,20,20,20));
            const int d[4][2]={{0,1},{1,0},{-1,0},{0,-1}};
            for(unsigned i=0;i<4;++i) if(mask&(1u<<i)) place(tool,x+d[i][0],y+d[i][1]);
            memcpy(before,ram,sizeof ram);memcpy(&world_before,&world,sizeof world);
            place(tool,x,y);unsigned expected[5]={raw(x,y)};
            for(unsigned i=0;i<4;++i) expected[i+1]=raw(x+d[i][0],y+d[i][1]);
            memcpy(ram,before,sizeof ram);memcpy(&world,&world_before,sizeof world);
            assert(ScClipboardPaste(&clip,ram,live(),rom,sizeof rom,x,y)==SC_BUILD_OK);
            assert(raw(x,y)==expected[0]);
            for(unsigned i=0;i<4;++i) assert(raw(x+d[i][0],y+d[i][1])==expected[i+1]);
        }
    map(4,1000000);place(1,300,300);place(3,300,300);
    assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,300,300,300,300));assert(clip.price==15);
    assert(ScClipboardPaste(&clip,ram,live(),rom,sizeof rom,1800,1500)==SC_BUILD_OK);
    assert((raw(1800,1500)&1023)==(raw(300,300)&1023));
    map(0,1000000);place(1,20,20);place(2,20,20);
    assert(ScClipboardCopy(&clip,ram,NULL,rom,sizeof rom,20,20,20,20));assert(clip.price==30);
    /* Every reward-building form is omitted, not even a corner is copied.
     * Landfill leaves terrain rather than a structure, so no reward exists. */
    for(unsigned gift=1;gift<=14;++gift) if(gift!=6) {
        map(4,100000);ram[0x3f5]=gift;place(15,1800,1500);place(4,1799,1500);
        assert(ScClipboardCopy(&clip,ram,&world,rom,sizeof rom,1799,1500,1802,1502));
        assert(clip.excluded==1 && clip.price==10 && clip.width==1 && clip.height==3);
        assert(!ScClipboardCopy(&clip,ram,&world,rom,sizeof rom,1802,1502,1802,1502));
        /* Missing owner marker cannot turn reward artwork into an ordinary
         * recovered object, or leave a partial reward in the clipboard. */
        write_cell(1801,1501,0);
        assert(ScClipboardCopy(&clip,ram,&world,rom,sizeof rom,1799,1500,1802,1502));
        assert(clip.excluded==1 && clip.price==10 && clip.width==1 && clip.count==3);
    }
    /* A reward inside a larger selection is a hole: a destination reward in
     * that hole remains intact. No gift inventory is granted or consumed. */
    map(4,100000);ram[0x3f5]=1;place(15,300,300);
    ram[0x3f5]=2;place(15,500,500);ram[0x3f5]=14;
    place(4,298,298);
    assert(ScClipboardCopy(&clip,ram,&world,rom,sizeof rom,298,298,304,304));
    assert(clip.width==7 && clip.height==7 && clip.count==40 && clip.excluded==1);
    unsigned reward_tile=raw(501,501);
    assert(ScClipboardPaste(&clip,ram,&world,rom,sizeof rom,498,498)==SC_BUILD_OK);
    assert(raw(501,501)==reward_tile && ram[0x3f5]==14);
    /* Extending a footprint must not cascade into an adjacent city block. */
    map(0,100000);place(5,20,20);place(5,23,20);
    assert(ScClipboardCopy(&clip,ram,NULL,rom,sizeof rom,22,21,22,21));
    assert(clip.width==3 && clip.height==3 && clip.count==9 && clip.price==100);
    put(0xb9d,99);ram[0xb9f]=0;memcpy(before,ram,sizeof ram);
    assert(ScClipboardPaste(&clip,ram,NULL,rom,sizeof rom,30,30)==SC_BUILD_FUNDS);
    assert(!memcmp(before,ram,sizeof ram));
    ram[0x425]=2;assert(ScClipboardPaste(&clip,ram,NULL,rom,sizeof rom,30,30)==SC_BUILD_OK);
    assert(word(0xb9d)==99);
    for(unsigned size=0;size<5;++size) {
        map(size,1000000);place(13,40,40);place(5,50,40);
        for(int x=44;x<50;++x) place(3,x,41);
        assert(ScConstructionRefreshPower(ram,live(),rom,sizeof rom));
        assert(raw(51,41)&0x8000);
        assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,40,40,52,42));
        assert(clip.height==4 && clip.price==5130);
        int x=world.active?ScWorldWidth(&world)-30:90,y=world.active?ScWorldHeight(&world)-20:80;
        assert(ScClipboardPaste(&clip,ram,live(),rom,sizeof rom,x,y)==SC_BUILD_OK);
        assert(!(raw(x+11,y+1)&0x8000));
        assert(ScConstructionRefreshPower(ram,live(),rom,sizeof rom));
        assert(raw(x+11,y+1)&0x8000);
    }
    /* Mature buildings retain their exact art and capacity but reconnect
     * power at the destination instead of carrying a stale power flag. */
    map(4,100000);for(int dy=0;dy<3;++dy) for(int dx=0;dx<3;++dx)
        write_cell(1800+dx,1500+dy,0x8000+0x140+dy*3+dx);
    assert(ScClipboardCopy(&clip,ram,&world,rom,sizeof rom,1802,1502,1802,1502));
    assert(ScClipboardPaste(&clip,ram,&world,rom,sizeof rom,1810,1500)==SC_BUILD_OK);
    assert(raw(1811,1501)==0x144);
    ScPopulation population={0};assert(ScPopulationRefreshLive(&population,ram,&world,rom,sizeof rom));
    assert(population.value==320);
    /* Every touched tile, not just the owner marker or southeast corner,
     * selects the full footprint in every mature RCI artwork variant. */
    for(unsigned size=0;size<5;++size) for(unsigned base=0x95;base<0x245;base+=9) {
        map(size,1000000);int x=size?ScWorldWidth(&world)-12:50,y=size?ScWorldHeight(&world)-12:50;
        for(unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx)
            write_cell(x+dx,y+dy,0x8000+base+dy*3+dx);
        for(unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx) {
            assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,x+dx,y+dy,x+dx,y+dy));
            assert(clip.source_x==x && clip.source_y==y && clip.width==3 && clip.height==3 && clip.count==9);
            assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,x+2,y+2,x+dx,y+dy));
            assert(clip.source_x==x && clip.source_y==y && clip.width==3 && clip.height==3 && clip.count==9);
        }
    }
    /* House artwork outside a surviving free-zone centre is still a valid
     * ordinary object. Houses around an intact centre select the whole zone. */
    for(unsigned size=0;size<5;++size) for(unsigned tile=0x89;tile<0x95;++tile) {
        map(size,1000000);place(5,40,40);write_cell(40,40,tile);
        assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,40,40,40,40));
        assert(clip.count==9 && clip.price==100);
        write_cell(60,60,tile);
        assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,60,60,60,60));
        assert(clip.count==1 && clip.width==1 && clip.height==1);
    }
    /* A mixed row with a lost centre marker used to reject the entire copy.
     * Recover the ordinary footprint without inventing replacement artwork. */
    for(unsigned size=0;size<5;++size) {
        map(size,1000000);
        for(unsigned dy=0;dy<3;++dy) for(unsigned dx=0;dx<3;++dx)
            write_cell(40+dx,40+dy,0x1eb+dy*3+dx);
        write_cell(41,41,0x1ad);
        assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,42,40,42,40));
        assert(clip.count==9 && clip.price==100 && clip.source_x==40 && clip.source_y==40);
        assert(clip.tiles[4]==0x1ad);
        assert(ScClipboardPaste(&clip,ram,live(),rom,sizeof rom,60,60)==SC_BUILD_OK && raw(61,61)==0x1ad);
    }
    /* Reconstruct all large ordinary footprints from any tile even with a
     * missing marker, including the airport's 6x6 artwork. */
    for(unsigned size=0;size<5;++size) for(unsigned tool=10;tool<=14;++tool) {
        map(size,1000000);unsigned cost=place(tool,40,40),n=tool==12?6:4,shift=n==6?2:1;
        write_cell(40+shift,40+shift,0);
        for(unsigned dy=0;dy<n;++dy) for(unsigned dx=0;dx<n;++dx) {
            if(dx==shift && dy==shift) continue;
            assert(ScClipboardCopy(&clip,ram,live(),rom,sizeof rom,40+dx,40+dy,40+dx,40+dy));
            assert(clip.source_x==40 && clip.source_y==40 && clip.width==(int)n && clip.height==(int)n);
            assert(clip.count==n*n && clip.price==cost);
        }
    }
    /* Optional read-only saved-city regression: each ordinary artwork cell
     * must remain selected when it alone is touched. No owner save is written. */
    if(argc==3) {
        f=fopen(argv[2],"rb");assert(f);uint8_t header[120];assert(fread(header,1,sizeof header,f)==sizeof header);
        unsigned start=40;assert(!memcmp(header+start,"SCWORLD",7));
        unsigned mw=header[start+8]|(unsigned)header[start+9]<<8,mh=header[start+10]|(unsigned)header[start+11]<<8;
        unsigned version=header[start+7];map(mw==1920?4:mw==960?3:mw==480?2:1,1000000);
        assert(ScWorldWidth(&world)==(int)mw && ScWorldHeight(&world)==(int)mh);
        assert(!fseek(f,start+(version>=4?80:version==3?64:48),SEEK_SET));
        assert(fread(world.tiles,2,mw*mh,f)==mw*mh);fclose(f);unsigned checked=0;
        for(unsigned y=0;y<mh;++y) for(unsigned x=0;x<mw;++x) {
            unsigned t=raw(x,y)&1023;if(t<0x80 || t>=958 || (t>=0x2bb && t<0x354)) continue;
            assert(ScClipboardCopy(&clip,ram,&world,rom,sizeof rom,x,y,x,y));
            assert(x>=(unsigned)clip.source_x && y>=(unsigned)clip.source_y);
            assert(clip.mask[(y-clip.source_y)*clip.width+x-clip.source_x]);++checked;
        }
        printf("PASS: %u individual ordinary artwork selections in supplied saved city\n",checked);
    }
    /* A whole maximum-sized infrastructure copy exceeds the native 24-bit
     * treasury. The total must stay wide and reject without partial writes. */
    map(4,0xffffff);
    for(int y=0;y<1600;++y) for(int x=0;x<1920;++x) write_cell(x,y,0x32);
    assert(ScClipboardCopy(&clip,ram,&world,rom,sizeof rom,0,0,1919,1599));
    assert(clip.count==3072000 && clip.price==UINT64_C(30720000));
    memset(world.tiles,0,sizeof world.tiles);memcpy(before,ram,sizeof ram);
    assert(ScClipboardPaste(&clip,ram,&world,rom,sizeof rom,0,0)==SC_BUILD_FUNDS);
    assert(!raw(0,0) && !raw(1919,1599) && !memcmp(before,ram,sizeof ram));
    ScClipboardClear(&clip);
    puts("PASS: five map sizes, whole objects, native prices/joins, infrastructure, parks, reward exclusions, wide totals and atomic paste");
}
