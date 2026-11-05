#import "Document.h"
#import <AppKit/AppKit.h>

@interface GBCheatSearchController<NSTableViewDelegate, NSTableViewDataSource,
                                   NSControlTextEditingDelegate> : NSObject
@property IBOutlet NSWindow *window;
@property IBOutlet NSTableView *tableView;
+ (instancetype)controllerWithDocument:(Document *)document;
@end
