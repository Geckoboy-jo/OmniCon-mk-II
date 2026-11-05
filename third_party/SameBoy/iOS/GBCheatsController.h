#import <Core/gb.h>
#import <UIKit/UIKit.h>

@interface GBCheatsController : UITableViewController
- (instancetype)initWithGameBoy:(GB_gameboy_t *)gb;
@end
