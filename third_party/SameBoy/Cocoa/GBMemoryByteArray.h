#import "Document.h"
#import "HexFiend/HFByteArray.h"
#import "HexFiend/HexFiend.h"

typedef enum {
  GBMemoryEntireSpace,
  GBMemoryROM,
  GBMemoryVRAM,
  GBMemoryExternalRAM,
  GBMemoryRAM
} GB_memory_mode_t;

@interface GBMemoryByteArray : HFByteArray
- (instancetype)initWithDocument:(Document *)document;
@property(nonatomic) uint16_t selectedBank;
@property(nonatomic) GB_memory_mode_t mode;
@end
