/* Host presentation of the expanded native Present palette. The game still
 * grants, places and consumes each gift through its original routines. */
static ScViewport gift_view(void) {
  ScViewport v=s_custom_renderer.view;
  if(!s_custom_video.enabled)v=(ScViewport){s_video_w,224,s_ws_extra,0,1,0};
  return v;
}
static ScGiftLayout gift_layout(void) {
  ScViewport v=gift_view();return ScDebugGiftLayout(v.width,v.height);
}
static bool gift_mouse_point(SDL_Window *window,SDL_Renderer *renderer,double wx,double wy,int *x,int *y) {
  int ww,wh,dw,dh;SDL_GetWindowSize(window,&ww,&wh);SDL_GetRendererOutputSize(renderer,&dw,&dh);
  ScViewport v=gift_view();double cx,cy;
  if(!ScVideoWindowToCanvas(v,ScVideoDestination(v,dw,dh),ww,wh,dw,dh,wx,wy,&cx,&cy))return false;
  *x=(int)cx;*y=(int)cy;s_debug_gift_mouse_x=cx;s_debug_gift_mouse_y=cy;return true;
}
static void gift_text(uint32_t *pixels,int stride,int x,int y,const char *text,uint32_t ink) {
  for(;*text;++text,x+=8) {
    const uint8_t *glyph=s_custom_renderer.clipboard_font[(unsigned char)*text&127];
    for(int dy=0;dy<8;++dy)for(int dx=0;dx<8;++dx)
      if(!(((glyph[dy*2]>>(7-dx))&1)|(((glyph[dy*2+1]>>(7-dx))&1)<<1)))
        pixels[(y+dy)*stride+x+dx]=ink;
  }
}
static void render_gift_picker(SDL_Renderer *renderer) {
  static uint8_t icons[SC_DEBUG_GIFT_COUNT][32*32];static bool ready;
  static SDL_Texture *panel;static int panel_h;
  if(!ready)ready=ScDebugGiftIcons(icons,g_snes->cart->rom,g_snes->cart->romSize);
  if(!ready)return;
  ScGiftLayout l=gift_layout();ScViewport v=gift_view();
  if(!panel || panel_h!=l.h) {
    if(panel)SDL_DestroyTexture(panel);
    panel=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,l.w,l.h);
    if(!panel)return;panel_h=l.h;snesrecomp_sdl_set_texture_linear(panel,false);
  }
  uint32_t pixels[SC_DEBUG_GIFT_PANEL_WIDTH*SC_DEBUG_GIFT_PANEL_HEIGHT],colors[16];
  /* Gift OBJ palette zero is shared with the original city hand. */
  for(int i=0;i<16;++i) {
    unsigned c=g_ppu->cgram[128+i];
    colors[i]=0xff000000u|((unsigned)g_ppu->brightnessMult[c&31]<<16)|
      ((unsigned)g_ppu->brightnessMult[(c>>5)&31]<<8)|g_ppu->brightnessMult[(c>>10)&31];
  }
  const uint32_t paper=0xff302000,edge=0xff909080,light=0xffe8e8d0,shadow=0xff505048;
  for(int y=0;y<l.h;++y)for(int x=0;x<l.w;++x) {
    uint32_t ci=paper;
    if(x==0 || y==0 || x==l.w-1 || y==l.h-1)ci=0xff000000;
    else if(x==1 || y==1)ci=light;
    else if(x==l.w-2 || y==l.h-2)ci=shadow;
    else if(x==2 || y==2 || x==l.w-3 || y==l.h-3)ci=edge;
    pixels[y*l.w+x]=ci;
  }
  gift_text(pixels,l.w,(l.w-8*8)/2,5,"PRESENTS",light);
  for(int row=0;row<l.rows;++row)for(int col=0;col<l.cols;++col) {
    int i=row*l.cols+col;if(i>=SC_DEBUG_GIFT_COUNT)continue;
    int bx=8+col*40,by=16+row*40;
    for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
      unsigned ci=icons[i][y*32+x];
      pixels[(by+y)*l.w+bx+x]=ci?colors[ci]:paper;
    }
    if(i==s_debug_gift_selected) {
      ScGiftBounds b=ScDebugGiftIconBounds(icons[i]);
      for(int y=-1;y<=b.h;++y)for(int x=-1;x<=b.w;++x) {
        bool corner=(x==-1 || x==b.w)?(y<5 || y>b.h-6):
          (y==-1 || y==b.h) && (x<5 || x>b.w-6);
        if(corner)pixels[(by+b.y+y)*l.w+bx+b.x+x]=0xffffff90;
      }
    }
  }
  /* The original 8px lettering; every gift name fits the expanded box. */
  const char *name=ScDebugGiftNames[s_debug_gift_selected];
  int ty=16+l.rows*40;
  gift_text(pixels,l.w,(l.w-(int)strlen(name)*8)/2,ty,name,light);
  SDL_UpdateTexture(panel,NULL,pixels,l.w*4);
  int dw,dh;SDL_GetRendererOutputSize(renderer,&dw,&dh);
  ScVideoRect dest=ScVideoDestination(v,dw,dh);double sx=(double)dest.w/v.width,sy=(double)dest.h/v.height;
  ScRect rect=SC_RECT(dest.x+l.x*sx,dest.y+l.y*sy,l.w*sx,l.h*sy);
  SDL_RenderCopy(renderer,panel,NULL,&rect);
  if(s_debug_gift_pointer)for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
    uint32_t ci=ScRendererCityHandPixel(g_ppu,x,y);if(!ci)continue;
    SDL_SetRenderDrawColor(renderer,ci>>16&255,ci>>8&255,ci&255,255);
    ScRect pixel=SC_RECT(dest.x+(s_debug_gift_mouse_x+x)*sx,dest.y+(s_debug_gift_mouse_y+y)*sy,sx,sy);
    SDL_RenderFillRect(renderer,&pixel);
  }
}
