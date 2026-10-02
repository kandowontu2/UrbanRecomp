/* Built-in presentation feature, using the same provider ABI as Super Metroid. */
#include "sc_mods.h"
#include <stdio.h>
#include <string.h>

static ScVideoSettings *video;
static const char *config;
static char error[160];
#define COPY(field, text) snprintf(field, sizeof(field), "%s", text)
static int identity(const char *p, const char *f) {
    return p && f && !strcmp(p,"sc-widescreen") && !strcmp(f,"widescreen");
}
static int count(void *ctx) { (void)ctx; return 1; }
static int package_get(void *ctx, int i, RecompLauncherCModPackage *out) {
    (void)ctx; if (i || !out) return 0;
    memset(out,0,sizeof(*out));
    COPY(out->id,"sc-widescreen"); COPY(out->name,"Adaptive Widescreen");
    COPY(out->version,"1"); COPY(out->author,"SimCitySNESRecomp contributors");
    COPY(out->description,"A wider or taller city view that adapts to your window.");
    out->enabled=video->enabled; return 1;
}
static int feature_get(void *ctx, int i, RecompLauncherCModFeature *out) {
    (void)ctx; if (i || !out) return 0;
    memset(out,0,sizeof(*out));
    COPY(out->id,"widescreen"); COPY(out->package_id,"sc-widescreen");
    COPY(out->package_name,"Adaptive Widescreen"); COPY(out->package_version,"1");
    COPY(out->name,"Adaptive Widescreen"); COPY(out->group,"Presentation");
    COPY(out->author,"SimCitySNESRecomp contributors");
    COPY(out->description,"Show more of your city while keeping the full original view visible. USA ROM.");
    out->enabled=video->enabled; out->option_count=2;
    COPY(out->status,video->enabled ? "Enabled" : "Disabled"); return 1;
}
static int option_get(void *ctx,const char *p,const char *f,int i,RecompLauncherCModOption *out) {
    (void)ctx; if (!out || !identity(p,f) || i<0 || i>1) return 0;
    memset(out,0,sizeof(*out)); out->type=RECOMP_MOD_OPTION_CHOICE; out->step=1;
    if (!i) {
        COPY(out->id,"aspect"); COPY(out->label,"View size");
        COPY(out->description,"Fit keeps tile size and adapts both axes. Fit height adds columns; Fit width adds rows. Full original view stays visible.");
        COPY(out->value,ScAspectName(video->aspect)); COPY(out->default_value,"Fit");
        out->choice_count=SC_ASPECT_COUNT;
    } else {
        COPY(out->id,"position"); COPY(out->label,"City controls position");
        COPY(out->description,"Position the city toolbar and status display. Title, menus and advisor pop-ups stay centered.");
        COPY(out->value,video->centered ? "Center" : "TopLeft");
        COPY(out->default_value,"TopLeft"); out->choice_count=2;
    }
    return 1;
}
static int choice_get(void *ctx,const char *p,const char *f,const char *o,int i,RecompLauncherCModChoice *out) {
    (void)ctx; if (!out || !o || !identity(p,f) || i<0) return 0;
    memset(out,0,sizeof(*out));
    if (!strcmp(o,"aspect") && i<SC_ASPECT_COUNT) {
        COPY(out->value,ScAspectName((ScAspect)i)); COPY(out->label,ScAspectLabel((ScAspect)i));
    } else if (!strcmp(o,"position") && i<2) {
        COPY(out->value,i ? "TopLeft" : "Center"); COPY(out->label,i ? "Top left" : "Center");
    } else return 0;
    return 1;
}
static int enable(void *ctx,const char *p,const char *f,int on) {
    (void)ctx; if (!identity(p,f)) return 0;
    video->enabled=on!=0; return 1;
}
static int set_option(void *ctx,const char *p,const char *f,const char *o,const char *v) {
    (void)ctx; if (!identity(p,f) || !o || !v) return 0;
    if (!strcmp(o,"aspect")) return ScParseAspect(v,&video->aspect);
    if (!strcmp(o,"position") && (!strcmp(v,"Center") || !strcmp(v,"TopLeft"))) {
        video->centered=!strcmp(v,"Center"); return 1;
    }
    return 0;
}
static int commit(void *ctx,const char *image) {
    (void)ctx; (void)image; error[0]=0;
    if (ScVideoSave(video,config)) return 1;
    COPY(error,"Cannot save widescreen settings. Check the settings directory."); return 0;
}
static const char *last_error(void *ctx) { (void)ctx; return error; }
const RecompLauncherCModProvider *ScModsProvider(ScVideoSettings *s,const char *path) {
    static RecompLauncherCModProvider provider;
    video=s; config=path; error[0]=0; memset(&provider,0,sizeof(provider));
    provider.package_count=count; provider.package_get=package_get;
    provider.feature_count=count; provider.feature_get=feature_get;
    provider.feature_option_get=option_get; provider.feature_choice_get=choice_get;
    provider.feature_enable=enable; provider.feature_set_option=set_option;
    provider.commit=commit; provider.last_error=last_error;
    provider.archive_extension=".snesmod"; provider.archive_description="SNESRecomp mod package";
    return &provider;
}
