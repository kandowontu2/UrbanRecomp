// Captured plane pairs and scanline state; native/HUD/warning pixels win.
StructuredBuffer<uint> image : register(t0);
StructuredBuffer<uint4> tiles : register(t1);
StructuredBuffer<uint> palette : register(t2);
struct Row {
 uint phase,core_x,main_mask,sub_mask,window_main,window_sub,windows,logic,bounds;
 uint math,control,fixed_color;
};
StructuredBuffer<uint> rows : register(t3);
RWTexture2D<float4> output_image : register(u0);
cbuffer Dimensions : register(b0) { uint width; uint height; uint stride; uint unused; };
uint decode(uint p,uint bit) {
 return ((p>>bit)&1)|(((p>>(bit+8))&1)<<1)|(((p>>(bit+16))&1)<<2)|(((p>>(bit+24))&1)<<3);
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
 if(id.x>=width || id.y>=height) return;
 uint c=image[id.y*width+id.x];
 if(c==0x01000000) {
  uint at=id.y*300;Row r;
  r.phase=rows[at];r.core_x=rows[at+1];r.main_mask=rows[at+2];r.sub_mask=rows[at+3];
  r.window_main=rows[at+4];r.window_sub=rows[at+5];r.windows=rows[at+6];
  r.logic=rows[at+7];r.bounds=rows[at+8];r.math=rows[at+9];r.control=rows[at+10];r.fixed_color=rows[at+11];
  uint x=id.x+r.phase;
  uint4 t=tiles[id.y*stride+x/8];
  uint b=decode(t.x,(t.z&(1<<16))?x&7:7-(x&7));
  uint roof=decode(t.y,(t.z&(1<<17))?x&7:7-(x&7));
  uint index=roof?roof+((t.z>>8)&127):b?b+(t.z&127):0;
  int edge=clamp(int(id.x)-int(r.core_x),0,255);
  bool bgwin=window_contains(r,1,edge),win=window_contains(r,5,edge);
  uint main=(r.main_mask&2) && (!(r.window_main&2) || !bgwin)?index:0;
  uint sub=(r.sub_mask&2) && (!(r.window_sub&2) || !bgwin)?index:(r.sub_mask&4)?rows[at+44+edge]:0;
  uint first=palette[id.y*256+main],clip=(r.control>>6)&3,prevent=(r.control>>4)&3;
  if(clip==3 || (clip==2 && win) || (clip==1 && !win)) first=0;
  bool math=(r.math&(1<<(main?1:5))) && !(prevent==3 || (prevent==2 && win) || (prevent==1 && !win));
  uint second=(r.control&2) && sub?palette[id.y*256+sub]:r.fixed_color;
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
