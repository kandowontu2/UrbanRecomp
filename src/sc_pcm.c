#include "sc_pcm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
void ScPcmDestroy(ScPcm *p) {
    for(unsigned i=1;i<=21;++i) free(p->tracks[i].data);
    memset(p,0,sizeof *p);
}
bool ScPcmLoad(ScPcm *p,const char *directory,unsigned rate) {
    ScPcmDestroy(p);
    if(!directory || !*directory || rate<8000 || rate>192000) return false;
    for(unsigned i=1;i<=21;++i) {
        char path[4096];uint8_t header[8];
        int n=snprintf(path,sizeof path,"%s/scity-msu1-%u.pcm",directory,i);
        if(n<0 || n>=(int)sizeof path) goto failed;
        FILE *f=fopen(path,"rb");if(!f) {if(i>=20)continue;goto failed;}
        bool valid=fseek(f,0,SEEK_END)==0;
        long bytes=valid?ftell(f):-1;
        valid=bytes>=12 && bytes<=256*1024*1024 && (bytes-8)%4==0 &&
            fseek(f,0,SEEK_SET)==0 && fread(header,1,8,f)==8 && !memcmp(header,"MSU1",4);
        uint32_t frames=valid?(uint32_t)(bytes-8)/4:0,loop=valid?le32(header+4):0;
        valid=valid && loop<frames;
        uint8_t *data=valid?malloc((size_t)frames*4):NULL;
        valid=data && fread(data,4,frames,f)==frames;
        fclose(f);
        if(!valid) {free(data);if(i>=20)continue;goto failed;}
        p->tracks[i]=(ScPcmTrack){data,frames,loop};
    }
    p->rate=rate;p->ready=p->enabled=true;return true;
failed:
    ScPcmDestroy(p);return false;
}
static unsigned city_track(const ScPcm *p,unsigned command) {
    if(command<=6 && p->city_milestone>=2 && p->tracks[21].data)return 21;
    if(command<=6 && p->city_milestone && p->tracks[20].data)return 20;
    return command;
}
uint8_t ScPcmCommand(ScPcm *p,uint8_t command) {
    if(!p->ready) return command;
    if(command)p->last_command=command;
    if(command>0 && command<20) {
        p->track=city_track(p,command);
        p->position=0;p->enabled=true;return 0;
    }
    if(command>=20) {p->track=0;p->position=0;}
    return command;
}
void ScPcmCityMilestone(ScPcm *p,unsigned level) {
    p->city_milestone=level>2?2:level;
    if(!p->ready || !p->last_command || p->last_command>6 || !p->track)return;
    unsigned track=city_track(p,p->last_command);
    if(p->track!=track) {p->track=track;p->position=0;}
}
static int sample(const ScPcmTrack *t,unsigned frame,unsigned channel) {
    const uint8_t *p=t->data+frame*4+channel*2;
    unsigned value=p[0]|(unsigned)p[1]<<8;
    return value<32768?(int)value:(int)value-65536;
}
void ScPcmMix(ScPcm *p,int16_t *stereo,unsigned frames) {
    if(!p->ready || !p->enabled || !p->track) return;
    const ScPcmTrack *t=p->tracks+p->track;
    uint64_t end=(uint64_t)t->frames*p->rate,loop=(uint64_t)t->loop*p->rate;
    for(unsigned i=0;i<frames;++i) {
        if(p->position>=end) p->position=loop+(p->position-end)%(end-loop);
        unsigned at=(unsigned)(p->position/p->rate),frac=(unsigned)(p->position%p->rate);
        unsigned next=at+1<t->frames?at+1:t->loop;
        for(unsigned c=0;c<2;++c) {
            int a=sample(t,at,c),b=sample(t,next,c);
            int value=stereo[2*i+c]+a+(int)((int64_t)(b-a)*frac/p->rate);
            stereo[2*i+c]=(int16_t)(value<-32768?-32768:value>32767?32767:value);
        }
        p->position+=44100;
    }
}
