// Live scanline terrain, repaired city cells, warnings and sprite composition.
StructuredBuffer<uint> image : register(t0, space0);
struct TerrainTile {uint base,roof,attributes,warning,expected,staged;};
StructuredBuffer<TerrainTile> tiles : register(t1, space0);
StructuredBuffer<uint> palette : register(t2, space0);
struct Row {
 uint phase,core_x,main_mask,sub_mask,window_main,window_sub,windows,logic,bounds;
 uint math,control,fixed_color;
};
StructuredBuffer<uint> rows : register(t3, space0);
StructuredBuffer<uint2> overlays : register(t4, space0);
StructuredBuffer<uint> native_rows : register(t5, space0);
StructuredBuffer<uint> resources : register(t6, space0);
StructuredBuffer<uint> city : register(t7, space0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> output_image : register(u0, space1);
cbuffer Dimensions : register(b0, space2) {
 uint width,height,stride,unused;
 uint output_width,output_height,origin_y,reserved_dimension;
};
uint fractional_step(uint fraction,uint step) {
 return fraction*(step>>16)+((fraction*(step&65535))>>16);
}
uint decode(uint p,uint bit) {
 return ((p>>bit)&1)|(((p>>(bit+8))&1)<<1)|(((p>>(bit+16))&1)<<2)|(((p>>(bit+24))&1)<<3);
}
uint resource_word(uint snapshot,uint at) {
 return (resources[snapshot+((at&32767)>>1)]>>((at&1)*16))&65535;
}
uint city_cell(uint at) {
 uint raw=(city[at/2]>>((at&1)*16))&65535;
 return (raw&1023)<958?raw:0xffffffff;
}
uint captured_sub_bg(uint at,uint x) {
 if(!(rows[at+9]&262144)) return rows[at+44+x];
 uint sx=(x+rows[at+44])&1023,sy=rows[at+45],size=rows[at+48];
 uint adr=rows[at+46]+((sy>>3)&31)*32+((sx>>3)&31);
 if((sx&256) && (size&1)) adr+=0x400;
 if((sy&256) && (size&2)) adr+=(size&1)?0x800:0x400;
 uint snapshot=rows[at+300],word=resource_word(snapshot,adr),dy=(word&0x8000)?7-(sy&7):sy&7;
 uint planes=resource_word(snapshot,rows[at+47]+(word&1023)*8+dy);
 uint ci=decode(planes,(word&0x4000)?sx&7:7-(sx&7));
 return ci?ci+((word>>10)&7)*4:0;
}
uint captured_object(uint at,uint x,uint world_y) {
 uint header=rows[at+307];
 if(header==0xffffffff) return 0;
 uint bucket=x/32;
 if(bucket>=city[header]) return 0;
 if(rows[at+9]&524288) bucket+=((world_y>>8)&255)/32*city[header];
 uint record=city[header+1+bucket*2],count=city[header+2+bucket*2],result=0;
 [loop] for(uint i=0;i<count;++i,record+=6) {
  int dx=int(x)-int(city[record]);uint size=city[record+1];
  if(int(x)<int(city[record+5]) || dx<0 || uint(dx)>=size) continue;
  uint y=city[record+2],attr=city[record+3];
  if(rows[at+9]&524288) {
   int dy=(int(world_y)>>8)-int(y);
   if(attr&65536) dy&=255;
   if(dy<0 || uint(dy)>=size) continue;
   y=uint(dy);
  }
  if(attr&0x4000) dx=int(size)-1-dx;
  if(attr&0x8000) y=size-1-y;
  uint tile=(((attr&240)+(y/8)*16)&255)|(((attr&15)+uint(dx)/8)&15);
  uint adr=city[record+4]+tile*16+(y&7),snapshot=rows[at+300];
  uint ci=decode(resource_word(snapshot,adr)|(resource_word(snapshot,adr+8)<<16),7-(uint(dx)&7));
  if(ci) result=(ci+128+((attr>>9)&7)*16)|(((attr>>12)&3)<<8);
 }
 return result;
}
TerrainTile resolve_tile(TerrainTile t,uint at,uint column,uint world_y) {
 if(rows[at+9]&32768) {
  t.base=city_cell(rows[at+304]+column+1);
  t.roof=city_cell(rows[at+305]+column+2);
  t.warning=city_cell(rows[at+306]+column);
 }
 uint snapshot=rows[at+300],chr=rows[at+301],warning_chr=rows[at+302],y=world_y&7;
 uint base=t.base==0xffffffff?0:resources[t.base&1023]&65535;
 uint roof=t.roof==0xffffffff?0x2300:resources[t.roof&1023]>>16;
 uint owner=t.warning,cell=owner&1023;
 bool warning=owner!=0xffffffff && !(owner&0x8000) && (resources[1024+cell]&1) && cell!=0x27c && cell!=0x28c;
 t.expected=base|(roof<<16);
 t.attributes|=((base>>10)&7)*16|((((roof>>10)&7)*16)<<8)|
  ((base&0x4000)?1u<<16:0)|((roof&0x4000)?1u<<17:0)|(warning?0x80000:0);
 uint dy=(base&0x8000)?7-y:y,adr=chr+(base&1023)*16+dy;
 t.base=t.base==0xffffffff?0:resource_word(snapshot,adr)|(resource_word(snapshot,adr+8)<<16);
 dy=(roof&0x8000)?7-y:y;adr=chr+(roof&1023)*16+dy;
 t.roof=(roof&1023)==0x300?0:resource_word(snapshot,adr)|(resource_word(snapshot,adr+8)<<16);
 adr=warning_chr+0x376*16+y;
 t.warning=warning?resource_word(snapshot,adr)|(resource_word(snapshot,adr+8)<<16):0;
 return t;
}
uint2 native_tile(uint nr,uint at,uint column,uint layer) {
 uint nt=nr+4+layer*66;
 if(!(native_rows[nr+3]&4)) return uint2(native_rows[nt+column*2],native_rows[nt+column*2+1]);
 uint sx=(native_rows[nt]+column*8)&1023,sy=native_rows[nt+1],size=native_rows[nt+4];
 uint map=native_rows[nt+2]+((sy>>3)&31)*32+((sx>>3)&31);
 if((sx&256) && (size&1)) map+=0x400;
 if((sy&256) && (size&2)) map+=(size&1)?0x800:0x400;
 uint snapshot=rows[at+300],word=resource_word(snapshot,map),depth=layer==2?2:4;
 uint dy=(word&0x8000)?7-(sy&7):sy&7,adr=native_rows[nt+3]+(word&1023)*(depth*4)+dy;
 uint planes=resource_word(snapshot,adr);
 if(depth==4) planes|=resource_word(snapshot,adr+8)<<16;
 return uint2(planes,word);
}
uint native_object(uint nr,uint at,uint x) {
 uint count=(native_rows[nr+3]>>8)&127;
 while(count) {
  uint i=--count,base=nr+4+(i/30)*66+5+(i%30)*2;
  uint position=native_rows[base],source=native_rows[base+1];
  int dx=int(x)-(int(position<<16)>>16);
  if(dx<int((source>>16)&15) || dx>=int((source>>20)&15)) continue;
  uint adr=source&32767,snapshot=rows[at+300];
  uint planes=resource_word(snapshot,adr)|(resource_word(snapshot,adr+8)<<16);
  uint pixel=decode(planes,(source&0x8000)?uint(dx):7-uint(dx));
  if(pixel) {pixel+=(position>>16);return (pixel&255)|((pixel>>12)<<8);}
 }
 return 0;
}
uint2 native_sample(uint nr,uint at,uint edge,uint layer) {
 uint nx=edge+native_rows[nr+layer];uint2 t=native_tile(nr,at,nx/8,layer);
 uint attr=t.y,pixel=decode(t.x,(attr&0x4000)?nx&7:7-(nx&7));
 if(pixel) pixel+=((attr>>10)&7)*(layer==2?4:16);
 bool high=(attr&0x2000)!=0;
 uint rank=layer==0?(high?12:8):layer==1?(high?11:7):high?((native_rows[nr+3]&1)?15:3):1;
 return uint2(pixel,rank);
}
bool window_contains(Row r,uint layer,int x) {
 uint flags=(r.windows>>(layer*4))&15;
 bool a=(x>=int(r.bounds&255) && x<=int((r.bounds>>8)&255))!=((flags&1)!=0);
 bool b=(x>=int((r.bounds>>16)&255) && x<=int(r.bounds>>24))!=((flags&4)!=0);
 if(!(flags&2)) return (flags&8)?b:false;
 if(!(flags&8)) return a;
 uint op=(r.logic>>(layer*2))&3;
 return op==0?a||b:op==1?a&&b:op==2?a!=b:a==b;
}
[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
 if(id.x>=output_width || id.y>=output_height) return;
 uint2 position=uint2((2*id.x+1)*width-1,(2*id.y+1)*height-1);
 uint2 canvas=position/uint2(2*output_width,2*output_height);
 uint2 fraction=uint2(output_width==width?0:(position.x%(2*output_width))*65536/(2*output_width),
     output_height==height?0:(position.y%(2*output_height))*65536/(2*output_height));
 uint c=image[canvas.y*width+canvas.x];
 if(c==0x01000000 || c==0x02000000 || (c>>24)==3) {
  bool relocated=(c>>24)==3,native_pixel=c==0x02000000 || relocated;
  uint source_y=relocated?(c>>8)&4095:canvas.y;
  uint world_y=rows[source_y*311+303];
  bool sharp=c==0x01000000 && (rows[source_y*311+9]&1081344)==1081344;
  if(sharp) {
   uint base_at=source_y*311,step=rows[base_at+308];
   uint phase=(uint(int(source_y)-int(origin_y))*step)&65535;
   uint delta=(phase+fractional_step(fraction.y,step))>>16;
   int target=(int(world_y)>>8)+int(delta);
   int chr=int(world_y&7)+int(delta);
   uint original_y=source_y,original_world=world_y;
   [loop] for(uint next=original_y+1;chr>=8 && next<height && next<=original_y+8;++next) {
    uint candidate=next*311;
    if((rows[candidate+9]&1081344)!=1081344) break;
    int candidate_chr=int(rows[candidate+303]&7)+target-(int(rows[candidate+303])>>8);
    if(candidate_chr>=0 && candidate_chr<8) {source_y=next;chr=candidate_chr;break;}
   }
   if(chr>=8) {target=int(original_world)>>8;chr=int(original_world&7);}
   world_y=uint(chr)|(uint(target)<<8);
  }
  uint at=source_y*311;Row r;
  r.phase=rows[at];r.core_x=rows[at+1];r.main_mask=rows[at+2];r.sub_mask=rows[at+3];
  r.window_main=rows[at+4];r.window_sub=rows[at+5];r.windows=rows[at+6];
  r.logic=rows[at+7];r.bounds=rows[at+8];r.math=rows[at+9];r.control=rows[at+10];r.fixed_color=rows[at+11];
  uint source_x=relocated?r.core_x+(c&255):canvas.x;
  uint virtual_x=(r.math&1048576)?((source_x*rows[at+308]+rows[at+309]+
      (sharp?fractional_step(fraction.x,rows[at+308]):0))>>16):source_x;
  uint x=virtual_x+r.phase;
  TerrainTile t;
  if(unused&1) {
   uint first=(r.core_x+r.phase)/8,column=x/8;
   t.base=t.roof=t.attributes=t.warning=t.expected=t.staged=0;
   if(column>=first && column<first+34) t=tiles[source_y*34+column-first];
  } else t=tiles[source_y*stride+x/8];
  if(r.math&16384) t=resolve_tile(t,at,x/8,world_y);
  uint b=decode(t.base,(t.attributes&(1<<16))?x&7:7-(x&7));
  uint roof=decode(t.roof,(t.attributes&(1<<17))?x&7:7-(x&7));
  uint index=roof?roof+((t.attributes>>8)&127):b?b+(t.attributes&127):0;
  int edge=clamp(int(source_x)-int(r.core_x),0,255);
  int local=int(source_x)-int(r.core_x);
  uint2 overlay=uint2(0,0);
  if(unused&2) {
   if(local>=0 && local<256) overlay=overlays[source_y*256+uint(local)];
  } else overlay=overlays[source_y*width+source_x];
  if(!native_pixel && (r.math&1048576))overlay.y=0; /* fixed UI is composed separately */
  if((native_pixel || !(r.math&1048576)) && local>=0 && local<256 && (native_rows[source_y*202+3]&8))
   overlay.y=(overlay.y&~4095u)|native_object(source_y*202,at,uint(local));
  if((r.math&65536) && (!(r.math&1048576) || !native_pixel) && !((r.math&131072) && local>=0 && local<256)) {
   overlay.y=captured_object(at,(r.math&1048576)?uint(int(virtual_x)+int(rows[at+310])):source_x,world_y)|
    (((r.math&4096) && ((r.math&8192) || local<56))?0x80000000:0);
  }
  if(!relocated && (r.math&1024) && local>=0 && local<256) {
   bool land=!(r.math&4096) || (!(r.math&8192) && local>=56);
   bool needs_power=land && (t.attributes&0x80000);
   uint nr=source_y*202,nx=uint(local)+native_rows[nr],bg=native_tile(nr,at,nx/8,0).y;
   bool clear=land && (t.attributes&0x200000) && bg==0x1376;
   uint base=t.staged&65535,staged_roof=t.staged>>16;
   bool bad=(r.math&2048) && land && (!(t.attributes&0x100000) || base!=(t.expected&65535) ||
       ((r.main_mask&1) && staged_roof!=(t.expected>>16) && !(staged_roof==0x1376 && needs_power)));
   if((t.attributes&0x40000) || needs_power || clear || bad) {
    native_pixel=false;overlay.x=0x80000000|0x40000000|((clear || bad)?0x20000000:0);
    if(!land) overlay.y|=0x80000000;
   }
  }
  if(native_pixel && (native_rows[source_y*202+3]&2)) r.logic=0;
  bool bgwin=window_contains(r,1,edge),win=window_contains(r,5,edge);
  uint warning=(overlay.y&0x80000000)?0:decode(t.warning,7-(x&7));
  if(warning) warning+=64;
  uint samples[2]={0,0},owners[2]={5,5};
  if(!native_pixel && (r.math&256)) {
   int local=int(source_x)-int(r.core_x);
   bool core=(r.math&512) && local>=0 && local<256;
   uint sub=index,priority=roof?11:7;
   if(core && local>=8 && local<248) {
    uint2 n=native_sample(source_y*202,at,uint(local),1);sub=n.x;priority=n.y;
   }
   if(!(r.sub_mask&2)) sub=0;
   samples[1]=sub;owners[1]=sub?1:5;
   if(core && (r.sub_mask&1)) {
    uint2 hud=native_sample(source_y*202,at,uint(local),0);
    if(hud.x && (!sub || hud.y==12 || priority!=11)) {samples[1]=hud.x;owners[1]=0;}
   }
  } else [unroll] for(uint screen=0;screen<2;++screen) {
   uint mask=screen?r.sub_mask:r.main_mask,window=screen?r.window_sub:r.window_main,rank=0;
   if(native_pixel) {
    uint nr=source_y*202;
    [unroll] for(uint layer=0;layer<3;++layer) {
     uint2 n=native_sample(nr,at,uint(edge),layer);uint pixel=n.x,z=n.y;
     if(pixel && z>rank && (mask&(1u<<layer)) && (!(window&(1u<<layer)) || !window_contains(r,layer,edge))) {
      samples[screen]=pixel;owners[screen]=layer;rank=z;
     }
    }
   } else {
   if((mask&2) && (!(window&2) || !bgwin)) {
    samples[screen]=index;owners[screen]=index?1:5;rank=index?(roof?11:7):0;
   } else if(!(overlay.x&0x80000000) && screen && (mask&4)) {
    samples[screen]=captured_sub_bg(at,uint(edge));owners[screen]=samples[screen]?2:5;
   }
   if(overlay.x&0x80000000) {
    [unroll] for(uint layer=0;layer<=2;layer+=2) {
     uint shift=layer?16:0,pixel=(overlay.x>>shift)&255,z=(overlay.x>>(shift+8))&15;
     if(overlay.x&0x40000000) {
      uint2 n=native_sample(source_y*202,at,uint(edge),layer);pixel=(layer==0 && (overlay.x&0x20000000))?0:n.x;z=n.y;
     }
     if(pixel && z>rank && (mask&(1u<<layer)) && (!(window&(1u<<layer)) || !window_contains(r,layer,edge))) {
      samples[screen]=pixel;owners[screen]=layer;rank=z;
     }
    }
   }
   if(warning && (!(overlay.x&0x80000000) || rank<8) && (mask&1) && (!(window&1) || !window_contains(r,0,edge))) {
    samples[screen]=warning;owners[screen]=0;rank=8;
   }
   }
   uint obj=overlay.y&255,priority=(overlay.y>>8)&15;
   bool higher=(native_pixel || (overlay.x&0x80000000))?priority>rank:!samples[screen] || priority>=(roof?3:2);
   if(obj && higher && (mask&16) && (!(window&16) || !window_contains(r,4,edge))) {
    samples[screen]=obj;owners[screen]=obj<192?6:4;
   }
  }
  uint main=samples[0],sub=samples[1];
  uint first=palette[source_y*256+main],clip=(r.control>>6)&3,prevent=(r.control>>4)&3;
  if(clip==3 || (clip==2 && win) || (clip==1 && !win)) first=0;
  bool math=((r.math&63)&(1u<<owners[0])) && !(prevent==3 || (prevent==2 && win) || (prevent==1 && !win));
  uint second=(r.control&2) && sub?palette[source_y*256+sub]:r.fixed_color;
  c=0xff000000;
  [unroll] for(uint channel=0;channel<3;++channel) {
   int value=(first>>(channel*5))&31;
   if(math) {
    int operand=(second>>(channel*5))&31;
    value+=(r.math&128)?-operand:operand;
    if((r.math&64) && (sub || !(r.control&2))) value>>=1;
    value=clamp(value,0,31);
   }
   c|=rows[at+12+value]<<(16-channel*8);
  }
 }
 output_image[id.xy]=float4((c>>16)&255,(c>>8)&255,c&255,255)/255.0;
}
