#import <MetalKit/MetalKit.h>
#import <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import "../iOS/GBView.h"
#else
#import "../Cocoa/GBView.h"
#endif

@interface GBViewMetal : GBView <MTKViewDelegate>
+ (bool)isSupported;
@end
