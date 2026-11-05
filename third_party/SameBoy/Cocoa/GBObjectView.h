#import "Document.h"
#import <Cocoa/Cocoa.h>

@interface GBObjectView : NSView
- (void)reloadData:(Document *)document;
@end
