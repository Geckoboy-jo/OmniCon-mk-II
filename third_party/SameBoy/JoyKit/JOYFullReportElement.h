#import "JOYElement.h"
#import <Foundation/Foundation.h>
#import <IOKit/hid/IOHIDLib.h>

@interface JOYFullReportElement : JOYElement <NSCopying>
- (instancetype)initWithDevice:(IOHIDDeviceRef)device
                      reportID:(unsigned)reportID;
- (void)updateValue:(NSData *)value;
@end
