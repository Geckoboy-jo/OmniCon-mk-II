#import "GBGLShader.h"
#import <Cocoa/Cocoa.h>

@interface GBOpenGLView : NSOpenGLView
@property(nonatomic) GBGLShader *shader;
@end
