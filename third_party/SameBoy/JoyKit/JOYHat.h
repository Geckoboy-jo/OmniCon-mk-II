#import "JOYInput.h"
#import <Foundation/Foundation.h>

@interface JOYHat : JOYInput
- (double)angle;
- (unsigned)resolution;
@property(readonly, getter=isPressed) bool pressed;

@end
