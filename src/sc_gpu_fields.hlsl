// Independent five-point fields and crime samples. Pack exact integer values
// and original timing metadata for bounded native publication; no atomics.
ByteAddressBuffer source : register(t0, space0);
RWStructuredBuffer<uint> result : register(u0, space1);
cbuffer Shape : register(b0, space2) { uint width; uint height; uint count; uint element_bytes; uint bias; };
uint read_byte(uint at) {return (source.Load(at&~3u)>>((at&3u)*8u))&255u;}
uint read_word(uint at) {return (source.Load((at*2)&~3u)>>((at&1u)*16u))&65535u;}
[numthreads(128,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint at=tid.x;if(at>=count) return;
    if(element_bytes==4) {
        uint x=at%width,y=at/width,first=4*y*width+2*x;
        uint raw[4]={read_word(first),read_word(first+1),read_word(first+2*width),read_word(first+2*width+1)};
        uint density=0,pollution=0,cost=0,extra=0,occupied=0,tail=0;
        [unroll] for(uint n=0;n<4;++n) {
            uint tile=raw[n]&1023,t=source.Load(bias+4*tile);
            if(t&(1u<<28)) density+=15;
            if(t&(1u<<29)) density=255;
            if(t&(1u<<31)) {pollution=(pollution+(t&65535))&65535;tail=(t&65535)|(tile<<16)|(1u<<29);}
            occupied+=(t>>30)&1;cost+=(t>>16)&255;extra+=(t>>24)&15;
        }
        result[5*at]=raw[0]|(raw[1]<<16);result[5*at+1]=raw[2]|(raw[3]<<16);
        result[5*at+2]=density|(pollution<<16);result[5*at+3]=tail|(occupied<<26);result[5*at+4]=cost|(extra<<16);return;
    }
    if(element_bytes==3) {
        uint packed=source.Load(at*4),land=packed&255,density=(packed>>8)&255,coverage=packed>>16;
        uint value=0,scratch=0,cost=0,extra=0;
        if(land) {
            cost=31;extra=2;value=(land<128?128-land:0)+density;
            cost+=3+3+4+7+3+2+4+(land<=128?3:5);extra+=3;
            cost+=3+2+5+3+(value>255?7:3);extra+=1+(value>255?1:0);
            value=(value+bias)&65535;
            cost+=3+4+2+5+4+4;extra+=3;
            if(value&32768) {cost+=3+3+4;extra+=1;value=0;}
            else {cost+=2+3;if(value>=300) {cost+=2+3+3+4;extra+=1;value=300;}else cost+=3;}
            cost+=36;extra+=2;scratch=(value-coverage)&65535;
            cost+=3+2+2+4+2+6+4;extra+=2;
            if(value<coverage) {cost+=2+3+3;value=0;}
            else {value-=coverage;cost+=3+4+3;extra+=1;if(value>=250) {cost+=2+3;value=250;}else cost+=3;}
            cost+=3+5+5+3+2+4+4;extra+=2;
        }
        result[3*at]=value|(scratch<<8);result[3*at+1]=cost|(extra<<16);result[3*at+2]=packed;return;
    }
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
