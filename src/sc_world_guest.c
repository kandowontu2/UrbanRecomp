#include "sc_world_guest.h"
#include "sc_transport.h"
#include "sc_infrastructure.h"
#include "sc_land.h"
#include "sc_land_summary.h"
#include "sc_power_traversal.h"
#include "sc_zoning.h"
#include "sc_smoothing.h"
#include "sc_service.h"
#include "sc_density.h"
#include "sc_tile_lookup.h"
#include "sc_postpass.h"
#include "sc_sweep.h"
#include "sc_land_type.h"
#include "snes/interp816.h"
#include "sc_program.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "sc_world_guest_layout.h"

static ScStencilBackend stencil_backend;
static const uint8_t *tile_properties;
void ScWorldGuestBindRom(const uint8_t *rom,size_t size) {
    tile_properties=rom && size>=0x188eb?rom+0x184eb:NULL;
}
static uint64_t stencil_cells,service_cells,service_gpu_cells,crime_gpu_cells,land_gpu_cells;
void ScWorldGuestSetStencilBackend(const ScStencilBackend *b) {
    if(b) stencil_backend=*b;else memset(&stencil_backend,0,sizeof stencil_backend);
    stencil_cells=service_cells=service_gpu_cells=crime_gpu_cells=land_gpu_cells=0;
}
uint64_t ScWorldGuestStencilCells(void) {return stencil_cells;}
uint64_t ScWorldGuestServiceCells(void) {return service_cells;}
uint64_t ScWorldGuestServiceGpuCells(void) {return service_gpu_cells;}
uint64_t ScWorldGuestCrimeGpuCells(void) {return crime_gpu_cells;}
uint64_t ScWorldGuestLandGpuCells(void) {return land_gpu_cells;}
static bool kernel_interpreter_profile;
static uint64_t kernel_interpreter_ops[65536];
void ScWorldGuestInterpreterProfile(bool enabled) {
    kernel_interpreter_profile=enabled;
    memset(kernel_interpreter_ops,0,sizeof kernel_interpreter_ops);
}
const uint64_t *ScWorldGuestInterpreterCounts(void) {return kernel_interpreter_ops;}

static unsigned word(const uint8_t *r,unsigned p) { return r[p]|((unsigned)r[p+1]<<8); }
static void put(uint8_t *r,unsigned p,unsigned v) { r[p]=(uint8_t)v; r[p+1]=(uint8_t)(v>>8); }
static void nz(Interp816 *c,unsigned v,bool byte) {
    c->z=(v&(byte?255:65535))==0; c->n=(v&(byte?128:32768))!=0;
}
static unsigned big_dimension(uint32_t pc);
static unsigned dimension(const ScWorld *w,uint32_t pc);
unsigned ScWorldGuestClockScale(const ScWorld *w,uint32_t pc) {
    if (!w || !w->active || (pc>>16)!=3) return 1;
    unsigned p=pc&65535;
    /* Verified US spatial loops: tile sweep and transport, traffic decay,
     * zone handlers, density/land value/crime, coverage and power flood.
     * Exclude the simulation dispatcher ($8000), demand ($90a7), budgets,
     * disasters and the redraw wait ($ae1c), which must keep normal time. */
    bool spatial=(p>=0x821d && p<0x88b4) ||
        (p>=0x88f3 && p<0x90a7) || (p>=0x90c5 && p<0xa2f5) ||
        (p>=0xa493 && p<0xae1c) || (p>=0xae25 && p<0xb42f) ||
        (p>=0xb66a && p<0xb6bd);
    return spatial?ScWorldCells(w)/12000:1;
}
unsigned ScWorldGuestMasterCycles(const ScWorld *w,uint32_t pc,
                                  unsigned master,unsigned *remainder) {
    unsigned area=ScWorldGuestClockScale(w,pc);
    return ScWorldGuestScaledCycles(master,area,remainder);
}
unsigned ScWorldGuestCpuClockScale(const ScWorld *w,const Interp816 *c,const uint8_t *r) {
    unsigned scale=ScWorldGuestClockScale(w,((unsigned)c->k<<16)|c->pc);
    if(scale>1 || !w || !w->active || c->k!=3 || c->e || c->d) return scale;
    unsigned p=c->pc;
    /* All four inline-operand arithmetic wrappers save the caller's DP above
     * the adjusted JSR return. From the operand-copy phase through PLD, that
     * return is exactly SP+3. After PLD it is SP+1. No scan of arbitrary stack
     * data or unsaved host phase flags is needed. */
    unsigned ret=0,caller=0;
    const unsigned entry[]={0xa2f5,0xa350,0xa3cf,0xa421};
    for(unsigned i=0;i<4;++i) if(p>=entry[i] && p<entry[i]+20) {
        unsigned phase=p-entry[i];
        if(phase<=2)ret=c->sp+1;
        else if(phase==3)caller=c->a+1;
        else if(phase<=8)caller=c->y+1;
        else if(phase<=11)ret=c->sp+1;
        else if(phase==12)ret=c->sp+3;
        else ret=c->sp+5;
        break;
    }
    if((p>=0xa309 && p<=0xa34e) || (p>=0xa364 && p<=0xa3cd) ||
       (p>=0xa3e3 && p<=0xa41f) || (p>=0xa435 && p<=0xa491)) ret=c->sp+3;
    else if(p==0xa34f || p==0xa3ce || p==0xa420 || p==0xa492)ret=c->sp+1;
    if(ret && ret+1<0x2000)caller=word(r,ret)+1;
    return caller?ScWorldGuestClockScale(w,0x30000|(caller&65535)):scale;
}
unsigned ScWorldGuestScaledCycles(unsigned master,unsigned area,unsigned *remainder) {
    if(area<=1) return master;
    unsigned total=master+*remainder;
    unsigned elapsed=(total/(2*area))*2;
    *remainder=total-elapsed*area;
    return elapsed;
}
static unsigned stencil_add(unsigned *sum,unsigned value,unsigned dp,bool carry_branch) {
    unsigned old=*sum&255;
    *sum+=value;
    unsigned cycles=7; /* CLC, ADC long,X */
    if (carry_branch) cycles+=old+value>255?7+dp:3; /* BCC / INC dp */
    return cycles;
}
/* Native tile pollution classification. This is the game's category curve,
 * including the negative stadium contribution. No ROM instructions execute
 * here; cycle accounting keeps the compatibility scheduler interruptible. */
unsigned ScWorldGuestFastStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,size_t size) {
    if (!w || !w->active || c->k!=3 || c->db!=3 || c->xf || c->d || c->e ||
        c->nmiWanted || (c->irqWanted && !c->i)) return 0;
    if(c->pc==0x9dca) {
        unsigned tile=c->a&1023,dp=(c->dp&255)!=0;
        c->mf=false;c->a=tile;nz(c,tile,false);
        if(tile && tile<0x28) {
            unsigned old=word(r,c->dp+0x22),value=old+15;
            c->a=(uint16_t)value;c->c=value>65535;
            c->v=((old^value)&(15^value)&32768)!=0;
            put(r,c->dp+0x22,value);nz(c,value,false);
        }
        unsigned cycles=tile?27+2*dp:9;
        if(tile>=0x28) {
            c->xf=false;
            cycles=3+3+2+3+3;
            cycles+=3+(tile<0x2bf?3:2);
            if(tile>=0x2bf) {
                cycles+=3+(tile>=0x354?3:2);
                if(tile<0x354) {
                    cycles+=3+(tile==0x307?3:2);
                    if(tile!=0x307) {
                        cycles+=3+(tile==0x310?3:2);
                        if(tile!=0x310) {
                            put(r,(uint16_t)(c->sp-1),tile);
                            put(r,c->dp+0x22,255);cycles+=4+3+4+dp+5;
                        }
                    }
                }
            }
            put(r,(uint16_t)(c->sp-1),tile);put(r,(uint16_t)(c->sp-3),0x9dfd);
            unsigned helper=0,score=ScLandTilePollution(tile,&helper);
            c->y=(uint16_t)score;
            unsigned old=word(r,c->dp+14),sum=old+score;
            put(r,c->dp+14,sum);c->v=((old^sum)&(score^sum)&32768)!=0;
            cycles+=4+6+helper+2+4+dp+4+dp+5+3+(tile<0x30?3:2);
            if(tile>=0x30) {put(r,c->dp+16,word(r,c->dp+16)+1);cycles+=7+dp;}
            c->a=tile;c->c=tile>=0x30;nz(c,tile>=0x30?word(r,c->dp+16):(uint16_t)(tile-0x30),false);
        }
        c->pc=0x9e0b;c->cyclesUsed=3;
        if(tile>=0x30) c->cyclesUsed=7+dp;
        return cycles;
    }
    if((c->pc==0x82ae || c->pc==0x9af7) && c->sp && rom && size>=0x188eb) {
        bool sweep=c->pc==0x82ae;
        unsigned x=sweep?(w->huge?w->scan_x:r[0xb85]):word(r,c->dp+0x10);
        unsigned y=sweep?(w->huge?w->scan_y:r[0xb86]):word(r,c->dp+0x12);
        if(!ScWorldContains(w,x,y)) return 0;
        unsigned offset=2*(y*ScWorldWidth(w)+x),raw=ScWorldCell(w,x,y),tile=raw&1023;
        unsigned property=rom[0x184eb+tile],dp=(c->dp&255)!=0;
        unsigned crossed=tile>=0x15;
        static int sweep_reference=-1;
        if(sweep_reference<0) {const char *e=getenv("SC_SWEEP_REFERENCE");sweep_reference=e && *e=='1';}
        /* Ordinary building artwork has no owner flag and does not call a
         * zone handler. Count it in the same C span as terrain; retain the
         * original paths for owners, infrastructure and active disasters. */
        if(sweep?(!c->mf && (!sweep_reference || tile<64) && tile<958 && tile!=0x7f && tile!=0x364 && tile!=0x365 &&
            !(property&0x71) && word(r,c->dp)==(uint16_t)offset):!(property&1)) {
            w->map_anchor=offset;put(r,0xb3f,x*2);put(r,0xb3d,y*32);put(r,0xb89,tile);
            c->mf=false;c->x=(uint16_t)offset;c->y=tile;c->a=0;
            /* JSR leaves its return bytes in WRAM after RTS. */
            put(r,(uint16_t)(c->sp-1),sweep?0x8340:0x9b00);
            if(!sweep) {
                c->c=offset>65535;nz(c,0,false);c->pc=0x9b5a;c->cyclesUsed=3;
                return 48+2*dp+crossed;
            }
            put(r,0xb49,offset);put(r,0xb87,raw);put(r,c->dp,offset+2);c->x=(uint16_t)(offset+2);
            unsigned counter=0,cycles=147+3*dp+4*crossed+7;
            if(!tile) {counter=0xe27;cycles+=2+8+3; c->c=false;}
            else {
                cycles+=3+3+(tile<0x26?3:2);
                if(tile>=0x26) {cycles+=3+(tile>=0x28?3:2);if(tile<0x28) {counter=0xe23;cycles+=8+3;}}
                if(!counter) {
                    cycles+=3+(tile<0x14?3:2);
                    if(tile>=0x14) {cycles+=3+(tile>=0x26?3:2);if(tile<0x26) {counter=0xe25;cycles+=8+3;}}
                    if(!counter) {c->a=property&8;cycles+=5+crossed+3+(c->a?2:3);if(c->a) {counter=0xe29;cycles+=8;}}
                }
                c->c=tile>=0x28;
            }
            if(counter) {unsigned value=(word(r,counter)+1)&65535;put(r,counter,value);nz(c,value,false);}
            else nz(c,c->a,false);
            c->pc=0x8341;c->cyclesUsed=6;
            return cycles+6;
        }
        return 0;
    }
    if((c->pc!=0xa04a && c->pc!=0xa0d0) || !c->mf) return 0;
    unsigned width=ScWorldFieldWidth(w,13),height=ScWorldFieldHeight(w,13);
    unsigned x=word(r,c->dp),y=word(r,c->dp+2),i=y*width+x;
    if(x>=width || y>=height || c->x!=(uint16_t)i) return 0;
    const uint8_t *source=w->fields[c->pc==0xa04a?13:14];
    uint8_t *dest=w->fields[c->pc==0xa04a?14:13];
    unsigned dp=(c->dp&255)!=0,sum=0;
    /* Same five-point stencil, clipping, scratch bytes, flags and cycle
     * cost as $a04a..$a09e / $a0d0..$a124. One cell stays interruptible. */
    unsigned cycles=(3+dp)+2+(4+dp)+(x?2:3);
    if(x) cycles+=stencil_add(&sum,source[i-1],dp,false);
    cycles+=3+(x+1<width?2:3);
    if(x+1<width) cycles+=stencil_add(&sum,source[i+1],dp,true);
    cycles+=(4+dp)+(y?2:3);
    if(y) cycles+=stencil_add(&sum,source[i-width],dp,true);
    cycles+=3+(y+1<height?2:3);
    if(y+1<height) cycles+=stencil_add(&sum,source[i+width],dp,true);
    unsigned old=sum&255;
    cycles+=stencil_add(&sum,source[i],dp,true);
    c->v=((old^(sum&255))&(source[i]^(sum&255))&128)!=0;
    put(r,c->dp+4,sum);
    unsigned value=sum/4;
    cycles+=(3+dp)+3+(4+dp)+4+3+(value<250?3:5)+3+5;
    c->c=value>=250;
    nz(c,value<250?(uint16_t)(value-250):250,false);
    if(value>250) value=250;
    dest[i]=(uint8_t)value;c->a=value;c->y=y;
    c->pc=c->pc==0xa04a?0xa09f:0xa125;
    c->cyclesUsed=5; /* final native STA long,X */
    return cycles;
}
static int lift(int low,int reference,int modulus) {
    int delta=(low-reference)%modulus;
    if(delta>modulus/2) delta-=modulus;
    if(delta<-modulus/2) delta+=modulus;
    return reference+delta;
}
static void coord_set(ScWorld *w,uint8_t *r,int x,int y) {
    w->coord[2][0]=x;w->coord[2][1]=y;r[0xb85]=(uint8_t)x;r[0xb86]=(uint8_t)y;
}
static bool huge_step(ScWorld *w,Interp816 *c,uint8_t *r) {
    if(c->k==1) {
        /* These bounds read byte proxies; compare their full coordinate. */
        unsigned axis=0;bool bound=true;
        switch(c->pc) {
        case 0xb6dd:case 0xb716:case 0xb8b1:case 0xc77b:axis=0;break;
        case 0xb6eb:case 0xb724:case 0xb8c1:case 0xc784:axis=1;break;
        default:bound=false;break;
        }
        if(bound) {
            int reference=c->pc==0xc77b?(int16_t)word(r,0x1d3):c->pc==0xc784?(int16_t)word(r,0x1d5):word(r,0x205+2*axis);
            int v=lift(c->a&255,reference,256);unsigned limit=axis?ScWorldHeight(w):ScWorldWidth(w);
            c->c=v<0 || (unsigned)v>=limit;c->z=(unsigned)v==limit;c->n=false;c->pc+=2;return true;
        }
    }
    if(!w->huge) return false;
    unsigned pc=((unsigned)c->k<<16)|c->pc,big=big_dimension(pc);
    bool load=pc==0x00b05f || pc==0x00b2f6 || pc==0x00b4c2 || pc==0x00b706 || pc==0x00b9f8 ||
        pc==0x01be51 || pc==0x01c794 || pc==0x03accf || pc==0x03acfe || pc==0x03bc9f || pc==0x03bca7;
    if(c->mf && !load && (big==240 || big==200 || big==239 || big==199)) {
        unsigned axis=big==200 || big==199;
        int reference=w->coord[c->k<3?c->k:2][axis];
        if(c->k==0) reference=(int16_t)word(r,axis?0x1bf:0x1bd)+16;
        unsigned raw=pc==0x03b385?c->x:pc==0x03b38f?c->y:c->a;
        int v=lift(raw&255,reference,256);unsigned limit=dimension(w,pc);
        c->c=v<0 || (unsigned)v>=limit;c->z=(unsigned)v==limit;c->n=false;c->pc+=2;return true;
    }
    if(c->k!=3) return false;
    switch(c->pc) {
    case 0x8281:
        if(w->colossal) w->field_scan=0;
        break;
    case 0x8291:
        if(w->colossal) {
            w->field_scan+=2;
            unsigned end=ScWorldFieldSizeWorld(w,10);
            c->c=w->field_scan>=end;c->z=w->field_scan==end;c->n=false;
            c->pc+=3;return true;
        }
        break;
    case 0x9c37:case 0x9c39:
        /* The ROM initializes byte coordinates. Their high bytes belong to
         * unused scratch in the stock routine, but are real coordinates on
         * expanded grids. Clear them at each corresponding byte STZ. */
        if(c->mf && c->dp<=0x1ff0) r[c->dp+(c->pc==0x9c37?11:9)]=0;
        break;
    case 0x9c3b:
        /* Older saves can resume a land pass with those scratch high bytes
         * still attached. Recover only impossible coordinates; full valid
         * columns/rows above 255 are retained. */
        if(c->dp<=0x1ff0) {
            unsigned x=word(r,c->dp+8),y=word(r,c->dp+10);
            if(x>=ScWorldWidth(w)/2) put(r,c->dp+8,(x&255)<ScWorldWidth(w)/2?x&255:0);
            if(y>=ScWorldHeight(w)/2) put(r,c->dp+10,(y&255)<ScWorldHeight(w)/2?y&255:0);
        }
        break;
    case 0x8297: {
        static int reference=-1;
        if(reference<0) {const char *e=getenv("SC_SCAN_ORDER_REFERENCE");reference=e && *e=='1';}
        w->scan_spread=w->huge && !reference;
        w->scan_x=w->scan_y=0;coord_set(w,r,0,0);break;
    }
    case 0x82ae:coord_set(w,r,w->scan_x,w->scan_y);break;
    case 0x9af7:coord_set(w,r,word(r,c->dp+0x10),word(r,c->dp+0x12));break;
    case 0x9b5c: {
        unsigned x=word(r,c->dp+0x10),y=word(r,c->dp+0x12);
        /* The native owner INC only updates the low count word. Complete its
         * carry at this unique iterator boundary, which immediately redirects
         * to the next cell. Empty cells must not repeatedly carry a zero count.
         * Keeping this here gives connected C and compatibility execution the
         * same rule, including a save taken between the INC and iterator. */
        if(w->giant && tile_properties && !word(r,c->dp+8) &&
           ScWorldContains(w,x,y) && (tile_properties[ScWorldCell(w,x,y)&1023]&1))
            put(r,c->dp+10,word(r,c->dp+10)+1);
        ++x;
        if(x==ScWorldWidth(w)) {x=0;++y;}
        put(r,c->dp+0x10,x);put(r,c->dp+0x12,y);
        coord_set(w,r,x,y);
        c->pc=y<ScWorldHeight(w)?0x9af7:0x9b6c;return true;
    }
    case 0x9cdf:case 0x9eb0:case 0x9c77:
        coord_set(w,r,word(r,c->dp+8)*2,word(r,c->dp+10)*2);break;
    case 0xa040:case 0xa0c6:
        coord_set(w,r,word(r,c->dp)*2,word(r,c->dp+2)*2);break;
    case 0x9fb7:
        coord_set(w,r,word(r,c->dp)*4,word(r,c->dp+2)*4);break;
    case 0xa01d:
        if(w->colossal) {
            /* The quarter-resolution density grid is now 480x400. Keep
             * word counters behind the original eight-bit loop operands. */
            unsigned x=word(r,c->dp)+1,y=word(r,c->dp+2);
            if(x==ScWorldWidth(w)/4) {x=0;++y;}
            put(r,c->dp,x);put(r,c->dp+2,y);
            /* The redirected SEP executes immediately, so its entry hook
             * will not run again before the packed coordinate helper. */
            coord_set(w,r,x*4,y*4);
            c->a=(c->a&0xff00)|(y&255);c->c=y>=ScWorldHeight(w)/4;
            nz(c,y-ScWorldHeight(w)/4,true);
            c->pc=c->c?0xa02d:0x9fb7;return true;
        }break;
    case 0xa258:
        if(w->mega) {r[c->dp+1]=r[c->dp+3]=0;}break;
    case 0xa164:case 0xa1e3:case 0xa25c:
        coord_set(w,r,(w->mega?word(r,c->dp):r[c->dp])*8,
            (w->mega?word(r,c->dp+2):r[c->dp+2])*8);break;
    case 0xa1a6:case 0xa225:case 0xa288:
        if(w->mega) {
            unsigned entry=c->pc,x=word(r,c->dp)+1,y=word(r,c->dp+2);
            if(x==ScWorldWidth(w)/8) {x=0;++y;}
            put(r,c->dp,x);put(r,c->dp+2,y);coord_set(w,r,x*8,y*8);
            c->a=(c->a&0xff00)|((x?x:y)&255);
            c->c=y>=ScWorldHeight(w)/8;nz(c,y-ScWorldHeight(w)/8,true);
            c->pc=c->c?(entry==0xa1a6?0xa1b6:entry==0xa225?0xa235:0xa298):
                (entry==0xa1a6?0xa164:entry==0xa225?0xa1e3:0xa25c);
            return true;
        }break;
    case 0x9c40: {
        unsigned x=word(r,c->dp+8)+1,y=word(r,c->dp+10);
        if(x==ScWorldWidth(w)/2) {x=0;++y;}
        put(r,c->dp+8,x);put(r,c->dp+10,y);
        c->pc=y<ScWorldHeight(w)/2?0x9c3b:0x9c50;return true;
    }
    case 0x88f3:case 0xb66a:case 0x9b77:w->field_scan=0;break;
    case 0x8921:case 0x9c1e:case 0xa1b8:case 0xa237:
        if(w->colossal) w->field_scan=0;break;
    case 0x9aaf:case 0x9f7a:
        if(w->colossal) w->field_scan=0;break;
    case 0x9ab2:case 0x9f7d:
        if(w->colossal) {
            unsigned end=ScWorldFieldSizeWorld(w,c->pc==0x9ab2?9:8);
            /* Old snapshots did not keep this conversion cursor. Recover
             * their low-word position; new snapshots retain the full cursor. */
            if(w->field_scan>end || (uint16_t)w->field_scan!=c->y)w->field_scan=c->y;
            w->field_anchor[2]=w->field_scan;
        }break;
    case 0x9acc:case 0x9f97:
        if(w->colossal) {
            ++w->field_scan;
            unsigned end=ScWorldFieldSizeWorld(w,c->pc==0x9acc?9:8);
            c->c=w->field_scan>=end;c->z=w->field_scan==end;c->n=false;
            c->pc+=3;return true;
        }break;
    case 0x8946:case 0x9c28:case 0xa1c5:case 0xa244:
        if(w->colossal) {
            w->field_scan+=2;
            unsigned end=ScWorldFieldSizeWorld(w,c->pc==0x8946?7:c->pc==0x9c28?15:16);
            c->c=w->field_scan>=end;c->z=w->field_scan==end;c->n=false;
            c->pc+=3;return true;
        }break;
    case 0x8919:case 0xb692:case 0x9b88:
        ++w->field_scan;c->z=c->c=w->field_scan==ScWorldFieldSizeWorld(w,0);c->n=false;c->pc+=3;return true;
    case 0xafc1:
        if(w->giant) {memset(w->fields[5],0,ScWorldFieldSizeWorld(w,5));c->x=(uint16_t)ScWorldFieldSizeWorld(w,5);c->pc=0xafcc;return true;}break;
    case 0xa141:
        if(w->giant) {memset(w->fields[13],0,ScWorldFieldSizeWorld(w,13));c->x=(uint16_t)ScWorldFieldSizeWorld(w,13);c->pc=0xa14c;return true;}break;
    case 0xb10b:
        if(w->giant) {c->c=!ScWorldContains(w,w->coord[2][0],w->coord[2][1]);c->z=false;c->n=false;c->pc+=3;return true;}break;
    case 0x9b6f: {
        uint32_t count=word(r,c->dp+8)|((uint32_t)word(r,c->dp+10)<<16);
        uint32_t x=word(r,c->dp)|((uint32_t)word(r,c->dp+2)<<16);
        uint32_t y=word(r,c->dp+4)|((uint32_t)word(r,c->dp+6)<<16);
        if(count) {w->center_x=x/count;w->center_y=y/count;w->center_valid=true;}
        break;
    }
    case 0x9ba0:c->a=(c->a&0xff00)|(w->center_x&255);break;
    case 0x9baf:c->a=(c->a&0xff00)|(w->center_y&255);break;
    case 0x9bb6:w->center_x=ScWorldWidth(w)/2;w->center_y=ScWorldHeight(w)/2;w->center_valid=true;break;
    case 0xb37e:c->n=lift(c->y,w->coord[2][1],256)<0;c->pc+=2;return true;
    case 0xb39a:c->n=lift(c->x,w->coord[2][0],256)<0;c->pc+=2;return true;
    case 0x8343:
        c->pc=ScWorldAdvanceScan(w)?0x82ac:0x835d;
        if(w->scan_spread && c->pc==0x82ac)put(r,c->dp,2*(w->scan_y*ScWorldWidth(w)+w->scan_x));
        coord_set(w,r,w->scan_x,w->scan_y);return true;
    case 0x8ff4: {
        int x=lift(r[0xb85],w->coord[2][0],256),y=lift(r[0xb86],w->coord[2][1],256);
        unsigned direction=c->a&255;
        int nx=x+(direction==1)-(direction==3),ny=y+(direction==2)-(direction==0);
        bool valid=ScWorldContains(w,nx,ny);
        if(direction<=3 && valid) coord_set(w,r,nx,ny);else coord_set(w,r,x,y);
        c->x=(uint8_t)(valid?nx:x);c->y=(uint8_t)(valid?ny:y);
        c->a=valid?1:0;c->mf=c->xf=false;c->z=!valid;c->n=false;c->pc=0x9034;return true;
    }
    case 0xb0a3: {
        unsigned count=word(r,0xc57);c->x=count;
        if(count) {coord_set(w,r,word(w->fields[17],2*count),word(w->fields[18],2*count));put(r,0xc57,count-1);}
        c->mf=false;c->pc=0xb0bd;return true;
    }
    case 0xb0be: {
        unsigned count=word(r,0xc57);c->c=count>=ScWorldFieldWidth(w,17)-1;c->z=count==ScWorldFieldWidth(w,17)-1;c->n=false;
        if(count<ScWorldFieldWidth(w,17)-1) {
            ++count;put(w->fields[17],2*count,lift(r[0xb85],w->coord[2][0],256));
            put(w->fields[18],2*count,lift(r[0xb86],w->coord[2][1],256));put(r,0xc57,count);
        }
        c->x=count;c->mf=false;c->pc=0xb0dc;return true;
    }
    default:break;
    }
    return false;
}
/* Observe the native month rollover after its INC, retaining the instruction's
 * flags, cycles and January budget path. Saturated native years keep the
 * original late-game date gates true; the serialized host year keeps advancing.
 * The post-INC observation is idempotent across connected-lane boundaries. */
static bool calendar_step(ScWorld *w,Interp816 *c,uint8_t *r) {
    if(!w || c->nmiWanted || (c->irqWanted && !c->i))return false;
    if(c->k==3) {
        switch(c->pc) {
        case 0xc60c:case 0xc645:case 0xce9e:case 0xcec8:
            w->calendar_year=word(r,0xb53);return true;
        case 0xc63c:case 0xc673:
            if(!w->calendar_year)w->calendar_year=word(r,0xb53);return true;
        case 0x804f: {
            unsigned native=word(r,0xb53);
            if(!w->calendar_year)w->calendar_year=native;
            else {
                unsigned proxy=w->calendar_year>65535?65535:w->calendar_year;
                if(native!=proxy && native==(uint16_t)(proxy+1) && w->calendar_year<SC_CALENDAR_YEAR_MAX)
                    ++w->calendar_year;
            }
            ScWorldYearMirror(w,r);return true;
        }
        default:break;
        }
    }
    if(c->k==2 && c->pc==0xb56e) {
        unsigned year=ScWorldYear(w,r);
        if(word(r,0x1fb)!=3 && !word(r,0xdc3) && year<SC_CALENDAR_YEAR_MAX)++year;
        char text[12];snprintf(text,sizeof text,"%u",year);
        unsigned count=(unsigned)strlen(text),end=word(r,0x1fb)==2?0x54:0x52;
        unsigned palette=word(r,0x1fb)==2?0x50:0xc50;
        for(unsigned i=0;i<6;++i) {
            unsigned tile=i+count<6?0x3ff:palette+(unsigned)(text[i+count-6]-'0');
            put(r,0x2840+end-10+i*2,tile);
            put(r,0x2880+end-10+i*2,tile==0x3ff?tile:tile+16);
        }
        return true;
    }
    return false;
}
static bool land_flood_step(ScWorld *w,const Interp816 *c,uint8_t *r) {
    /* US 03:bc53 seeds a flood, 03:adb3 spreads it. Run immediately before
     * their STA, after the native bounds/flammability/demolition checks.
     * The preceding 849e lookup supplies the full destination, not the
     * truncated simulation-sweep coordinates on expanded maps. */
    if(!w || w->land_type!=SC_LAND_BASALT || c->k!=3 || c->mf ||
       (c->pc!=0xbc53 && c->pc!=0xadb3) || c->a!=0x365 || !tile_properties ||
       c->e || c->d || c->waiting || c->stopped || c->nmiWanted ||
       (c->irqWanted && !c->i))return false;
    unsigned width=w->active?ScWorldWidth(w):120,height=w->active?ScWorldHeight(w):100;
    unsigned offset=w->active?w->map_anchor:c->x;
    if(offset==UINT32_MAX || (offset&1) || offset>=width*height*2)return true;
    uint8_t *map=w->active?w->tiles:r+0x10200;
    if((word(map,offset)&1023)==0x365)return true;
    unsigned cell=offset/2,mask=ScLandFloodFireMask(w->land_type,map,width,height,cell,tile_properties);
    const int dx[]={-1,1,0,0},dy[]={0,0,-1,1};
    for(unsigned d=0;d<4;++d)if(mask&(1u<<d)) {
        int x=(int)(cell%width)+dx[d],y=(int)(cell/width)+dy[d];
        if(w->active)ScWorldPutCell(w,x,y,0x7f);
        else put(map,2*((unsigned)y*width+(unsigned)x),0x7f);
    }
    return true;
}
void ScWorldGuestStep(ScWorld *w,Interp816 *c,uint8_t *r) {
    if(land_flood_step(w,c,r))return;
    if(calendar_step(w,c,r))return;
    if(w && w->active && c->k==3 && c->pc==0x9c39 && !c->nmiWanted && !(c->irqWanted && !c->i) && stencil_backend.land_begin)
        stencil_backend.land_begin(stencil_backend.context,w);
    if(w && w->active && c->k==3 && c->pc==0x9eb0 && !c->nmiWanted && !(c->irqWanted && !c->i) &&
       !word(r,c->dp+8) && !word(r,c->dp+10) && stencil_backend.crime_begin)
        stencil_backend.crime_begin(stencil_backend.context,w,word(r,0xc71));
    if(w && w->active && c->k==3 && !c->nmiWanted && !(c->irqWanted && !c->i) &&
       (c->pc==0xa02f || c->pc==0xa0b5 || c->pc==0xa14d || c->pc==0xa1cc) && stencil_backend.begin)
        stencil_backend.begin(stencil_backend.context,w,c->pc==0xa02f?13:c->pc==0xa0b5?14:c->pc==0xa14d?10:11);

    if (!w->active) return;
    if(c->k==3 && c->pc==0x9e61) {
        /* Keep the stock land-value curve, stretching its physical radius
         * with map dimensions (Big 2x, Huge 4x, 960x800 8x). Normal maps
         * execute the ROM untouched. Big's native center fits in bytes;
         * larger maps use the full centroid behind those byte proxies. */
        int x=c->a&255,y=c->a>>8;
        int cx=r[0xbab],cy=r[0xbac];
        if(w->huge) {
            x=lift(x,w->coord[2][0]/2,256);y=lift(y,w->coord[2][1]/2,256);
            cx=w->center_valid?w->center_x/2:ScWorldWidth(w)/4;
            cy=w->center_valid?w->center_y/2:ScWorldHeight(w)/4;
        }
        int dx=x-cx,dy=y-cy;
        if(dx<0) dx=-dx;if(dy<0) dy=-dy;
        unsigned distance=(dx+dy)/(ScWorldWidth(w)/120);
        c->c=distance>=32;if(distance>32) distance=32;
        c->a=((dx&255)<<8)|distance;c->mf=true;nz(c,distance,true);c->pc=0x9e8d;return;
    }
    if(huge_step(w,c,r)) return;
    if (c->k==1) {
        if (c->pc==0x8abf || c->pc==0x8acb) {
            int camera=(int16_t)word(r,c->pc==0x8abf?0x1bd:0x1bf);
            if (camera<0) camera=0;
            c->a=(uint16_t)((c->pc==0x8abf?200:56)+camera/8);
            nz(c,c->a,false);
        }
        /* The placement helper packs Y in the low byte, X in the high byte. */
        if (c->pc==0xba3f || c->pc==0xbdce || c->pc==0xbe07) {
            bool read=c->pc==0xba3f;
            int x=read?c->a>>8:word(r,0x219),y=read?c->a&255:word(r,0x21b);
            if(w->huge) {x=lift(x,word(r,0x205),256);y=lift(y,word(r,0x207),256);}
            unsigned offset=2*(y*ScWorldWidth(w)+x);
            w->bank_anchor[1]=ScWorldContains(w,x,y)?offset:UINT32_MAX;
            c->x=(uint16_t)offset; c->mf=false;
            if (read) { put(r,0x79,y*ScWorldWidth(w)); c->a=ScWorldCell(w,x,y)&1023; c->pc=0xba7f; }
            else { c->a=(uint16_t)word(r,0x215); ScWorldPutCell(w,x,y,c->a); c->pc=c->pc==0xbdce?0xbe06:0xbe3f; }
            nz(c,c->a,false); c->c=offset>65535;
        } else if (c->pc==0xc7b4 || c->pc==0xbe71 || c->pc==0xbe82) {
            unsigned pos=c->pc==0xc7b4?0x1d3:0x233;
            int x=(int16_t)word(r,pos),y=(int16_t)word(r,pos+2);
            if(w->huge && pos==0x233) {x=lift(x,word(r,0x205),256);y=lift(y,word(r,0x207),256);}
            unsigned offset=2*(y*ScWorldWidth(w)+x);
            w->bank_anchor[1]=ScWorldContains(w,x,y)?offset:UINT32_MAX;
            c->x=(uint16_t)offset;
        } else if (c->pc==0xb6db || c->pc==0xb6e9 || c->pc==0xb714 ||
                   c->pc==0xb722 || c->pc==0xb8af || c->pc==0xb8bf ||
                   c->pc==0xc779 || c->pc==0xc782) {
            /* Run the untaken BMI so the next instruction gets its own hook.
             * Skipping directly to CMP bypasses the full-coordinate check,
             * leaving Huge's byte-sized width/height truncated to 224/144. */
            c->n=false;
        }
        return;
    }
    if (c->k!=3) return;
    if (c->pc==0xb120) {
        if(w->huge) coord_set(w,r,lift(r[0xb85],w->coord[2][0],256),lift(r[0xb86],w->coord[2][1],256));
        unsigned row=(w->huge?w->coord[2][1]:r[0xb86])*(ScWorldWidth(w)/8);
        put(r,0xc5b,row); c->x=(uint16_t)(row+(w->huge?w->coord[2][0]:r[0xb85])/8);
        c->y=c->a=(w->huge?w->coord[2][0]:r[0xb85])&7; c->mf=false; c->c=false;
        nz(c,c->a,false); c->pc=0xb151;
    } else if (c->pc==0xb37e || c->pc==0xb39a) {
        c->n=(c->pc==0xb37e?c->y:c->x)==255;
    } else if (c->pc==0xade7) {
        w->map_anchor=0;
    } else if (c->pc==0xadee) {
        if (c->x==(uint16_t)(w->map_anchor+2)) w->map_anchor+=2;
    } else if (c->pc==0xae17) {
        unsigned end=w->map_anchor+2;
        c->z=c->c=end==(ScWorldCells(w)*2); c->n=false; c->pc=0xae1a;
    } else if (c->pc==0xaca7 || c->pc==0xacda || c->pc==0xad09 || c->pc==0xbaca) {
        unsigned x=word(r,c->dp),y=word(r,c->dp+4),offset=2*(y*ScWorldWidth(w)+x);
        if(w->huge) {w->coord[2][0]=x;w->coord[2][1]=y;}
        w->map_anchor=ScWorldContains(w,x,y)?offset:UINT32_MAX; c->x=(uint16_t)offset;
    } else if (c->pc==0x849e || c->pc==0x84c4) {
        int x=c->a&255,y=c->a>>8;
        if(w->huge) {x=lift(x,w->coord[2][0],256);y=lift(y,w->coord[2][1],256);}
        unsigned offset=2*(y*ScWorldWidth(w)+x);
        bool valid=ScWorldContains(w,x,y);
        w->map_anchor=valid?offset:UINT32_MAX;
        put(r,0xb3f,x*2); put(r,0xb3d,y*32);
        c->x=(uint16_t)offset; c->mf=false;
        if (c->pc==0x849e) {
            c->a=valid?ScWorldCell(w,x,y):0; c->pc=0x84c3; nz(c,c->a,false);
        } else {
            if (valid) ScWorldPutCell(w,x,y,c->y);
            /* The native writer finishes with TYA/STA, returning the tile. */
            c->a=c->y; c->pc=0x84ea; nz(c,c->a,false);
        }
        c->c=offset>65535;
    } else if (c->pc==0xb152) {
        /* A byte offset for 48,000 tiles cannot be represented by native X.
         * Apply the original MSB-first power bitmap to every full-size cell. */
        static int reference=-1;
        if(reference<0) {const char *e=getenv("SC_POWER_PUBLISH_REFERENCE");reference=e && *e=='1';}
        if(!reference) ScWorldPublishPower(w,w->tiles,w->fields[5],0,ScWorldCells(w));
        else {
        for (unsigned i=0;i<ScWorldCells(w);++i) {
            unsigned tile=word(w->tiles,i*2)&0x7fff;
            if (w->fields[5][i/8]&(128>>(i&7))) tile|=0x8000;
            if(word(w->tiles,i*2)!=tile) {put(w->tiles,i*2,tile);ScWorldTilesTouch(w,i*2,2);}
        }
        }
        put(r,0x23f,0); c->a=0; c->x=(uint16_t)(ScWorldCells(w)*2);
        c->y=ScWorldCells(w)/8; c->mf=false; c->c=true;
        nz(c,c->dp,false); c->pc=0xb1a4;
    } else if (c->pc==0x82b9) {
        /* The native sweep keeps a byte offset in its DP frame. Reconstruct
         * it from the full unsigned coordinates before each load; the second
         * bank of cells must not alias the first after offset 65,535. */
        unsigned offset=2*((w->huge?w->scan_y:r[0xb86])*ScWorldWidth(w)+(w->huge?w->scan_x:r[0xb85]));
        put(r,c->dp,offset); w->map_anchor=offset;
    } else if (c->pc==0xa29a || c->pc==0xa2b9 || c->pc==0xa2d7) {
        unsigned div=c->pc==0xa29a?2:c->pc==0xa2b9?4:8,stride=ScWorldWidth(w)/div;
        unsigned x=c->a&255,y=c->a>>8;
        if(w->huge) {x=lift(x,w->coord[2][0]/(int)div,256/div);y=lift(y,w->coord[2][1]/(int)div,256/div);}
        unsigned offset=y*stride+x;
        w->field_anchor[div==2?0:div==4?1:2]=offset;
        put(r,0xb3f,x); put(r,0xb3d,y);
        c->a=c->x=(uint16_t)offset; c->mf=true; nz(c,offset,false);
        c->c=false;
        c->pc=c->pc==0xa29a?0xa2b8:c->pc==0xa2b9?0xa2d6:0xa2f4;
    }
}
void ScWorldGuestVehicles(ScWorld *w,Interp816 *c,const uint8_t *r,uint8_t my) {
    if (!w->active || c->k!=0) return;
    unsigned x,y;
    if (c->pc==0xb077) { x=word(r,0xa5d); y=word(r,0xa5b); }
    else if (c->pc==0xb4dd) { x=word(r,0xae1)/8; y=word(r,0xae3)/8; }
    else if (c->pc==0xb719) { x=word(r,0xa61); y=word(r,0xa5f); }
    else if (c->pc==0xb314) {
        unsigned row=2*my*ScWorldWidth(w);
        int full=(int)row+(int16_t)(c->x-(uint16_t)row);
        w->bank_anchor[0]=full>=0 && full<(ScWorldCells(w)*2)?(uint32_t)full:UINT32_MAX;
        return;
    } else if (c->pc==0xb8f3 || c->pc==0xb968 || c->pc==0xb9ab) {
        int delta=(int16_t)c->a;
        int row=delta>=0?(delta+120)/240:(delta-120)/240;
        c->a=(uint16_t)(delta+row*(2*(ScWorldWidth(w)-120))); return;
    } else if (c->pc==0xb8f6 || c->pc==0xb96b || c->pc==0xb9ae) {
        unsigned row=2*my*ScWorldWidth(w);
        int full=(int)row+(int16_t)(c->a-(uint16_t)row);
        w->bank_anchor[0]=full>=0 && full<(ScWorldCells(w)*2)?(uint32_t)full:UINT32_MAX;
        c->c=full<0 || full>=(ScWorldCells(w)*2); c->z=false; c->n=c->c; c->pc+=3; return;
    } else return;
    unsigned offset=2*(y*ScWorldWidth(w)+x);
    w->bank_anchor[0]=ScWorldContains(w,x,y)?offset:UINT32_MAX; c->x=(uint16_t)offset;
}
/* Verified map geometry only; preserve currency, tile IDs and time lengths. */
static unsigned big_dimension(uint32_t pc) {
    switch (pc) {
    case 0x00ac85: return 240;
    case 0x00ac9d: return 200;
    case 0x00aefb: return 239;
    case 0x00af6b: return 199;
    case 0x00afde: return 240;
    case 0x00aff7: return 200;
    case 0x00b05f: return 240;
    case 0x00b29d: return 240;
    case 0x00b2f6: return 240;
    case 0x00b4c2: return 240;
    case 0x00b706: return 240;
    case 0x00b7ed: return 200;
    case 0x00b806: return 240;
    case 0x00b9f8: return 240;
    case 0x01a08e: return 215;
    case 0x01a09a: return 178;
    case 0x01a0a9: return 210;
    case 0x01a0b5: return 174;
    case 0x01a698: case 0x01a69d: return 210;
    case 0x01a6b1: case 0x01a6b6: return 172;
    case 0x01b3a7: return 240;
    case 0x01b3ae: return 240;
    case 0x01b3d3: return 200;
    case 0x01b3da: return 200;
    case 0x01b447: return 240;
    case 0x01b44f: return 200;
    case 0x01b492: return 240;
    case 0x01b49a: return 200;
    case 0x01b551: return 240;
    case 0x01b559: return 200;
    case 0x01b6dd: return 240;
    case 0x01b6eb: return 200;
    case 0x01b716: return 240;
    case 0x01b724: return 200;
    case 0x01b7cd: return 240;
    case 0x01b7e9: return 200;
    case 0x01b8b1: return 240;
    case 0x01b8c1: return 200;
    case 0x01be51: return 240;
    case 0x01c200: return 200;
    case 0x01c28d: return 240;
    case 0x01c77b: return 240;
    case 0x01c784: return 200;
    case 0x01c794: return 240;
    case 0x038291: return 1500;
    case 0x038349: return 240;
    case 0x038356: return 200;
    case 0x038919: return 12000;
    case 0x038946: return 1500;
    case 0x03900a: return 239;
    case 0x039014: return 199;
    case 0x039886: return 200;
    case 0x03988f: return 240;
    case 0x039acc: return 750;
    case 0x039b60: return 240;
    case 0x039b68: return 200;
    case 0x039b88: return 12000;
    case 0x039c28: return 3000;
    case 0x039c44: return 120;
    case 0x039c4c: return 100;
    case 0x039cb6: return 120;
    case 0x039cbf: return 100;
    case 0x039f4d: return 120;
    case 0x039f59: return 100;
    case 0x039f97: return 750;
    case 0x039fd2: return 59;
    case 0x039fed: return 49;
    case 0x03a021: return 60;
    case 0x03a029: return 50;
    case 0x03a057: return 119;
    case 0x03a072: return 99;
    case 0x03a0a5: return 120;
    case 0x03a0ae: return 100;
    case 0x03a0dd: return 119;
    case 0x03a0f8: return 99;
    case 0x03a12b: return 120;
    case 0x03a134: return 100;
    case 0x03a147: return 12000;
    case 0x03a17c: return 29;
    case 0x03a18f: return 24;
    case 0x03a1aa: return 30;
    case 0x03a1b2: return 25;
    case 0x03a1c5: return 1500;
    case 0x03a1fb: return 29;
    case 0x03a20e: return 24;
    case 0x03a229: return 30;
    case 0x03a231: return 25;
    case 0x03a244: return 1500;
    case 0x03a28c: return 30;
    case 0x03a294: return 25;
    case 0x03a9c8: return 240;
    case 0x03a9cd: return 200;
    case 0x03acbd: return 198;
    case 0x03accf: return 199;
    case 0x03acec: return 238;
    case 0x03acfe: return 239;
    case 0x03ad1f: return 198;
    case 0x03afc7: return 6000;
    case 0x03b0c1: return 20000;
    case 0x03b10b: return 6000;
    case 0x03b385: return 239;
    case 0x03b38f: return 199;
    case 0x03b420: return 240;
    case 0x03b425: return 200;
    case 0x03b692: return 12000;
    case 0x03bae6: return 240;
    case 0x03baef: return 200;
    case 0x03bc9f: return 239;
    case 0x03bca7: return 199;
    default: return 0;
    }
}
static unsigned scale_dimension(const ScWorld *w,unsigned v) {
    if(!w->huge) return v;
    unsigned scale=ScWorldScale(w);
    switch(v) {
    case 240:case 200:case 120:case 100:case 60:case 50:case 30:case 25:return v*scale;
    case 239:case 199:case 119:case 99:case 59:case 49:case 29:case 24:return (v+1)*scale-1;
    case 238:case 198:return (v+2)*scale-2;
    case 215:case 210:return 240*scale-(240-v);
    case 178:case 174:case 172:return 200*scale-(200-v);
    case 750:case 1500:case 3000:case 6000:case 12000:return v*scale*scale;
    default:return v;
    }
}
static unsigned dimension(const ScWorld *w,uint32_t pc) {return scale_dimension(w,big_dimension(pc));}
static bool map_base(unsigned base) {
    switch (base) {
    case 0x001a: case 0x010c: case 0x010e: case 0x01fc: case 0x01fe:
    case 0x0200: case 0x0202: case 0x0204: case 0x0206:
    case 0x02f0: case 0x02f2: case 0x02f4:
    case 0x03e0: case 0x03e2: case 0x03e4:
    case 0x04ca: case 0x04d0: case 0x04d6: case 0x07a0: return true;
    default: return false;
    }
}
void ScWorldGuestBegin(ScWorldGuest *g,ScWorld *w,const Interp816 *c,
                       const uint8_t *rom,size_t size) {
    memset(g,0,sizeof *g);
    if (!w->active || c->pc<0x8000) return;
    /* Native city initialization clears a contiguous stock WRAM range.
     * New host worlds are already zeroed; a loaded world must retain its
     * restored spatial fields while the native scratch buffers reset. */
    if(c->k==3 && c->pc==0xc877) return;
    if (c->k==3 && c->pc>=0xcf80) return; /* original-size native save codecs */
    size_t p=(size_t)(c->k&0x7f)*32768+c->pc-0x8000;
    if (p+3>=size) return;
    unsigned op=rom[p],base=word(rom,(unsigned)p+1),index=0,bank=c->db;
    uint32_t pc=((uint32_t)c->k<<16)|c->pc;
    unsigned dim=(op==0xa9 || op==0xc9 || op==0xe0 || op==0xc0)?dimension(w,pc):0;
    if(op==0xe9 && !c->mf) {
        if(pc==0x03a8c6) dim=4*ScWorldWidth(w)+4;
        if(pc==0x03a8f0 || pc==0x03a90b) dim=2*ScWorldWidth(w)+2;
    }
    /* Native footprint tables encode row offsets with the stock 240-byte
     * pitch. Translate only their verified consumers, before ADC/LDA; this
     * preserves CPU flags and the repair table's -1 terminator. */
    bool footprint=c->k==3 && (pc==0x0396b4 || pc==0x03970e ||
        pc==0x03a8d3 || pc==0x03a8fd || pc==0x03a918 ||
        pc==0x03aebb || pc==0x03aee3 || pc==0x03af0b);
    if(footprint && !c->mf && (op==0x79 || op==0xb9)) {
        size_t at=(size_t)(c->db&0x7f)*32768+((base+c->y)&32767);
        if(at+1<size) {
            unsigned value=word(rom,(unsigned)at);
            if(value!=65535) value=(value/240)*(2*ScWorldWidth(w))+value%240;
            g->bytes=2;g->immediate[0]=(uint8_t)value;g->immediate[1]=(uint8_t)(value>>8);
            g->data=g->immediate;g->mapped=true;
            g->address=((uint32_t)c->db<<16)|((base+c->y)&65535);return;
        }
    }
    if (dim) {
        bool index_width=op==0xe0 || op==0xc0;
        g->bytes=(index_width?c->xf:c->mf)?1:2;
        g->immediate[0]=(uint8_t)dim; g->immediate[1]=(uint8_t)(dim>>8);
        g->data=g->immediate; g->mapped=true; g->address=pc+1; return;
    }
    /* Byte-sized geometry operands are emulated before the opcode, not
     * truncated to an 8-bit immediate. Most geometry is already 16-bit. */
    bool indexed=false;
    switch (op) {
    case 0x1f: case 0x3f: case 0x5f: case 0x7f:
    case 0x9f: case 0xbf: case 0xdf: case 0xff:
        index=c->x; indexed=true; /* fall through */
    case 0x0f: case 0x2f: case 0x4f: case 0x6f:
    case 0x8f: case 0xaf: case 0xcf: case 0xef:
        bank=rom[p+3]; g->address=((uint32_t)bank<<16)+base+index; break;
    case 0x19: case 0x39: case 0x59: case 0x79:
    case 0x99: case 0xb9: case 0xd9: case 0xf9:
        index=c->y; indexed=true;
        g->address=((uint32_t)bank<<16)|((base+index)&65535); break;
    case 0x1d: case 0x3d: case 0x5d: case 0x7d:
    case 0x9d: case 0xbd: case 0xdd: case 0xfd:
        index=c->x; indexed=true;
        g->address=((uint32_t)bank<<16)|((base+index)&65535); break;
    default: return;
    }
    if (bank!=0x7f) return;
    g->bytes=c->mf?1:2;
    if (map_base(base)) {
        if (c->k==3 && c->pc>=0xcf80) return; /* native map codecs, handled separately */
        int offset=(int)base-0x200;
        int row=offset>=0?(offset+120)/240:(offset-120)/240;
        offset+=row*(2*(ScWorldWidth(w)-120)); /* double row pitch; retain the column displacement */
        uint32_t anchor=c->k==3?w->map_anchor:c->k<3?w->bank_anchor[c->k]:UINT32_MAX;
        int logical=(int)anchor;
        if (indexed) logical+=(int16_t)(index-(uint16_t)anchor);
        else logical=0;
        logical+=offset;
        if (c->k==2) {
            unsigned cell=index/2;
            logical=2*((cell/120)*(ScWorldWidth(w)/120)*ScWorldWidth(w)+(cell%120)*(ScWorldWidth(w)/120))+offset;
            anchor=0;
        }
        g->mapped=true;
        if (anchor!=UINT32_MAX && logical>=0 &&
            (unsigned)logical+g->bytes<=(ScWorldCells(w)*2)) {
            g->data=w->tiles+logical;g->tile_world=w;g->tile_offset=(unsigned)logical;
        }
    } else {
        unsigned field; int displacement;
        if (!ScWorldFieldResolve((uint16_t)base,&field,&displacement)) return;
        if (c->k==2 && field==5) {
            /* An overview pixel represents two cells along each axis. Packed
             * power bytes need bit resampling, rather than a wider byte stride. */
            for (unsigned i=0;i<g->bytes;++i) {
                unsigned scale=ScWorldWidth(w)/120;
                unsigned original=index+i,y=original/15*scale,x=original%15*8*scale;
                unsigned bits=0;
                if (y<ScWorldHeight(w)) for (unsigned bit=0;bit<8;++bit) {
                    unsigned cell=y*ScWorldWidth(w)+x+bit*scale;
                    if (w->fields[5][cell/8]&(128>>(cell&7))) bits|=128>>bit;
                }
                g->immediate[i]=(uint8_t)bits;
            }
            g->mapped=true; g->data=g->immediate; return;
        }
        const ScWorldField *layout=&ScWorldFields[field];
        if(w->huge && (displacement==(int)(layout->width*layout->element_bytes) || displacement==-(int)(layout->width*layout->element_bytes))) displacement*=ScWorldScale(w);
        int logical=(int)index+displacement;
        if(w->giant && c->k==3 && field<17) {
            unsigned anchor;
            if(field==5) anchor=((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8;
            else anchor=w->field_anchor[field<5 || field==13 || field==14?0:field==6 || field==15?1:2]*layout->element_bytes;
            if((pc>=0x0388fa && pc<=0x038919) || (pc>=0x03b676 && pc<=0x03b692) || (pc>=0x039b7a && pc<=0x039b88)) anchor=w->field_scan;
            /* Linear word scans use an unsigned index even before 64 KiB.
             * A signed displacement from an unrelated spatial anchor loses
             * the upper half of the 960x800 quarter-grid clear. */
            if((pc>=0x038287 && pc<=0x038291) ||
               (pc>=0x038924 && pc<=0x038946) || (pc>=0x039c22 && pc<=0x039c28))
                anchor=w->colossal?w->field_scan:index;
            if(w->colossal && (
                (pc>=0x03a1bb && pc<=0x03a1c5) ||
                (pc>=0x03a23a && pc<=0x03a244))) anchor=w->field_scan;
            logical=(int)anchor+(int16_t)(index-(uint16_t)anchor)+displacement;
        }
        if (c->k==2) {
            const ScWorldField *f=&ScWorldFields[field];
            unsigned cell=index/f->element_bytes;
            unsigned scale=ScWorldWidth(w)/120;
            unsigned y=cell/f->stock_width*scale,x=cell%f->stock_width*scale;
            if (y>=ScWorldFieldHeight(w,field)) y=ScWorldFieldHeight(w,field)-1;
            logical=(y*ScWorldFieldWidth(w,field)+x)*f->element_bytes+displacement;
        }
        g->mapped=true;
        if (logical>=0 && (unsigned)logical+g->bytes<=ScWorldFieldSizeWorld(w,field))
            g->data=w->fields[field]+logical;
    }
}
/* Ownership and operand decoding are preparation, not guest instructions.
 * Resolve them once while retaining a guard against every live four-byte ROM
 * edit. Width, bank, indexes, anchors and actual field binding stay live. */
static bool preparation_reference(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_WORLD_PREPARATION_REFERENCE");reference=e && *e=='1';}
    return reference;
}
static bool preparation_site(const uint8_t *bits,unsigned pc) {
    return pc>=0x8000 && (bits[(pc-0x8000)>>3]&(1u<<(pc&7)))!=0;
}
void ScWorldGuestStepPrepared(ScWorld *w,Interp816 *c,uint8_t *r) {
    if(land_flood_step(w,c,r))return;
    if(calendar_step(w,c,r))return;
    if(preparation_reference()) {ScWorldGuestStep(w,c,r);return;}
    if(!w->active || c->k>=4 || !preparation_site(sc_world_step_sites,c->pc)) return;
    /* Native simulation families repeatedly enter these coordinate helpers.
     * Execute their C operation directly, avoiding unrelated geometry and
     * bank dispatch. The original Step remains the independent oracle. */
    if(c->k==3 && (c->pc==0x849e || c->pc==0x84c4)) {
        int x=c->a&255,y=c->a>>8;
        if(w->huge) {x=lift(x,w->coord[2][0],256);y=lift(y,w->coord[2][1],256);}
        unsigned offset=2*(y*ScWorldWidth(w)+x);
        bool valid=ScWorldContains(w,x,y);
        w->map_anchor=valid?offset:UINT32_MAX;
        put(r,0xb3f,x*2);put(r,0xb3d,y*32);c->x=(uint16_t)offset;c->mf=false;
        if(c->pc==0x849e) {c->a=valid?ScWorldCell(w,x,y):0;c->pc=0x84c3;}
        else {if(valid) ScWorldPutCell(w,x,y,c->y);c->a=c->y;c->pc=0x84ea;}
        nz(c,c->a,false);c->c=offset>65535;return;
    }
    if(c->k==3 && (c->pc==0xa29a || c->pc==0xa2b9 || c->pc==0xa2d7)) {
        unsigned div=c->pc==0xa29a?2:c->pc==0xa2b9?4:8;
        unsigned x=c->a&255,y=c->a>>8;
        if(w->huge) {x=lift(x,w->coord[2][0]/(int)div,256/div);y=lift(y,w->coord[2][1]/(int)div,256/div);}
        unsigned offset=y*(ScWorldWidth(w)/div)+x;
        w->field_anchor[div==2?0:div==4?1:2]=offset;
        put(r,0xb3f,x);put(r,0xb3d,y);c->a=c->x=(uint16_t)offset;c->mf=true;
        nz(c,offset,false);c->c=false;
        c->pc=c->pc==0xa29a?0xa2b8:c->pc==0xa2b9?0xa2d6:0xa2f4;return;
    }
    ScWorldGuestStep(w,c,r);
}
void ScWorldGuestVehiclesPrepared(ScWorld *w,Interp816 *c,const uint8_t *r,uint8_t my) {
    if(preparation_reference() || (w->active && c->k==0 && preparation_site(sc_world_vehicle_sites,c->pc)))
        ScWorldGuestVehicles(w,c,r,my);
}
typedef struct {
    uint32_t tag,signature;
    uint16_t base,dimension;
    int16_t displacement;
    uint8_t opcode,bank,field,footprint;
} ScWorldAccessPlan;
static ScWorldAccessPlan access_plans[4096];
static const uint8_t *access_rom;
static size_t access_size;
static bool access_footprint(uint32_t pc) {
    return pc==0x0396b4 || pc==0x03970e || pc==0x03a8d3 ||
        pc==0x03a8fd || pc==0x03a918 || pc==0x03aebb || pc==0x03aee3 || pc==0x03af0b;
}
static const ScWorldAccessPlan *access_plan(const uint8_t *rom,size_t size,size_t p) {
    if(rom!=access_rom || size!=access_size) {
        memset(access_plans,0,sizeof access_plans);access_rom=rom;access_size=size;
    }
    uint32_t signature;memcpy(&signature,rom+p,4);
    ScWorldAccessPlan *plan=&access_plans[(p^(p>>12))&4095];
    if(plan->tag==p+1 && plan->signature==signature) return plan;
    memset(plan,0,sizeof *plan);plan->tag=p+1;plan->signature=signature;
    plan->opcode=rom[p];plan->base=word(rom,p+1);plan->bank=rom[p+3];plan->field=254;
    uint32_t pc=(p/32768)*65536+(p%32768)+0x8000;
    unsigned op=plan->opcode;
    if(op==0xa9 || op==0xc9 || op==0xe0 || op==0xc0) plan->dimension=big_dimension(pc);
    plan->footprint=access_footprint(pc);
    if(map_base(plan->base)) plan->field=255;
    else {
        unsigned field;int displacement;
        if(ScWorldFieldResolve(plan->base,&field,&displacement)) {
            plan->field=field;plan->displacement=displacement;
        }
    }
    return plan;
}
void ScWorldGuestBeginPrepared(ScWorldGuest *g,ScWorld *w,const Interp816 *c,
                       const uint8_t *rom,size_t size) {
    if(preparation_reference() || c->k>=16) {ScWorldGuestBegin(g,w,c,rom,size);return;}
    memset(g,0,sizeof *g);
    if (!w->active || c->pc<0x8000) return;
    /* Native city initialization clears a contiguous stock WRAM range.
     * New host worlds are already zeroed; a loaded world must retain its
     * restored spatial fields while the native scratch buffers reset. */
    if(c->k==3 && c->pc==0xc877) return;
    if (c->k==3 && c->pc>=0xcf80) return; /* original-size native save codecs */
    size_t p=(size_t)(c->k&0x7f)*32768+c->pc-0x8000;
    if (p+3>=size) return;
    /* Most driver instructions cannot bind a world array. Classify the live
     * opcode before cache lookup, retaining even an unmapped address exactly.
     * Footprint table consumers and geometry immediates are bank-independent
     * exceptions to ordinary data-bank admission. */
    unsigned live_op=rom[p],form=live_op&31;
    bool indexed_y=form==0x19,indexed_x=form==0x1d,long_address=(live_op&15)==15;
    uint32_t live_pc=((uint32_t)c->k<<16)|c->pc;
    if(!indexed_y && !indexed_x && !long_address) {
        if(live_op==0xa9 || live_op==0xc9 || live_op==0xe0 || live_op==0xc0) {
            if(!preparation_site(sc_world_step_sites,c->pc)) return;
        } else if(live_op!=0xe9 || c->mf ||
            (live_pc!=0x03a8c6 && live_pc!=0x03a8f0 && live_pc!=0x03a90b)) return;
    }
    if(long_address && rom[p+3]!=0x7f) {
        g->address=((uint32_t)rom[p+3]<<16)+word(rom,p+1)+((live_op&16)?c->x:0);return;
    }
    if((indexed_x || indexed_y) && c->db!=0x7f &&
       !(indexed_y && (live_op==0x79 || live_op==0xb9) && !c->mf && access_footprint(live_pc))) {
        unsigned index=indexed_x?c->x:c->y;
        g->address=((uint32_t)c->db<<16)|((word(rom,p+1)+index)&65535);return;
    }
    const ScWorldAccessPlan *plan=access_plan(rom,size,p);
    unsigned op=plan->opcode,base=plan->base,index=0,bank=c->db;
    uint32_t pc=((uint32_t)c->k<<16)|c->pc;
    unsigned dim=plan->dimension;
    if(dim && w->huge) dim=scale_dimension(w,dim);
    if(op==0xe9 && !c->mf) {
        if(pc==0x03a8c6) dim=4*ScWorldWidth(w)+4;
        if(pc==0x03a8f0 || pc==0x03a90b) dim=2*ScWorldWidth(w)+2;
    }
    /* Native footprint tables encode row offsets with the stock 240-byte
     * pitch. Translate only their verified consumers, before ADC/LDA; this
     * preserves CPU flags and the repair table's -1 terminator. */
    bool footprint=plan->footprint;
    if(footprint && !c->mf && (op==0x79 || op==0xb9)) {
        size_t at=(size_t)(c->db&0x7f)*32768+((base+c->y)&32767);
        if(at+1<size) {
            unsigned value=word(rom,(unsigned)at);
            if(value!=65535) value=(value/240)*(2*ScWorldWidth(w))+value%240;
            g->bytes=2;g->immediate[0]=(uint8_t)value;g->immediate[1]=(uint8_t)(value>>8);
            g->data=g->immediate;g->mapped=true;
            g->address=((uint32_t)c->db<<16)|((base+c->y)&65535);return;
        }
    }
    if (dim) {
        bool index_width=op==0xe0 || op==0xc0;
        g->bytes=(index_width?c->xf:c->mf)?1:2;
        g->immediate[0]=(uint8_t)dim; g->immediate[1]=(uint8_t)(dim>>8);
        g->data=g->immediate; g->mapped=true; g->address=pc+1; return;
    }
    /* Byte-sized geometry operands are emulated before the opcode, not
     * truncated to an 8-bit immediate. Most geometry is already 16-bit. */
    bool indexed=false;
    switch (op) {
    case 0x1f: case 0x3f: case 0x5f: case 0x7f:
    case 0x9f: case 0xbf: case 0xdf: case 0xff:
        index=c->x; indexed=true; /* fall through */
    case 0x0f: case 0x2f: case 0x4f: case 0x6f:
    case 0x8f: case 0xaf: case 0xcf: case 0xef:
        bank=plan->bank; g->address=((uint32_t)bank<<16)+base+index; break;
    case 0x19: case 0x39: case 0x59: case 0x79:
    case 0x99: case 0xb9: case 0xd9: case 0xf9:
        index=c->y; indexed=true;
        g->address=((uint32_t)bank<<16)|((base+index)&65535); break;
    case 0x1d: case 0x3d: case 0x5d: case 0x7d:
    case 0x9d: case 0xbd: case 0xdd: case 0xfd:
        index=c->x; indexed=true;
        g->address=((uint32_t)bank<<16)|((base+index)&65535); break;
    default: return;
    }
    if (bank!=0x7f) return;
    g->bytes=c->mf?1:2;
    if (plan->field==255) {
        if (c->k==3 && c->pc>=0xcf80) return; /* native map codecs, handled separately */
        int offset=(int)base-0x200;
        int row=offset>=0?(offset+120)/240:(offset-120)/240;
        offset+=row*(2*(ScWorldWidth(w)-120)); /* double row pitch; retain the column displacement */
        uint32_t anchor=c->k==3?w->map_anchor:c->k<3?w->bank_anchor[c->k]:UINT32_MAX;
        int logical=(int)anchor;
        if (indexed) logical+=(int16_t)(index-(uint16_t)anchor);
        else logical=0;
        logical+=offset;
        if (c->k==2) {
            unsigned cell=index/2;
            logical=2*((cell/120)*(ScWorldWidth(w)/120)*ScWorldWidth(w)+(cell%120)*(ScWorldWidth(w)/120))+offset;
            anchor=0;
        }
        g->mapped=true;
        if (anchor!=UINT32_MAX && logical>=0 &&
            (unsigned)logical+g->bytes<=(ScWorldCells(w)*2)) {
            g->data=w->tiles+logical;g->tile_world=w;g->tile_offset=(unsigned)logical;
        }
    } else {
        if(plan->field==254) return;
        unsigned field=plan->field;int displacement=plan->displacement;
        if (c->k==2 && field==5) {
            /* An overview pixel represents two cells along each axis. Packed
             * power bytes need bit resampling, rather than a wider byte stride. */
            for (unsigned i=0;i<g->bytes;++i) {
                unsigned scale=ScWorldWidth(w)/120;
                unsigned original=index+i,y=original/15*scale,x=original%15*8*scale;
                unsigned bits=0;
                if (y<ScWorldHeight(w)) for (unsigned bit=0;bit<8;++bit) {
                    unsigned cell=y*ScWorldWidth(w)+x+bit*scale;
                    if (w->fields[5][cell/8]&(128>>(cell&7))) bits|=128>>bit;
                }
                g->immediate[i]=(uint8_t)bits;
            }
            g->mapped=true; g->data=g->immediate; return;
        }
        const ScWorldField *layout=&ScWorldFields[field];
        if(w->huge && (displacement==(int)(layout->width*layout->element_bytes) || displacement==-(int)(layout->width*layout->element_bytes))) displacement*=ScWorldScale(w);
        int logical=(int)index+displacement;
        if(w->giant && c->k==3 && field<17) {
            unsigned anchor;
            if(field==5) anchor=((unsigned)w->coord[2][1]*ScWorldWidth(w)+(unsigned)w->coord[2][0])/8;
            else anchor=w->field_anchor[field<5 || field==13 || field==14?0:field==6 || field==15?1:2]*layout->element_bytes;
            if((pc>=0x0388fa && pc<=0x038919) || (pc>=0x03b676 && pc<=0x03b692) || (pc>=0x039b7a && pc<=0x039b88)) anchor=w->field_scan;
            /* Linear word scans use an unsigned index even before 64 KiB.
             * A signed displacement from an unrelated spatial anchor loses
             * the upper half of the 960x800 quarter-grid clear. */
            if((pc>=0x038287 && pc<=0x038291) ||
               (pc>=0x038924 && pc<=0x038946) || (pc>=0x039c22 && pc<=0x039c28))
                anchor=w->colossal?w->field_scan:index;
            if(w->colossal && (
                (pc>=0x03a1bb && pc<=0x03a1c5) ||
                (pc>=0x03a23a && pc<=0x03a244))) anchor=w->field_scan;
            logical=(int)anchor+(int16_t)(index-(uint16_t)anchor)+displacement;
        }
        if (c->k==2) {
            const ScWorldField *f=&ScWorldFields[field];
            unsigned cell=index/f->element_bytes;
            unsigned scale=ScWorldWidth(w)/120;
            unsigned y=cell/f->stock_width*scale,x=cell%f->stock_width*scale;
            if (y>=ScWorldFieldHeight(w,field)) y=ScWorldFieldHeight(w,field)-1;
            logical=(y*ScWorldFieldWidth(w,field)+x)*f->element_bytes+displacement;
        }
        g->mapped=true;
        if (logical>=0 && (unsigned)logical+g->bytes<=ScWorldFieldSizeWorld(w,field))
            g->data=w->fields[field]+logical;
    }
}
bool ScWorldGuestRead(const ScWorldGuest *g,uint32_t a,uint8_t *v) {
    if (!g->mapped || a<g->address || a-g->address>=g->bytes) return false;
    *v=g->data?g->data[a-g->address]:0; return true;
}
bool ScWorldGuestWrite(ScWorldGuest *g,uint32_t a,uint8_t v) {
    if (!g->mapped || a<g->address || a-g->address>=g->bytes) return false;
    if (g->data && g->data[a-g->address]!=v) {
        g->data[a-g->address]=v;
        if(g->tile_world) ScWorldTilesTouch(g->tile_world,g->tile_offset+(a-g->address),1);
    }
    return true;
}

typedef struct {
    ScWorld *world; Interp816 *cpu; uint8_t *ram; const uint8_t *rom;
    ScWorldGuest guest;
} KernelBus;
static uint8_t kernel_read(void *context,uint32_t a) {
    KernelBus *b=context;uint8_t value;
    if(ScWorldGuestRead(&b->guest,a,&value)) return value;
    unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f) return b->ram[a-0x7e0000];
    if(p<0x8000) return b->ram[p];
    return b->rom[((bank&15)*32768)+p-0x8000];
}
static void kernel_write(void *context,uint32_t a,uint8_t value) {
    KernelBus *b=context;
    if(ScWorldGuestWrite(&b->guest,a,value)) return;
    unsigned bank=a>>16,p=a&65535;
    if(bank==0x7e || bank==0x7f) b->ram[a-0x7e0000]=value;
    else if(p<0x8000) b->ram[p]=value;
}
/* A vacant 2x2 group takes the same path through the native pollution/land
 * routine. Preserve its scratch words, temporary stack bytes and byte ADC
 * overflow, including the density scratch accumulator's intentional wrap. */
static unsigned empty_terrain_cell(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(c->pc!=0x9cdf) return 0;
    unsigned x=word(r,c->dp+8),y=word(r,c->dp+10),width=ScWorldWidth(w);
    if(x>=width/2 || y>=ScWorldHeight(w)/2) return 0;
    unsigned offset=2*y*width+2*x,count=0;
    const unsigned cells[]={offset,offset+1,offset+width,offset+width+1};
    for(unsigned n=0;n<4;++n) {
        unsigned tile=word(w->tiles,2*cells[n])&1023;
        if(tile>=0x28) return 0;
        count+=tile!=0;
    }
    unsigned dp=(c->dp&255)!=0,cycles=273+16*dp+count*(18+2*dp);
    if(cycles+12>=max_cycles) return 0;
    if(w->huge) coord_set(w,r,x*2,y*2);
    unsigned index=y*(width/2)+x,coarse=(y/2)*(width/4)+x/2,sum=count*15;
    unsigned old=w->fields[15][coarse],value=old+sum;
    w->map_anchor=2*offset;w->field_anchor[0]=index;w->field_anchor[1]=coarse;
    w->fields[15][coarse]=(uint8_t)value;
    w->fields[13][index]=w->fields[0][index]=0;
    put(r,0xb3f,x);put(r,0xb3d,y);
    put(r,c->dp+0x22,sum);put(r,c->dp+0x0e,0);put(r,c->dp+0x10,0);
    put(r,(uint16_t)(c->sp-1),index);
    c->a=(uint16_t)index&0xff00;c->x=(uint16_t)index;
    c->v=((old^value)&(sum^value)&128)!=0;c->c=c->n=false;c->z=true;c->mf=true;
    c->pc=0x9dc9;c->cyclesUsed=5;
    return cycles;
}
static unsigned vacant_crime_cell(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(c->pc!=0x9eb0) return 0;
    unsigned x=word(r,c->dp+8),y=word(r,c->dp+10),width=ScWorldWidth(w)/2;
    if(x>=width || y>=ScWorldHeight(w)/2) return 0;
    unsigned index=y*width+x,cycles=42+2*((c->dp&255)!=0);
    if(w->fields[0][index] || cycles+12>=max_cycles) return 0;
    if(w->huge) coord_set(w,r,x*2,y*2);
    w->field_anchor[0]=index;w->fields[1][index]=0;
    put(r,0xb3f,x);put(r,0xb3d,y);put(r,(uint16_t)(c->sp-1),0x9eb9);
    c->a=(uint16_t)index&0xff00;c->x=(uint16_t)index;
    c->c=c->n=false;c->z=true;c->mf=true;c->pc=0x9f47;c->cyclesUsed=5;
    return cycles;
}
/* Commit the pollution cell and its statistics directly. This is a native C
 * operation over full-width fields; the CPU state is only the compatibility
 * boundary for the remaining guest scheduler. Retain byte scratch writes and
 * aggregate overflow so a saved city resumes identically. */
static unsigned pollution_cell(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(c->pc!=0x9c77 || max_cycles<160) return 0;
    unsigned x=word(r,c->dp+8),y=word(r,c->dp+10),width=ScWorldWidth(w)/2;
    if(x>=width || y>=ScWorldHeight(w)/2) return 0;
    unsigned index=y*width+x,dp=(c->dp&255)!=0;
    if(w->huge) coord_set(w,r,x*2,y*2);
    w->field_anchor[0]=index;put(r,0xb3f,x);put(r,0xb3d,y);
    put(r,(uint16_t)(c->sp-1),0x9c80);
    unsigned value=w->fields[13][index];w->fields[2][index]=(uint8_t)value;
    r[c->dp+12]=(uint8_t)value;c->x=(uint16_t)index;c->mf=true;c->c=false;
    c->a=(index&0xff00)|value;nz(c,value,true);c->pc=0x9cb0;c->cyclesUsed=3;
    if(!value) return 40+3*dp;
    c->mf=false;put(r,c->dp+20,word(r,c->dp+20)+1);
    unsigned sample=word(r,c->dp+12),old=word(r,c->dp),sum=old+sample;
    c->v=((old^sum)&(sample^sum)&32768)!=0;put(r,c->dp,sum);
    unsigned cycles=39+3*dp+27+4*dp;
    if(sum>65535) {put(r,c->dp+2,word(r,c->dp+2)+1);cycles+=6+dp;}
    unsigned maximum=word(r,c->dp+28);c->a=sample;c->c=sample>=maximum;
    nz(c,(uint16_t)(sample-maximum),false);
    if(sample<maximum) return cycles+11+2*dp;
    put(r,c->dp+28,sample);put(r,0xc09,x/2);put(r,0xc0b,y/2);
    c->a=y/2;c->c=(y&1)!=0;nz(c,c->a,false);c->cyclesUsed=5;
    return cycles+10+2*dp+26+3*dp;
}
static bool diffusion_reference(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_DIFFUSION_REFERENCE");reference=e && *e=='1';}
    return reference!=0;
}
static unsigned diffusion_cycles(const ScWorld *w,const Interp816 *c,const uint8_t *r,bool first) {
    unsigned x=word(r,c->dp),y=word(r,c->dp+2),width=ScWorldFieldWidth(w,13),height=ScWorldFieldHeight(w,13);
    if(x>=width || y>=height) return 0;
    unsigned i=y*width+x,dp=(c->dp&255)!=0,sum=0;
    const uint8_t *source=w->fields[first?13:14];
    unsigned cycles=(3+dp)+2+(4+dp)+(x?2:3);
    if(x) cycles+=stencil_add(&sum,source[i-1],dp,false);
    cycles+=3+(x+1<width?2:3);
    if(x+1<width) cycles+=stencil_add(&sum,source[i+1],dp,true);
    cycles+=(4+dp)+(y?2:3);
    if(y) cycles+=stencil_add(&sum,source[i-width],dp,true);
    cycles+=3+(y+1<height?2:3);
    if(y+1<height) cycles+=stencil_add(&sum,source[i+width],dp,true);
    cycles+=stencil_add(&sum,source[i],dp,true);
    return cycles+(3+dp)+3+(4+dp)+4+3+(sum/4<250?3:5)+3+5;
}
static unsigned diffusion_resume(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(diffusion_reference() || !c->mf) return 0;
    unsigned cost=diffusion_cycles(w,c,r,c->pc==0xa04a);
    if(!cost || cost>max_cycles) return 0;
    return ScWorldGuestFastStep(w,c,r,NULL,0);
}
/* The whole stencil already runs in C when it fits. A short beam budget used
 * to drop its remaining neighbour sums back into the interpreter. Continue
 * that same ordered calculation from every reachable boundary, retaining
 * byte carry/overflow and the scratch high byte across interrupts. */
static unsigned diffusion_stencil(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_DIFFUSION_STENCIL_REFERENCE");reference=e && *e=='1';}
    if(reference || diffusion_reference() || !c->mf || c->waiting || c->stopped ||
       c->dp<0x20 || c->dp>0x1fc0) return 0;
    unsigned base=c->pc>=0xa0d0?0xa0d0:0xa04a,stage=c->pc-base;
    unsigned width=ScWorldFieldWidth(w,13),height=ScWorldFieldHeight(w,13);
    unsigned x=word(r,c->dp),y=word(r,c->dp+2),index=y*width+x;
    if(x>=width || y>=height || c->x!=(uint16_t)index) return 0;
    const uint8_t *source=w->fields[base==0xa04a?13:14];unsigned cycles=0,dp=(c->dp&255)!=0;
    while(stage<65) {
        unsigned cost,next=stage,value=0;bool adc=false;
        switch(stage) {
        case 0:cost=3+dp;next=2;break;
        case 2:cost=2;next=4;break;
        case 4:case 27:cost=4+dp;next=stage==4?6:29;break;
        case 6:cost=c->z?3:2;next=c->z?13:8;break;
        case 13:cost=3;next=16;break;
        case 16:cost=c->z?3:2;next=c->z?27:18;break;
        case 29:cost=c->z?3:2;next=c->z?40:31;break;
        case 40:cost=3;next=43;break;
        case 43:cost=c->z?3:2;next=c->z?54:45;break;
        case 8:case 18:case 31:case 45:case 54:cost=2;next=stage+1;break;
        case 9:if(!x) return cycles;value=source[index-1];adc=true;cost=5;next=13;break;
        case 19:if(x+1>=width) return cycles;value=source[index+1];adc=true;cost=5;next=23;break;
        case 32:if(!y) return cycles;value=source[index-width];adc=true;cost=5;next=36;break;
        case 46:if(y+1>=height) return cycles;value=source[index+width];adc=true;cost=5;next=50;break;
        case 55:value=source[index];adc=true;cost=5;next=59;break;
        case 23:case 36:case 50:case 59:
            cost=c->c?2:3;next=c->c?stage+2:stage+4;break;
        case 25:case 38:case 52:case 61:cost=5+dp;next=stage+2;break;
        case 63:cost=3+dp;next=65;break;
        default:return cycles;
        }
        if(cost>max_cycles-cycles) break;
        if(adc) {
            unsigned old=c->a&255,sum=old+value+c->c;
            c->v=((~(old^value))&(old^sum)&128)!=0;c->c=sum>255;
            c->a=(c->a&0xff00)|(sum&255);nz(c,sum,true);
        } else switch(stage) {
        case 0:r[c->dp+5]=0;break;
        case 2:c->a&=0xff00;nz(c,0,true);break;
        case 4:case 27:c->y=(uint16_t)(stage==4?x:y);nz(c,c->y,false);break;
        case 13:case 40:value=(stage==13?width:height)-1;c->c=c->y>=value;nz(c,(uint16_t)(c->y-value),false);break;
        case 8:case 18:case 31:case 45:case 54:c->c=false;break;
        case 25:case 38:case 52:case 61:++r[c->dp+5];nz(c,r[c->dp+5],true);break;
        case 63:r[c->dp+4]=(uint8_t)c->a;break;
        }
        cycles+=cost;c->cyclesUsed=cost;stage=next;c->pc=base+stage;
    }
    return cycles;
}
/* A beam yield can leave the completed stencil in its original scratch word.
 * Finish its shifts, clamp and store in bounded C stages, without rerunning
 * neighbour reads or crossing the caller's deadline. */
static unsigned diffusion_finish(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(diffusion_reference()) return 0;
    unsigned base=c->pc>=0xa111?0xa111:0xa08b,stage=c->pc-base;
    unsigned x=word(r,c->dp),y=word(r,c->dp+2),width=ScWorldFieldWidth(w,13);
    if(x>=width || y>=ScWorldFieldHeight(w,13) || c->x!=(uint16_t)(y*width+x)) return 0;
    unsigned cycles=0,dp=(c->dp&255)!=0;
    while(true) {
        unsigned cost=stage==2?4+dp:stage==4 || stage==5?2:
            stage==9?(c->c?2:3):stage==16?5:3;
        if(cost>max_cycles-cycles) break;
        switch(stage) {
        case 0:c->mf=false;stage=2;break;
        case 2:if(c->mf) return cycles;c->a=word(r,c->dp+4);nz(c,c->a,false);stage=4;break;
        case 4:case 5:
            if(c->mf) return cycles;
            c->c=(c->a&1)!=0;c->a>>=1;nz(c,c->a,false);++stage;break;
        case 6:
            if(c->mf) return cycles;
            c->c=c->a>=250;nz(c,(uint16_t)(c->a-250),false);stage=9;break;
        case 9:stage=c->c?11:14;break;
        case 11:
            if(c->mf) return cycles;
            c->a=250;nz(c,c->a,false);stage=14;break;
        case 14:c->mf=true;stage=16;break;
        case 16:
            if(!c->mf) return cycles;
            w->fields[base==0xa08b?14:13][y*width+x]=(uint8_t)c->a;
            c->pc=base==0xa08b?0xa09f:0xa125;c->cyclesUsed=cost;
            return cycles+cost;
        default:return cycles;
        }
        cycles+=cost;c->cyclesUsed=cost;c->pc=base+stage;
    }
    return cycles;
}
static bool spatial_continuation_enabled(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_SPATIAL_CONTINUATION_REFERENCE");reference=e && *e=='1';}
    return !reference;
}
/* The five-instruction packed-coordinate setup used to enter KernelBus when
 * the full cell did not fit. Retain every yield boundary directly in C. The
 * common field lookup/return and the ordered stencil continue in C afterward. */
static unsigned diffusion_begin(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    if(!spatial_continuation_enabled() || diffusion_reference() || c->waiting || c->stopped ||
       c->dp<0x20 || c->dp>0x1fc0 || c->sp<0x100 || c->sp>0x1ffd) return 0;
    unsigned base=c->pc>=0xa0c6?0xa0c6:0xa040,stage=c->pc-base;
    if(stage!=0 && !c->mf) return 0;
    unsigned cycles=0,dp=(c->dp&255)!=0;
    for(;;) {
        unsigned cost;
        switch(stage) {
        case 0:case 4:cost=3;break;
        case 2:case 5:cost=3+dp;break;
        case 7:cost=6;break;
        default:return cycles;
        }
        if(cost>budget-cycles) return cycles;
        switch(stage) {
        case 0:
            if(w->huge) coord_set(w,r,word(r,c->dp)*2,word(r,c->dp+2)*2);
            c->mf=true;stage=2;break;
        case 2:c->a=(c->a&0xff00)|r[c->dp+2];nz(c,c->a,true);stage=4;break;
        case 4:c->a=(uint16_t)((c->a<<8)|(c->a>>8));nz(c,c->a,true);stage=5;break;
        case 5:c->a=(c->a&0xff00)|r[c->dp];nz(c,c->a,true);stage=7;break;
        case 7:
            put(r,c->sp-1,base+9);c->sp-=2;c->pc=0xa29a;
            c->cyclesUsed=6;return cycles+6;
        }
        c->pc=base+stage;c->cyclesUsed=(uint8_t)cost;cycles+=cost;
    }
}
static unsigned diffusion_cell(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(c->pc!=0xa040 && c->pc!=0xa0c6) return 0;
    unsigned x=word(r,c->dp),y=word(r,c->dp+2),width=ScWorldWidth(w)/2;
    if(x>=width || y>=ScWorldHeight(w)/2) return 0;
    unsigned cost=diffusion_cycles(w,c,r,c->pc==0xa040)+24+2*((c->dp&255)!=0);
    if(diffusion_reference()?max_cycles<250:cost>max_cycles) return 0;
    unsigned index=y*width+x,entry=c->pc;
    if(w->huge) coord_set(w,r,x*2,y*2);
    w->field_anchor[0]=index;put(r,0xb3f,x);put(r,0xb3d,y);
    put(r,(uint16_t)(c->sp-1),entry==0xa040?0xa049:0xa0cf);
    c->a=c->x=(uint16_t)index;c->mf=true;c->c=false;nz(c,index,false);
    c->pc=entry==0xa040?0xa04a:0xa0d0;
    return 24+2*((c->dp&255)!=0)+ScWorldGuestFastStep(w,c,r,NULL,0);
}
static unsigned crime_gpu_cell(ScWorld *w,Interp816 *c,uint8_t *r,
                                unsigned max_cycles,const ScCrimeSample *packed) {
    if(!packed || c->pc!=0x9eb0 || c->sp<0x1004 || c->sp>0x1fff ||
       c->dp<0x20 || c->dp>0x1fc0 || (c->sp>=c->dp && c->sp-3<=c->dp+34)) return 0;
    unsigned x=word(r,c->dp+8),y=word(r,c->dp+10),width=ScWorldWidth(w)/2;
    if(x>=width || y>=ScWorldHeight(w)/2) return 0;
    unsigned index=y*width+x,land=w->fields[0][index];
    if(!land) return vacant_crime_cell(w,c,r,max_cycles);
    unsigned coarse=(y/4)*(width/4)+x/4,coverage=word(w->fields[11],2*coarse);
    ScCrimeSample sample;memcpy(&sample,packed+index,sizeof sample);
    /* The pass is asynchronous. Validate the actual per-cell inputs even if
     * an editor or another stage changes a source before publication. */
    if(sample.source!=(land|((unsigned)w->fields[3][index]<<8)|(coverage<<16))) return 0;
    unsigned dp=(c->dp&255)!=0,value=sample.value_scratch&255,old=word(r,c->dp+4),total=old+value;
    unsigned cycles=(sample.clocks&65535)+dp*(sample.clocks>>16)+(total>65535?2+7+dp+3:3);
    if(cycles>max_cycles) return 0;
    if(w->huge) coord_set(w,r,x*2,y*2);
    w->field_anchor[0]=index;w->field_anchor[2]=coarse;
    put(r,0xb3f,x/4);put(r,0xb3d,y/4);
    put(r,c->dp,word(r,c->dp)+1);put(r,c->dp+12,sample.value_scratch>>8);
    put(r,c->sp-1,index);put(r,c->sp-3,0x9f0c);
    w->fields[1][index]=(uint8_t)value;put(r,c->dp+4,total);
    c->a=(uint16_t)total;c->x=(uint16_t)index;
    c->v=((old^total)&(value^total)&32768)!=0;c->c=total>65535;
    c->mf=false;nz(c,total,false);c->pc=0x9f47;c->cyclesUsed=3;
    if(c->c) {unsigned high=word(r,c->dp+6)+1;put(r,c->dp+6,high);nz(c,high,false);}
    ++crime_gpu_cells;return cycles;
}
static unsigned crime_cell(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(c->pc!=0x9eb0 || max_cycles<400) return 0;
    unsigned x=word(r,c->dp+8),y=word(r,c->dp+10),width=ScWorldWidth(w)/2;
    if(x>=width || y>=ScWorldHeight(w)/2) return 0;
    unsigned index=y*width+x,land=w->fields[0][index];
    if(!land) return vacant_crime_cell(w,c,r,max_cycles);
    unsigned dp=(c->dp&255)!=0,cycles=31+2*dp;
    if(w->huge) coord_set(w,r,x*2,y*2);
    w->field_anchor[0]=index;put(r,0xb3f,x);put(r,0xb3d,y);
    put(r,c->dp+12,land);put(r,c->dp,word(r,c->dp)+1);
    cycles+=3+3+(4+dp)+(7+dp)+3+2+(4+dp)+(land<=128?3:5);
    unsigned density=w->fields[3][index],value=(land<128?128-land:0)+density;
    put(r,c->dp+12,value);
    cycles+=3+2+5+(3+dp)+(value>255?7+dp:3);
    unsigned old=value,bias=word(r,0xc71);value=(value+bias)&65535;
    put(r,c->dp+12,value);
    cycles+=3+(4+dp)+2+5+(4+dp)+(4+dp);
    if(value&32768) {cycles+=3+3+(4+dp);value=0;put(r,c->dp+12,value);}
    else {
        cycles+=2+3;
        if(value>=300) {cycles+=2+3+3+(4+dp);value=300;put(r,c->dp+12,value);}
        else cycles+=3;
    }
    unsigned coarse=(y/4)*(ScWorldWidth(w)/8)+x/4;
    w->field_anchor[2]=coarse;put(r,0xb3f,x/4);put(r,0xb3d,y/4);
    put(r,(uint16_t)(c->sp-1),index);put(r,(uint16_t)(c->sp-3),0x9f0c);
    cycles+=36+2*dp;
    unsigned coverage=word(w->fields[11],2*coarse);
    put(r,c->dp+12,value-coverage);
    cycles+=3+2+2+(4+dp)+2+6+(4+dp);
    if(value<coverage) {cycles+=2+3+3;value=0;}
    else {
        value-=coverage;cycles+=3+(4+dp)+3;
        if(value>=250) {cycles+=2+3;value=250;} else cycles+=3;
    }
    w->fields[1][index]=(uint8_t)value;
    old=word(r,c->dp+4);unsigned total=value+old;
    put(r,c->dp+4,total);c->a=(uint16_t)total;c->x=(uint16_t)index;
    c->v=((old^total)&(value^total)&32768)!=0;c->c=total>65535;
    c->mf=false;nz(c,total,false);c->pc=0x9f47;c->cyclesUsed=3;
    cycles+=3+5+5+3+2+(4+dp)+(4+dp);
    if(c->c) {unsigned high=word(r,c->dp+6)+1;put(r,c->dp+6,high);nz(c,high,false);cycles+=2+(7+dp)+3;}
    else cycles+=3;
    return cycles;
}
/* Finish one land-value cell after its four-tile statistics are ready.
 * Shared by the complete C cell and resumable mid-cell entry. */
static unsigned land_value_finish(ScWorld *w,Interp816 *c,uint8_t *r,unsigned cycles) {
    unsigned x=word(r,c->dp+8),y=word(r,c->dp+10),width=ScWorldWidth(w);
    unsigned dp=(c->dp&255)!=0,coarse=(y/2)*(width/4)+x/2,index=y*(width/2)+x;
    unsigned old,sum;
    unsigned pollution=word(r,c->dp+14);
    cycles+=(4+dp)+3+(pollution<250?3:5)+(4+dp)+3;
    if(pollution>250) pollution=250;put(r,c->dp+14,pollution);
    w->field_anchor[0]=index;put(r,0xb3f,x);put(r,0xb3d,y);
    put(r,(uint16_t)(c->sp-1),index);c->x=(uint16_t)index;
    w->fields[13][index]=(uint8_t)pollution;
    cycles+=(3+dp)+3+(3+dp)+6+6+(3+dp)+5+4+(3+dp);
    c->mf=true;c->pc=0x9dc9;
    if(!r[c->dp+16]) {
        w->fields[0][index]=0;c->a=(uint16_t)index&0xff00;
        c->c=c->n=false;c->z=true;c->cyclesUsed=5;
        return cycles+3+5+2+5;
    }
    cycles+=2+(3+dp)+3+(3+dp)+6+6;
    int cx=w->huge?(w->center_valid?w->center_x/2:width/4):r[0xbab];
    int cy=w->huge?(w->center_valid?w->center_y/2:ScWorldHeight(w)/4):r[0xbac];
    unsigned dx=x>cx?x-cx:cx-x,dy=y>cy?y-cy:cy-y;
    unsigned distance=(dx+dy)/(width/120);if(distance>32) distance=32;
    put(r,(uint16_t)(c->sp-3),0x9d58);r[c->dp+30]=(uint8_t)distance;
    unsigned base=(34-distance)*4;r[c->dp+30]=(uint8_t)base;r[c->dp+31]=0;
    cycles+=(3+dp)+2+2+(3+dp)+2+2+(3+dp)+(3+dp);
    w->field_anchor[1]=coarse;put(r,0xb3f,x/2);put(r,0xb3d,y/2);
    put(r,(uint16_t)(c->sp-3),0x9d6f);
    cycles+=(3+dp)+2+3+(3+dp)+2+6+6+(3+dp)+2+5;
    old=base;unsigned terrain=w->fields[6][coarse];sum=old+terrain;
    c->v=((old^sum)&(terrain^sum)&128)!=0;
    if(sum>255) {++r[c->dp+31];cycles+=2+5+dp;} else cycles+=3;
    unsigned p=w->fields[2][index],low=sum&255,difference=(low-p)&255;
    c->v=((low^p)&(low^difference)&128)!=0;r[c->dp+30]=(uint8_t)difference;
    cycles+=5+2+5+(3+dp);
    if(low<p) {--r[c->dp+31];cycles+=2+5+dp;} else cycles+=3;
    unsigned crime=w->fields[1][index];cycles+=5+2;
    if(crime<190) cycles+=3;
    else {
        cycles+=2+(3+dp)+2+2+(3+dp);
        unsigned result=(difference-20)&255;
        c->v=((difference^20)&(difference^result)&128)!=0;
        r[c->dp+30]=(uint8_t)result;
        if(difference<20) {--r[c->dp+31];cycles+=2+5+dp;} else cycles+=3;
    }
    unsigned value=word(r,c->dp+30);cycles+=3+(4+dp);
    if(value&32768) {value=1;cycles+=2+3+3;}
    else {cycles+=3+3+(value<250?3:5);if(value>=250) value=250;}
    w->fields[0][index]=(uint8_t)value;
    old=word(r,c->dp+4);sum=old+value;put(r,c->dp+4,sum);
    c->v=((old^sum)&(value^sum)&32768)!=0;c->c=sum>65535;
    if(c->c) put(r,c->dp+6,word(r,c->dp+6)+1);
    cycles+=3+5+3+2+(4+dp)+(4+dp)+(c->c?2+7+dp:3);
    unsigned count=(word(r,c->dp+24)+1)&65535;put(r,c->dp+24,count);
    c->a=(uint16_t)sum;c->mf=false;nz(c,count,false);c->cyclesUsed=3;
    return cycles+7+dp+3;
}
static unsigned land_value_gpu_begin(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    if(c->pc!=0x9cdf || !stencil_backend.land_data || c->dp<0x1000 || c->dp>0x1fc0 ||
       c->sp<0x1006 || c->sp>0x1fff || (c->sp>=c->dp && c->sp-5<=c->dp+34)) return 0;
    unsigned width=ScWorldWidth(w),x=word(r,c->dp+8),y=word(r,c->dp+10);
    if(x>=width/2 || y>=ScWorldHeight(w)/2) return 0;
    const ScLandSummary *data=stencil_backend.land_data(stencil_backend.context,w);if(!data) return 0;
    ScLandSummary p;memcpy(&p,data+y*(width/2)+x,sizeof p);
    unsigned offset=2*y*width+2*x;
    unsigned top=word(w->tiles,2*offset)|(word(w->tiles,2*(offset+1))<<16);
    unsigned bottom=word(w->tiles,2*(offset+width))|(word(w->tiles,2*(offset+width+1))<<16);
    if(p.tiles[0]!=top || p.tiles[1]!=bottom) return 0;
    unsigned dp=(c->dp&255)!=0,density=p.stats&65535;
    unsigned cycles=3+3*(5+dp)+3+(3+dp)+2+3+(3+dp)+2+6+6;
    cycles+=63+(p.clocks&65535)+dp*(p.clocks>>16);
    cycles+=(4+dp)+3+(density<255?3:5)+(4+dp)+3;
    cycles+=(3+dp)+2+3+(3+dp)+2+6+6+5+2+(3+dp)+5+3;
    if(cycles>budget) return 0;
    if(w->huge) coord_set(w,r,x*2,y*2);
    if(density>255) density=255;
    put(r,c->dp+34,density);put(r,c->dp+14,p.stats>>16);put(r,c->dp+16,(p.tail>>26)&7);
    if(p.tail&(1u<<29)) {put(r,c->sp-3,(p.tail>>16)&1023);put(r,c->sp-5,0x9dfd);c->y=(uint16_t)p.tail;}
    unsigned coarse=(y/2)*(width/4)+x/2,old=w->fields[15][coarse],sum=old+density;
    w->map_anchor=2*offset;w->field_anchor[1]=coarse;
    put(r,0xb3f,x/2);put(r,0xb3d,y/2);put(r,c->sp-1,0x9d22);
    w->fields[15][coarse]=(uint8_t)sum;c->v=((old^sum)&(density^sum)&128)!=0;
    c->a=(coarse&0xff00)|(sum&255);c->x=(uint16_t)coarse;c->c=sum>255;nz(c,sum,true);c->mf=false;
    c->pc=0x9d30;c->cyclesUsed=3;++land_gpu_cells;return cycles;
}
static unsigned land_value_finish_cycles(const ScWorld *w,const Interp816 *c,const uint8_t *r);
static unsigned land_value_cell(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    unsigned gpu=land_value_gpu_begin(w,c,r,max_cycles);
    if(gpu) {
        /* Keep the complete-cell caller fused when the original finish also
         * fits. A short beam budget still resumes at the exact mid-cell PC. */
        if(land_value_finish_cycles(w,c,r)<=max_cycles-gpu)
            return land_value_finish(w,c,r,gpu);
        return gpu;
    }
    if(c->pc!=0x9cdf || max_cycles<1500) return 0;
    unsigned x=word(r,c->dp+8),y=word(r,c->dp+10),width=ScWorldWidth(w);
    if(x>=width/2 || y>=ScWorldHeight(w)/2) return 0;
    unsigned dp=(c->dp&255)!=0,offset=2*y*width+2*x;
    const unsigned cells[]={offset,offset+1,offset+width,offset+width+1};
    if(w->huge) coord_set(w,r,x*2,y*2);
    w->map_anchor=2*offset;put(r,0xb3f,x*4);put(r,0xb3d,y*64);
    put(r,c->dp+34,0);put(r,c->dp+14,0);put(r,c->dp+16,0);
    c->x=(uint16_t)(2*offset);c->c=2*offset>65535;
    unsigned cycles=3+3*(5+dp)+3+(3+dp)+2+3+(3+dp)+2+6+6;
    for(unsigned n=0;n<4;++n) {
        c->a=word(w->tiles,2*cells[n]);
        put(r,(uint16_t)(c->sp-1),n==0?0x9cf5:n==1?0x9cfc:n==2?0x9d03:0x9d0a);
        c->sp-=2;c->pc=0x9dca;
        cycles+=6+ScWorldGuestFastStep(w,c,r,NULL,0)+6+(n?5:0);
        c->sp+=2;
    }
    unsigned density=word(r,c->dp+34);
    cycles+=(4+dp)+3+(density<255?3:5)+(4+dp)+3;
    if(density>255) density=255;put(r,c->dp+34,density);
    unsigned coarse=(y/2)*(width/4)+x/2,index=y*(width/2)+x;
    w->field_anchor[1]=coarse;put(r,0xb3f,x/2);put(r,0xb3d,y/2);
    put(r,(uint16_t)(c->sp-1),0x9d22);
    unsigned old=w->fields[15][coarse],sum=old+density;
    w->fields[15][coarse]=(uint8_t)sum;
    c->v=((old^sum)&(density^sum)&128)!=0;
    cycles+=(3+dp)+2+3+(3+dp)+2+6+6+5+2+(3+dp)+5+3;
    return land_value_finish(w,c,r,cycles);
}
static unsigned land_value_finish_cycles(const ScWorld *w,const Interp816 *c,const uint8_t *r) {
    unsigned dp=(c->dp&255)!=0,pollution=word(r,c->dp+14);
    unsigned cycles=(4+dp)+3+(pollution<250?3:5)+(4+dp)+3;
    cycles+=(3+dp)+3+(3+dp)+6+6+(3+dp)+5+4+(3+dp);
    if(!r[c->dp+16]) return cycles+3+5+2+5;
    cycles+=2+(3+dp)+3+(3+dp)+6+6;
    cycles+=(3+dp)+2+2+(3+dp)+2+2+(3+dp)+(3+dp);
    cycles+=(3+dp)+2+3+(3+dp)+2+6+6+(3+dp)+2+5;
    unsigned width=ScWorldWidth(w),x=word(r,c->dp+8),y=word(r,c->dp+10);
    int cx=w->huge?(w->center_valid?w->center_x/2:width/4):r[0xbab];
    int cy=w->huge?(w->center_valid?w->center_y/2:ScWorldHeight(w)/4):r[0xbac];
    unsigned distance=((x>cx?x-cx:cx-x)+(y>cy?y-cy:cy-y))/(width/120);
    if(distance>32) distance=32;
    unsigned coarse=(y/2)*(width/4)+x/2,index=y*(width/2)+x;
    unsigned sum=(34-distance)*4+w->fields[6][coarse],low=sum&255,p=w->fields[2][index];
    cycles+=sum>255?2+5+dp:3;
    cycles+=5+2+5+(3+dp)+(low<p?2+5+dp:3)+5+2;
    if(w->fields[1][index]>=190) {
        cycles+=2+(3+dp)+2+2+(3+dp)+(((low-p)&255)<20?2+5+dp:3);sum-=20;
    } else cycles+=3;
    unsigned value=(sum-p)&65535;
    cycles+=3+(4+dp);
    if(value&32768) {value=1;cycles+=2+3+3;}
    else {cycles+=3+3+(value<250?3:5);if(value>=250) value=250;}
    cycles+=3+5+3+2+(4+dp)+(4+dp)+(word(r,c->dp+4)+value>65535?2+7+dp:3);
    return cycles+7+dp+3;
}
static bool land_pipeline_reference(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_LAND_PIPELINE_REFERENCE");reference=e && *e=='1';}
    return reference!=0;
}
static unsigned land_value_resume(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_LAND_FINISH_REFERENCE");reference=e && *e=='1';}
    if(reference || c->pc!=0x9d30 || c->mf || c->waiting || c->stopped ||
       c->dp<0x1000 || c->dp>0x1fc0 || c->sp<0x1004 || c->sp>0x1fff ||
       (c->sp>=c->dp && c->sp-3<=c->dp+34)) return 0;
    if(word(r,c->dp+8)>=ScWorldWidth(w)/2 || word(r,c->dp+10)>=ScWorldHeight(w)/2) return 0;
    if(land_pipeline_reference()?max_cycles<400:land_value_finish_cycles(w,c,r)>max_cycles) return 0;
    return land_value_finish(w,c,r,0);
}
/* Price the tile-statistics helper before committing a bounded land cell.
 * This lets a complete cell run in C when its actual branches fit the beam
 * budget, rather than rejecting every budget below the worst-case bound. */
unsigned ScWorldGuestLandStageStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(!w || !w->active || !c || !r || c->k!=3 || c->db!=3 || c->e || c->d || c->xf ||
       c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x20 || c->dp>0x1fc0 || c->sp<0x100 || c->sp>0x1ffd) return 0;
    if(c->pc==0x9dca) {
        unsigned cost=ScLandTileCycles(c->a&1023,(c->dp&255)!=0);
        if(cost>max_cycles) return 0;
        return ScWorldGuestFastStep(w,c,r,NULL,0);
    }
    if(c->pc!=0x9e0c) return 0;
    unsigned cost,tile=c->a,score=ScLandTilePollution(tile,&cost);
    if(cost>max_cycles) return 0;
    /* REP/LDY/CMP branches only change widths, carry and N/Z. The final
     * TYA sets N/Z from the score; the last category comparison owns carry. */
    unsigned comparison=tile==0x7f?0x7f:tile==0x364?0x364:
        tile<0x60?(tile>=0x50?0x50:0x40):tile<0x1fd?0x1fd:
        tile<0x245?0x245:tile<0x267?0x267:tile<0x277?0x277:
        tile<0x287?0x287:0x2ba;
    c->mf=false;c->c=tile>=comparison;c->a=c->y=(uint16_t)score;nz(c,score,false);
    c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;
    return cost;
}
static unsigned land_value_begin(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_LAND_BEGIN_REFERENCE");reference=e && *e=='1';}
    if(reference || c->pc!=0x9cdf || c->dp<0x1000 || c->dp>0x1fc0 ||
       c->sp<0x1004 || c->sp>0x1fff || (c->sp>=c->dp && c->sp-3<=c->dp+34)) return 0;
    unsigned gpu=land_value_gpu_begin(w,c,r,max_cycles);if(gpu) return gpu;
    unsigned width=ScWorldWidth(w),x=word(r,c->dp+8),y=word(r,c->dp+10),dp=(c->dp&255)!=0;
    if(x>=width/2 || y>=ScWorldHeight(w)/2) return 0;
    unsigned offset=2*y*width+2*x,cycles=3+3*(5+dp)+3+(3+dp)+2+3+(3+dp)+2+6+6;
    const unsigned cells[]={offset,offset+1,offset+width,offset+width+1};
    unsigned density=0;
    for(unsigned n=0;n<4;++n) {
        unsigned tile=word(w->tiles,2*cells[n])&1023;
        density=tile && tile<0x28?density+15:
            tile>=0x2bf && tile<0x354 && tile!=0x307 && tile!=0x310?255:density;
        cycles+=12+ScLandTileCycles(tile,dp)+(n?5:0);
    }
    cycles+=(4+dp)+3+(density<255?3:5)+(4+dp)+3;
    cycles+=(3+dp)+2+3+(3+dp)+2+6+6+5+2+(3+dp)+5+3;
    if(cycles>max_cycles) return 0;
    if(w->huge) coord_set(w,r,x*2,y*2);
    w->map_anchor=2*offset;put(r,0xb3f,x*4);put(r,0xb3d,y*64);
    put(r,c->dp+34,0);put(r,c->dp+14,0);put(r,c->dp+16,0);
    c->x=(uint16_t)(2*offset);c->c=2*offset>65535;
    for(unsigned n=0;n<4;++n) {
        c->a=word(w->tiles,2*cells[n]);
        put(r,c->sp-1,n==0?0x9cf5:n==1?0x9cfc:n==2?0x9d03:0x9d0a);
        c->sp-=2;c->pc=0x9dca;ScWorldGuestFastStep(w,c,r,NULL,0);c->sp+=2;
    }
    if(density>255) density=255;put(r,c->dp+34,density);
    unsigned coarse=(y/2)*(width/4)+x/2,old=w->fields[15][coarse],sum=old+density;
    w->field_anchor[1]=coarse;put(r,0xb3f,x/2);put(r,0xb3d,y/2);put(r,c->sp-1,0x9d22);
    w->fields[15][coarse]=(uint8_t)sum;c->v=((old^sum)&(density^sum)&128)!=0;
    c->a=(uint16_t)coarse&0xff00;c->x=(uint16_t)coarse;
    c->a|=(sum&255);c->c=sum>255;nz(c,sum,true);c->mf=false;
    c->pc=0x9d30;c->cyclesUsed=3;
    return cycles;
}
static unsigned density_cell(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_DENSITY_CELL_REFERENCE");reference=e && *e=='1';}
    bool advance=c->pc==0xa01d && w->colossal;
    if((c->pc!=0x9fb7 && !advance) || (reference && max_cycles<350)) return 0;
    static int batch_reference=-1;
    if(batch_reference<0) {const char *e=getenv("SC_DENSITY_BATCH_REFERENCE");batch_reference=e && *e=='1';}
    if(advance && batch_reference) return 0;
    unsigned x=word(r,c->dp),y=word(r,c->dp+2),width=ScWorldWidth(w)/4,height=ScWorldHeight(w)/4;
    if(x>=width || y>=height) return 0;
    if(advance) {if(++x==width) {x=0;++y;}if(y>=height) return 0;}
    unsigned index=y*width+x,dp=(c->dp&255)!=0;
    const uint8_t *source=w->fields[15];unsigned sum=0;
    unsigned cycles=3+(3+dp)+3+(3+dp)+6+6+(3+dp)+2+(4+dp);
    cycles+=x?2+stencil_add(&sum,source[index-1],dp,true):3;
    cycles+=3;
    cycles+=x+1<width?2+stencil_add(&sum,source[index+1],dp,true):3;
    cycles+=4+dp;
    cycles+=y?2+stencil_add(&sum,source[index-width],dp,true):3;
    cycles+=3;
    cycles+=y+1<height?2+stencil_add(&sum,source[index+width],dp,true):3;
    unsigned average=sum/4,center=source[index],total=average+center;
    cycles+=(3+dp)+3+2*(7+dp)+3+(3+dp)+2+5;
    cycles+=total>255?2+5+dp:3;
    cycles+=(3+dp)+3+(4+dp)+2+3+5;
    if(cycles>max_cycles) return 0;
    /* The Colossal word-coordinate loop hook costs no guest clocks. Fuse it
     * only after the next full cell fits, so a rejected or final-cell span
     * never publishes an uncharged trailing hook across an interrupt. */
    if(advance) ScWorldGuestStep(w,c,r);
    if(w->huge) coord_set(w,r,x*4,y*4);
    w->field_anchor[1]=index;put(r,0xb3f,x);put(r,0xb3d,y);
    put(r,(uint16_t)(c->sp-1),0x9fc0);c->x=(uint16_t)index;
    put(r,c->dp+4,total);w->fields[6][index]=(uint8_t)(total/2);
    c->a=(uint16_t)(total/2);c->y=(uint16_t)y;c->mf=true;c->c=total&1;
    c->v=((average^total)&(center^total)&128)!=0;nz(c,c->a,false);
    c->pc=0xa01d;c->cyclesUsed=5;
    return cycles;
}
static bool power_exact_budget(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_POWER_BUDGET_REFERENCE");reference=e && *e=='1';}
    return !reference;
}
/* Price the complete branch before touching coordinates, stack shadows or
 * the electrical bitmap. Short neighbours should not require a worst-case
 * reservation that forces them through per-instruction continuations. */
typedef struct PowerCandidate {
    int x,y,nx,ny;
    unsigned cycles,previous,index,bit,tile,offset;
    bool valid,connected;
} PowerCandidate;
static bool power_candidate(const ScWorld *w,const Interp816 *c,const uint8_t *r,
    const uint8_t *rom,unsigned direction,PowerCandidate *p) {
    *p=(PowerCandidate){0};if(direction>3) return false;
    unsigned stack=c->pc==0xb03d?(unsigned)c->sp-2:c->sp;
    if(c->dp<0x1000 || c->dp>0x1fc0 || stack<0x1008 || stack>0x1ffd ||
       (stack+2>=c->dp && stack-8<=c->dp+24)) return false;
    unsigned dp=(c->dp&255)!=0,cycles=21+dp;
    int x=lift(r[0xb85],w->coord[2][0],256),y=lift(r[0xb86],w->coord[2][1],256);
    if(!ScWorldContains(w,x,y)) return false;
    int nx=x+(direction==1)-(direction==3),ny=y+(direction==2)-(direction==0);
    p->x=x;p->y=y;p->nx=nx;p->ny=ny;p->valid=ScWorldContains(w,nx,ny);
    if(!p->valid) cycles+=3;
    else {
        unsigned bit=1,previous=word(r,0xb89);cycles+=2+6+3+5+3;p->previous=previous;
        if(previous==0x27c) cycles+=3+6;
        else {
            cycles+=2+3;
            if(previous==0x28c) cycles+=3+6;
            else {
                unsigned index=(unsigned)ny*(ScWorldWidth(w)/8)+(unsigned)nx/8;
                bit=w->fields[5][index]&(128>>(nx&7));p->index=index;
                cycles+=2+6+6+(w->giant?0:3)+2+3+5+4+3+3+2+6;
            }
        }
        p->bit=bit;
        cycles+=3+(bit?3:2);
        if(!bit) {
            unsigned tile=ScWorldCell(w,nx,ny)&1023;
            p->tile=tile;p->offset=2*((unsigned)ny*ScWorldWidth(w)+(unsigned)nx);
            p->connected=(rom[0x184eb+tile]&128)!=0;
            cycles+=5+6+6+3+2+3+4+3+(tile>=0x15)+(p->connected?2:3);
        }
    }
    p->cycles=cycles+4+dp+5+3;return true;
}
/* Apply the already-priced candidate. Search and standalone neighbours share
 * this publication: coordinates, bitmap and tile properties are read once,
 * before any guest-visible write, rather than repeating the same traversal. */
static void power_candidate_publish(ScWorld *w,Interp816 *c,uint8_t *r,const PowerCandidate *p) {
    unsigned original=word(r,0xb85);
    put(r,c->dp+22,original);put(r,(uint16_t)(c->sp-1),0xb073);
    coord_set(w,r,p->valid?p->nx:p->x,p->valid?p->ny:p->y);
    c->x=(uint8_t)(p->valid?p->nx:p->x);c->y=(uint8_t)(p->valid?p->ny:p->y);
    c->mf=c->xf=false;
    if(p->valid) {
        put(r,(uint16_t)(c->sp-1),0xb078);
        if(p->previous==0x27c || p->previous==0x28c) c->y=1;
        else {
            put(r,(uint16_t)(c->sp-3),0xb10a);
            put(r,0xc5b,(unsigned)p->ny*(ScWorldWidth(w)/8));c->x=(uint16_t)p->index;
            c->y=(uint16_t)p->bit;
        }
        /* CPY #0 establishes carry even when the bit is clear. */
        c->c=true;
        if(!c->y) {
            put(r,(uint16_t)(c->sp-1),0xb083);
            w->map_anchor=p->offset;put(r,0xb3f,p->nx*2);put(r,0xb3d,p->ny*32);
            c->x=(uint16_t)p->tile;c->c=p->offset>65535;
        }
    }
    /* Byte proxies restore against the last full-coordinate candidate. */
    put(r,0xb85,original);c->a=p->connected?1:0;nz(c,c->a,false);
    c->pc=p->connected?0xb099:0xb0a2;c->cyclesUsed=3;
}
static unsigned power_neighbor(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    if(c->pc!=0xb06c || !w->huge || c->mf || (!power_exact_budget() && max_cycles<200)) return 0;
    PowerCandidate p;
    if(!power_candidate(w,c,r,rom,c->a&255,&p) || p.cycles>max_cycles) return 0;
    power_candidate_publish(w,c,r,&p);return p.cycles;
}
/* Police/fire coverage uses 16-bit samples. Preserve the ROM's wrapped
 * neighbour sum and the rounding carry from its second LSR into ADC. This
 * differs from an ordinary floating-point five-point average. */
static unsigned service_cell(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if((c->pc!=0xa164 && c->pc!=0xa1e3) || !c->mf || max_cycles<180) return 0;
    unsigned x=word(r,c->dp),y=word(r,c->dp+2),width=ScWorldWidth(w)/8,height=ScWorldHeight(w)/8;
    if(x>=width || y>=height) return 0;
    unsigned entry=c->pc,index=y*width+x,dp=(c->dp&255)!=0;
    const uint8_t *source=w->fields[entry==0xa164?10:11];
    if(w->huge) coord_set(w,r,x*8,y*8);
    w->field_anchor[2]=index;put(r,0xb3f,x);put(r,0xb3d,y);
    put(r,(uint16_t)(c->sp-1),entry==0xa164?0xa16b:0xa1ea);
    unsigned sum=0,cycles=3+dp+3+3+dp+6+6+3+2+2+3+4+dp;
    cycles+=x?2+2+6:3;if(x) sum=word(source,2*(index-1));
    cycles+=3+(x+1<width?2+2+6:3);
    if(x+1<width) sum=(sum+word(source,2*(index+1)))&65535;
    cycles+=4+dp+(y?2+2+6:3);
    if(y) sum=(sum+word(source,2*(index-width)))&65535;
    cycles+=3+(y+1<height?2+2+6:3);
    if(y+1<height) sum=(sum+word(source,2*(index+width)))&65535;
    unsigned average=sum/4,center=word(source,2*index),total=average+center+((sum>>1)&1);
    unsigned wrapped=total&65535,value=wrapped/2;
    put(w->fields[16],2*index,value);c->a=(uint16_t)value;c->x=(uint16_t)(2*index);c->y=y;
    c->c=wrapped&1;c->v=((average^total)&(center^total)&32768)!=0;
    c->mf=true;nz(c,value,false);c->pc=entry==0xa164?0xa1a6:0xa225;c->cyclesUsed=3;
    return cycles+2+2+6+2+6+3;
}
/* An interrupt cannot observe scratch registers inside a bounded field span.
 * Defer their publication only for the normal disjoint stack/direct-page
 * layout. Unusual aliasing layouts retain ordered per-cell shadow writes. */
static bool field_shadow_deferred(const Interp816 *c,unsigned last_scratch) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_FIELD_SHADOW_REFERENCE");reference=e && *e=='1';}
    return !reference && c->dp>=0x1000 && c->dp<=0x1fc0 && c->sp>=0x1008 &&
        c->sp<=0x1ffd && !(c->sp>=c->dp && c->sp-2<=c->dp+last_scratch);
}
/* Whole independent word field, fused with exact byte-coordinate control.
 * GPU data is ready asynchronously; the same packed integer C calculation
 * continues until then. Publication never crosses the caller's deadline. */
static unsigned service_field_span(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_SERVICE_REFERENCE");reference=e && *e=='1';}
    if(reference || !c->mf || c->waiting || c->stopped || c->dp<0x20 || c->dp>0x1fc0 ||
       c->sp<0x100 || c->sp>0x1ffd || (c->sp>=c->dp && c->sp-2<=c->dp+3)) return 0;
    unsigned entry=c->pc;if(entry!=0xa164 && entry!=0xa1e3) return 0;
    unsigned source=entry==0xa164?10:11,width=ScWorldFieldWidth(w,source),height=ScWorldFieldHeight(w,source);
    unsigned x=word(r,c->dp),y=word(r,c->dp+2),dp=(c->dp&255)!=0,cycles=0;
    if(x>=width || y>=height) return 0;
    const uint32_t *packed=stencil_backend.data?stencil_backend.data(stencil_backend.context,w,source):NULL;
    bool deferred=field_shadow_deferred(c,3),row=false;
    unsigned last_x=x,last_y=y,last_p=0,cells=0;
    while(y<height) {
        unsigned index=y*width+x,p=packed?packed[index]:ScServicePack(w->fields[source],width,height,x,y);
        unsigned cell=((p>>16)&511)+4*dp,advance=13+2*dp;
        if(x+1==width) advance+=y+1<height?15+3*dp:11+2*dp;
        if(cell+advance>budget-cycles) break;
        unsigned value=p&65535;put(w->fields[16],2*index,value);
        if(deferred) {
            last_x=x;last_y=y;last_p=p;row=++x==width;
            if(row) {++y;if(y<height)x=0;}
        } else {
            if(w->huge) coord_set(w,r,x*8,y*8);
            w->field_anchor[2]=index;put(r,0xb3f,x);put(r,0xb3d,y);put(r,c->sp-1,entry==0xa164?0xa16b:0xa1ea);
            c->x=(uint16_t)(2*index);c->y=(uint16_t)y;
            c->v=(p>>31)!=0;c->mf=true;
            ++x;if(w->mega)put(r,c->dp,x);else r[c->dp]=(uint8_t)x;
            c->a=(uint16_t)((value&0xff00)|(x&255));
            c->c=x>=width;nz(c,x-width,true);c->pc=(uint16_t)entry;c->cyclesUsed=3;
            if(x==width) {
                ++y;if(w->mega)put(r,c->dp+2,y);else r[c->dp+2]=(uint8_t)y;
                c->a=(uint16_t)((value&0xff00)|(y&255));c->c=y>=height;nz(c,y-height,true);
                if(y<height) {x=0;if(w->mega)put(r,c->dp,0);else r[c->dp]=0;c->cyclesUsed=(uint8_t)(3+dp);}
                else {c->pc=entry==0xa164?0xa1b6:0xa235;c->cyclesUsed=2;}
            }
        }
        cycles+=cell+advance;++cells;
    }
    if(deferred && cells) {
        unsigned index=last_y*width+last_x,value=last_p&65535;
        if(w->huge)coord_set(w,r,last_x*8,last_y*8);
        w->field_anchor[2]=index;put(r,0xb3f,last_x);put(r,0xb3d,last_y);
        put(r,c->sp-1,entry==0xa164?0xa16b:0xa1ea);
        c->x=(uint16_t)(2*index);c->y=(uint16_t)last_y;c->v=(last_p>>31)!=0;c->mf=true;
        if(w->mega) {put(r,c->dp,x);put(r,c->dp+2,y);}
        else {r[c->dp]=(uint8_t)x;r[c->dp+2]=(uint8_t)y;}
        c->a=(uint16_t)((value&0xff00)|((row?y:x)&255));
        c->c=row?y>=height:x>=width;nz(c,row?y-height:x-width,true);
        c->pc=y==height?(entry==0xa164?0xa1b6:0xa235):(uint16_t)entry;
        c->cyclesUsed=y==height?2:row?(uint8_t)(3+dp):3;
    }
    service_cells+=cells;if(packed)service_gpu_cells+=cells;
    return cycles;
}
static unsigned terrain_quality_cell(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(c->pc!=0xa25c || !c->mf || max_cycles<140) return 0;
    /* The 480x400 coarse grid needs word counters on the mega map. Smaller
     * maps retain native bytes, whose adjacent scratch bytes are unspecified. */
    unsigned x=w->mega?word(r,c->dp):r[c->dp],
        y=w->mega?word(r,c->dp+2):r[c->dp+2],width=ScWorldWidth(w);
    if(x>=width/8 || y>=ScWorldHeight(w)/8) return 0;
    unsigned index=y*(width/8)+x,dp=(c->dp&255)!=0;
    if(w->huge) coord_set(w,r,x*8,y*8);
    int cx=w->huge?(w->center_valid?w->center_x/2:width/4):r[0xbab];
    int cy=w->huge?(w->center_valid?w->center_y/2:ScWorldHeight(w)/4):r[0xbac];
    unsigned dx=x*4>cx?x*4-cx:cx-x*4,dy=y*4>cy?y*4-cy:cy-y*4;
    unsigned distance=(dx+dy)/(width/120);if(distance>32) distance=32;
    put(r,c->dp+6,distance*4);put(r,0xb3f,x);put(r,0xb3d,y);
    put(r,(uint16_t)(c->sp-1),0xa273);w->field_anchor[2]=index;
    unsigned value=(64-distance*4)&65535;
    put(w->fields[12],2*index,value);c->a=(uint16_t)value;c->x=(uint16_t)(2*index);
    c->c=64>=distance*4;c->v=((64^(distance*4))&(64^value)&32768)!=0;
    c->mf=true;nz(c,value,false);c->pc=0xa288;c->cyclesUsed=3;
    return (3+dp)+4+3+(3+dp)+4+6+6+(3+dp)+(3+dp)+
        (3+dp)+3+(3+dp)+6+6+3+2+2+2*(7+dp)+3+2+(4+dp)+6+3;
}
/* Retire terrain quality and its byte-coordinate iterator in one C loop.
 * The centering constants are stable for the pass; keep ordered publication,
 * scratch/stack shadows and exact loop clocks at each beam deadline. */
static unsigned terrain_quality_span(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_TERRAIN_QUALITY_SPAN_REFERENCE");reference=e && *e=='1';}
    /* Keep scratch and stack disjoint from each other and the low-RAM globals
     * so intermediate shadows cannot feed a subsequent cell's inputs. */
    if(reference || c->pc!=0xa25c || !c->mf || c->dp<0x1000 || c->dp>0x1fc0 ||
       c->sp<0x1008 || c->sp>0x1ffd || (c->sp>=c->dp && c->sp-2<=c->dp+7)) return 0;
    unsigned dp=(c->dp&255)!=0;
    unsigned cell=(3+dp)+4+3+(3+dp)+4+6+6+(3+dp)+(3+dp)+
        (3+dp)+3+(3+dp)+6+6+3+2+2+2*(7+dp)+3+2+(4+dp)+6+3;
    /* Short spans retain the cell-only path, including interrupt-edge resumes. */
    if(budget<2*(cell+13+2*dp)) return 0;
    unsigned width=ScWorldWidth(w),height=ScWorldHeight(w)/8,columns=width/8;
    unsigned x=w->mega?word(r,c->dp):r[c->dp],
        y=w->mega?word(r,c->dp+2):r[c->dp+2],cycles=0;
    if(x>=columns || y>=height) return 0;
    int cx=w->huge?(w->center_valid?w->center_x/2:width/4):r[0xbab];
    int cy=w->huge?(w->center_valid?w->center_y/2:ScWorldHeight(w)/4):r[0xbac];
    unsigned scale=width/120,shift=0;
    if(!scale || (scale&(scale-1))) return 0;
    for(unsigned factor=scale;factor>1;factor>>=1)++shift;
    unsigned last_x=x,last_y=y,last_offset=0,last_value=0;
    bool row=false;
    while(y<height) {
        unsigned advance=13+2*dp;
        if(x+1==columns) advance+=y+1<height?15+3*dp:11+2*dp;
        if(cell+advance>budget-cycles) break;
        unsigned dx=x*4>cx?x*4-cx:cx-x*4,dy=y*4>cy?y*4-cy:cy-y*4;
        unsigned distance=(dx+dy)>>shift;if(distance>32) distance=32;
        last_x=x;last_y=y;last_offset=distance*4;last_value=(64-last_offset)&65535;
        put(w->fields[12],2*(y*columns+x),last_value);
        row=++x==columns;
        if(row) {++y;if(y<height)x=0;}
        cycles+=cell+advance;
    }
    if(!cycles) return 0;
    /* No interrupts, observers or dependent reads occur inside this bounded
     * span. Publish only the final CPU/RAM shadows, retaining every field write. */
    if(w->huge)coord_set(w,r,last_x*8,last_y*8);
    put(r,c->dp+6,last_offset);put(r,0xb3f,last_x);put(r,0xb3d,last_y);
    put(r,c->sp-1,0xa273);w->field_anchor[2]=last_y*columns+last_x;
    c->x=(uint16_t)(2*w->field_anchor[2]);
    c->v=((64^last_offset)&(64^last_value)&32768)!=0;
    if(w->mega) {put(r,c->dp,x);put(r,c->dp+2,y);}
    else {r[c->dp]=(uint8_t)x;r[c->dp+2]=(uint8_t)y;}
    c->a=(uint16_t)((last_value&0xff00)|((row?y:x)&255));
    c->c=row?y>=height:x>=columns;nz(c,(row?y-height:x-columns),true);
    c->pc=y==height?0xa298:0xa25c;
    c->cyclesUsed=y==height?2:row?(uint8_t)(3+dp):3;
    return cycles;
}
/* Copy the completed diffusion field in bounded C spans. Each word retains
 * its original loop cost, so HDMA and interrupts still see the same boundary. */
static unsigned service_copy(ScWorld *w,Interp816 *c,unsigned max_cycles) {
    if((c->pc!=0xa1bb && c->pc!=0xa23a) || c->mf || (c->x&1)) return 0;
    unsigned end=ScWorldFieldSizeWorld(w,16),x=w->colossal?w->field_scan:c->x,cycles=0;
    if(x>=end) return 0;
    unsigned field=c->pc==0xa1bb?10:11;
    unsigned step=w->colossal?19:22; /* Full-index CPX is a zero-clock hook. */
    while(x<end && cycles+step<=max_cycles) {
        c->a=word(w->fields[16],x);put(w->fields[field],x,c->a);x+=2;
        cycles+=step-(x==end);
    }
    if(!cycles) return 0;
    if(w->colossal) w->field_scan=x;
    c->x=(uint16_t)x;c->c=x>=end;nz(c,(uint16_t)(x-end),false);
    if(w->colossal) c->n=false; /* Full-index CPX hook's flags. */
    c->cyclesUsed=x==end?2:3;
    if(x==end) c->pc=field==10?0xa1ca:0xa249;
    return cycles;
}
/* Mark one visited electrical-network cell. The compatibility hooks already
 * provide full coordinates and a zero-clock bitmap-address calculation. Keep
 * their stack shadows, byte flags and timing while doing the bitmap work in C. */
static unsigned power_mark(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    bool caller=c->pc==0xb036;
    if(!caller && c->pc!=0xb0e5) return 0;
    unsigned dp=(c->dp&255)!=0,cost=caller?52+2*dp:32;
    int x=w->huge?lift(r[0xb85],w->coord[2][0],256):r[0xb85];
    int y=w->huge?lift(r[0xb86],w->coord[2][1],256):r[0xb86];
    if(cost>max_cycles || !ScWorldContains(w,x,y)) return 0;
    unsigned row=(unsigned)y*(ScWorldWidth(w)/8),index=row+(unsigned)x/8,bit=x&7;
    if(caller) put(r,c->sp-1,0xb038);
    put(r,c->sp-(caller?3:1),0xb0e7);
    if(w->huge) coord_set(w,r,x,y);
    put(r,0xc5b,row);c->x=(uint16_t)index;c->y=(uint16_t)bit;
    unsigned value=w->fields[5][index]|rom[0x1b0dd+bit];
    w->fields[5][index]=(uint8_t)value;c->a=(uint16_t)value;
    c->c=false;c->mf=false;nz(c,value,true);
    if(caller) {put(r,c->dp+2,0);put(r,c->dp+4,0);c->pc=0xb03d;c->cyclesUsed=4+dp;}
    else {c->pc=0xb0f7;c->cyclesUsed=3;}
    return cost;
}
static unsigned power_visit(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_POWER_SPAN_REFERENCE");reference=e && *e=='1';}
    if(reference || c->mf || c->waiting || c->stopped || c->dp<0x1000 || c->dp>0x1fc0 || c->sp<0x108 || c->sp>0x1fff) return 0;
    if(c->pc==0xb036 || c->pc==0xb0e5) return power_mark(w,c,r,rom,max_cycles);
    unsigned dp=(c->dp&255)!=0;
    if(c->pc==0xaffd || c->pc==0xb000 || c->pc==0xb005) {
        if(!w->huge) return 0;
        unsigned count=word(r,0xc57),floor=word(r,0xc59);
        unsigned value=c->pc==0xaffd?count:c->a;
        bool compare=c->pc!=0xb005,done=compare && value==floor;
        unsigned cost=(c->pc==0xaffd?5:0)+(compare?5+(done?3:2):0)+(done?0:19+dp);
        if(cost>max_cycles || count>=ScWorldFieldWidth(w,17)) return 0;
        if(compare) {c->a=(uint16_t)value;c->c=value>=floor;nz(c,value-floor,false);}
        if(done) {c->pc=0xb06a;c->cyclesUsed=3;return cost;}
        put(r,c->sp-1,0xb007);c->sp-=2;c->pc=0xb0a3;
        ScWorldGuestStep(w,c,r);c->sp+=2;
        c->a=4;nz(c,4,false);put(r,c->dp,4);c->pc=0xb00d;c->cyclesUsed=4+dp;
        return cost;
    }
    if(c->pc==0xb05a) {
        unsigned count=word(r,c->dp+2),cost=count>=2?28+2*dp:count?17+2*dp:19+2*dp;
        if(cost>max_cycles || (count>=2 && !w->huge)) return 0;
        c->a=(uint16_t)count;c->c=count>=2;nz(c,count-2,false);
        if(count>=2) {
            put(r,c->sp-1,0xb063);c->sp-=2;c->pc=0xb0be;
            ScWorldGuestStep(w,c,r);c->sp+=2;
        }
        c->a=(uint16_t)count;nz(c,count,false);c->pc=count?0xb00d:0xaffd;c->cyclesUsed=3;
        return cost;
    }
    if(c->pc!=0xb00d) return 0;
    int x=w->huge?lift(r[0xb85],w->coord[2][0],256):r[0xb85];
    int y=w->huge?lift(r[0xb86],w->coord[2][1],256):r[0xb86];
    if(!ScWorldContains(w,x,y)) return 0;
    unsigned low=(word(r,c->dp+18)+1)&65535,high=(word(r,c->dp+20)+!low)&65535;
    unsigned caplow=word(r,c->dp+14),caphi=word(r,c->dp+16),borrow=caplow<low;
    int upper=(int)caphi-(int)high-(int)borrow;
    unsigned cycles=7+dp+(low?3:9+dp)+16+4*dp+(upper>=0?3:2);
    unsigned cost=cycles+(upper<0?27:4+dp+6+(w->huge?6+52+2*dp:0));
    if(cost>max_cycles) return 0;
    put(r,c->dp+18,low);if(!low) put(r,c->dp+20,high);
    c->a=(uint16_t)upper;c->c=upper>=0;c->v=((caphi^high)&(caphi^(unsigned)upper)&32768)!=0;
    nz(c,c->a,false);
    if(upper<0) {
        put(r,0x381,0x1a);put(r,0x383,1);put(r,0x38b,300);
        c->a=300;nz(c,c->a,false);c->pc=0xb06a;c->cyclesUsed=3;return cost;
    }
    c->a=(uint16_t)word(r,c->dp);nz(c,c->a,false);
    put(r,c->sp-1,0xb035);c->sp-=2;c->pc=0x8ff4;c->cyclesUsed=6;
    if(!w->huge) return cost;
    ScWorldGuestStep(w,c,r);c->sp+=2;c->pc=0xb036;
    power_mark(w,c,r,rom,52+2*dp);
    return cost;
}
/* One ordered power-search iteration. Yield between neighbours instead of
 * dispatching each instruction, while retaining the guest traversal stack. */
static unsigned power_search(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    if(c->pc!=0xb03d || !w->huge || c->mf) return 0;
    unsigned count=word(r,c->dp+2),direction=word(r,c->dp+4),dp=(c->dp&255)!=0;
    unsigned cycles=4+dp+3;
    if(count>=2) {
        if(cycles+3>max_cycles) return 0;
        c->a=count;c->c=true;nz(c,(uint16_t)(count-2),false);
        c->pc=0xb05a;c->cyclesUsed=3;return cycles+3;
    }
    cycles+=2+4+dp+3;
    if(direction>=4) {
        if(cycles+3>max_cycles) return 0;
        c->a=direction;c->c=true;nz(c,(uint16_t)(direction-4),false);
        c->pc=0xb05a;c->cyclesUsed=3;return cycles+3;
    }
    PowerCandidate p;
    if(!power_candidate(w,c,r,rom,direction,&p)) return 0;
    unsigned required=cycles+2+6+p.cycles+6+(p.connected?2+7+dp+4+dp+4+dp:3)+7+dp+3;
    if(required>max_cycles || (!power_exact_budget() && max_cycles<350)) return 0;
    cycles+=2+6;c->a=direction;c->c=false;
    put(r,(uint16_t)(c->sp-1),0xb04d);c->sp-=2;c->pc=0xb06c;
    power_candidate_publish(w,c,r,&p);cycles+=p.cycles;c->sp+=2;cycles+=6;
    if(c->a) {
        ++count;put(r,c->dp+2,count);put(r,c->dp,direction);
        c->a=direction;cycles+=2+7+dp+4+dp+4+dp;
    } else cycles+=3;
    direction=(direction+1)&65535;put(r,c->dp+4,direction);nz(c,direction,false);
    c->pc=0xb03d;c->cyclesUsed=3;
    return cycles+7+dp+3;
}
/* Connected traversal calls these whole operations at their original entry
 * labels. Atomic continuations keep the one-instruction path. */
unsigned ScWorldGuestPowerStageStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_POWER_STAGE_REFERENCE");reference=e && *e=='1';}
    if(reference || c->mf || c->sp<0x108 || c->sp>0x1ffd || c->dp<0x1000 || c->dp>0x1fc0 ||
       (c->sp+2>=c->dp && c->sp-8<=c->dp+24)) return 0;
    if(c->pc==0xb03d) {
        unsigned cycles=0;
        do {
            unsigned cost=power_search(w,c,r,rom,budget-cycles);
            if(!cost) break;
            cycles+=cost;
        } while(c->pc==0xb03d);
        return cycles;
    }
    if(c->pc==0xb06c) return power_neighbor(w,c,r,rom,budget);
    return power_visit(w,c,r,rom,budget);
}
static unsigned spatial_advance(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    unsigned entry=c->pc,offset=0,divisor=2,next=0,end=0;
    bool byte=false,rep=true,crime=false;
    switch(entry) {
    case 0x9cb0:offset=8;next=0x9c77;end=0x9cc4;break;
    case 0x9f47:offset=8;next=0x9eb0;end=0x9f61;crime=true;break;
    case 0xa09f:next=0xa040;end=0xa0b3;break;
    case 0xa125:next=0xa0c6;end=0xa139;break;
    case 0xa01d:
        if(w->colossal) return 0; /* Full-word coordinate hook owns this loop. */
        divisor=4;next=0x9fb7;end=0xa02d;byte=true;rep=false;break;
    case 0xa1a6:divisor=8;next=0xa164;end=0xa1b6;byte=true;rep=false;break;
    case 0xa225:divisor=8;next=0xa1e3;end=0xa235;byte=true;rep=false;break;
    case 0xa288:divisor=8;next=0xa25c;end=0xa298;byte=true;rep=false;break;
    case 0x9c40:
        if(w->huge) return 0; /* Zero-clock full-coordinate hook owns this. */
        offset=8;next=0x9c3b;end=0x9c50;byte=true;rep=false;break;
    default:return 0;
    }
    if((!rep && c->mf!=byte) || (diffusion_reference() && max_cycles<80)) return 0;
    /* Connected service code can reach this iterator without returning to
     * the outer hook. Its eighth-resolution mega grid needs word counters
     * here as well, including a short cell at either byte seam. */
    bool wide=byte && divisor==8 && w->mega;
    unsigned width=ScWorldWidth(w)/divisor,height=ScWorldHeight(w)/divisor,mask=byte && !wide?255:65535;
    unsigned at=c->dp+offset,x=(word(r,at)+1)&mask,y=word(r,at+2)&mask,dp=(c->dp&255)!=0;
    if(!x || x>width || y>=height) return 0;
    /* Price the chosen loop branch before changing coordinates. Most cells
     * need only the short column advance, rather than the old 80-cycle bound. */
    unsigned estimate=(rep?3:0)+(byte?5:7)+dp+(byte?3:4)+dp+(byte?2:3);
    if(x<width) estimate+=crime?2+3:3;
    else {
        unsigned next_y=(y+1)&mask;
        estimate+=(crime?3:2)+(byte?5:7)+dp+(byte?3:4)+dp+(byte?2:3);
        estimate+=next_y<height?(crime?2+3:3)+(byte?3:4)+dp:crime?3:2;
    }
    if(estimate>max_cycles) return 0;
    if(byte && !wide) r[at]=x;else put(r,at,x);
    c->mf=byte;c->a=byte?(c->a&0xff00)|(x&255):x;
    c->c=x>=width;nz(c,(uint16_t)(x-width),byte);
    unsigned cycles=(rep?3:0)+(byte?5:7)+dp+(byte?3:4)+dp+(byte?2:3);
    if(x<width) {
        c->pc=next;c->cyclesUsed=3;
        return cycles+(crime?2+3:3);
    }
    y=(y+1)&mask;if(byte && !wide) r[at+2]=y;else put(r,at+2,y);
    c->a=byte?(c->a&0xff00)|(y&255):y;c->c=y>=height;nz(c,(uint16_t)(y-height),byte);
    cycles+=(crime?3:2)+(byte?5:7)+dp+(byte?3:4)+dp+(byte?2:3);
    if(y<height) {
        if(byte && !wide) r[at]=0;else put(r,at,0);
        c->pc=next;c->cyclesUsed=(byte?3:4)+dp;
        return cycles+(crime?2+3:3)+c->cyclesUsed;
    }
    c->pc=end;c->cyclesUsed=crime?3:2;
    return cycles+c->cyclesUsed;
}
static unsigned crime_field_span(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    if(c->pc!=0x9eb0 || !stencil_backend.crime_data) return 0;
    const ScCrimeSample *packed=stencil_backend.crime_data(stencil_backend.context,w,word(r,0xc71));
    if(!packed) return 0;
    unsigned cycles=0;
    while(c->pc==0x9eb0) {
        unsigned cost=crime_gpu_cell(w,c,r,budget-cycles,packed);
        if(!cost) break;
        cycles+=cost;
        cost=spatial_advance(w,c,r,budget-cycles);
        if(!cost) break;
        cycles+=cost;
    }
    return cycles;
}
static unsigned land_value_call(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(c->pc!=0x9c3b || max_cycles<32 || c->sp<0x1006 || c->sp>0x1fff ||
       c->dp<0x1000 || c->dp>0x1fc0 || (c->sp>=c->dp && c->sp-5<=c->dp+34)) return 0;
    if(land_pipeline_reference() && max_cycles<1515) return 0;
    if(word(r,c->dp+8)>=ScWorldWidth(w)/2 || word(r,c->dp+10)>=ScWorldHeight(w)/2) return 0;
    Interp816 next=*c;next.sp-=2;next.pc=0x9cdf;
    unsigned cost=max_cycles>=15?empty_terrain_cell(w,&next,r,max_cycles-15):0;
    if(!cost && (max_cycles>=1515 || stencil_backend.land_data))
        cost=land_value_cell(w,&next,r,max_cycles-15);
    if(!cost) cost=land_value_begin(w,&next,r,max_cycles-6);
    if(!cost) return 0;
    put(r,c->sp-1,0x9c3d);*c=next;
    if(c->pc!=0x9dc9) return cost+6; /* Keep the helper's frame for its C finish. */
    c->sp+=2;c->pc=0x9c40;c->mf=true;c->cyclesUsed=3;
    if(w->huge) {
        unsigned x=word(r,c->dp+8)+1,y=word(r,c->dp+10);
        if(x==ScWorldWidth(w)/2) {x=0;++y;}
        put(r,c->dp+8,x);put(r,c->dp+10,y);
        c->pc=y<ScWorldHeight(w)/2?0x9c3b:0x9c50;
    }
    return cost+15;
}
static unsigned land_value_return(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(land_pipeline_reference() || c->pc!=0x9dc9 || !w->huge || c->sp<0x1004 || c->sp>0x1ffd ||
       word(r,c->sp+1)!=0x9c3d || max_cycles<9) return 0;
    unsigned x=word(r,c->dp+8)+1,y=word(r,c->dp+10);
    if(x>ScWorldWidth(w)/2 || y>=ScWorldHeight(w)/2) return 0;
    if(x==ScWorldWidth(w)/2) {x=0;++y;}
    put(r,c->dp+8,x);put(r,c->dp+10,y);c->sp+=2;c->mf=true;
    c->pc=y<ScWorldHeight(w)/2?0x9c3b:0x9c50;c->cyclesUsed=3;return 9;
}
static unsigned zone_score(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    unsigned entry=c->pc;
    if((entry!=0x99d2 && entry!=0x9a0e && entry!=0x9a31) || c->mf || max_cycles<140) return 0;
    if(entry!=0x9a31 && c->a && !ScWorldContains(w,w->huge?w->coord[2][0]:r[0xb85],w->huge?w->coord[2][1]:r[0xb86])) return 0;
    c->c=true;
    if(entry==0x9a31) {
        c->y=c->a?0:0xfc18;c->a=c->y;nz(c,c->a,false);
        c->pc=0x9a3d;c->cyclesUsed=2;return c->a?11:13;
    }
    c->y=0xf448;
    if(!c->a) {
        c->pc=entry==0x99d2?0x9a0d:0x9a30;
        c->cyclesUsed=entry==0x99d2?2:3;
        if(entry==0x99d2) {c->a=c->y;nz(c,c->a,false);return 11;}
        nz(c,0,false);return 9;
    }
    unsigned divisor=entry==0x99d2?2:8;
    unsigned x=(w->huge?w->coord[2][0]:r[0xb85])/divisor;
    unsigned y=(w->huge?w->coord[2][1]:r[0xb86])/divisor;
    unsigned index=y*(ScWorldWidth(w)/divisor)+x;
    w->field_anchor[divisor==2?0:2]=index;put(r,0xb3f,x);put(r,0xb3d,y);
    put(r,(uint16_t)(c->sp-1),entry==0x99d2?0x99e7:0x9a27);
    if(entry==0x9a0e) {
        c->x=(uint16_t)(2*index);c->a=word(w->fields[12],2*index);
        c->c=false;nz(c,c->a,false);c->pc=0x9a30;c->cyclesUsed=6;
        return 3+3+2+3+4+6+3+4+6+6+6+3+2+2+6;
    }
    unsigned land=w->fields[0][index],pollution=w->fields[2][index];
    unsigned score=(land>=pollution?land-pollution:0)*32;
    unsigned cycles=3+3+2+3+4+2+3+4+2+6+6+5+2+5;
    cycles+=land>=pollution?3:4;
    cycles+=3+3+10+3+(score<6000?3:5);if(score>6000) score=6000;
    c->x=(uint16_t)index;c->a=c->y=(uint16_t)(score-3000);c->mf=false;
    c->c=score>=3000;c->v=false;nz(c,c->a,false);c->pc=0x9a0d;c->cyclesUsed=2;
    return cycles+2+3+2+2;
}
/* Traffic selects ordinary road artwork without entering the CPU dispatcher
 * for each flag/load/branch. Keep the mapped field and tile byte write order,
 * scratch/stack shadows and original call cost. Short deadlines use C edges. */
typedef struct InfrastructureRoad {
    unsigned x,y,field,traffic,art,cost;
    int64_t tile;
} InfrastructureRoad;
static bool infrastructure_road_plan(const ScWorld *w,const Interp816 *c,const uint8_t *r,
                                    unsigned budget,InfrastructureRoad *p) {
    /* These ranges exclude aliases with the native scratch words and stack.
     * Unusual direct-page frames remain supported by the instruction path. */
    if(c->dp<0x1000 || c->dp>0x1efe || c->sp<0x1f02 || c->sp>0x1ffd) return false;
    unsigned x=r[0xb85]>>1,y=r[0xb86]>>1;
    if(w->huge) {x=lift(x,w->coord[2][0]/2,128);y=lift(y,w->coord[2][1]/2,128);}
    unsigned field=y*(ScWorldWidth(w)/2)+x;
    int64_t tile=(int64_t)w->map_anchor+(int16_t)(word(r,c->dp)-(uint16_t)w->map_anchor)-2;
    if(field>=ScWorldFieldSizeWorld(w,4) || w->map_anchor==UINT32_MAX ||
       tile<0 || tile+2>ScWorldCells(w)*2) return false;
    unsigned traffic=w->fields[4][field],art=traffic<100?0x30:traffic<200?0x40:0x50;
    unsigned cost=30+3+5+2+(traffic<100?3:2+3+2+(traffic<200?3:2+3))+
                  5+3+4+((c->dp&255)!=0)+5+3+5+6+6;
    if(cost>budget) return false;
    *p=(InfrastructureRoad){x,y,field,traffic,art,cost,tile};return true;
}
static void infrastructure_road_publish(ScWorld *w,Interp816 *c,uint8_t *r,const InfrastructureRoad *p) {
    w->field_anchor[0]=p->field;put(r,0xb3f,p->x);put(r,0xb3d,p->y);
    put(r,c->sp-1,0xa510);put(r,0xb41,p->art);
    c->x=(uint16_t)word(r,c->dp);c->y=(uint16_t)p->art;
    c->a=(uint16_t)((word(r,0xb87)&0xff0f)|p->art);c->mf=false;
    c->c=p->traffic>=200;nz(c,c->a,false);
    for(unsigned byte=0;byte<2;++byte) {
        uint8_t value=(uint8_t)(c->a>>(byte*8));
        if(w->tiles[p->tile+byte]!=value) {
            w->tiles[p->tile+byte]=value;ScWorldTilesTouch(w,(unsigned)p->tile+byte,1);
        }
    }
    c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;
}
static unsigned infrastructure_road(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    InfrastructureRoad p;if(!infrastructure_road_plan(w,c,r,budget,&p))return 0;
    infrastructure_road_publish(w,c,r,&p);return p.cost;
}
/* Fully funded ordinary roads skip both the decay RNG and bridge handler.
 * Price their complete upkeep/artwork call before the first counter write. */
static unsigned infrastructure_funded_road(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    unsigned tile=word(r,0xb89),prefix=3+8+5+3+2+5+3+3+5+3+3+3+5+3+2;
    if(tile>=0x80 || (tile&15)<2 || word(r,0xbc5)<30 || budget<prefix)return 0;
    InfrastructureRoad p;if(!infrastructure_road_plan(w,c,r,budget-prefix,&p))return 0;
    put(r,0xe15,word(r,0xe15)+1);c->xf=false;
    infrastructure_road_publish(w,c,r,&p);return prefix+p.cost;
}
/* Bridge/road distance probe. Keep the original byte arithmetic, stack
 * shadows and final PLD flags, but calculate the complete call in C when
 * it fits before the next beam event. Short deadlines retain native edges. */
unsigned ScWorldGuestInfrastructureStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_INFRASTRUCTURE_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || c->k!=3 || c->db!=3 ||
       (c->pc!=0xa70c && c->pc!=0xa503 && c->pc!=0xa493) || c->e || c->d || c->xf || c->waiting || c->stopped ||
       c->nmiWanted || (c->irqWanted && !c->i) || c->dp<0x102 || c->dp>0x1ffe ||
       c->sp<0x104 || c->sp>0x1ffd ||
       (c->sp+2>=c->dp-2 && c->sp-3<=c->dp))return 0;
    if(c->pc==0xa503)return infrastructure_road(w,c,r,budget);
    if(c->pc==0xa493)return infrastructure_funded_road(w,c,r,budget);
    unsigned dx=(uint8_t)(r[0xa65]-r[0xb85]),dy=(uint8_t)(r[0xa63]-r[0xb86]);
    bool xborrow=r[0xa65]<r[0xb85],yborrow=r[0xa63]<r[0xb86];
    if(xborrow)dx=(uint8_t)(-dx);if(yborrow)dy=(uint8_t)(-dy);
    unsigned unaligned=((c->dp-2)&255)!=0;
    /* REP/PHD/PHA/local-frame/PLA/SEP; two absolute differences; scratch
     * store and addition; REP/AND/PLD/RTS. Taken BCS costs one extra cycle. */
    unsigned cost=28+10+(xborrow?6:3)+3+unaligned+10+(yborrow?6:3)+
                  2+3+unaligned+3+3+5+6;
    if(cost>budget)return 0;
    unsigned sp=c->sp,dp=c->dp;
    put(r,sp-1,dp);put(r,sp-3,c->a);r[dp-2]=(uint8_t)dx;
    unsigned sum=dx+dy;
    c->a=(uint16_t)(sum&255);c->mf=false;
    c->c=sum>255;c->v=(~(dx^dy)&(dy^sum)&128)!=0;
    c->dp=(uint16_t)dp;c->n=(dp&32768)!=0;c->z=dp==0;
    c->pc=(uint16_t)(word(r,sp+1)+1);c->sp=(uint16_t)(sp+2);c->cyclesUsed=6;
    return cost;
}
static unsigned zone_replacement_impl(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_ZONE_REFERENCE");reference=e && *e!='0';}
    if(reference) return 0;
    unsigned entry=c->pc;
    if((entry!=0x9940 && entry!=0x9952 && entry!=0x998b) || c->dp<4 || c->dp>0x1ffb || c->sp<0x108 || c->sp>0x1fff) return 0;
    if(max_cycles<(entry==0x9940?50:entry==0x9952?100:120) || (entry!=0x9940 && (c->mf || c->y>=9))) return 0;
    int cx=w->huge?lift(r[0xb85],w->coord[2][0],256):r[0xb85];
    int cy=w->huge?lift(r[0xb86],w->coord[2][1],256):r[0xb86];
    if(!ScWorldContains(w,cx,cy)) return 0;
    if(entry==0x9940) {
        unsigned base=c->a,old_dp=c->dp,value=old_dp-4;
        put(r,(uint16_t)(c->sp-1),old_dp);c->sp-=2;c->dp-=4;
        put(r,(uint16_t)(c->sp-1),base);put(r,c->dp+2,base);
        c->mf=c->xf=false;c->c=true;c->v=((old_dp^4)&(old_dp^value)&32768)!=0;
        c->y=0;c->n=false;c->z=true;c->pc=0x9952;c->cyclesUsed=3;
        return 35+((c->dp&255)!=0);
    }
    unsigned dp=(c->dp&255)!=0,cycles=0,n=c->y;
    /* Keep the row-major nine-cell order. Infrastructure, disasters and
     * special occupied cells abort before any tile is replaced. */
    if(entry==0x9952) {
        int dx=(int)(n%3)-1,dy=(int)(n/3)-1,x=cx+dx,y=cy+dy;
        x&=255;y&=255;
        if(w->huge) {x=lift(x,w->coord[2][0],256);y=lift(y,w->coord[2][1],256);}
        unsigned offset=2*(y*ScWorldWidth(w)+x),tile=ScWorldCell(w,x,y)&1023;
        unsigned low=r[0xb85],operand=(uint8_t)dx,sum=low+operand;
        c->v=((low^sum)&(operand^sum)&128)!=0;
        w->map_anchor=ScWorldContains(w,x,y)?offset:UINT32_MAX;
        put(r,0xb3f,x*2);put(r,0xb3d,y*32);put(r,(uint16_t)(c->sp-1),0x9965);
        c->x=(uint16_t)offset;c->y=n;c->a=tile;
        cycles+=41+3;
        bool blocked=tile==0x7f;
        if(blocked) {c->c=true;nz(c,0,false);cycles+=3;}
        else {
            cycles+=2+3;blocked=tile==0x364;
            if(blocked) {c->c=true;nz(c,0,false);cycles+=3;}
            else {
                cycles+=2+3;blocked=tile==0x365;
                if(blocked) {c->c=true;nz(c,0,false);cycles+=3;}
                else {
                    cycles+=2+3;c->c=tile>=0x28;nz(c,(uint16_t)(tile-0x28),false);
                    if(tile<0x28) cycles+=3;
                    else {
                        cycles+=2+3;c->c=tile>=0x80;nz(c,(uint16_t)(tile-0x80),false);
                        blocked=tile<0x80;cycles+=blocked?3:2;
                    }
                }
            }
        }
        if(blocked) {c->pc=0x99be;c->cyclesUsed=3;return cycles;}
        c->c=n+1>=9;nz(c,(uint16_t)(n+1-9),false);cycles+=2+3+(n==8?2:3);
        c->y=n+1;c->pc=0x9952;c->cyclesUsed=3;
        if(n==8) {c->y=0;c->n=false;c->z=true;c->pc=0x998b;cycles+=3;}
        return cycles;
    }
    {
        int dx=(int)(n%3)-1,dy=(int)(n/3)-1,x=cx+dx,y=cy+dy;
        x&=255;y&=255;
        if(w->huge) {x=lift(x,w->coord[2][0],256);y=lift(y,w->coord[2][1],256);}
        unsigned offset=2*(y*ScWorldWidth(w)+x),power=ScWorldCell(w,x,y)&0x8000;
        unsigned low=r[0xb85],operand=(uint8_t)dx,sum=low+operand;
        c->v=((low^sum)&(operand^sum)&128)!=0;
        put(r,(uint16_t)(c->sp-1),n);put(r,(uint16_t)(c->sp-3),0x999f);
        w->map_anchor=ScWorldContains(w,x,y)?offset:UINT32_MAX;
        put(r,0xb3f,x*2);put(r,0xb3d,y*32);put(r,c->dp,power);
        unsigned base=word(r,c->dp+2),value=base|power|(n==8?0x4000:0);
        ScWorldPutCell(w,x,y,(uint16_t)value);put(r,c->dp+2,base+1);
        c->a=(uint16_t)value;c->x=(uint16_t)offset;
        cycles+=4+41+(4+dp)+(4+dp)+3+(n==8?2+3:3)+(4+dp)+6+(7+dp)+5+2+3+(n==8?2:3);
    }
    c->y=n+1;c->c=n==8;nz(c,(uint16_t)(c->y-9),false);
    c->pc=n==8?0x99be:0x998b;c->cyclesUsed=n==8?2:3;
    return cycles;
}
static unsigned zone_replacement(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    static int validate=-1;static unsigned samples;
    if(validate<0) {const char *e=getenv("SC_ZONE_VALIDATE");validate=e?atoi(e):0;}
    if(!validate || (c->pc!=0x9940 && c->pc!=0x9952 && c->pc!=0x998b))
        return zone_replacement_impl(w,c,r,max_cycles);
    if(samples++%(unsigned)validate) return zone_replacement_impl(w,c,r,max_cycles);
    static ScWorld *oracle;static uint8_t *ram;
    if(!oracle) {oracle=malloc(sizeof *oracle);ram=malloc(0x20000);}
    if(!oracle || !ram) abort();
    *oracle=*w;memcpy(ram,r,0x20000);Interp816 before=*c,reference=*c;
    unsigned cost=zone_replacement_impl(w,c,r,max_cycles);if(!cost) return 0;
    KernelBus bus={oracle,&reference,ram,rom,{0}};
    reference.mem=&bus;reference.read=kernel_read;reference.write=kernel_write;
    reference.read_word=NULL;reference.write_word=NULL;
    unsigned cycles=0;
    while(cycles<cost) {
        ScWorldGuestStep(oracle,&reference,ram);ScWorldGuestBegin(&bus.guest,oracle,&reference,rom,0x80000);
        cycles+=interp816_runOpcode(&reference);
    }
    if(cycles!=cost || memcmp(r,ram,0x20000) || memcmp(w,oracle,sizeof *w) ||
       memcmp(&c->a,&reference.a,(char *)&c->cyclesUsed-(char *)&c->a+1)) {
        fprintf(stderr,"[native zone mismatch] pc=%x dp=%x sp=%x A=%x Y=%x mode=%d cycles=%u/%u end=%x/%x flags=%x/%x\n",
            before.pc,before.dp,before.sp,before.a,before.y,before.mf,cost,cycles,c->pc,reference.pc,interp816_getFlags(c),interp816_getFlags(&reference));
        for(unsigned p=0,shown=0;p<0x20000 && shown<8;++p) if(r[p]!=ram[p]) {fprintf(stderr,"RAM %x=%x/%x\n",p,r[p],ram[p]);++shown;}
        abort();
    }
    return cost;
}
unsigned ScWorldGuestZoneReplacementStep(ScWorld *w,Interp816 *c,uint8_t *r,
                                        const uint8_t *rom,unsigned budget) {
    if(!w || !w->active || !c || !r || !rom || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted ||
       (c->irqWanted && !c->i)) return 0;
    return zone_replacement(w,c,r,rom,budget);
}
static bool zone_art_return(unsigned value) {
    return value==0x98da || value==0x9903 || value==0x9928;
}
unsigned ScWorldGuestZoneArtStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_ZONE_ART_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || !rom || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i)) return 0;
    unsigned cycles=0;
    for(;;) {
        unsigned entry=c->pc,cost=0;
        if(entry==0x98b8 || entry==0x98dd || entry==0x9906) {
            unsigned frame=entry==0x98dd?4:2,dp=c->dp-frame;
            if(c->dp<0x1004 || c->dp>0x1ffb || c->sp<0x1008 || c->sp>0x1ffd ||
               (c->sp>=dp && c->sp-7<=c->dp+2)) return cycles;
            unsigned capacity=c->a&255,quality=c->a>>8;
            unsigned shifted=(quality*4)&255,carry=(quality&64)!=0;
            unsigned sum=shifted+(entry==0x98dd?quality:capacity)+carry;
            if(entry==0x98dd) sum=(sum&255)+capacity+(sum>255);
            unsigned index=sum&255,unaligned=(dp&255)!=0;
            cost=63+2*unaligned+((0x2b+index)>255)+(entry==0x98dd?6+2*unaligned:0);
            if(cost>budget-cycles) return cycles;
            unsigned origin=entry==0x98b8?0x95:entry==0x98dd?0x140:0x1fd;
            unsigned base=rom[0x1992b+index]+origin;
            put(r,c->sp-1,c->dp);put(r,c->sp-3,c->a);
            c->dp=(uint16_t)dp;r[dp]=(uint8_t)capacity;
            if(entry==0x98dd) r[dp+2]=(uint8_t)quality;
            put(r,c->sp-3,entry==0x98b8?0x98da:entry==0x98dd?0x9903:0x9928);
            c->sp-=4;c->a=(uint16_t)base;c->x&=255;c->y=(uint16_t)index;
            c->mf=c->xf=false;c->c=c->v=false;nz(c,base,false);
            c->pc=0x9940;c->cyclesUsed=6;
        } else if(entry==0x9940 || entry==0x9952 || entry==0x998b) {
            /* Continue only our own nested redraw frame. Direct users of the
             * shared writer retain its existing scheduler boundaries. */
            unsigned saved=entry==0x9940?c->sp+1:c->sp+3;
            if(saved>0x1ffe || !zone_art_return(word(r,saved))) return cycles;
            cost=zone_replacement_impl(w,c,r,budget-cycles);
            if(!cost) return cycles;
        } else if(entry==0x99be || entry==0x99bf || entry==0x98db || entry==0x9904 || entry==0x9929 ||
                  entry==0x98dc || entry==0x9905 || entry==0x992a) {
            bool inner=entry==0x99be || entry==0x99bf;
            bool restore=entry==0x99be || entry==0x98db || entry==0x9904 || entry==0x9929;
            if(c->sp>0x1ffb || (inner && !zone_art_return(word(r,c->sp+(restore?3:1))))) return cycles;
            cost=restore?5:6;
            if(cost>budget-cycles) return cycles;
            if(restore) {
                c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);
                c->pc=inner?0x99bf:(uint16_t)(entry+1);c->cyclesUsed=5;
                /* Wrapper RTS follows directly if it also fits. */
                if(!inner && 6<=budget-cycles-cost) {
                    c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;cost+=6;
                } else if(!inner) return cycles+cost;
            } else {
                c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;
            }
        } else return cycles;
        cycles+=cost;
    }
}
/* Fuse classification with the tile sweep's loop control. The original
 * owner/transport/disaster calls remain yield points, so a span never skips
 * a zone's development or a mutable infrastructure handler. */
static unsigned sweep_cell(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_SWEEP_REFERENCE");reference=e && *e=='1';}
    if(reference || c->pc!=0x82ac || !c->sp || c->dp<0x20 || c->dp>0x1fe0 || max_cycles<256) return 0;
    unsigned x=w->huge?w->scan_x:r[0xb85],y=w->huge?w->scan_y:r[0xb86];
    unsigned width=ScWorldWidth(w),height=ScWorldHeight(w),offset=2*(y*width+x);
    if(x>=width || y>=height || word(r,c->dp)!=(uint16_t)offset) return 0;
    unsigned tile=ScWorldCell(w,x,y)&1023;
    if(tile>=958 || tile==0x7f || tile==0x364 || tile==0x365 || (rom[0x184eb+tile]&0x71)) return 0;
    c->pc=0x82ae;c->mf=false; /* REP #$20 */
    if(w->huge) coord_set(w,r,x,y);
    unsigned cycles=ScWorldGuestFastStep(w,c,r,rom,0x80000)+6; /* REP, cell, SEP */
    c->mf=true;c->pc=0x8343;c->cyclesUsed=3;
    if(w->huge) {
        /* Full-coordinate loop control is already a zero-clock compatibility
         * hook. Retain its CPU flags and full scan/proxy-coordinate state. */
        ScWorldGuestStep(w,c,r);return cycles;
    }
    r[0xb85]=(uint8_t)++x;c->a=(c->a&0xff00)|x;
    c->c=x>=width;nz(c,x-width,true);cycles+=6+4+2;
    if(x<width) {c->pc=0x82ac;c->cyclesUsed=3;return cycles+2+3;}
    r[0xb86]=(uint8_t)++y;c->a=(c->a&0xff00)|y;
    c->c=y>=height;nz(c,y-height,true);cycles+=3+6+4+2;
    if(y<height) {
        r[0xb85]=0;c->pc=0x82ac;c->cyclesUsed=4;return cycles+2+3+4;
    }
    c->pc=0x835d;c->cyclesUsed=3;return cycles+3;
}
unsigned ScWorldGuestSweepStageStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    return w && w->active?sweep_cell(w,c,r,rom,max_cycles):0;
}
unsigned ScWorldGuestHousingStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_HOUSING_REFERENCE");reference=e && *e=='1';}
    if(reference || !c || !r || c->pc!=0x9a3e || c->k!=3 || c->db!=3 || c->xf || c->e || c->d ||
       c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x20 || c->dp>0x1fc0 || c->sp<0x108 || c->sp>0x1fff) return 0;
    bool large=w && w->active;
    int x=(uint8_t)(r[0xb85]-1),y=(uint8_t)(r[0xb86]-1);
    if(large && w->huge) {x=lift(x,w->coord[2][0],256);y=lift(y,w->coord[2][1],256);}
    if(!large && (x>=118 || y>=98)) return 0; /* retain native out-of-city WRAM accesses */
    unsigned width=large?ScWorldWidth(w):120,offset=2*(y*width+x);
    bool valid=!large || ScWorldContains(w,x,y);
    const int deltas[]={0,2,4,0,4,0,2,4},rows[]={0,0,0,1,1,2,2,2};
    unsigned count=0,dp=((c->dp-6)&255)!=0,cycles=62+dp+(large?0:68),last=0;
    for(unsigned i=0;i<8;++i) {
        unsigned at=offset+rows[i]*2*width+deltas[i],tile=0;
        if(valid && at+1<(large?ScWorldCells(w)*2:24000))
            tile=word(large?w->tiles:r+0x10200,at)&1023;
        if(i) cycles+=6; /* LDA long,X; the first load belongs to $849e. */
        if(tile<0x89) cycles+=21;
        else if(tile>=0x95) cycles+=26;
        else {cycles+=32+dp;++count;}
        last=tile;
    }
    cycles+=4+dp+5; /* LDA count, PLD; caller's RTS remains native. */
    if(cycles>max_cycles) return 0;
    put(r,c->sp-1,c->dp);put(r,c->sp-3,0x9a8e);put(r,c->dp-6,count);
    if(large) w->map_anchor=valid?offset:UINT32_MAX;
    else put(r,c->sp-5,y); /* $849e's popped PHA/PLA scratch. */
    put(r,0xb3f,x*2);put(r,0xb3d,y*(large?32:16));
    c->a=(uint16_t)count;c->x=(uint16_t)offset;c->c=last>=0x95;c->v=false;
    c->mf=false;nz(c,c->dp,false);c->pc=0x9a92;c->cyclesUsed=5;
    return cycles;
}
unsigned ScWorldGuestHouseSiteStep(ScWorld *w,Interp816 *c,uint8_t *r,
    const uint8_t *rom,size_t size,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_HOUSE_SITE_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || !rom || size!=0x80000 ||
       c->pc!=0x987f || c->k!=3 || c->db!=3 || !c->mf || c->xf || c->e || c->d ||
       c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x1000 || c->dp>0x1fc0 || c->sp<0x1004 || c->sp>0x1fff || c->x>=4 ||
       (c->sp>=c->dp && c->sp-3<=c->dp+10)) return 0;
    unsigned cycles=0,dp=(c->dp&255)!=0;
    while(c->x<4) {
        /* These are absolute ADC operands in the original ROM. Both loads
         * read the same scratch byte; do not substitute a conventional
         * indexed four-neighbour calculation and change city development. */
        unsigned source=r[c->dp+6];
        unsigned y=(source+rom[0x198b4])&255,x=(source+rom[0x198b0])&255;
        int fullx=w->huge?lift(x,w->coord[2][0],256):(int)x;
        int fully=w->huge?lift(y,w->coord[2][1],256):(int)y;
        bool read=fullx>=0 && (unsigned)fullx<ScWorldWidth(w);
        bool valid=ScWorldContains(w,fullx,fully);
        unsigned offset=2*(fully*ScWorldWidth(w)+fullx);
        unsigned tile=read && valid?ScWorldCell(w,fullx,fully)&1023:0;
        bool increase=read && tile<0x60,last=c->x==3;
        unsigned cost=4+3+dp+2+4+(w->huge?0:2)+3+3+dp+2+4+(w->huge?0:2)+(read?2:3);
        if(read) cost+=12+3+3+3+(increase?2:3)+(increase?3+5+dp:0);
        cost+=3+5+2+3+(last?2:3);
        if(cost>max_cycles-cycles) break;
        put(r,c->sp-1,c->x);
        if(read) {
            put(r,c->sp-3,0x9895);w->map_anchor=valid?offset:UINT32_MAX;
            put(r,0xb3f,fullx*2);put(r,0xb3d,fully*32);c->a=tile;
            if(increase) ++r[c->dp+10];
        } else c->a=(uint16_t)((y<<8)|x);
        unsigned sum=source+rom[0x198b0];
        c->v=((~(source^rom[0x198b0]))&(source^sum)&128)!=0;
        ++c->x;c->c=c->x>=4;nz(c,c->x-4,false);c->mf=true;
        c->cyclesUsed=last?2:3;cycles+=cost;
    }
    if(cycles && c->x==4) c->pc=0x98ad;
    return cycles;
}
static unsigned density_advance(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    unsigned x=word(r,c->dp+0x10),y=word(r,c->dp+0x12),width=ScWorldWidth(w),height=ScWorldHeight(w);
    unsigned dp=(c->dp&255)!=0;
    if(x>=width || y>=height) return 0;
    unsigned cycles=w->huge?3:x+1<width?16+2*dp:y+1<height?31+5*dp:27+4*dp;
    if(cycles>max_cycles) return 0;
    c->mf=true;c->pc=0x9b5c;c->cyclesUsed=3;
    if(w->huge) {ScWorldGuestStep(w,c,r);return cycles;}
    r[c->dp+0x10]=(uint8_t)++x;c->a=(c->a&0xff00)|x;
    c->c=x>=width;nz(c,x-width,true);c->cyclesUsed=3;c->pc=0x9af7;
    if(x<width) return cycles;
    r[c->dp+0x12]=(uint8_t)++y;c->a=(c->a&0xff00)|y;
    c->c=y>=height;nz(c,y-height,true);
    if(y<height) {r[c->dp+0x10]=0;c->cyclesUsed=3+dp;}
    else {c->pc=0x9b6c;c->cyclesUsed=2;}
    return cycles;
}
static unsigned density_owner(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles);
static bool density_owner_reference(void) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_DENSITY_OWNER_REFERENCE");reference=e && *e=='1';}
    return reference!=0;
}
static bool density_owner_layout(Interp816 *c,unsigned tile) {
    static int housing_reference=-1;
    if(housing_reference<0) {const char *e=getenv("SC_HOUSING_REFERENCE");housing_reference=e && *e=='1';}
    return !density_owner_reference() && c->dp>=0x1000 && c->dp<=0x1fc0 &&
        c->sp>=0x1004 && c->sp<=0x1fff && !(c->sp>=c->dp-6 && c->sp-7<=c->dp+0x14) &&
        !(tile==0x84 && housing_reference);
}
static unsigned density_scan(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_DENSITY_SCAN_REFERENCE");reference=e && *e=='1';}
    if(reference || c->dp<0x20 || c->dp>0x1fc0 || c->sp<0x108 || c->sp>0x1fff) return 0;
    if(c->pc==0x9b5a) return density_advance(w,c,r,max_cycles);
    if(c->pc!=0x9af7 || max_cycles<128) return 0;
    unsigned x=word(r,c->dp+0x10),y=word(r,c->dp+0x12);
    if(!ScWorldContains(w,x,y)) return 0;
    unsigned tile=ScWorldCell(w,x,y)&1023;
    if(rom[0x184eb+tile]&1) {
        /* Leave a whole-owner budget before changing any state. This also
         * allows tight beam deadlines to resume at the separate owner entry. */
        if(max_cycles<1024 || !density_owner_layout(c,tile)) return 0;
        if(w->huge) coord_set(w,r,x,y);
        w->map_anchor=2*(y*ScWorldWidth(w)+x);put(r,0xb3f,x*2);put(r,0xb3d,y*32);
        put(r,c->sp-1,0x9b00);put(r,0xb89,tile);
        c->y=tile;c->a=1;c->mf=false;c->pc=0x9b12;
        unsigned cycles=47+2*((c->dp&255)!=0)+(tile>=0x15);
        cycles+=density_owner(w,c,r,max_cycles-cycles);
        return cycles+density_advance(w,c,r,max_cycles-cycles);
    }
    if(w->huge) coord_set(w,r,x,y);
    unsigned cycles=ScWorldGuestFastStep(w,c,r,rom,0x80000);
    return cycles+density_advance(w,c,r,max_cycles-cycles);
}
static unsigned density_capacity(ScWorld *w,Interp816 *c,uint8_t *r) {
    unsigned tile=word(r,0xb89),cycles=3+5+3;c->mf=c->xf=false;c->a=tile;
    if(tile>=0x376) {c->a=48;return cycles+2+3+6;}
    cycles+=3+3;
    if(tile==0x84) {
        cycles+=2+3;c->pc=0x9a3e;
        return cycles+ScWorldGuestHousingStep(w,c,r,800)+6;
    }
    cycles+=3+3;
    unsigned helper,origin,period;
    bool multiply=false;
    if(tile<0x137) {helper=0x842f;origin=0x99;period=36;cycles+=2+3;}
    else {
        cycles+=3+3;
        if(tile<0x1f4) {helper=0x8456;origin=0x144;period=45;cycles+=2+6;multiply=true;}
        else {
            cycles+=3+3;
            if(tile>=0x249) {c->a=0;return cycles+3+3+6;}
            helper=0x847a;origin=0x201;period=36;cycles+=2+6;multiply=true;
        }
    }
    if(multiply) put(r,c->sp-1,helper==0x8456?0x9bfa:0x9c07);
    if(tile<origin) {c->a=0;cycles+=15+6;}
    else {
        unsigned remaining=(tile-origin)&255,reductions=remaining/period;
        unsigned steps=(remaining%period)/9+1;
        c->y=steps+(origin==0x99);c->a=c->y;
        if(origin==0x99) c->a*=8;
        cycles+=30+9*reductions+7*steps+(origin==0x99?6:0)+6;
    }
    if(multiply) {c->a*=8;cycles+=6+3+6;}
    return cycles;
}
/* Occupied-zone density and centroid accounting in one C span. Retain the
 * free-house neighbour probe, nested return bytes, 32-bit carry counters,
 * byte density clamp and each original flag at the loop boundary. */
static unsigned density_owner(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(c->pc!=0x9b12 || c->mf || max_cycles<800 || !density_owner_layout(c,word(r,0xb89))) return 0;
    unsigned x=word(r,c->dp+16),y=word(r,c->dp+18),tile=word(r,0xb89);
    if(!ScWorldContains(w,x,y) || tile>=1024) return 0;
    /* Header writes the original byte coordinate proxies. The scan's hook
     * already holds their full host coordinates on maps wider than 256. */
    r[0xb85]=x;r[0xb86]=y;put(r,c->sp-1,0x9b22);c->sp-=2;
    unsigned dp=(c->dp&255)!=0,cycles=26+2*dp+density_capacity(w,c,r);
    c->sp+=2;
    unsigned value=(c->a<<3)&65535;cycles+=6+3+(value<254?3:2+3);
    if(value>=254) value=254;
    c->mf=true;r[c->sp]=(uint8_t)value;put(r,c->sp-2,0x9b3a);
    unsigned index=(y/2)*(ScWorldWidth(w)/2)+x/2;
    w->field_anchor[0]=index;put(r,0xb3f,x/2);put(r,0xb3d,y/2);
    c->x=(uint16_t)index;c->a=(uint16_t)value;c->mf=false;
    w->fields[13][index]=(uint8_t)value;
    cycles+=3+3+(3+dp)+2+3+(3+dp)+2+6+6+4+5+3;
    unsigned old=word(r,c->dp),sum=old+x;put(r,c->dp,sum);
    cycles+=(4+dp)+2+(4+dp)+(4+dp)+(sum>65535?2+7+dp:3);
    if(sum>65535) put(r,c->dp+2,word(r,c->dp+2)+1);
    old=word(r,c->dp+4);sum=old+y;put(r,c->dp+4,sum);
    c->a=(uint16_t)sum;c->c=sum>65535;c->v=((~(old^y))&(old^sum)&32768)!=0;
    cycles+=(4+dp)+2+(4+dp)+(4+dp)+(sum>65535?2+7+dp:3);
    if(sum>65535) put(r,c->dp+6,word(r,c->dp+6)+1);
    unsigned count=(word(r,c->dp+8)+1)&65535;put(r,c->dp+8,count);nz(c,count,false);
    c->pc=0x9b5a;c->cyclesUsed=7+dp;
    return cycles+c->cyclesUsed;
}
/* Clear both coverage accumulators in contiguous, beam-bounded spans.
 * The full byte index survives X wrapping on the largest field. */
static unsigned coverage_clear(ScWorld *w,Interp816 *c,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_COVERAGE_CLEAR_REFERENCE");reference=e && *e=='1';}
    if(reference || c->pc!=0x8287 || c->mf || c->waiting || c->stopped) return 0;
    unsigned x=w->colossal?w->field_scan:c->x,end=ScWorldFieldSizeWorld(w,10),cycles=0;
    if(x>=end || (x&1) || c->x!=(uint16_t)x) return 0;
    while(x<end) {
        unsigned cost=(w->colossal?19:22)-(x+2==end);
        if(cost>max_cycles-cycles) break;
        put(w->fields[10],x,c->a);put(w->fields[11],x,c->a);
        x+=2;cycles+=cost;
        if(w->colossal) w->field_scan=x;
    }
    if(!cycles) return 0;
    c->x=(uint16_t)x;c->c=c->z=x==end;
    if(w->colossal) c->n=false;else nz(c,(uint16_t)(x-end),false);
    c->pc=x==end?0x8296:0x8287;c->cyclesUsed=x==end?2:3;
    return cycles;
}
/* Convert the completed police/fire word fields to byte coverage in tight C
 * spans. Keep the original PHX stack shadow, clamp, proxy index wrap and
 * exact loop cost so a beam deadline can still split the conversion. */
static unsigned coverage_pack(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_COVERAGE_PACK_REFERENCE");reference=e && *e=='1';}
    unsigned entry=c->pc;
    if(reference || (entry!=0x9ab2 && entry!=0x9f7d) || c->waiting || c->stopped ||
       c->sp<0x102 || c->sp>0x1fff) return 0;
    unsigned field=entry==0x9ab2?10:11,target=entry==0x9ab2?9:8;
    unsigned y=w->colossal?w->field_scan:c->y,end=ScWorldFieldSizeWorld(w,target),cycles=0,last=0;
    if(y>=end || c->y!=(uint16_t)y || c->x!=(uint16_t)(2*y)) return 0;
    while(y<end) {
        unsigned value=word(w->fields[field],2*y),cost=(w->colossal?43:46)+2*(value>=256)-(y+1==end);
        if(cost>max_cycles-cycles) break;
        if(w->colossal) w->field_anchor[2]=y;
        put(r,c->sp-1,(uint16_t)(2*y));
        last=value>=256?255:value;w->fields[target][y]=(uint8_t)last;
        ++y;cycles+=cost;
    }
    if(!cycles) return 0;
    if(w->colossal) w->field_scan=y;
    c->a=(uint16_t)last;c->x=(uint16_t)(2*y);c->y=(uint16_t)y;c->mf=true;
    c->c=c->z=y==end;nz(c,(uint16_t)(y-end),false);
    if(w->colossal) c->n=false;
    c->pc=y==end?(entry==0x9ab2?0x9ad1:0x9f9c):entry;c->cyclesUsed=y==end?2:3;
    return cycles;
}
/* Whole-field postpasses: traffic decay, density expansion and transport
 * totals. Process contiguous host arrays without a ROM/bus dispatch for
 * each byte; preserve the beam deadline and final per-cell CPU boundary. */
static unsigned field_sweep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_FIELD_SWEEP_REFERENCE");reference=e && *e=='1';}
    unsigned entry=c->pc;
    if(reference || !c->mf || (entry!=0x88fa && entry!=0x9b7a && entry!=0xb676) ||
       c->dp<0x20 || c->dp>0x1fc0) return 0;
    unsigned x=w->huge?w->field_scan:c->x,end=ScWorldFieldSizeWorld(w,0),cycles=0,dp=(c->dp&255)!=0;
    if(x>=end || c->x!=(uint16_t)x) return 0;
    while(x<end) {
        unsigned value=w->fields[entry==0x9b7a?14:entry==0x88fa?4:0][x],cost;
        unsigned sum=0,old=0;
        if(entry==0x9b7a) cost=20+(value>=128);
        else if(entry==0x88fa) cost=value==0?13:value<24?24:value<200?33:32;
        else {
            if(value) {old=word(r,c->dp);sum=old+w->fields[4][x];cost=41+2*dp+(sum>65535?6+dp:0);}
            else cost=13;
        }
        cost+=w->huge?0:3;cost-=x+1==end;
        if(cost>max_cycles-cycles) break;
        if(entry==0x9b7a) {
            value=value>=128?255:value*2;w->fields[3][x]=(uint8_t)value;
            c->a=(c->a&0xff00)|value;
        } else if(entry==0x88fa) {
            if(value>=24) {
                unsigned decrement=value>=200?34:24,result=value-decrement;
                c->v=((value^decrement)&(value^result)&128)!=0;value=result;
            } else value=0;
            w->fields[4][x]=(uint8_t)value;c->a=(c->a&0xff00)|value;
        } else {
            if(value) {
                put(r,c->dp,sum);if(sum>65535) put(r,c->dp+2,word(r,c->dp+2)+1);
                c->v=((~(old^w->fields[4][x]))&(old^sum)&32768)!=0;
                c->a=(uint16_t)sum;++c->y;
            } else c->a&=0xff00;
        }
        ++x;cycles+=cost;
    }
    if(!cycles) return 0;
    if(w->huge) w->field_scan=x;
    c->x=(uint16_t)x;c->c=c->z=x==end;c->n=!w->huge && ((uint16_t)(x-end)&32768)!=0;
    c->cyclesUsed=x==end?2:3;
    if(x==end) c->pc=entry==0x88fa?0x891e:entry==0x9b7a?0x9b8d:0xb697;
    return cycles;
}
/* Word fields retain the original branch order and deadline, including the
 * zero-clock full-index comparison used beyond 64 KiB on the largest map. */
static unsigned field_words(ScWorld *w,Interp816 *c,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_FIELD_WORD_REFERENCE");reference=e && *e=='1';}
    unsigned entry=c->pc,field=entry==0x9c22?15:7;
    if(reference || c->mf || (entry!=0x9c22 && entry!=0x8924) || (entry==0x9c22 && c->a)) return 0;
    unsigned x=w->colossal?w->field_scan:c->x,end=ScWorldFieldSizeWorld(w,field),cycles=0;
    if((x&1) || x>=end || c->x!=(uint16_t)x) return 0;
    while(x<end) {
        unsigned value=word(w->fields[field],x),cost,result=value;
        if(entry==0x9c22) cost=6+4+3;
        else if(!value) cost=6+3+4+3;
        else if(!(value&32768)) {
            result=value-1;cost=6+2+2+2+3+(result<200?3:2+3+3)+6+4+3;
            if(result>=200) result=200;
        } else {
            result=(value+1)&65535;cost=6+2+3+2+3+(result>=0xff38?3:2+3)+6+4+3;
            if(result<0xff38) result=0xff38;
        }
        cost+=3; /* BNE back to the next word. */
        if(w->colossal) cost-=3;
        if(x+2==end) --cost;
        if(cost>max_cycles-cycles) break;
        if(entry==0x9c22) put(w->fields[field],x,0);
        else {c->a=(uint16_t)result;if(value) put(w->fields[field],x,result);}
        x+=2;cycles+=cost;
    }
    if(!cycles) return 0;
    if(w->colossal) w->field_scan=x;
    c->x=(uint16_t)x;c->c=c->z=x==end;c->n=!w->colossal && ((uint16_t)(x-end)&32768)!=0;
    c->cyclesUsed=x==end?2:3;
    if(x==end) c->pc=entry==0x9c22?0x9c2d:0x894b;
    return cycles;
}
unsigned ScWorldGuestFieldStageStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    if(!w || !w->active) return 0;
    if(c->pc==0x8924) return field_words(w,c,max_cycles);
    if(c->pc==0x88fa || c->pc==0x9b7a || c->pc==0xb676) return field_sweep(w,c,r,max_cycles);
    return 0;
}
/* Common transport neighbour reads, including the original temporary byte
 * indexes and packed-coordinate seams. Boundary paths retain their existing
 * guest hooks. Restore PHP/PLP flags and stack shadows exactly. */
unsigned ScWorldGuestTransportNeighborStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_TRANSPORT_NEIGHBOR_REFERENCE");reference=e && *e=='1';}
    unsigned direction=c->a&255;
    if(reference || direction>3 || c->sp<2 || c->sp>0x1ffd) return 0;
    int x=r[0xb85],y=r[0xb86];
    if(w->huge) {x=lift(x,w->coord[2][0],256);y=lift(y,w->coord[2][1],256);}
    int nx=x+(direction==1)-(direction==3),ny=y+(direction==2)-(direction==0);
    if(!ScWorldContains(w,x,y) || !ScWorldContains(w,nx,ny)) return 0;
    /* The byte index wraps before the hooked map read reconstructs it. */
    unsigned offset=2*(ny*ScWorldWidth(w)+nx);
    static const unsigned huge_cost[]={58,65,70,70};
    unsigned cost=huge_cost[direction]+(w->huge?0:2);
    if(cost>max_cycles) return 0;
    r[c->sp]=interp816_getFlags(c);put(r,c->sp-2,0xb3a3);
    w->map_anchor=offset;put(r,0xb3f,nx*2);put(r,0xb3d,ny*32);
    c->a=ScWorldCell(w,nx,ny)&1023;c->x=(uint16_t)offset;c->y=(uint8_t)ny;
    c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;
    return cost;
}
unsigned ScWorldGuestDensityStageStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    switch(c->pc) {
    case 0x9af7:case 0x9b5a:return density_scan(w,c,r,rom,max_cycles);
    case 0x9b12:return density_owner(w,c,r,max_cycles);
    case 0x9b7a:return field_sweep(w,c,r,max_cycles);
    case 0x9c22:return field_words(w,c,max_cycles);
    case 0x9c3b:return land_value_call(w,c,r,max_cycles);
    default:return 0;
    }
}
static unsigned diffusion_field_span(ScWorld *w,Interp816 *c,uint8_t *r,unsigned budget) {
    if(!stencil_backend.data || diffusion_reference() || c->waiting || c->stopped ||
       c->dp<0x20 || c->dp>0x1fc0 || c->sp<0x100 || c->sp>0x1ffd ||
       (c->sp>=c->dp && c->sp-2<=c->dp+6)) return 0;
    unsigned entry=c->pc;
    if(entry!=0xa040 && entry!=0xa0c6) return 0;
    unsigned source=entry==0xa040?13:14,width=ScWorldFieldWidth(w,13),height=ScWorldFieldHeight(w,13);
    unsigned x=word(r,c->dp),y=word(r,c->dp+2),dp=(c->dp&255)!=0,cycles=0;
    if(x>=width || y>=height) return 0;
    const uint32_t *packed=stencil_backend.data(stencil_backend.context,w,source);
    if(!packed) return 0;
    bool deferred=field_shadow_deferred(c,6),row=false;
    unsigned last_x=x,last_y=y,last_p=0,cells=0;
    while(y<height) {
        unsigned index=y*width+x,p=packed[index],sum=p&2047;
        unsigned cell=24+2*dp+((p>>11)&511)+dp*((p>>20)&31);
        /* Cell ends at the original STA, then the exact coordinate-control
         * branch. Full cells are independent; their source stays immutable. */
        unsigned advance=20+2*dp;
        if(x+1==width) advance+=y+1<height?20+3*dp:15+2*dp;
        if(cell+advance>budget-cycles) break;
        unsigned value=sum/4;w->fields[source==13?14:13][index]=(uint8_t)(value>250?250:value);
        if(deferred) {
            last_x=x;last_y=y;last_p=p;row=++x==width;
            if(row) {++y;if(y<height)x=0;}
        } else {
            if(w->huge) coord_set(w,r,x*2,y*2);
            w->field_anchor[0]=index;put(r,0xb3f,x);put(r,0xb3d,y);
            put(r,c->sp-1,entry==0xa040?0xa049:0xa0cf);
            put(r,c->dp+4,sum);c->x=(uint16_t)index;

            c->v=((p>>25)&1)!=0;c->y=(uint16_t)y;
            /* The loop's INC is binary and preserves V. Its CMP/branches own
             * final carry/N/Z, rather than the clipped stencil's flags. */
            ++x;put(r,c->dp,x);c->a=(uint16_t)x;c->c=x>=width;nz(c,(uint16_t)(x-width),false);
            c->mf=false;c->pc=(uint16_t)entry;c->cyclesUsed=3;
            if(x==width) {
                ++y;put(r,c->dp+2,y);c->a=(uint16_t)y;c->c=y>=height;nz(c,(uint16_t)(y-height),false);
                if(y<height) {x=0;put(r,c->dp,0);c->cyclesUsed=(uint8_t)(4+dp);}
                else {c->pc=entry==0xa040?0xa0b3:0xa139;c->cyclesUsed=2;}
            }
        }
        cycles+=cell+advance;++cells;
    }
    if(deferred && cells) {
        unsigned index=last_y*width+last_x;
        if(w->huge)coord_set(w,r,last_x*2,last_y*2);
        w->field_anchor[0]=index;put(r,0xb3f,last_x);put(r,0xb3d,last_y);
        put(r,c->sp-1,entry==0xa040?0xa049:0xa0cf);put(r,c->dp+4,last_p&2047);
        c->x=(uint16_t)index;c->y=(uint16_t)last_y;c->v=((last_p>>25)&1)!=0;
        put(r,c->dp,x);put(r,c->dp+2,y);c->a=(uint16_t)(row?y:x);
        c->c=row?y>=height:x>=width;nz(c,(uint16_t)(row?y-height:x-width),false);
        c->mf=false;c->pc=y==height?(entry==0xa040?0xa0b3:0xa139):(uint16_t)entry;
        c->cyclesUsed=y==height?2:row?(uint8_t)(4+dp):3;
    }
    stencil_cells+=cells;
    return cycles;
}
unsigned ScWorldGuestSmoothingStageStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    switch(c->pc) {
    case 0xa040:case 0xa0c6: {
        unsigned cost=diffusion_field_span(w,c,r,max_cycles);
        return cost?cost:diffusion_cell(w,c,r,max_cycles);
    }
    case 0xa09f:case 0xa125:return spatial_advance(w,c,r,max_cycles);
    default:return 0;
    }
}
unsigned ScWorldGuestServiceStageStep(ScWorld *w,Interp816 *c,uint8_t *r,unsigned max_cycles) {
    switch(c->pc) {
    case 0xa164:case 0xa1e3: {
        unsigned cost=service_field_span(w,c,r,max_cycles);
        return cost?cost:service_cell(w,c,r,max_cycles);
    }
    case 0xa1a6:case 0xa225:return spatial_advance(w,c,r,max_cycles);
    case 0xa1bb:case 0xa23a:return service_copy(w,c,max_cycles);
    default:return 0;
    }
}
unsigned ScWorldGuestHouseArtStep(ScWorld *w,Interp816 *c,uint8_t *r,
    const uint8_t *rom,unsigned budget) {
    static int reference=-1;
    if(reference<0) {const char *e=getenv("SC_HOUSE_ART_REFERENCE");reference=e && *e=='1';}
    if(reference || !w || !w->active || !c || !r || !rom || c->k!=3 || c->db!=3 ||
       c->e || c->d || c->xf || c->waiting || c->stopped || c->nmiWanted || (c->irqWanted && !c->i) ||
       c->dp<0x1000 || c->dp>0x1ff0 || c->sp<0x1008 || c->sp>0x1ffb ||
       (c->sp+4>=c->dp && c->sp-1<=c->dp+10)) return 0;
    unsigned dp=(c->dp&255)!=0,cost,value;
    switch(c->pc) {
    case 0x980e:
        value=word(r,c->dp+2);cost=(value?9:10)+dp;
        if(cost>budget) return 0;
        c->mf=false;c->x=(uint16_t)value;nz(c,value,false);
        c->pc=value?0x9814:0x9846;c->cyclesUsed=value?2:3;return cost;
    case 0x9814:
        if(budget<6) return 0;
        put(r,c->sp-1,0x9816);c->sp-=2;c->pc=0x9468;c->cyclesUsed=6;return 6;
    case 0x9817:
        cost=29+3*dp;if(cost>budget) return 0;
        value=c->a&255;put(r,c->dp+4,value);put(r,c->dp+4,value*3);
        c->mf=false;c->a=2;c->c=c->v=c->n=c->z=false;
        put(r,c->sp-1,0x9828);c->sp-=2;c->pc=0x9035;c->cyclesUsed=6;return cost;
    case 0x9829: {
        /* The original selected lot is one of eight byte-coordinate
         * offsets. Unsupported scratch values retain the compatibility
         * path instead of treating inline table bytes as valid artwork. */
        unsigned lot=word(r,c->dp+2);
        cost=53+2*dp;if(c->mf || !lot || lot>8 || cost>budget) return 0;
        unsigned sum=c->a+0x89,carry=sum>65535;
        sum=(sum&65535)+word(r,c->dp+4)+carry;c->y=(uint16_t)sum;c->x=(uint16_t)lot;
        unsigned a=r[0xb86]+rom[0x19851+lot];
        unsigned high=(a&255)<<8;
        a=r[0xb85]+rom[0x19848+lot];
        c->a=(uint16_t)(high|(a&255));c->mf=true;c->c=a>255;
        c->v=((~(r[0xb85]^rom[0x19848+lot]))&(r[0xb85]^a)&128)!=0;
        put(r,c->sp-1,0x9845);c->sp-=2;c->pc=0x84c4;
        ScWorldGuestStep(w,c,r);
        c->sp+=2;c->pc=0x9846;c->cyclesUsed=6;return cost;
    }
    case 0x9846:
        if(budget<5) return 0;
        c->dp=(uint16_t)word(r,c->sp+1);c->sp+=2;nz(c,c->dp,false);
        c->pc=0x9847;c->cyclesUsed=5;
        if(budget<11) return 5;
        c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;return 11;
    case 0x9847:
        if(budget<6) return 0;
        c->pc=(uint16_t)(word(r,c->sp+1)+1);c->sp+=2;c->cyclesUsed=6;return 6;
    default:return 0;
    }
}
static unsigned native_kernel(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,unsigned max_cycles) {
    if(ScSweepOwns(c->pc)) {
        unsigned cost=ScSweepStep(w,c,r,rom,max_cycles);if(cost) return cost;
    }
    if(ScPostpassOwns(c->pc)) {
        unsigned cost=ScPostpassStep(w,c,r,max_cycles);if(cost) return cost;
    }
    if(ScZoningOwns(c->pc)) {
        unsigned cost=ScZoningControlStep(w,c,r,rom,max_cycles);
        if(cost) return cost;
    }
    if(c->pc>=0x980e && c->pc<=0x9847) {
        unsigned cost=ScWorldGuestHouseArtStep(w,c,r,rom,max_cycles);
        if(cost) return cost;
    }
    if(c->pc>=0x98b8 && c->pc<=0x99bf) {
        unsigned cost=ScWorldGuestZoneArtStep(w,c,r,rom,max_cycles);
        if(cost) return cost;
    }
    if(c->pc==0x9468) {
        unsigned cost=ScZoningQualityStep(w,c,r,max_cycles);
        if(cost) return cost;
    }
    if(ScZoningOwns(c->pc)) return ScZoningStep(w,c,r,rom,max_cycles);
    if(ScServiceOwns(c->pc)) {
        unsigned cost=ScServiceStep(w,c,r,rom,max_cycles);
        if(cost) return cost;
    }
    if(ScSmoothingOwns(c->pc)) {
        unsigned cost=ScSmoothingStep(w,c,r,rom,max_cycles);
        if(cost) return cost;
    }
    if(ScDensityOwns(c->pc)) {
        unsigned cost=ScDensityStep(w,c,r,rom,max_cycles);
        if(cost) return cost;
    }
    if(ScPowerTraversalOwns(c->pc) && c->pc!=0xaffd && c->pc!=0xb000 && c->pc!=0xb005 &&
       c->pc!=0xb00d && c->pc!=0xb036 && c->pc!=0xb0e5 && c->pc!=0xb05a &&
       c->pc!=0xb03d && c->pc!=0xb06c) return ScPowerTraversalStep(w,c,r,rom,max_cycles);
    if(ScLandOwns(c->pc) && c->pc!=0x9cdf && c->pc!=0x9d30 && c->pc!=0x9dc9)
        return ScLandStep(w,c,r,max_cycles);
    if(c->pc!=0xb370 && ScTransportOwns(c->pc)) return ScTransportStep(w,c,r,rom,max_cycles);
    if(ScInfrastructureOwns(c->pc)) {
        unsigned cost=ScWorldGuestInfrastructureStep(w,c,r,max_cycles);
        return cost?cost:ScInfrastructureStep(w,c,r,rom,max_cycles);
    }
    switch(c->pc) {
    case 0xb370:return ScWorldGuestTransportNeighborStep(w,c,r,max_cycles);
    case 0x8287:return coverage_clear(w,c,max_cycles);
    case 0x9ab2:case 0x9f7d:return coverage_pack(w,c,r,max_cycles);
    case 0x9c22:case 0x8924:return field_words(w,c,max_cycles);
    case 0x88fa:case 0x9b7a:case 0xb676:return field_sweep(w,c,r,max_cycles);
    case 0xaffd:case 0xb000:case 0xb005:case 0xb00d:case 0xb036:case 0xb0e5:case 0xb05a: {
        unsigned cost=power_visit(w,c,r,rom,max_cycles);
        return cost?cost:ScPowerTraversalStep(w,c,r,rom,max_cycles);
    }
    case 0x9a3e:return ScWorldGuestHousingStep(w,c,r,max_cycles);
    case 0x987f:return ScWorldGuestHouseSiteStep(w,c,r,rom,0x80000,max_cycles);
    case 0x9af7:case 0x9b5a:return density_scan(w,c,r,rom,max_cycles);
    case 0x9b12:return density_owner(w,c,r,max_cycles);
    case 0x82ac:return sweep_cell(w,c,r,rom,max_cycles);
    case 0x9940:case 0x9952:case 0x998b:return zone_replacement(w,c,r,rom,max_cycles);
    case 0x99d2:case 0x9a0e:case 0x9a31:return zone_score(w,c,r,max_cycles);
    case 0xa01d:return w->colossal?density_cell(w,c,r,max_cycles):spatial_advance(w,c,r,max_cycles);
    case 0x9cb0:case 0x9f47:case 0xa09f:case 0xa125:
    case 0xa1a6:case 0xa225:case 0xa288:case 0x9c40:return spatial_advance(w,c,r,max_cycles);
    case 0x9c3b:return land_value_call(w,c,r,max_cycles);
    case 0x9dc9:return land_value_return(w,c,r,max_cycles);
    case 0x9d30: {
        unsigned cost=land_value_resume(w,c,r,max_cycles);
        return cost?cost:ScLandStep(w,c,r,max_cycles);
    }
    case 0x9c77:return pollution_cell(w,c,r,max_cycles);
    case 0xa040:case 0xa0c6: {
        unsigned cost=diffusion_field_span(w,c,r,max_cycles);
        if(cost) return cost;
        cost=diffusion_cell(w,c,r,max_cycles);
        return cost?cost:diffusion_begin(w,c,r,max_cycles);
    }
    case 0xa042:case 0xa044:case 0xa045:case 0xa047:
    case 0xa0c8:case 0xa0ca:case 0xa0cb:case 0xa0cd:
        return diffusion_begin(w,c,r,max_cycles);
    case 0xa04a:case 0xa0d0: {
        unsigned cost=diffusion_resume(w,c,r,max_cycles);
        return cost?cost:diffusion_stencil(w,c,r,max_cycles);
    }
    case 0xa04c:case 0xa04e:case 0xa050:case 0xa052:case 0xa053:case 0xa057:case 0xa05a:
    case 0xa05c:case 0xa05d:case 0xa061:case 0xa063:case 0xa065:case 0xa067:case 0xa069:
    case 0xa06a:case 0xa06e:case 0xa070:case 0xa072:case 0xa075:case 0xa077:case 0xa078:
    case 0xa07c:case 0xa07e:case 0xa080:case 0xa081:case 0xa085:case 0xa087:case 0xa089:
    case 0xa0d2:case 0xa0d4:case 0xa0d6:case 0xa0d8:case 0xa0d9:case 0xa0dd:case 0xa0e0:
    case 0xa0e2:case 0xa0e3:case 0xa0e7:case 0xa0e9:case 0xa0eb:case 0xa0ed:case 0xa0ef:
    case 0xa0f0:case 0xa0f4:case 0xa0f6:case 0xa0f8:case 0xa0fb:case 0xa0fd:case 0xa0fe:
    case 0xa102:case 0xa104:case 0xa106:case 0xa107:case 0xa10b:case 0xa10d:case 0xa10f:
        return diffusion_stencil(w,c,r,max_cycles);
    case 0xa08b:case 0xa08d:case 0xa08f:case 0xa090:case 0xa091:
    case 0xa094:case 0xa096:case 0xa099:case 0xa09b:
    case 0xa111:case 0xa113:case 0xa115:case 0xa116:case 0xa117:
    case 0xa11a:case 0xa11c:case 0xa11f:case 0xa121:return diffusion_finish(w,c,r,max_cycles);
    case 0x9eb0: {
        unsigned gpu=crime_field_span(w,c,r,max_cycles);if(gpu) return gpu;
        unsigned cost=crime_cell(w,c,r,max_cycles);
        return cost?cost:vacant_crime_cell(w,c,r,max_cycles);
    }
    case 0x9fb7:return density_cell(w,c,r,max_cycles);
    case 0xb06c: {
        unsigned cost=power_neighbor(w,c,r,rom,max_cycles);
        return cost?cost:ScPowerTraversalStep(w,c,r,rom,max_cycles);
    }
    case 0xa164:case 0xa1e3: {
        unsigned cost=service_field_span(w,c,r,max_cycles);
        return cost?cost:service_cell(w,c,r,max_cycles);
    }
    case 0xa25c: {
        unsigned cost=terrain_quality_span(w,c,r,max_cycles);
        return cost?cost:terrain_quality_cell(w,c,r,max_cycles);
    }
    case 0xa1bb:case 0xa23a:return service_copy(w,c,max_cycles);
    case 0xb03d: {
        unsigned cost=power_search(w,c,r,rom,max_cycles);
        return cost?cost:ScPowerTraversalStep(w,c,r,rom,max_cycles);
    }
    case 0x9cdf: {
        unsigned cost=empty_terrain_cell(w,c,r,max_cycles);
        if(!cost) cost=land_value_cell(w,c,r,max_cycles);
        if(!cost) cost=land_value_begin(w,c,r,max_cycles);
        return cost?cost:ScLandStep(w,c,r,max_cycles);
    }
    default:return 0;
    }
}
unsigned ScWorldGuestKernelStep(ScWorld *w,Interp816 *c,uint8_t *r,
    const uint8_t *rom,size_t size,unsigned max_cycles) {
    static int layout=-1;static unsigned layout_samples,diffusion_layout_samples,field_layout_samples,crime_layout_samples;
    if(layout<0) {const char *e=getenv("SC_KERNEL_LAYOUT");layout=e && *e=='1';}
    if(layout && c->k==3 && (c->pc==0x9eb0 || c->pc==0x9f47) && crime_layout_samples++<16)
        fprintf(stderr,"[crime layout] pc=%x dp=%x sp=%x db=%x mf=%d xf=%d wait=%d stop=%d budget=%u xy=%u,%u p=%x\n",
            c->pc,c->dp,c->sp,c->db,c->mf,c->xf,c->waiting,c->stopped,max_cycles,word(r,c->dp+8),word(r,c->dp+10),interp816_getFlags(c));
    if(layout && c->k==3 && (c->pc==0x9c3b || c->pc==0x9cdf || c->pc==0x9d30) && layout_samples++<24)
        fprintf(stderr,"[kernel layout] pc=%x dp=%x sp=%x db=%x mf=%d xf=%d wait=%d stop=%d budget=%u xy=%u,%u p=%x\n",c->pc,c->dp,c->sp,c->db,c->mf,c->xf,c->waiting,c->stopped,max_cycles,word(r,c->dp+8),word(r,c->dp+10),interp816_getFlags(c));
    if(layout && c->k==3 && (c->pc==0xa040 || c->pc==0xa09f) && diffusion_layout_samples++<12)
        fprintf(stderr,"[diffusion layout] pc=%x dp=%x sp=%x db=%x mf=%d xf=%d active=%d rom=%zu budget=%u xy=%u,%u p=%x\n",
            c->pc,c->dp,c->sp,c->db,c->mf,c->xf,w->active,size,max_cycles,word(r,c->dp),word(r,c->dp+2),interp816_getFlags(c));
    if(layout && c->k==3 && c->pc==0xb676 && field_layout_samples++<12)
        fprintf(stderr,"[field layout] pc=%x dp=%x sp=%x db=%x mf=%d xf=%d active=%d budget=%u index=%u/%u flags=%x\n",
            c->pc,c->dp,c->sp,c->db,c->mf,c->xf,w->active,max_cycles,c->x,w->field_scan,interp816_getFlags(c));
    if(!w || !rom || size!=0x80000 || c->k!=3 || c->db!=3 ||
        c->e || c->d || c->xf || c->nmiWanted || (c->irqWanted && !c->i) || !max_cycles) return 0;
    if(ScPostpassOwns(c->pc)) {
        unsigned cost=ScPostpassStep(w,c,r,max_cycles);if(cost) return cost;
    }
    if(ScSweepOwns(c->pc)) {
        unsigned cost=ScSweepStep(w,c,r,rom,max_cycles);if(cost) return cost;
    }
    if(!w->active) return 0;
    if(max_cycles<32) {
        if(c->pc>=0x980e && c->pc<=0x9847) {
            unsigned house=ScWorldGuestHouseArtStep(w,c,r,rom,max_cycles);
            if(house) return house;
        }
        if(c->pc>=0x98b8 && c->pc<=0x99bf) {
            unsigned art=ScWorldGuestZoneArtStep(w,c,r,rom,max_cycles);
            if(art) return art;
        }
        if(ScZoningOwns(c->pc)) return ScZoningStep(w,c,r,rom,max_cycles);
        if(ScServiceOwns(c->pc)) {
            unsigned cost=ScServiceStep(w,c,r,rom,max_cycles);
            if(cost) return cost;
        }
        if(ScSmoothingOwns(c->pc)) {
            unsigned cost=ScSmoothingStep(w,c,r,rom,max_cycles);
            if(cost) return cost;
        }
        if(ScDensityOwns(c->pc)) return ScDensityStep(w,c,r,rom,max_cycles);
        if(ScPowerTraversalOwns(c->pc)) return ScPowerTraversalStep(w,c,r,rom,max_cycles);
        if(ScLandOwns(c->pc)) return ScLandStep(w,c,r,max_cycles);
        if(ScInfrastructureOwns(c->pc)) return ScInfrastructureStep(w,c,r,rom,max_cycles);
        if(ScTransportOwns(c->pc)) return ScTransportStep(w,c,r,rom,max_cycles);
        if((c->pc>=0xa040 && c->pc<0xa04a) || (c->pc>=0xa0c6 && c->pc<0xa0d0))
            return diffusion_begin(w,c,r,max_cycles);
        if((c->pc>=0xa04a && c->pc<=0xa089) || (c->pc>=0xa0d0 && c->pc<=0xa10f))
            return diffusion_stencil(w,c,r,max_cycles);
        return 0;
    }
    unsigned direct=native_kernel(w,c,r,rom,max_cycles);
    if(direct) return direct;
    unsigned end;
    switch(c->pc) {
    case 0x9cdf:end=0x9dc9;break;
    case 0x9c77:end=0x9cb0;break;
    case 0x9eb0:end=0x9f47;break;
    case 0xa040:end=0xa09f;break;
    case 0xa0c6:end=0xa125;break;
    default:return 0;
    }
    KernelBus b={w,c,r,rom,{0}};
    void *saved_mem=c->mem;
    Interp816ReadHandler saved_read=c->read;Interp816WriteHandler saved_write=c->write;
    Interp816ReadWordHandler saved_read_word=c->read_word;
    Interp816WriteWordHandler saved_write_word=c->write_word;
    c->mem=&b;c->read=kernel_read;c->write=kernel_write;c->read_word=NULL;c->write_word=NULL;
    unsigned cycles=0;
    while(c->pc!=end && cycles+12<max_cycles) {
        ScWorldGuestStepPrepared(w,c,r);
        if(c->pc==end) break;
        /* A compatibility span can reach a resumable C helper after its
         * initial instruction. Keep those helpers native inside this loop,
         * rather than dispatching their original opcodes through KernelBus.
         * Limit continuation to helpers with exact deadline preflight. */
        if(spatial_continuation_enabled() && c->k==3 && c->db==3 && !c->e && !c->d && !c->xf &&
           !c->waiting && !c->stopped && !c->nmiWanted && !(c->irqWanted && !c->i) &&
           (ScLandOwns(c->pc) || ScTransportOwns(c->pc) || c->pc==0x9d30 ||
            (c->pc>=0xa040 && c->pc<=0xa09b) || (c->pc>=0xa0c6 && c->pc<=0xa121))) {
            unsigned native=native_kernel(w,c,r,rom,max_cycles-cycles);
            if(native) {cycles+=native;continue;}
        }
        if(((c->pc==0xa04a || c->pc==0xa0d0) && max_cycles-cycles>160) ||
            (c->pc==0x9dca && max_cycles-cycles>192)) {
            unsigned fast=ScWorldGuestFastStep(w,c,r,rom,size);
            if(fast) {cycles+=fast;continue;}
        }
        ScWorldGuestBeginPrepared(&b.guest,w,c,rom,size);
        unsigned program_pc=c->pc,program_bank=c->k;
        unsigned program=ScProgramStep(c);
        if(!program && kernel_interpreter_profile && program_bank==3) ++kernel_interpreter_ops[program_pc];
        cycles+=program?program:ScProgramExecute(c);
    }
    c->mem=saved_mem;c->read=saved_read;c->write=saved_write;
    c->read_word=saved_read_word;c->write_word=saved_write_word;
    return cycles;
}
unsigned ScWorldGuestBatchStep(ScWorld *w,Interp816 *c,uint8_t *r,
    const uint8_t *rom,size_t size,unsigned max_cycles) {
    unsigned cycles=ScWorldGuestKernelStep(w,c,r,rom,size,max_cycles);
    if(!cycles) return 0;
    /* No instruction dispatch, ROM reads or guest bus calls between C cells.
     * The caller's beam budget remains the hard limit for the entire span. */
    while(cycles+32<max_cycles) {
        /* A connected C call or return can enter another smoothing pass. Let main
         * execute its submission/invalidation hook before batching that pass. */
        if(c->k==3 && (c->pc==0xa02f || c->pc==0xa0b5 || c->pc==0xa14d || c->pc==0xa1cc)) break;
        unsigned cost=native_kernel(w,c,r,rom,max_cycles-cycles);
        if(!cost) break;
        cycles+=cost;
    }
    return cycles;
}

/* C instruction tier for converted families at scanline/IRQ boundaries. This
 * is deliberately independent of bounded batching: the original CPU executes
 * one indivisible instruction even when the beam deadline is already due.
 * Full-cell fusions are disabled here, so timing and interrupt latency match. */
unsigned ScWorldGuestInstructionStep(ScWorld *w,Interp816 *c,uint8_t *r,
    const uint8_t *rom,size_t size) {
    if(!w || !c || !r || !rom || size!=0x80000 || c->nmiWanted ||
       (c->irqWanted && !c->i)) return 0;
    if(c->k==1 && ScTileLookupOwns(c->pc))
        return ScTileLookupInstructionStep(w,c,r,rom,size);
    if(c->k!=3) return 0;
    if(ScPostpassOwns(c->pc)) return ScPostpassInstructionStep(w,c,r);
    if(ScSweepOwns(c->pc)) return ScSweepInstructionStep(w,c,r,rom);
    if(!w->active) return 0;
    if(ScZoningOwns(c->pc)) return ScZoningInstructionStep(w,c,r,rom);
    if(ScServiceOwns(c->pc)) return ScServiceInstructionStep(w,c,r,rom);
    if(ScSmoothingOwns(c->pc)) return ScSmoothingInstructionStep(w,c,r,rom);
    if(ScPowerTraversalOwns(c->pc)) return ScPowerTraversalInstructionStep(w,c,r,rom);
    if(ScDensityOwns(c->pc)) return ScDensityInstructionStep(w,c,r,rom);
    if(ScLandOwns(c->pc)) return ScLandInstructionStep(w,c,r);
    if(ScInfrastructureOwns(c->pc)) return ScInfrastructureInstructionStep(w,c,r,rom);
    if(ScTransportOwns(c->pc)) return ScTransportInstructionStep(w,c,r,rom);
    return 0;
}
