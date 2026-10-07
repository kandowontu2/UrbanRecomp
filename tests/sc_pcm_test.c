#include "sc_pcm.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv) {
    ScPcm p={0};int16_t out[32]={0};
    assert(ScPcmCommand(&p,3)==3);
    assert(!ScPcmLoad(&p,"no-such-pack",44100) && !p.ready);
    /* Immutable synthetic stereo: introduction, opposite channel signs,
     * nonzero loop point, seam interpolation, mute/resume and saturation. */
    const uint8_t data[]={0xe8,3,0x18,0xfc,0xd0,7,0x30,0xf8,0xb8,11,0x48,0xf4};
    p.tracks[1]=(ScPcmTrack){malloc(sizeof data),3,1};assert(p.tracks[1].data);
    memcpy(p.tracks[1].data,data,sizeof data);p.ready=p.enabled=true;p.rate=44100;
    assert(ScPcmCommand(&p,1)==0 && p.track==1);
    ScPcmMix(&p,out,5);
    const int16_t expected[]={1000,-1000,2000,-2000,3000,-3000,2000,-2000,3000,-3000};
    assert(!memcmp(out,expected,sizeof expected));
    assert(ScPcmCommand(&p,0)==0 && p.track==1);uint64_t position=p.position;
    p.enabled=false;memset(out,0,sizeof out);ScPcmMix(&p,out,5);
    assert(p.position==position && !out[0]);p.enabled=true;
    ScPcmMix(&p,out,1);assert(out[0]==2000 && out[1]==-2000);
    ScPcmCommand(&p,1);p.rate=88200;memset(out,0,sizeof out);ScPcmMix(&p,out,8);
    assert(out[0]==1000 && out[2]==1500 && out[4]==2000 && out[6]==2500 && out[10]==2500 && out[12]==2000);
    ScPcmCommand(&p,1);p.rate=44100;out[0]=32000;out[1]=-32000;ScPcmMix(&p,out,1);
    assert(out[0]==32767 && out[1]==-32768);
    p.tracks[20]=(ScPcmTrack){malloc(sizeof data),3,1};assert(p.tracks[20].data);
    memcpy(p.tracks[20].data,data,sizeof data);
    ScPcmCityMilestone(&p,true);assert(p.track==20 && !p.position);
    p.position=44100;p.enabled=false;
    ScPcmCityMilestone(&p,true);assert(p.track==20 && p.position==44100 && !p.enabled);
    ScPcmCityMilestone(&p,false);assert(p.track==1 && !p.position && !p.enabled);
    ScPcmCityMilestone(&p,true);
    for(unsigned command=1;command<=6;++command)
        assert(ScPcmCommand(&p,(uint8_t)command)==0 && p.track==20);
    p.tracks[21]=(ScPcmTrack){malloc(sizeof data),3,1};assert(p.tracks[21].data);
    memcpy(p.tracks[21].data,data,sizeof data);
    ScPcmCityMilestone(&p,2);assert(p.track==21);
    p.position=44100;ScPcmCityMilestone(&p,2);assert(p.position==44100);
    for(unsigned command=1;command<=6;++command)
        assert(ScPcmCommand(&p,(uint8_t)command)==0 && p.track==21);
    assert(ScPcmCommand(&p,8)==0 && p.track==8);
    ScPcmCityMilestone(&p,2);assert(p.track==8);
    assert(ScPcmCommand(&p,11)==0 && p.track==11);
    ScPcmCityMilestone(&p,false);assert(p.track==11);
    assert(ScPcmCommand(&p,20)==20 && !p.track);
    assert(ScPcmCommand(&p,21)==21 && !p.track);ScPcmDestroy(&p);
    if(argc>1) {
        assert(ScPcmLoad(&p,argv[1],44100));
        for(unsigned n=1;n<20;++n) {
            ScPcmCityMilestone(&p,false);
            assert(ScPcmCommand(&p,(uint8_t)n)==0);
            ScPcmTrack *t=p.tracks+n;p.position=(uint64_t)(t->frames-1)*p.rate;
            memset(out,0,sizeof out);ScPcmMix(&p,out,4);
            assert(p.position==(uint64_t)(t->loop+3)*p.rate);
            assert(out[2]==(int16_t)(t->data[t->loop*4]|t->data[t->loop*4+1]<<8));
        }
        ScPcmCityMilestone(&p,true);assert(ScPcmCommand(&p,6)==0);
        assert(p.track==(p.tracks[20].data?20:6));
        if(p.tracks[20].data) {
            ScPcmTrack *t=p.tracks+20;p.position=(uint64_t)(t->frames-1)*p.rate;
            memset(out,0,sizeof out);ScPcmMix(&p,out,4);
            assert(p.position==(uint64_t)(t->loop+3)*p.rate);
            puts("PASS: imported Megagopolos theme, native city-song replacement and loop seam");
        }
        ScPcmCityMilestone(&p,2);assert(ScPcmCommand(&p,6)==0);
        assert(p.track==(p.tracks[21].data?21:p.tracks[20].data?20:6));
        if(p.tracks[21].data) {
            ScPcmTrack *t=p.tracks+21;p.position=(uint64_t)(t->frames-1)*p.rate;
            memset(out,0,sizeof out);ScPcmMix(&p,out,4);
            assert(p.position==(uint64_t)(t->loop+3)*p.rate);
            puts("PASS: imported Gigagopolois theme, complete arrangement, loop and higher milestone priority");
        }
        ScPcmDestroy(&p);puts("PASS: all 19 imported tracks, exact stereo sample decode and authored loop points");
    }
    puts("PASS: PCM command mapping, fallback, loops, resampling seams, mute/resume, mixing saturation and cleanup");
}
