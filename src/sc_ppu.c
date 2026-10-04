/* Keep the pinned runner's normal renderer intact. Including its translation
 * unit also lets the skipped-frame path reuse its exact sprite evaluator and
 * OAM history; neither guest flags nor the next displayed frame may change. */
#include "sc_ppu.h"
#include "sc_obj.h"
#include "sc_native_ppu.h"
#define ppu_reset ScPpuReferenceReset
#define ppu_free ScPpuReferenceFree
#define ppu_saveload ScPpuReferenceSaveLoad
#define ppu_runLine ScPpuReferenceLine
#define ppu_write ScPpuReferenceWrite
#include "snes/ppu.c"
#undef ppu_runLine
#undef ppu_write
#undef ppu_reset
#undef ppu_free
#undef ppu_saveload
void ppu_reset(Ppu *p) {ScObjInvalidate(p);ScPpuReferenceReset(p);}
void ppu_free(Ppu *p) {ScObjInvalidate(p);ScPpuReferenceFree(p);}
void ppu_saveload(Ppu *p,SaveLoadInfo *sli) {
    ScObjMaterialize(p);ScObjInvalidate(p);ScPpuReferenceSaveLoad(p,sli);
}

static uint64_t sc_vram_revision;
uint64_t ScPpuVramRevision(const Ppu *ppu) {(void)ppu;return sc_vram_revision;}
void ScPpuVramChanged(void) {++sc_vram_revision;}
void ppu_write(Ppu *ppu,uint8_t adr,uint8_t value) {
    ScPpuReferenceWrite(ppu,adr,value);
    if(adr==0x18 || adr==0x19) ++sc_vram_revision;
}

/* Cache vertical OAM membership, preserving the original rotated forward
 * order. Compare the complete live OAM on every line: DMA, host cursor edits
 * and mid-frame size/priority changes must invalidate it immediately. X,
 * widescreen hints and hardware range/sliver admission remain live below. */
static const uint8_t *sc_obj_candidates(Ppu *p,unsigned line,const uint8_t sizes[8][2],unsigned *count) {
    static Ppu *owner;
    static uint16_t oam[256];static uint8_t high[32];
    static uint8_t rows[256][128],counts[256];
    static unsigned old_size,old_interlace,old_first;
    unsigned size=PPU_objSize(p),interlace=PPU_objInterlace(p)!=0;
    unsigned first=PPU_objPriority(p)?p->oamaddl&0xfe:0;
    if(owner!=p || size!=old_size || interlace!=old_interlace || first!=old_first ||
       memcmp(oam,p->oam,sizeof oam) || memcmp(high,p->highOam,sizeof high)) {
        owner=p;old_size=size;old_interlace=interlace;old_first=first;
        memcpy(oam,p->oam,sizeof oam);memcpy(high,p->highOam,sizeof high);memset(counts,0,sizeof counts);
        for(unsigned i=0;i<128;++i) {
            unsigned index=(first+2*i)&255,y=p->oam[index]>>8;
            unsigned h=sizes[size][(p->highOam[index>>3]>>((index&7)+1))&1];
            if(interlace) h/=2;
            for(unsigned dy=0;dy<h;++dy) {
                unsigned row=(y+dy)&255;rows[row][counts[row]++]=(uint8_t)index;
            }
        }
    }
    *count=counts[line&255];return rows[line&255];
}
static bool sc_evaluate_obj_descriptors(Ppu* ppu, int line) {
  static const uint8 spriteSizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32},
    {16, 64}, {32, 64}, {16, 32}, {16, 32}
  };

  // TODO: rectangular sprites, wierdness with sprites at -256
  uint8_t index = PPU_objPriority(ppu) ? (ppu->oamaddl & 0xfe) : 0;
  int spritesFound = 0;
  int tilesFound = 0;
  uint8_t foundSprites[128];

  // Range evaluation walks OAM forward, but tile fetching walks the accepted
  // sprites backward. This is observable when the 34-sliver limit is reached.
  unsigned candidate_count;
  const uint8_t *candidates=sc_obj_candidates(ppu,(unsigned)line,spriteSizes,&candidate_count);
  for(unsigned i = 0; i < candidate_count; i++) {
    index=candidates[i];
    uint8_t y = ppu->oam[index] >> 8;
    uint8_t row = line - y;
    int spriteSize = spriteSizes[PPU_objSize(ppu)][(ppu->highOam[index >> 3] >> ((index & 7) + 1)) & 1];
    int spriteHeight = PPU_objInterlace(ppu) ? spriteSize / 2 : spriteSize;
    if(row < spriteHeight) {
      int x = PpuDecodeOamX(ppu, index);
      const int left_extra =
          PpuWidescreenHudOamSlot(ppu, index, y) &&
                  (ppu->wsHudSplitHeight & 0x80)
              ? ppu->extraLeftRight
              : ppu->extraLeftCur;
      if (PpuWidescreenOamLeftHintAllows(ppu, index, x, spriteSize,
                                          left_extra)) {
        x = PpuAdjustWidescreenHudOamX(ppu, index, y, x);
        if(x + spriteSize > -left_extra) {
          spritesFound++;
          if(spritesFound > 32 &&
             !(ppu->renderFlags & kPpuRenderFlags_NoSpriteLimits)) {
            ppu->rangeOver = true;
            spritesFound = 32;
            break;
          }
          foundSprites[spritesFound - 1] = index;
        }
      }
    }
  }

  for(int i = spritesFound; i > 0; i--) {
    index = foundSprites[i - 1];
    uint8_t row = line - (ppu->oam[index] >> 8);
    int spriteSize = spriteSizes[PPU_objSize(ppu)][(ppu->highOam[index >> 3] >> ((index & 7) + 1)) & 1];
    int x = PpuDecodeOamX(ppu, index);
    const uint8_t sprite_y = ppu->oam[index] >> 8;
    const bool full_budget_hud =
        PpuWidescreenHudOamSlot(ppu, index, sprite_y) &&
        (ppu->wsHudSplitHeight & 0x80);
    const int left_extra =
        full_budget_hud ? ppu->extraLeftRight : ppu->extraLeftCur;
    const int right_extra =
        full_budget_hud ? ppu->extraLeftRight : ppu->extraRightCur;
    x = PpuAdjustWidescreenHudOamX(ppu, index, sprite_y, x);
        if(PPU_objInterlace(ppu)) row = row * 2 + (ppu->evenFrame ? 0 : 1);
        int oam1 = ppu->oam[index + 1];
        int objAdr = (oam1 & 0x100) ? PPU_objTileAdr2(ppu) : PPU_objTileAdr1(ppu);
        if(oam1 & 0x8000) row = spriteSize - 1 - row;
        int paletteBase = 0x80 + 16 * ((oam1 & 0xe00) >> 9);
        int prio = SPRITE_PRIO_TO_PRIO((oam1 & 0x3000) >> 12, (oam1 & 0x800) == 0);
        PpuZbufType z = paletteBase + (prio << 8);

        for(int col = 0; col < spriteSize; col += 8) {
      if(col + x <= -8 - left_extra ||
         col + x >= 256 + right_extra)
        continue;
            tilesFound++;
            if(tilesFound > 34 &&
               !(ppu->renderFlags & kPpuRenderFlags_NoSpriteLimits)) {
              ppu->timeOver = true;
              break;
            }
            int usedCol = oam1 & 0x4000 ? spriteSize - 1 - col : col;
      int usedTile = ((((oam1 & 0xff) >> 4) + (row >> 3)) << 4) |
                     (((oam1 & 0xf) + (usedCol >> 3)) & 0xf);
            uint16 *addr = &ppu->vram[(objAdr + usedTile * 16 + (row & 0x7)) & 0x7fff];
            uint32 plane = addr[0] | addr[8] << 16;
            int px_left = IntMax(-(col + x + kPpuExtraLeftRight), 0);
            int px_right = IntMin(256 + kPpuExtraLeftRight - (col + x), 8);
            int slot = index >> 1;
             /* Clip an unhinted sprite at the SCREEN EDGE, per pixel.
             *
             * PpuWidescreenOamLeftHintAllows() gates whole sprites, so a
             * sprite is drawn entirely or not at all. For an object sliding
             * off the left that means it pops out in whole-sprite steps --
             * the title's SimCity sign is three 16 px sprites, so it leaves
             * in three 16 px chunks instead of sliding.
             *
             * Hardware clips at x=0 per pixel. Doing the same here makes the
             * object leave smoothly and costs nothing elsewhere: a sprite
             * fully on screen never has a negative pixel, and a host-placed
             * one is hinted and keeps its margin pixels. */
            static int ws_edge_clip_on = -1;
            if (ws_edge_clip_on < 0) {
              const char *e = getenv("SC_WS_OBJ_EDGE_CLIP");
              ws_edge_clip_on = (e && *e) ? (*e != '0') : 1;
            }
            /* NOT while the sprite is moving.
             *
             * PpuWidescreenOamLeftHintAllows() now lets an unhinted OBJ
             * with motion grace travel into the widened left margin. This
             * clip then threw away every one of its pixels past x=0, so
             * the object was admitted and immediately erased -- reported
             * from play as the title's SimCity sign fading out ON screen
             * rather than leaving at the true edge. Measured on the title:
             * with the clip off the sign tracks smoothly from x=56..94
             * down to x=34..72 across the margin; with it on those pixels
             * are simply gone.
             *
             * It still applies to a sprite that is NOT moving, which is
             * what it was written for: an unhinted parked object has no
             * business in the margin, and clipping it at the screen edge
             * beats popping it out a whole sprite at a time. */
            const bool ws_clip_left =
                ws_edge_clip_on && ppu->wsOamLeftHintStrict &&
                !(ppu->wsOamLeftHint[slot >> 3] & (1u << (slot & 7))) &&
                !(ppu->wsOamMotionGraceOn && ppu->wsOamMotionGrace[slot]);
            if(ws_clip_left && col+x+px_left<0) px_left=IntMax(px_left,-(col+x));
            if(px_left<px_right) ScObjAppend((uint16_t)(col+x)|((uint32_t)z<<16),
                (uint32_t)(addr-ppu->vram)|((oam1&0x4000)?0x8000:0)|
                ((unsigned)px_left<<16)|((unsigned)px_right<<20),plane);
        }
        if(tilesFound > 34 &&
           !(ppu->renderFlags & kPpuRenderFlags_NoSpriteLimits))
      break;
  }
  return tilesFound != 0;
}

static bool sc_skip_pixels;
static bool sc_defer_objects,sc_skip_objects;
void ScPpuDeferObjects(bool defer) {sc_defer_objects=defer;}
void ScPpuSkipObjects(bool skip) {sc_skip_objects=skip;}
static uint64_t (*sc_pixel_counter)(void);
static double sc_pixel_ms,sc_tick_ms;
void ScPpuSkipPixels(bool skip) { sc_skip_pixels=skip; }
bool ScPpuPixelsSkipped(void) { return sc_skip_pixels; }
void ScPpuMeasurePixels(uint64_t (*counter)(void),double milliseconds_per_tick) {
    sc_pixel_counter=counter;sc_tick_ms=milliseconds_per_tick;sc_pixel_ms=0;
}
double ScPpuPixelMilliseconds(void) { return sc_pixel_ms; }

void ppu_runLine(Ppu *ppu,int line) {
    if(line==0) ScObjMaterialize(ppu);
    ScObjInvalidate(ppu);
    static int obj_reference=-1;
    if(obj_reference<0) {const char *e=getenv("SC_GPU_NATIVE_OBJ_REFERENCE");obj_reference=e && *e=='1';}
    bool deferred_obj=!obj_reference && sc_skip_pixels && (sc_defer_objects || sc_skip_objects) &&
        ScNativePpuSupported(ppu);
    if ((!sc_skip_pixels && !sc_pixel_counter) || line==0) {
        ScPpuReferenceLine(ppu,line);
        return;
    }
    /* Same non-image work as the runner's ppu_runLine, including per-line
     * diagnostics, brightness caching and original sprite/sliver selection.
     * Eligible native and undisplayed rows retain descriptors instead of pixels.
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
    if(deferred_obj) {
        ScObjBegin(ppu);ppu->lineHasSprites=sc_evaluate_obj_descriptors(ppu,line-1);
    } else ppu->lineHasSprites=!PPU_forcedBlank(ppu) && ppu_evaluateSprites(ppu,line-1);
    if(!sc_skip_pixels) {
        uint64_t start=sc_pixel_counter?sc_pixel_counter():0;
        if(ppu->renderFlags & kPpuRenderFlags_NewRenderer) PpuDrawWholeLine(ppu,line);
        else ppu_draw_whole_line_legacy(ppu,line);
        if(sc_pixel_counter) sc_pixel_ms+=(sc_pixel_counter()-start)*sc_tick_ms;
    }
}
