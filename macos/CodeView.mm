#import "CodeView.h"

#include <cstring>

#import "Text.h"

@implementation CodeView

// ---- rows and columns -------------------------------------------------------

- (NSInteger)rowOfIndex:(NSUInteger)index {
    NSString* all = self.string;
    NSUInteger stop = MIN(index, all.length);
    NSInteger row = 0;
    for (NSUInteger i = 0; i < stop; ++i)
        if ([all characterAtIndex:i] == '\n') ++row;
    return row;
}

- (NSUInteger)indexOfRow:(NSInteger)row {
    if (row <= 0) return 0;
    NSString* all = self.string;
    NSInteger seen = 0;
    for (NSUInteger i = 0; i < all.length; ++i) {
        if ([all characterAtIndex:i] == '\n' && ++seen == row) return i + 1;
    }
    return NSNotFound;
}

- (NSInteger)caretRow { return [self rowOfIndex:self.selectedRange.location]; }

- (NSInteger)caretColumn {
    NSUInteger caret = self.selectedRange.location;
    NSRange line = [self.string lineRangeForRange:NSMakeRange(caret, 0)];
    return (NSInteger)(caret - line.location);
}

- (NSRange)contentsOfRow:(NSInteger)row {
    NSUInteger start = [self indexOfRow:row];
    if (start == NSNotFound) return NSMakeRange(NSNotFound, 0);
    NSString* all = self.string;
    NSRange line = [all lineRangeForRange:NSMakeRange(start, 0)];
    NSUInteger end = NSMaxRange(line);
    if (end > line.location && end <= all.length && [all characterAtIndex:end - 1] == '\n')
        --end;
    return NSMakeRange(line.location, end - line.location);
}

static NSUInteger leadingSpace(NSString* line) {
    NSUInteger lead = 0;
    while (lead < line.length) {
        unichar c = [line characterAtIndex:lead];
        if (c != ' ' && c != '\t') break;
        ++lead;
    }
    return lead;
}

- (void)goToLine:(NSInteger)line column:(NSInteger)column {
    if (line < 1) line = 1;
    NSUInteger start = [self indexOfRow:line - 1];
    if (start == NSNotFound) start = self.string.length;
    NSRange contents = [self contentsOfRow:line - 1];
    NSUInteger room = contents.location == NSNotFound ? 0 : contents.length;
    NSUInteger at = start + (NSUInteger)MIN((NSInteger)room, MAX((NSInteger)0, column - 1));
    self.selectedRange = NSMakeRange(at, 0);
    [self scrollRangeToVisible:NSMakeRange(at, 0)];
    [self showFindIndicatorForRange:(contents.location == NSNotFound
                                         ? NSMakeRange(at, 0) : contents)];
}

// ---- editing ------------------------------------------------------------------

- (void)replaceRange:(NSRange)range with:(NSString*)text {
    if (![self shouldChangeTextInRange:range replacementString:text]) return;
    [self.textStorage replaceCharactersInRange:range withString:text];
    [self didChangeText];
}

- (NSString*)indentUnit {
    id<CodeViewHost> host = self.host;
    if (host != nil && [host indentTabs] != 0) return @"\t";
    int width = host != nil ? [host indentWidth] : 4;
    if (width < 1) width = 4;
    return [@"" stringByPaddingToLength:(NSUInteger)width withString:@" " startingAtIndex:0];
}

- (void)realignRow:(NSInteger)row {
    id<CodeViewHost> host = self.host;
    if (host == nil) return;
    NSRange contents = [self contentsOfRow:row];
    if (contents.location == NSNotFound) return;

    NSString* line = [self.string substringWithRange:contents];
    NSUInteger lead = leadingSpace(line);

    NSString* want = Take(ride_indent_for(Utf8(self.string), (int)row,
                                              [host indentWidth], [host indentTabs],
                                              [host indentCase], [host indentDialect]));
    if ([want isEqualToString:[line substringToIndex:lead]]) return;

    NSRange caret = self.selectedRange;
    [self replaceRange:NSMakeRange(contents.location, lead) with:want];

    // The caret keeps its place in the text after the leading space.
    NSInteger moved = (NSInteger)caret.location + (NSInteger)want.length - (NSInteger)lead;
    if (caret.location < contents.location + lead) moved = (NSInteger)(contents.location + want.length);
    moved = MAX((NSInteger)0, MIN(moved, (NSInteger)self.string.length));
    self.selectedRange = NSMakeRange((NSUInteger)moved, 0);
}

- (void)insertNewline:(id)sender {
    id<CodeViewHost> host = self.host;
    if (host == nil || ![host laysOut]) {
        [super insertNewline:sender];
        return;
    }
    NSString* all = self.string;
    NSRange selection = self.selectedRange;
    NSInteger row = [self rowOfIndex:selection.location];
    NSRange line = [all lineRangeForRange:NSMakeRange(selection.location, 0)];
    NSString* before = [all substringWithRange:NSMakeRange(line.location,
                                                           selection.location - line.location)];
    // The core counts columns in bytes of UTF-8, as it counts everything.
    int column = (int)std::strlen(Utf8(before));

    NSString* lead = Take(ride_indent_after_newline(Utf8(all), (int)row, column,
                                                        [host indentWidth], [host indentTabs],
                                                        [host indentCase], [host indentDialect]));
    [self insertText:[@"\n" stringByAppendingString:lead] replacementRange:selection];
}

- (void)insertTab:(id)sender {
    id<CodeViewHost> host = self.host;
    if (host == nil || ![host laysOut] || self.selectedRange.length != 0) {
        [super insertTab:sender];
        return;
    }
    NSInteger row = [self caretRow];
    NSRange contents = [self contentsOfRow:row];
    NSString* line = [self.string substringWithRange:contents];
    NSUInteger lead = leadingSpace(line);

    // Tab in the leading space puts the line where it belongs; anywhere
    // else it is a step of indentation, as the project spells one.
    if ((NSUInteger)[self caretColumn] <= lead) {
        [self realignRow:row];
        NSRange now = [self contentsOfRow:row];
        NSString* laid = [self.string substringWithRange:now];
        self.selectedRange = NSMakeRange(now.location + leadingSpace(laid), 0);
        return;
    }
    [self insertText:[self indentUnit] replacementRange:self.selectedRange];
}

- (void)insertText:(id)text replacementRange:(NSRange)range {
    [super insertText:text replacementRange:range];

    id<CodeViewHost> host = self.host;
    if (host == nil || ![host laysOut]) return;
    NSString* typed = [text isKindOfClass:[NSAttributedString class]]
                          ? [(NSAttributedString*)text string] : (NSString*)text;
    if (typed.length != 1) return;
    unichar just = [typed characterAtIndex:0];
    if (just != '}' && just != '#' && just != ':') return;

    // A closing brace or a '#' re-lays its line only when it is the first
    // thing on it; a ':' may end a case label anywhere.
    NSInteger row = [self caretRow];
    if (just != ':') {
        NSRange contents = [self contentsOfRow:row];
        NSString* line = [self.string substringWithRange:contents];
        NSInteger column = [self caretColumn] - 1;
        for (NSInteger i = 0; i < column && i < (NSInteger)line.length; ++i) {
            unichar c = [line characterAtIndex:(NSUInteger)i];
            if (c != ' ' && c != '\t') return;
        }
    }
    [self realignRow:row];
}

- (void)reindentSelectionOrAll {
    id<CodeViewHost> host = self.host;
    if (host == nil) return;
    NSString* all = self.string;
    NSString* laid = Take(ride_reindent(Utf8(all), [host indentWidth],
                                            [host indentTabs], [host indentCase],
                                            [host indentDialect]));
    if ([laid isEqualToString:all]) return;

    NSRange selection = self.selectedRange;
    NSInteger caretRow = [self caretRow];
    NSInteger caretColumn = [self caretColumn];

    NSArray<NSString*>* was = [all componentsSeparatedByString:@"\n"];
    NSArray<NSString*>* now = [laid componentsSeparatedByString:@"\n"];

    NSInteger first = 0;
    NSInteger last = (NSInteger)was.count - 1;
    if (selection.length > 0) {
        first = [self rowOfIndex:selection.location];
        NSUInteger end = NSMaxRange(selection);
        // A selection ending at the start of a line does not take that line.
        if (end > selection.location && [all characterAtIndex:end - 1] == '\n') --end;
        last = [self rowOfIndex:end];
    }

    // The selection decides which lines are written back, not what is
    // measured; the re-indent keeps the line count, so rows correspond.
    if (now.count != was.count) {
        [self replaceRange:NSMakeRange(0, all.length) with:laid];
    } else {
        NSUInteger start = [self indexOfRow:first];
        NSRange lastLine = [self contentsOfRow:last];
        if (start == NSNotFound || lastLine.location == NSNotFound) return;
        NSRange span = NSMakeRange(start, NSMaxRange(lastLine) - start);
        NSArray* chosen = [now subarrayWithRange:NSMakeRange((NSUInteger)first,
                                                             (NSUInteger)(last - first + 1))];
        [self replaceRange:span with:[chosen componentsJoinedByString:@"\n"]];
    }

    if (selection.length > 0) {
        NSUInteger start = [self indexOfRow:first];
        NSRange lastLine = [self contentsOfRow:last];
        if (start != NSNotFound && lastLine.location != NSNotFound)
            self.selectedRange = NSMakeRange(start, NSMaxRange(lastLine) - start);
    } else {
        NSUInteger start = [self indexOfRow:caretRow];
        if (start == NSNotFound) start = self.string.length;
        NSRange line = [self contentsOfRow:caretRow];
        NSInteger room = line.location == NSNotFound ? 0 : (NSInteger)line.length;
        self.selectedRange = NSMakeRange(start + (NSUInteger)MIN(room, caretColumn), 0);
    }
}

@end
