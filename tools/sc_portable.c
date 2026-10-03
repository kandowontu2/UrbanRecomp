/* Single-file Windows distribution. The appended, compressed bundle contains
 * only the release manifest's files, never a ROM or personal save/settings.
 * Windows loads runtime DLLs from a versioned per-user cache. Game data stays
 * in the portable executable's directory. No external unpacker is required. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum {PATH_CAP=32768,FOOTER_BYTES=56};
static wchar_t self[PATH_CAP],cache[PATH_CAP],destination[PATH_CAP],data_dir[PATH_CAP];
static uint32_t u32(const uint8_t *p) {uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t u64(const uint8_t *p) {uint64_t v;memcpy(&v,p,8);return v;}
static int fail(const wchar_t *message) {
    MessageBoxW(NULL,message,L"Urban Recomp",MB_OK|MB_ICONERROR);return 1;
}
static int digest(const void *p,size_t n,uint8_t out[32]) {
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;
    int ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0;
    if(ok) ok=BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)>=0;
    const uint8_t *bytes=p;
    while(ok && n) {ULONG part=n>0x1000000?0x1000000:(ULONG)n;
        ok=BCryptHashData(hash,(PUCHAR)bytes,part,0)>=0;bytes+=part;n-=part;}
    if(ok) ok=BCryptFinishHash(hash,out,32,0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(alg) BCryptCloseAlgorithmProvider(alg,0);
    return ok;
}
static int same_file(const wchar_t *path,const uint8_t *data,size_t size,const uint8_t hash[32]) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(f==INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER length;int ok=GetFileSizeEx(f,&length) && (uint64_t)length.QuadPart==size;
    HANDLE map=NULL;void *view=NULL;
    if(ok && size) {map=CreateFileMappingW(f,NULL,PAGE_READONLY,0,0,NULL);
        if(map) view=MapViewOfFile(map,FILE_MAP_READ,0,0,0);
        ok=view!=NULL;}
    uint8_t actual[32];if(ok) ok=digest(size?view:data,size,actual) && !memcmp(actual,hash,32);
    if(view) UnmapViewOfFile(view);
    if(map) CloseHandle(map);
    CloseHandle(f);return ok;
}
static int directories(wchar_t *path,int include_last) {
    size_t len=wcslen(path);
    for(size_t i=3;i<=len;++i) if(path[i]==L'\\' || (include_last && i==len)) {
        wchar_t c=path[i];path[i]=0;
        int ok=CreateDirectoryW(path,NULL) || GetLastError()==ERROR_ALREADY_EXISTS;
        path[i]=c;if(!ok) return 0;
    }
    return 1;
}
static int safe_name(const uint8_t *s,unsigned n) {
    if(!n || n>512 || s[0]=='/' || s[n-1]=='/') return 0;
    unsigned segment=0;
    for(unsigned i=0;i<=n;++i) {
        if(i==n || s[i]=='/') {
            unsigned length=i-segment;
            if(!length || (length==1 && s[segment]=='.') ||
                (length==2 && s[segment]=='.' && s[segment+1]=='.')) return 0;
            if(s[i?i-1:0]=='.' || s[i?i-1:0]==' ') return 0;
            segment=i+1;
        } else if(s[i]<32 || s[i]>126 || strchr("\\:*?\"<>|",s[i])) return 0;
    }
    return 1;
}
static int extract(const uint8_t *raw,size_t bytes,const wchar_t *root) {
    if(bytes<12 || memcmp(raw,"SCFILES1",8)) return 0;
    unsigned count=u32(raw+8);if(!count || count>4096) return 0;
    size_t at=12;
    for(unsigned i=0;i<count;++i) {
        if(bytes-at<44) return 0;
        unsigned n=u32(raw+at);uint64_t length=u64(raw+at+4);const uint8_t *hash=raw+at+12;at+=44;
        if(n>bytes-at || !safe_name(raw+at,n) || length>bytes-at-n) return 0;
        size_t base=wcslen(root);if(base+n+2>=PATH_CAP) return 0;
        wcscpy(destination,root);destination[base++]=L'\\';
        for(unsigned j=0;j<n;++j) destination[base+j]=raw[at+j]=='/'?L'\\':raw[at+j];
        destination[base+n]=0;at+=n;const uint8_t *data=raw+at;at+=(size_t)length;
        if(same_file(destination,data,(size_t)length,hash)) continue;
        uint8_t actual[32];if(!digest(data,(size_t)length,actual) || memcmp(actual,hash,32)) return 0;
        if(!directories(destination,0)) return 0;
        wchar_t *temp=malloc(PATH_CAP*sizeof *temp);if(!temp) return 0;
        int name_ok=swprintf(temp,PATH_CAP,L"%ls.%lu.part",destination,GetCurrentProcessId())>0;
        HANDLE f=name_ok?CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL):INVALID_HANDLE_VALUE;
        int ok=f!=INVALID_HANDLE_VALUE;size_t left=(size_t)length;
        while(ok && left) {DWORD written=0,chunk=left>0x1000000?0x1000000:(DWORD)left;
            ok=WriteFile(f,data,chunk,&written,NULL) && written==chunk;data+=chunk;left-=chunk;}
        if(f!=INVALID_HANDLE_VALUE) {ok=FlushFileBuffers(f) && ok;CloseHandle(f);}
        if(ok) ok=MoveFileExW(temp,destination,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        if(!ok) DeleteFileW(temp);
        free(temp);if(!ok) return 0;
    }
    return at==bytes;
}
/* Quote each original argument according to the Windows CRT rules, including
 * trailing backslashes. No shell, script or string command is executed. */
static int argument(wchar_t *out,size_t *at,const wchar_t *s) {
    if(*at+wcslen(s)*2+4>=PATH_CAP) return 0;
    out[(*at)++]=L'"';
    while(*s) {
        unsigned slashes=0;while(*s==L'\\') {++slashes;++s;}
        unsigned repeat=(*s==L'"' || !*s)?slashes*2:slashes;
        while(repeat--) out[(*at)++]=L'\\';
        if(*s==L'"') out[(*at)++]=L'\\';
        if(*s) out[(*at)++]=*s++;
    }
    out[(*at)++]=L'"';out[(*at)++]=L' ';out[*at]=0;return 1;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR line,int show) {
    (void)instance;(void)previous;(void)line;(void)show;
    DWORD n=GetModuleFileNameW(NULL,self,PATH_CAP);if(!n || n>=PATH_CAP) return fail(L"Cannot find the portable executable.");
    HANDLE f=CreateFileW(self,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    if(f==INVALID_HANDLE_VALUE) return fail(L"Cannot read the portable executable.");
    LARGE_INTEGER length;int ok=GetFileSizeEx(f,&length) && length.QuadPart>FOOTER_BYTES;
    HANDLE map=ok?CreateFileMappingW(f,NULL,PAGE_READONLY,0,0,NULL):NULL;
    const uint8_t *mapped=map?MapViewOfFile(map,FILE_MAP_READ,0,0,0):NULL;
    if(!mapped) {if(map) CloseHandle(map);CloseHandle(f);return fail(L"Cannot open the embedded bundle.");}
    const uint8_t *footer=mapped+length.QuadPart-FOOTER_BYTES;
    uint64_t compressed=u64(footer+8),raw_size=u64(footer+16);
    ok=!memcmp(footer,"SCBNDL01",8) && compressed>0 && compressed<(uint64_t)length.QuadPart-FOOTER_BYTES && raw_size>=12 && raw_size<UINT64_C(2147483648);
    uint8_t *raw=ok?malloc((size_t)raw_size):NULL;ok=raw!=NULL;
    HMODULE cabinet=LoadLibraryW(L"cabinet.dll");
    typedef BOOL (WINAPI *CreateFn)(DWORD,void *,void **);
    typedef BOOL (WINAPI *ExpandFn)(void *,const void *,SIZE_T,void *,SIZE_T,SIZE_T *);
    typedef BOOL (WINAPI *CloseFn)(void *);
    CreateFn create=NULL;ExpandFn expand=NULL;CloseFn close=NULL;
    if(cabinet) {
        FARPROC proc=GetProcAddress(cabinet,"CreateDecompressor");memcpy(&create,&proc,sizeof create);
        proc=GetProcAddress(cabinet,"Decompress");memcpy(&expand,&proc,sizeof expand);
        proc=GetProcAddress(cabinet,"CloseDecompressor");memcpy(&close,&proc,sizeof close);
    }
    void *decoder=NULL;SIZE_T got=0;
    ok=ok && create && expand && close && create(4,NULL,&decoder);
    if(ok) ok=expand(decoder,footer-compressed,(SIZE_T)compressed,raw,(SIZE_T)raw_size,&got) && got==raw_size;
    uint8_t hash[32];if(ok) ok=digest(raw,(size_t)raw_size,hash) && !memcmp(hash,footer+24,32);
    if(decoder) close(decoder);
    if(cabinet) FreeLibrary(cabinet);
    UnmapViewOfFile(mapped);CloseHandle(map);CloseHandle(f);
    if(!ok) {free(raw);return fail(L"The embedded bundle is damaged or cannot be unpacked. Download the release again.");}
    int argc;wchar_t **argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv) {free(raw);return fail(L"Cannot read launch arguments.");}
    int extraction=argc==3 && !wcscmp(argv[1],L"--portable-extract");
    if(extraction) {
        n=GetFullPathNameW(argv[2],PATH_CAP,cache,NULL);ok=n && n<PATH_CAP;
    } else {
        n=GetEnvironmentVariableW(L"LOCALAPPDATA",cache,PATH_CAP);ok=n && n<PATH_CAP-100;
        if(ok) {wcscat(cache,L"\\UrbanRecomp\\bundles\\");size_t pos=wcslen(cache);
            for(unsigned i=0;i<32;++i) swprintf(cache+pos+2*i,3,L"%02x",hash[i]);}
    }
    wchar_t mutex_name[100]=L"Local\\UrbanRecompBundle_";size_t m=wcslen(mutex_name);
    for(unsigned i=0;i<32;++i) swprintf(mutex_name+m+2*i,3,L"%02x",hash[i]);
    HANDLE mutex=ok?CreateMutexW(NULL,FALSE,mutex_name):NULL;
    if(!mutex) ok=0;
    if(ok) {DWORD wait=WaitForSingleObject(mutex,120000);ok=wait==WAIT_OBJECT_0 || wait==WAIT_ABANDONED;}
    if(ok) ok=directories(cache,1) && extract(raw,(size_t)raw_size,cache);
    if(mutex) {ReleaseMutex(mutex);CloseHandle(mutex);}free(raw);
    if(!ok) {LocalFree(argv);return fail(L"Cannot prepare the private runtime cache. Check available disk space and folder permissions.");}
    if(extraction) {LocalFree(argv);return 0;}
    if(argc==2 && !wcscmp(argv[1],L"--portable-docs")) {
        ShellExecuteW(NULL,L"open",cache,NULL,NULL,SW_SHOWNORMAL);LocalFree(argv);return 0;
    }
    wcscpy(data_dir,self);wchar_t *slash=wcsrchr(data_dir,L'\\');if(slash) *slash=0;
    n=GetEnvironmentVariableW(L"SC_PORTABLE_DATA_DIR",destination,PATH_CAP);
    if(n && n<PATH_CAP) wcscpy(data_dir,destination);
    if(swprintf(destination,PATH_CAP,L"%ls\\UrbanRecomp.exe",cache)<0) {LocalFree(argv);return fail(L"Runtime path is too long.");}
    wchar_t *command=calloc(PATH_CAP,sizeof *command);size_t at=0;
    ok=command && argument(command,&at,destination);
    for(int i=1;ok && i<argc;++i) ok=argument(command,&at,argv[i]);
    LocalFree(argv);
    if(!ok) {free(command);return fail(L"Launch arguments are too long.");}
    STARTUPINFOW startup={0};startup.cb=sizeof startup;
    HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
    if(output && output!=INVALID_HANDLE_VALUE) {startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdOutput=output;startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);}
    PROCESS_INFORMATION process={0};
    ok=CreateProcessW(destination,command,NULL,NULL,TRUE,0,NULL,data_dir,&startup,&process);free(command);
    if(!ok) return fail(L"Cannot start the bundled game.");
    CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);
    DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);return (int)code;
}
