#pragma once
#include <stdbool.h>

typedef enum ScAspect {
    SC_FIT, SC_FIT_HEIGHT, SC_FIT_WIDTH, SC_4_3, SC_8_7,
    SC_16_10, SC_16_9, SC_21_9, SC_32_9, SC_ASPECT_COUNT
} ScAspect;
typedef struct ScVideoSettings {
    bool enabled; ScAspect aspect; bool centered;
    double fit_scale, fit_pixel_aspect;
    double map_zoom; /* terrain magnification; UI scale is independent */
} ScVideoSettings;
typedef struct ScViewport {
    int width, height, core_x, core_y; double pixel_aspect;
    double pixel_scale; /* Fit keeps this drawable-pixel scale across resizes. */
} ScViewport;
typedef struct ScVideoRect { int x, y, w, h; } ScVideoRect;
enum { SC_MAX_CANVAS = 4096, SC_MAX_MAP_SPAN = 65536 };
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
/* Change terrain magnification while retaining the HUD, menu and drawable
 * scale. Smaller factors expose more land within the same window. */
bool ScVideoZoom(ScVideoSettings *settings,ScViewport current,int width,int height,double factor);
bool ScVideoToGuest(ScViewport view, ScVideoRect destination, double x, double y,
                   int *guest_x, int *guest_y);
/* Raw displayed canvas position, including widescreen and fractional pixels.
 * Reject letterboxing; do not remap fixed HUD elements into SNES coordinates. */
bool ScVideoWindowToCanvas(ScViewport view, ScVideoRect destination,
                          int window_w,int window_h,int drawable_w,int drawable_h,
                          double x,double y,double *canvas_x,double *canvas_y);
/* SDL pointer coordinates are window units; destination is drawable pixels.
 * Use the rendered viewport (including its live menu anchor). */
bool ScVideoWindowToGuest(ScViewport view, ScVideoRect destination,
                          int window_w, int window_h, int drawable_w, int drawable_h,
                          double x, double y, int *guest_x, int *guest_y);
