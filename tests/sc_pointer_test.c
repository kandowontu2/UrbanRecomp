#include "sc_video.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

int main(void) {
    const int sizes[][2] = {{800,600},{1920,1080},{3440,1440},{720,1280}};
    for (int aspect=0; aspect<SC_ASPECT_COUNT; ++aspect)
    for (int centered=0; centered<2; ++centered)
    for (int dpi=1; dpi<=3; ++dpi)
    for (unsigned i=0; i<sizeof sizes/sizeof sizes[0]; ++i) {
        ScVideoSettings settings={.enabled=true,.aspect=(ScAspect)aspect,.centered=centered!=0};
        int ww=sizes[i][0], wh=sizes[i][1], dw=ww*dpi, dh=wh*dpi;
        ScViewport v=ScVideoViewport(&settings,dw,dh);
        ScVideoRect d=ScVideoDestination(v,dw,dh);
        const int points[][2]={{0,0},{255,223},{128,112},{24,32}};
        for (unsigned j=0;j<sizeof points/sizeof points[0];++j) {
            double x=(d.x+(v.core_x+points[j][0]+.5)*d.w/v.width)/dpi;
            double y=(d.y+(v.core_y+points[j][1]+.5)*d.h/v.height)/dpi;
            int gx=-1,gy=-1;
            assert(ScVideoWindowToGuest(v,d,ww,wh,dw,dh,x,y,&gx,&gy));
            assert(gx==points[j][0] && gy==points[j][1]);
        }
        int gx,gy;
        assert(!ScVideoWindowToGuest(v,d,ww,wh,dw,dh,-1,-1,&gx,&gy));
        if (v.width>256) {
            double x=(d.x+(v.core_x+256.5)*d.w/v.width)/dpi;
            double y=(d.y+(v.core_y+112.5)*d.h/v.height)/dpi;
            assert(!ScVideoWindowToGuest(v,d,ww,wh,dw,dh,x,y,&gx,&gy));
        }
    }
    ScViewport v={448,224,96,0,7.0/6.0,0};
    ScVideoRect d={0,0,1920,1080};
    assert(!ScVideoWindowToGuest(v,d,0,600,1920,1080,1,1,NULL,NULL));
    puts("PASS: absolute pointer mapping, all aspects, live anchors, DPI, resize and margins");
    return 0;
}
