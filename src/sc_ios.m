#include "sc_mobile.h"
#include <stdatomic.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
const char *ScIosDataPath(void) {
    static NSString *path;
    path=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
    [[NSFileManager defaultManager] createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil];
    // Expose the bundled controls, credits and licenses alongside saves in
    // Files. Only these app-owned documents are refreshed on launch.
    for(NSString *name in @[@"CREDITS.md",@"MOBILE.md",@"CHANGELOG.md",@"THIRD_PARTY_NOTICES.md",@"LICENSE",@"licenses"]) {
        NSString *source=[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:name];
        NSString *target=[path stringByAppendingPathComponent:name];
        if([[NSFileManager defaultManager] fileExistsAtPath:source]) {
            [[NSFileManager defaultManager] removeItemAtPath:target error:nil];
            [[NSFileManager defaultManager] copyItemAtPath:source toPath:target error:nil];
        }
    }
    return path.fileSystemRepresentation;
}

@interface ScRomPicker : NSObject <UIDocumentPickerDelegate>
@property(nonatomic,assign) atomic_int *result;
@end
@implementation ScRomPicker
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    NSURL *url=urls.firstObject;
    BOOL scoped=[url startAccessingSecurityScopedResource];
    NSNumber *size=nil;[url getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
    NSData *data=size.unsignedLongLongValue<=0x80200?[NSData dataWithContentsOfURL:url]:nil;
    bool ok=data&&ScMobileImportRom(data.bytes,(unsigned)data.length);
    if(scoped)[url stopAccessingSecurityScopedResource];
    atomic_store(self.result,ok?1:-2);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {atomic_store(self.result,-1);}
@end
void ScIosPickRom(SDL_Window *window,atomic_int *result) {
    static ScRomPicker *delegate;
    delegate=[ScRomPicker new];delegate.result=result;
    UIWindow *ui=(__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER,NULL);
    UIDocumentPickerViewController *picker=[[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeData] asCopy:YES];
    picker.delegate=delegate;picker.allowsMultipleSelection=NO;
    [ui.rootViewController presentViewController:picker animated:YES completion:nil];
}
