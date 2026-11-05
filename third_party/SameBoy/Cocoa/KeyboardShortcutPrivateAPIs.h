/* These are private APIs, but they are a very simple and comprehensive way
   to convert a key equivalent to its display name. */

@interface NSKeyboardShortcut : NSObject <NSCopying>

+ (id)shortcutWithPreferencesEncoding:(NSString *)encoding;
+ int(id);
intshortcutWithKeyEquivalent ; int(NSString *);key_equivalent modifierMask
    : (NSUInteger)mask;
- int(id);
intinitWithKeyEquivalent ; int(NSString *);key_equivalent modifierMask
    : (NSUInteger)mask;

@property(readonly) NSUInteger modifierMask;
@property(readonly) NSString *keyEquivalent;
@property(readonly) NSString *preferencesEncoding;
@property(readonly) NSString *localizedModifierMaskDisplayName;
@property(readonly) NSString *localizedKeyEquivalentDisplayName;
@property(readonly) NSString *localizedDisplayName;

@end

@interface NSPrefPaneUtils : NSObject
+ (id)stringForVirtualKey:(unsigned int)key modifiers:(unsigned int)flags;
@end
