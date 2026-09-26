// The text of the file: an NSTextView that lays C, C++ and Shalimar out as it
// is typed, by asking the core (indent.cpp through winforms/bridge.h) - the
// same rules the terminal editor and the Windows window follow.
#ifndef MACOS_CODE_VIEW_H
#define MACOS_CODE_VIEW_H

#import <Cocoa/Cocoa.h>

// What the view needs to know from the window to lay a line out.
@protocol CodeViewHost <NSObject>
- (int)indentWidth;
- (int)indentTabs;
- (int)indentCase;
// RIDE_DIALECT_*, from the file's language.
- (int)indentDialect;
// Whether layout-as-you-type applies at all (not for plain text or JSON).
- (BOOL)laysOut;
@end

@interface CodeView : NSTextView

@property(nonatomic, weak) id<CodeViewHost> host;

// 0-based row and column (in characters) of the caret.
- (NSInteger)caretRow;
- (NSInteger)caretColumn;
// 0-based row holding a character index.
- (NSInteger)rowOfIndex:(NSUInteger)index;
// The character index where a 0-based row begins, or NSNotFound past the end.
- (NSUInteger)indexOfRow:(NSInteger)row;
// 1-based, as a compiler names them; the column is clamped to the line.
- (void)goToLine:(NSInteger)line column:(NSInteger)column;

// Put one line's leading space where the rules say it belongs.
- (void)realignRow:(NSInteger)row;
// Edit > Re-indent: the selected lines, or the whole file with nothing
// selected. The whole file is always measured.
- (void)reindentSelectionOrAll;

// Replace a range as a user edit would: undoable, and announced.
- (void)replaceRange:(NSRange)range with:(NSString*)text;

@end

#endif
