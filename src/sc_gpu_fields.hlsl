// Independent five-point fields. Pack exact integer sums and original timing
// metadata for bounded native publication; no floating point or atomics.
ByteAddressBuffer source : register(t0, space0);
RWStructuredBuffer<uint> result : register(u0, space1);
cbuffer Shape : register(b0, space2) { uint width; uint height; uint count; uint element_bytes; };
uint read_byte(uint at) {return (source.Load(at&~3u)>>((at&3u)*8u))&255u;}
uint read_word(uint at) {return (source.Load((at*2)&~3u)>>((at&1u)*16u))&65535u;}
[numthreads(128,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint at=tid.x;if(at>=count) return;
    uint x=at%width,y=at/width,sum=0,cost=0,extra=5;
    if(element_bytes==2) {
        uint offsets[4]={x?at-1:at,x+1<width?at+1:at,y?at-width:at,y+1<height?at+width:at};
        bool present_word[4]={x!=0,x+1<width,y!=0,y+1<height};
        cost=66;
        [unroll] for(uint n=0;n<4;++n) {
            uint value=read_word(offsets[n]);cost+=present_word[n]?10:3;
            if(present_word[n]) sum=(sum+value)&65535u;
        }
        uint average=sum/4,center=read_word(at),total=average+center+((sum>>1)&1u),wrapped=total&65535u;
        uint overflow=((average^total)&(center^total)&32768u)!=0?1:0;
        result[at]=wrapped/2|(cost<<16)|((wrapped&1u)<<30)|(overflow<<31);return;
    }
    // Clamp addresses before reading. Absent neighbours are excluded below;
    // even a compiler that evaluates both sides never issues an invalid load.
    uint values[5]={read_byte(x?at-1:at),read_byte(x+1<width?at+1:at),read_byte(y?at-width:at),read_byte(y+1<height?at+width:at),read_byte(at)};
    bool present[5]={x!=0,x+1<width,y!=0,y+1<height,true};
    [unroll] for(uint n=0;n<5;++n) if(present[n]) {
        uint old=sum&255u;sum+=values[n];cost+=7;
        if(n) {bool carry=old+values[n]>255;cost+=carry?7:3;extra+=carry?1:0;}
    }
    uint before=(sum-values[4])&255u;
    uint overflow=((before^(sum&255u))&(values[4]^(sum&255u))&128u)!=0?1:0;
    cost+=3+2+4+(x?2:3)+3+(x+1<width?2:3)+4+(y?2:3)+3+(y+1<height?2:3);
    cost+=3+3+4+4+3+(sum/4<250?3:5)+3+5;
    result[at]=sum|(cost<<11)|(extra<<20)|(overflow<<25);
}
