/* The cartridge's save memory on disk -- see sc_sram.h. */
#include "sc_sram.h"

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Host frames the SRAM must stay unchanged before it is written. */
enum { kQuietFrames = 30 };

static uint8_t *s_ram;          /* the cart model's SRAM */
static uint32_t s_size;
static char s_path[1024];
static uint8_t *s_disk;         /* what the file holds */
static uint8_t *s_seen;         /* the SRAM as of the last tick */
static uint8_t *s_held;         /* the SRAM from before a state load */
static int s_quiet;
static bool s_active;
static bool s_write_failed;
static uint8_t *s_extra;
static uint32_t s_extra_size;
static bool s_extra_dirty,s_suspended;
enum {kExtraHeader=24,kExtraMax=32*1024*1024};
static uint64_t extra_hash(const uint8_t *p,uint32_t n) {
  uint64_t h=UINT64_C(14695981039346656037);
  for(uint32_t i=0;i<n;++i) h=(h^p[i])*UINT64_C(1099511628211);
  return h;
}
static uint64_t extra_get(const uint8_t *p,unsigned n) {
  uint64_t v=0;for(unsigned i=0;i<n;++i)v|=(uint64_t)p[i]<<(8*i);return v;
}
static void extra_put(uint8_t *p,uint64_t v,unsigned n) {for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(8*i));}

static bool write_file(const char *path, const uint8_t *data, uint32_t size) {
  char tmp[1040];
  snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb");
  if (!f) return false;
  bool ok = fwrite(data, 1, size, f) == size;
  ok = fclose(f) == 0 && ok;
#ifdef _WIN32
  ok = ok && MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING |
                                        MOVEFILE_WRITE_THROUGH) != 0;
#else
  ok = ok && rename(tmp, path) == 0;
#endif
  if (!ok) remove(tmp);
  return ok;
}
static bool write_bundle(const char *path,const uint8_t *ram,uint32_t size) {
  uint32_t n=size+(s_extra_size?kExtraHeader+s_extra_size:0);
  uint8_t *data=malloc(n);if(!data)return false;
  memcpy(data,ram,size);
  if(s_extra_size) {
    uint8_t *h=data+size;memset(h,0,kExtraHeader);memcpy(h,"SCEXTRA",7);h[7]=1;
    extra_put(h+8,s_extra_size,4);extra_put(h+16,extra_hash(s_extra,s_extra_size),8);
    memcpy(h+kExtraHeader,s_extra,s_extra_size);
  }
  bool ok=write_file(path,data,n);free(data);return ok;
}

bool ScSram_Open(uint8_t *ram, uint32_t size, const char *path) {
  s_active=false;s_suspended=false;s_extra_dirty=false;s_write_failed=false;
  free(s_disk);free(s_seen);free(s_held);free(s_extra);
  s_disk=s_seen=s_held=s_extra=NULL;s_extra_size=0;
  if (!ram || !size || !path || !*path) return false;
  snprintf(s_path, sizeof s_path, "%s", path);

  FILE *f = fopen(s_path, "rb");
  if (f) {
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    bool valid=n==(long)size;
    if(n>(long)size && n<=(long)size+kExtraHeader+kExtraMax) {
      uint8_t header[kExtraHeader];fseek(f,size,SEEK_SET);
      valid=fread(header,1,sizeof header,f)==sizeof header && !memcmp(header,"SCEXTRA",7) &&
          header[7]==1 && extra_get(header+8,4)==(uint64_t)n-size-kExtraHeader &&
          !extra_get(header+12,4);
      if(valid) {
        s_extra_size=(uint32_t)extra_get(header+8,4);s_extra=malloc(s_extra_size);
        valid=s_extra && fread(s_extra,1,s_extra_size,f)==s_extra_size &&
            extra_hash(s_extra,s_extra_size)==extra_get(header+16,8);
      }
    }
    if (!valid) {
      fclose(f);
      free(s_extra);s_extra=NULL;s_extra_size=0;
      fprintf(stderr, "sram: %s has %ld bytes, this cartridge %u (invalid size or trailer) -- the file is "
                      "left alone and saving is off\n", s_path, n, (unsigned)size);
      return false;
    }
    fseek(f, 0, SEEK_SET);
    const bool ok = fread(ram, 1, size, f) == size;
    fclose(f);
    if (!ok) {
      fprintf(stderr, "sram: could not read %s -- saving is off\n", s_path);
      return false;
    }
    char bak[1040];
    snprintf(bak, sizeof bak, "%s.bak", s_path);
    if (!write_bundle(bak, ram, size))
      fprintf(stderr, "sram: could not write the backup %s\n", bak);
    fprintf(stderr, "sram: loaded %s\n", s_path);
  } else {
    if(errno!=ENOENT) {fprintf(stderr,"sram: cannot read %s -- saving is off\n",s_path);return false;}
    fprintf(stderr, "sram: no %s yet; it is written when the game first "
                    "stores something\n", s_path);
  }

  s_disk = (uint8_t *)malloc(size);
  s_seen = (uint8_t *)malloc(size);
  s_held = (uint8_t *)malloc(size);
  if (!s_disk || !s_seen || !s_held) {
    free(s_disk); free(s_seen); free(s_held);
    s_disk = s_seen = s_held = NULL;
    return false;
  }
  memcpy(s_disk, ram, size);
  memcpy(s_seen, ram, size);
  s_ram = ram;
  s_size = size;
  s_quiet = kQuietFrames;
  s_active = true;
  return true;
}

bool ScSram_Active(void) { return s_active; }

static void save_now(void) {
  bool ok=write_bundle(s_path,s_ram,s_size);
  if (ok) {
    memcpy(s_disk, s_ram, s_size);
    s_write_failed = false;
    s_extra_dirty=false;
    fprintf(stderr, "sram: saved %s\n", s_path);
  } else if (!s_write_failed) {
    s_write_failed = true;
    fprintf(stderr, "sram: could not write %s\n", s_path);
  }
}

void ScSram_Tick(void) {
  if (!s_active || s_suspended) return;
  if (memcmp(s_ram, s_seen, s_size) != 0) {
    memcpy(s_seen, s_ram, s_size);
    s_quiet = 0;
    return;
  }
  if (s_extra_dirty) {save_now();return;}
  if (s_quiet >= kQuietFrames) return;
  if (++s_quiet < kQuietFrames) return;
  if (memcmp(s_ram, s_disk, s_size) != 0) {
    save_now();
    if (s_write_failed) s_quiet = 0;   /* try again in half a second */
  }
}

void ScSram_Flush(void) {
  if (s_active && !s_suspended && (s_extra_dirty || memcmp(s_ram, s_disk, s_size) != 0)) save_now();
}

void ScSram_Hold(void) {
  if (s_active) memcpy(s_held, s_ram, s_size);
}

void ScSram_Release(void) {
  if (!s_active) return;
  memcpy(s_ram, s_held, s_size);
  memcpy(s_seen, s_ram, s_size);
}
const uint8_t *ScSram_Extra(uint32_t *size) {if(size)*size=s_extra_size;return s_extra;}
bool ScSram_SetExtra(const uint8_t *data,uint32_t size) {
  if(!s_active || !data || !size || size>kExtraMax) return false;
  uint8_t *copy=malloc(size);if(!copy)return false;memcpy(copy,data,size);
  free(s_extra);s_extra=copy;s_extra_size=size;s_extra_dirty=true;return true;
}
void ScSram_Suspend(bool suspended) {s_suspended=suspended;}

bool ScSram_Stored(void) {return s_active && !s_suspended && !s_extra_dirty && !s_write_failed && !memcmp(s_ram,s_disk,s_size);}
