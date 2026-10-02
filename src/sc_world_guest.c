#include "sc_world_guest.h"
#include "snes/interp816.h"
#include <string.h>

static unsigned word(const uint8_t *r,unsigned p) { return r[p]|((unsigned)r[p+1]<<8); }
static void put(uint8_t *r,unsigned p,unsigned v) { r[p]=(uint8_t)v; r[p+1]=(uint8_t)(v>>8); }
static void nz(Interp816 *c,unsigned v,bool byte) {
    c->z=(v&(byte?255:65535))==0; c->n=(v&(byte?128:32768))!=0;
}
static unsigned big_dimension(uint32_t pc);
static unsigned dimension(const ScWorld *w,uint32_t pc);
unsigned ScWorldGuestMasterCycles(const ScWorld *w,uint32_t pc,
                                  unsigned master,unsigned *remainder) {
    if (!w || !w->active || (pc>>16)!=3) return master;
    unsigned p=pc&65535;
    /* Verified US spatial loops: tile sweep and transport, traffic decay,
     * zone handlers, density/land value/crime, coverage and power flood.
     * Exclude the simulation dispatcher ($8000), demand ($90a7), budgets,
     * disasters and the redraw wait ($ae1c), which must keep normal time. */
    bool spatial=(p>=0x821d && p<0x88b4) ||
        (p>=0x88f3 && p<0x90a7) || (p>=0x90c5 && p<0xa2f5) ||
        (p>=0xa493 && p<0xae1c) || (p>=0xafb0 && p<0xb42f);
    if (!spatial) return master;
    unsigned area=ScWorldCells(w)/12000;
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
unsigned ScWorldGuestFastStep(ScWorld *w,Interp816 *c,uint8_t *r,const uint8_t *rom,size_t size) {
    if (!w || !w->active || c->k!=3 || c->db!=3 || c->xf || c->d || c->e ||
        c->nmiWanted || (c->irqWanted && !c->i)) return 0;
    if(c->pc==0x9dca && (c->a&1023)<0x28) {
        unsigned tile=c->a&1023,dp=(c->dp&255)!=0;
        c->mf=false;c->a=tile;nz(c,tile,false);
        if(tile) {
            unsigned old=word(r,c->dp+0x22),value=old+15;
            c->a=(uint16_t)value;c->c=value>65535;
            c->v=((old^value)&(15^value)&32768)!=0;
            put(r,c->dp+0x22,value);nz(c,value,false);
        }
        c->pc=0x9e0b;c->cyclesUsed=3;
        return tile?27+2*dp:9;
    }
    if((c->pc==0x82ae || c->pc==0x9af7) && c->sp && rom && size>=0x188eb) {
        bool sweep=c->pc==0x82ae;
        unsigned x=sweep?(w->huge?w->scan_x:r[0xb85]):word(r,c->dp+0x10);
        unsigned y=sweep?(w->huge?w->scan_y:r[0xb86]):word(r,c->dp+0x12);
        if(!ScWorldContains(w,x,y)) return 0;
        unsigned offset=2*(y*ScWorldWidth(w)+x),raw=ScWorldCell(w,x,y),tile=raw&1023;
        unsigned property=rom[0x184eb+tile],dp=(c->dp&255)!=0;
        unsigned crossed=tile>=0x15;
        if(sweep?(!c->mf && tile<64 && !(property&0x71) && word(r,c->dp)==(uint16_t)offset):!(property&1)) {
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
    if(x>=width || y>=height || c->x!=i) return 0;
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
    case 0x8297:w->scan_x=w->scan_y=0;coord_set(w,r,0,0);break;
    case 0x82ae:coord_set(w,r,w->scan_x,w->scan_y);break;
    case 0x9af7:coord_set(w,r,word(r,c->dp+0x10),word(r,c->dp+0x12));break;
    case 0x9b5c: {
        unsigned x=word(r,c->dp+0x10)+1,y=word(r,c->dp+0x12);
        if(x==480) {x=0;++y;}
        put(r,c->dp+0x10,x);put(r,c->dp+0x12,y);
        coord_set(w,r,x,y);
        c->pc=y<400?0x9af7:0x9b6c;return true;
    }
    case 0x9cdf:case 0x9eb0:case 0x9c77:
        coord_set(w,r,r[c->dp+8]*2,r[c->dp+10]*2);break;
    case 0x9b6f: {
        uint32_t count=word(r,c->dp+8)|((uint32_t)word(r,c->dp+10)<<16);
        uint32_t x=word(r,c->dp)|((uint32_t)word(r,c->dp+2)<<16);
        uint32_t y=word(r,c->dp+4)|((uint32_t)word(r,c->dp+6)<<16);
        if(count) {w->center_x=x/count;w->center_y=y/count;w->center_valid=true;}
        break;
    }
    case 0x9ba0:c->a=(c->a&0xff00)|(w->center_x&255);break;
    case 0x9baf:c->a=(c->a&0xff00)|(w->center_y&255);break;
    case 0x9bb6:w->center_x=240;w->center_y=200;w->center_valid=true;break;
    case 0x9e61: {
        int dx=(c->a&255)-(w->center_valid?w->center_x/2:120);
        int dy=(c->a>>8)-(w->center_valid?w->center_y/2:100);
        if(dx<0) dx=-dx;if(dy<0) dy=-dy;
        unsigned distance=dx+dy;c->c=distance>=32;if(distance>32) distance=32;
        c->a=((dx&255)<<8)|distance;c->mf=true;nz(c,distance,true);c->pc=0x9e8d;return true;
    }
    case 0xb37e:c->n=lift(c->y,w->coord[2][1],256)<0;c->pc+=2;return true;
    case 0xb39a:c->n=lift(c->x,w->coord[2][0],256)<0;c->pc+=2;return true;
    case 0x8343:
        if(++w->scan_x<480) c->pc=0x82ac;
        else {w->scan_x=0;if(++w->scan_y<400) c->pc=0x82ac;else c->pc=0x835d;}
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
        unsigned count=word(r,0xc57);c->c=count>=20000;c->z=count==20000;c->n=false;
        if(count<20000) {
            ++count;put(w->fields[17],2*count,lift(r[0xb85],w->coord[2][0],256));
            put(w->fields[18],2*count,lift(r[0xb86],w->coord[2][1],256));put(r,0xc57,count);
        }
        c->x=count;c->mf=false;c->pc=0xb0dc;return true;
    }
    default:break;
    }
    return false;
}
void ScWorldGuestStep(ScWorld *w,Interp816 *c,uint8_t *r) {
    if (!w->active) return;
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
        for (unsigned i=0;i<ScWorldCells(w);++i) {
            unsigned tile=word(w->tiles,i*2)&0x7fff;
            if (w->fields[5][i/8]&(128>>(i&7))) tile|=0x8000;
            put(w->tiles,i*2,tile);
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
    else if (c->pc==0xb719) { x=word(r,0xa61); y=r[0xa5f]; }
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
static unsigned dimension(const ScWorld *w,uint32_t pc) {
    unsigned v=big_dimension(pc);
    if(!w->huge) return v;
    switch(v) {
    case 240:return 480;case 200:return 400;case 239:return 479;case 199:return 399;
    case 238:return 478;case 198:return 398;
    case 215:return 455;case 178:return 378;case 210:return 450;case 174:return 374;case 172:return 372;
    case 120:return 240;case 100:return 200;case 119:return 239;case 99:return 199;
    case 60:return 120;case 50:return 100;case 59:return 119;case 49:return 99;
    case 30:return 60;case 25:return 50;case 29:return 59;case 24:return 49;
    case 750:case 1500:case 3000:case 6000:case 12000:return v*4;
    default:return v;
    }
}
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
    if (c->k==3 && c->pc>=0xcf80) return; /* original-size native save codecs */
    size_t p=(size_t)(c->k&0x7f)*32768+c->pc-0x8000;
    if (p+3>=size) return;
    unsigned op=rom[p],base=word(rom,(unsigned)p+1),index=0,bank=c->db;
    uint32_t pc=((uint32_t)c->k<<16)|c->pc;
    unsigned dim=(op==0xa9 || op==0xc9 || op==0xe0 || op==0xc0)?dimension(w,pc):0;
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
            (unsigned)logical+g->bytes<=(ScWorldCells(w)*2)) g->data=w->tiles+logical;
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
        if(w->huge && (displacement==(int)(layout->width*layout->element_bytes) || displacement==-(int)(layout->width*layout->element_bytes))) displacement*=2;
        int logical=(int)index+displacement;
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
    if (g->data) g->data[a-g->address]=v;
    return true;
}
