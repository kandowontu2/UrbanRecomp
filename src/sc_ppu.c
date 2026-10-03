/* Keep the pinned runner's normal renderer intact. Including its translation
 * unit also lets the skipped-frame path reuse its exact sprite evaluator and
 * OAM history; neither guest flags nor the next displayed frame may change. */
#include "sc_ppu.h"
#define ppu_runLine ScPpuReferenceLine
#define ppu_write ScPpuReferenceWrite
#include "snes/ppu.c"
#undef ppu_runLine
#undef ppu_write

static uint64_t sc_vram_revision;
uint64_t ScPpuVramRevision(const Ppu *ppu) {(void)ppu;return sc_vram_revision;}
void ScPpuVramChanged(void) {++sc_vram_revision;}
void ppu_write(Ppu *ppu,uint8_t adr,uint8_t value) {
    ScPpuReferenceWrite(ppu,adr,value);
    if(adr==0x18 || adr==0x19) ++sc_vram_revision;
}

static bool sc_skip_pixels;
static uint64_t (*sc_pixel_counter)(void);
static double sc_pixel_ms,sc_tick_ms;
void ScPpuSkipPixels(bool skip) { sc_skip_pixels=skip; }
bool ScPpuPixelsSkipped(void) { return sc_skip_pixels; }
void ScPpuMeasurePixels(uint64_t (*counter)(void),double milliseconds_per_tick) {
    sc_pixel_counter=counter;sc_tick_ms=milliseconds_per_tick;sc_pixel_ms=0;
}
double ScPpuPixelMilliseconds(void) { return sc_pixel_ms; }

void ppu_runLine(Ppu *ppu,int line) {
    if ((!sc_skip_pixels && !sc_pixel_counter) || line==0) {
        ScPpuReferenceLine(ppu,line);
        return;
    }
    /* Same non-image work as the runner's ppu_runLine, including per-line
     * diagnostics, brightness caching and full sprite/sliver evaluation.
     * BG buffers and framebuffer bytes are display scratch, not guest state. */
    debug_server_on_ppu_line(line);
    if (s_oam_snap_frame!=snes_frame_counter) {
        s_oam_snap_frame=snes_frame_counter;
        debug_server_on_oam_render();
    }
    PpuUpdateWidescreenOamHistory(ppu,line);
    if (PPU_brightness(ppu)!=ppu->lastBrightnessMult) {
        uint8_t brightness=PPU_brightness(ppu);
        ppu->lastBrightnessMult=brightness;
        for (int i=0;i<32;++i)
            ppu->brightnessMultHalf[i*2]=ppu->brightnessMultHalf[i*2+1]=ppu->brightnessMult[i]=
                ((i<<3)|(i>>2))*brightness/15;
        memset(&ppu->brightnessMult[32],ppu->brightnessMult[31],31);
    }
    ClearBackdrop(&ppu->objBuffer);
    if (ppu->overlayRenderBuffer[kPpuOverlaySource_Obj])
        memset(&ppu->overlayBuffers[kPpuOverlaySource_Obj],0,
               sizeof(ppu->overlayBuffers[kPpuOverlaySource_Obj]));
    ppu->lineHasSprites=!PPU_forcedBlank(ppu) && ppu_evaluateSprites(ppu,line-1);
    if(!sc_skip_pixels) {
        uint64_t start=sc_pixel_counter?sc_pixel_counter():0;
        if(ppu->renderFlags & kPpuRenderFlags_NewRenderer) PpuDrawWholeLine(ppu,line);
        else ppu_draw_whole_line_legacy(ppu,line);
        if(sc_pixel_counter) sc_pixel_ms+=(sc_pixel_counter()-start)*sc_tick_ms;
    }
}
