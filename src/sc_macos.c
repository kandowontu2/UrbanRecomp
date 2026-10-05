#include "sc_macos.h"
#ifdef __APPLE__
#include "sc_sdl_compat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

bool ScMacPreparePaths(int argc,char **argv) {
#if SNESRECOMP_SDL3
    const char *base=SDL_GetBasePath();
#else
    char *base=SDL_GetBasePath();
#endif
    bool bundled=base && strstr(base,".app/Contents/");
#if !SNESRECOMP_SDL3
    SDL_free(base);
#endif
    if(!bundled)return true; /* preserve command-line developer builds */
    char *data=SDL_GetPrefPath("UrbanRecomp","UrbanRecomp");
    if(!data) {fprintf(stderr,"Cannot create Mac save/settings directory: %s\n",SDL_GetError());return false;}
    /* Resolve existing command-line files before changing directory. Finder's
     * ROM picker supplies absolute paths; terminal ROM/state arguments may not. */
    for(int i=1;i<argc;++i) {
        if(argv[i][0]=='-' || argv[i][0]=='/')continue;
        char absolute[PATH_MAX];
        if(realpath(argv[i],absolute)) {
            char *copy=strdup(absolute);
            if(!copy) {SDL_free(data);return false;}
            argv[i]=copy; /* main owns these arguments for the process lifetime */
        }
    }
    bool ok=chdir(data)==0;
    if(ok)fprintf(stderr,"[macOS] saves/settings: %s\n",data);
    else fprintf(stderr,"Cannot open Mac save/settings directory: %s\n",data);
    SDL_free(data);return ok;
}
#else
bool ScMacPreparePaths(int argc,char **argv) {(void)argc;(void)argv;return true;}
#endif
