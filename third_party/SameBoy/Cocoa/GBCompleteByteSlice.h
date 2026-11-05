#import "Document.h"
#import "HexFiend/HFByteSlice.h"
#import "HexFiend/HexFiend.h"

@interface GBCompleteByteSlice : HFByteSlice
- (instancetype)initWithByteArray:(HFByteArray *)array;
@end
