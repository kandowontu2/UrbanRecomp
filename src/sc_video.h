#pragma once
#include <stdbool.h>

typedef enum ScAspect {
    SC_FIT, SC_FIT_HEIGHT, SC_FIT_WIDTH, SC_4_3, SC_8_7,
    SC_16_10, SC_16_9, SC_21_9, SC_32_9, SC_ASPECT_COUNT
} ScAspect;
typedef struct ScVideoSettings {
    bool enabled; ScAspect aspect; bool centered;
    double fit_scale, fit_pixel_aspect;
} ScVideoSettings;
typedef struct ScViewport {
    int width, height, core_x, core_y; double pixel_aspect;
    double pixel_scale; /* Fit keeps this drawable-pixel scale across resizes. */
} ScViewport;
typedef struct ScVideoRect { int x, y, w, h; } ScVideoRect;
enum { SC_MAX_CANVAS = 2048 };
const char *ScAspectName(ScAspect aspect);
const char *ScAspectLabel(ScAspect aspect);
bool ScParseAspect(const char *value, ScAspect *out);
void ScVideoDefaults(ScVideoSettings *settings);
bool ScVideoLoad(ScVideoSettings *settings, const char *path);
bool ScVideoSave(const ScVideoSettings *settings, const char *path);
ScViewport ScVideoViewport(const ScVideoSettings *settings, int width, int height);
ScVideoRect ScVideoDestination(ScViewport view, int width, int height);
/* Capture the displayed tile scale before expanding the drawable window. */
void ScVideoCaptureScale(ScVideoSettings *settings, ScViewport view, int width, int height);
/* Change tile scale while retaining a complete native HUD and the drawable
 * window. Smaller scales expose more land rather than resizing the window. */
bool ScVideoZoom(ScVideoSettings *settings,ScViewport current,int width,int height,double factor);
bool ScVideoToGuest(ScViewport view, ScVideoRect destination, double x, double y,
                   int *guest_x, int *guest_y);
/* SDL pointer coordinates are window units; destination is drawable pixels.
 * Use the rendered viewport (including its live menu anchor). */
bool ScVideoWindowToGuest(ScViewport view, ScVideoRect destination,
                          int window_w, int window_h, int drawable_w, int drawable_h,
                          double x, double y, int *guest_x, int *guest_y);
