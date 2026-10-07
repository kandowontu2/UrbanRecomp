#include "sc_mobile.h"
#include <stdatomic.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

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
