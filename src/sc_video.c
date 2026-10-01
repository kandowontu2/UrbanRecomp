#include "sc_video.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static const char *const names[] = {"Fit", "Height", "Width", "4:3", "8:7",
                                   "16:10", "16:9", "21:9", "32:9"};
static const char *const labels[] = {"Fit to window", "Fit height", "Fit width",
                                    "4:3", "8:7 (square pixels)", "16:10",
                                    "16:9", "21:9", "32:9"};
const char *ScAspectName(ScAspect a) { return names[a >= 0 && a < SC_ASPECT_COUNT ? a : 0]; }
const char *ScAspectLabel(ScAspect a) { return labels[a >= 0 && a < SC_ASPECT_COUNT ? a : 0]; }
bool ScParseAspect(const char *value, ScAspect *out) {
    if (!value || !out) return false;
    for (int i = 0; i < SC_ASPECT_COUNT; ++i)
        if (!strcmp(value, names[i])) { *out = (ScAspect)i; return true; }
    return false;
}
/* The adaptive renderer is the default since 2026-09-22, at 21:9 -- a
 * 448x224 canvas, as wide as the classic widescreen was, so a fresh install
 * keeps its margins whatever the window's shape. Enabled=0 in sc-video.ini
 * (Mods: Adaptive Widescreen off) goes back to the classic renderer. */
void ScVideoDefaults(ScVideoSettings *s) {
    *s = (ScVideoSettings){true, SC_21_9, false};
}
bool ScVideoLoad(ScVideoSettings *s, const char *path) {
    ScVideoDefaults(s);
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT;
    ScVideoSettings parsed = *s;
    char line[256], key[64], value[64];
    bool valid = true;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " %63[^= \t] = %63s", key, value) != 2) continue;
        if (!strcmp(key, "Aspect")) valid &= ScParseAspect(value, &parsed.aspect);
        else if (!strcmp(key, "Enabled") || !strcmp(key, "Centered")) {
            if (strcmp(value, "0") && strcmp(value, "1")) { valid = false; continue; }
            if (!strcmp(key, "Enabled")) parsed.enabled = value[0] == '1';
            else parsed.centered = value[0] == '1';
        }
    }
    valid &= !ferror(f);
    fclose(f);
    if (valid) *s = parsed;
    return valid;
}
bool ScVideoSave(const ScVideoSettings *s, const char *path) {
    if (!path || s->aspect < 0 || s->aspect >= SC_ASPECT_COUNT) return false;
    char temp[1024];
    if (snprintf(temp, sizeof(temp), "%s.tmp", path) >= (int)sizeof(temp)) return false;
    FILE *f = fopen(temp, "w");
    if (!f) return false;
    bool ok = fprintf(f, "[Widescreen]\nEnabled=%d\nAspect=%s\nCentered=%d\n",
                      s->enabled, ScAspectName(s->aspect), s->centered) > 0;
    if (fclose(f)) ok = false;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(temp, path) == 0;
#endif
    }
    if (!ok) remove(temp);
    return ok;
}
static int extent(double value, int minimum) {
    if (value > SC_MAX_CANVAS) return SC_MAX_CANVAS;
    /* Round outward to even pixels; tolerate binary noise at exact ratios
     * (21:9 is exactly 448 columns, not an accidental 450). */
    int result = (int)ceil(value / 2 - 1e-9) * 2;
    return result < minimum ? minimum : result;
}
ScViewport ScVideoViewport(const ScVideoSettings *s, int w, int h) {
    ScViewport v = {256, 224, 0, 0, 7.0 / 6.0};
    if (!s->enabled) return v;
    double aspect = w > 0 && h > 0 ? (double)w / h : 4.0 / 3.0;
    switch (s->aspect) {
    case SC_4_3: aspect = 4.0/3.0; break;
    case SC_8_7: aspect = 8.0/7.0; v.pixel_aspect = 1; break;
    case SC_16_10: aspect = 16.0/10.0; break;
    case SC_16_9: aspect = 16.0/9.0; break;
    case SC_21_9: aspect = 21.0/9.0; break;
    case SC_32_9: aspect = 32.0/9.0; break;
    default: break;
    }
    if (s->aspect != SC_FIT_WIDTH)
        v.width = extent(224 * aspect / v.pixel_aspect, 256);
    if (s->aspect != SC_FIT_HEIGHT)
        v.height = extent(256 * v.pixel_aspect / aspect, 224);
    v.core_x = s->centered ? (v.width - 256) / 2 : 0;
    v.core_y = s->centered ? (v.height - 224) / 2 : 0;
    return v;
}
ScVideoRect ScVideoDestination(ScViewport v, int w, int h) {
    if (w <= 0 || h <= 0) return (ScVideoRect){0,0,0,0};
    double aspect = v.width * v.pixel_aspect / v.height;
    int dw = w, dh = (int)floor(w / aspect + .5);
    if (dh > h) { dh = h; dw = (int)floor(h * aspect + .5); }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    return (ScVideoRect){(w-dw)/2, (h-dh)/2, dw, dh};
}
bool ScVideoToGuest(ScViewport v, ScVideoRect d, double x, double y, int *gx, int *gy) {
    if (d.w <= 0 || d.h <= 0) return false;
    int px = (int)floor((x-d.x) * v.width/d.w) - v.core_x;
    int py = (int)floor((y-d.y) * v.height/d.h) - v.core_y;
    if (gx) *gx = px;
    if (gy) *gy = py;
    return px >= 0 && px < 256 && py >= 0 && py < 224;
}
bool ScVideoWindowToGuest(ScViewport v, ScVideoRect d,
                          int ww, int wh, int dw, int dh,
                          double x, double y, int *gx, int *gy) {
    if (ww <= 0 || wh <= 0 || dw <= 0 || dh <= 0) return false;
    return ScVideoToGuest(v, d, x * dw / ww, y * dh / wh, gx, gy);
}
