#import <Core/gb.h>
#import <UIKit/UIKit.h>

@interface GBPalettePicker : UITableViewController
+ (const GB_palette_t *)paletteForTheme:(NSString *)theme;
+ (bool)importPalette:(NSString *)path;
+ (NSString *)makeUnique:(NSString *)name;
@end
