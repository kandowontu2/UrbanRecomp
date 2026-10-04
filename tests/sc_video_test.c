#include "sc_video.h"
#include "sc_mods.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void check_fit_scale(void) {
    /* Maximizing and later resizes add land, preserving the displayed tile
     * size from both corrected and square pixels, and at high DPI. */
    const int windows[][2]={{1920,1080},{3440,1440},{1200,1920},{800,600}};
    for(int aspect=SC_8_7;aspect<SC_ASPECT_COUNT;++aspect)
    for(int dpi=1;dpi<=3;++dpi) {
        ScVideoSettings s={.enabled=true,.aspect=(ScAspect)aspect};
        ScViewport before=ScVideoViewport(&s,800*dpi,600*dpi);
        ScVideoRect old=ScVideoDestination(before,800*dpi,600*dpi);
        ScVideoCaptureScale(&s,before,800*dpi,600*dpi);
        s.aspect=SC_FIT;
        assert(s.fit_scale>0 && s.fit_pixel_aspect==before.pixel_aspect);
        for(unsigned i=0;i<sizeof windows/sizeof windows[0];++i) {
            int w=windows[i][0]*dpi,h=windows[i][1]*dpi;
            ScViewport v=ScVideoViewport(&s,w,h);
            ScVideoRect d=ScVideoDestination(v,w,h);
            assert(v.width>=before.width-2 && v.height>=before.height-2);
            assert(d.x>=0 && d.y>=0 && d.x+d.w<=w && d.y+d.h<=h);
            assert(fabs((double)d.h/v.height-(double)old.h/before.height)<.005);
            assert(fabs((double)d.w/v.width-s.fit_scale*before.pixel_aspect)<.005);
            assert(v.width==SC_MAX_CANVAS || w-d.w<4*s.fit_scale);
            assert(v.height==SC_MAX_CANVAS || h-d.h<2*s.fit_scale+1);
            int gx,gy;
            /* Points well past the original core still map across the entire
             * expanded view, including DPI-scaled window coordinates. */
            double x=(d.x+(v.width-8+.5)*d.w/v.width)/dpi;
            double y=(d.y+(v.height-8+.5)*d.h/v.height)/dpi;
            ScVideoWindowToGuest(v,d,w/dpi,h/dpi,w,h,x,y,&gx,&gy);
            assert(gx==v.width-8 && gy==v.height-8);
            double scale=s.fit_scale;
            ScVideoCaptureScale(&s,v,w,h);
            assert(s.fit_scale==scale); /* Repeated Fit retains exactly the same scale. */
        }
        ScViewport capped=ScVideoViewport(&s,65536,65536);
        ScVideoRect d=ScVideoDestination(capped,65536,65536);
        assert(capped.width==SC_MAX_CANVAS && capped.height==SC_MAX_CANVAS);
        assert(fabs((double)d.h/capped.height-s.fit_scale)<.005);
        ScViewport small=ScVideoViewport(&s,64,64);
        d=ScVideoDestination(small,64,64);
        assert(d.w<=64 && d.h<=64); /* Shrinking retains the full native UI. */
    }
}
static void check_zoom(void) {
    for(unsigned dpi=1;dpi<=3;++dpi) for(unsigned centered=0;centered<2;++centered) {
        int w=1920*dpi,h=1080*dpi;
        ScVideoSettings s={.enabled=true,.aspect=SC_FIT,.centered=centered,
            .fit_scale=3*dpi,.fit_pixel_aspect=7.0/6};
        ScViewport before=ScVideoViewport(&s,w,h);
        assert(ScVideoZoom(&s,before,w,h,.5));
        ScViewport after=ScVideoViewport(&s,w,h);ScVideoRect d=ScVideoDestination(after,w,h);
        assert(after.width==before.width && after.height==before.height);
        assert(after.pixel_scale==before.pixel_scale && s.map_zoom==.5);
        assert(d.w<=w && d.h<=h);
        int x,y;
        double px=d.x+(after.core_x+128.5)*d.w/after.width;
        double py=d.y+(after.core_y+112.5)*d.h/after.height;
        assert(ScVideoToGuest(after,d,px,py,&x,&y) && x==128 && y==112);
        assert(ScVideoZoom(&s,after,w,h,2));assert(s.map_zoom==1);
        assert(ScVideoZoom(&s,after,w,h,1e-30));
        assert(s.map_zoom>0 && before.width/s.map_zoom<=SC_MAX_MAP_SPAN+1);
        assert(before.width/s.map_zoom>=16384); /* full largest-city coverage */
        after=ScVideoViewport(&s,w,h);
        assert(after.width==before.width && after.height==before.height);
        assert(ScVideoZoom(&s,after,w,h,1e30));assert(s.map_zoom==4);
        ScVideoSettings valid=s;
        assert(!ScVideoZoom(&s,after,w,h,NAN) && !ScVideoZoom(&s,after,w,h,0));
        assert(!ScVideoZoom(&s,after,0,h,2));assert(!memcmp(&s,&valid,sizeof s));
    }
}

int main(int argc,char **argv) {
    assert(argc==2);
    check_fit_scale();
    check_zoom();
    ScVideoSettings s; ScVideoDefaults(&s);
    /* The adaptive renderer is the default, at 21:9. */
    assert(s.enabled && s.aspect==SC_21_9 && !s.centered);
    ScViewport v=ScVideoViewport(&s,3840,1080);
    assert(v.width==448 && v.height==224);
    s.enabled=false; v=ScVideoViewport(&s,3840,1080);
    assert(v.width==256 && v.height==224);
    s.enabled=true;
    const int sizes[][2]={{1,1},{320,240},{1280,720},{2520,1080},{3840,1080},
                         {720,1280},{1280,1280},{16384,64},{64,16384},{0,0}};
    for (int a=0;a<SC_ASPECT_COUNT;++a) for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        s.aspect=(ScAspect)a;
        v=ScVideoViewport(&s,sizes[i][0],sizes[i][1]);
        assert(v.width>=256 && v.height>=224);
        assert(v.width<=SC_MAX_CANVAS && v.height<=SC_MAX_CANVAS);
        assert(v.core_x>=0 && v.core_x+256<=v.width);
        assert(v.core_y>=0 && v.core_y+224<=v.height);
        ScVideoRect d=ScVideoDestination(v,sizes[i][0],sizes[i][1]);
        assert(d.x>=0 && d.y>=0 && d.x+d.w<=sizes[i][0] && d.y+d.h<=sizes[i][1]);
        if (d.w && d.h) {
            int x,y;
            double px=d.x+(v.core_x+128.5)*d.w/v.width;
            double py=d.y+(v.core_y+112.5)*d.h/v.height;
            assert(ScVideoToGuest(v,d,px,py,&x,&y) && x==128 && y==112);
            assert(!ScVideoToGuest(v,d,-100,-100,&x,&y));
        }
    }
    const int fixed[]={342,448,684};
    for (int i=0;i<3;++i) {
        s.aspect=(ScAspect)(SC_16_9+i); v=ScVideoViewport(&s,800,600);
        assert(v.width==fixed[i] && v.height==224);
    }
    s.aspect=SC_FIT_WIDTH; v=ScVideoViewport(&s,720,1280);
    assert(v.width==256 && v.height==532);
    s.aspect=SC_FIT_HEIGHT; v=ScVideoViewport(&s,3840,1080);
    assert(v.width==684 && v.height==224);
    const RecompLauncherCModProvider *mods=ScModsProvider(&s,argv[1]);
    RecompLauncherCModFeature feature;
    assert(mods->feature_count(NULL)==1 && mods->feature_get(NULL,0,&feature));
    assert(!mods->feature_enable(NULL,"invalid","widescreen",1));
    assert(mods->feature_enable(NULL,"sc-widescreen","widescreen",1));
    assert(!mods->feature_set_option(NULL,"sc-widescreen","widescreen","aspect","32:0"));
    assert(mods->feature_set_option(NULL,"sc-widescreen","widescreen","aspect","32:9"));
    assert(mods->feature_set_option(NULL,"sc-widescreen","widescreen","position","TopLeft"));
    for (int i=0;i<SC_ASPECT_COUNT;++i) {
        RecompLauncherCModChoice choice;
        assert(mods->feature_choice_get(NULL,"sc-widescreen","widescreen","aspect",i,&choice));
        ScAspect parsed; assert(ScParseAspect(choice.value,&parsed) && parsed==(ScAspect)i);
    }
    assert(mods->commit(NULL,NULL));
    ScVideoSettings loaded;
    assert(ScVideoLoad(&loaded,argv[1]));
    assert(loaded.enabled && loaded.aspect==SC_32_9 && !loaded.centered);
    assert(mods->feature_enable(NULL,"sc-widescreen","widescreen",0));
    assert(mods->commit(NULL,NULL));
    assert(ScVideoLoad(&loaded,argv[1]) && !loaded.enabled);
    s.enabled=true;s.aspect=SC_8_7;
    ScVideoCaptureScale(&s,ScVideoViewport(&s,800,600),800,600);
    s.aspect=SC_FIT;
    assert(ScVideoSave(&s,argv[1]) && ScVideoLoad(&loaded,argv[1]));
    assert(loaded.aspect==SC_FIT && loaded.fit_scale==s.fit_scale &&
           loaded.fit_pixel_aspect==1);
    FILE *f=fopen(argv[1],"w"); assert(f);
    fputs("Enabled=1\nAspect=invalid\n",f); fclose(f);
    assert(!ScVideoLoad(&loaded,argv[1]));   /* the caller refuses to start */
    f=fopen(argv[1],"w");assert(f);
    fputs("Aspect=Fit\nFitScale=nan\n",f);fclose(f);
    assert(!ScVideoLoad(&loaded,argv[1]));
    remove(argv[1]); puts("PASS: fixed-scale expansion, fit containment, input mapping, Mods choices and atomic persistence");
    return 0;
}
