#import "RIDELineNumbers.h"

@implementation RIDELineNumbers {
    NSDictionary* numberAttributes_;
    NSUInteger lastDigits_;
}

- (instancetype)initWithTextView:(NSTextView*)textView {
    self = [super initWithScrollView:textView.enclosingScrollView
                         orientation:NSVerticalRuler];
    if (self) {
        self.clientView = textView;
        _errorLines = [NSIndexSet indexSet];
        _warningLines = [NSIndexSet indexSet];
        lastDigits_ = 0;
        [self remeasure];

        // Scrolling moves the lines under the numbers; the ruler is redrawn
        // with the clip view rather than on the next keystroke.
        NSClipView* clip = textView.enclosingScrollView.contentView;
        clip.postsBoundsChangedNotifications = YES;
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(scrolled:)
                                                     name:NSViewBoundsDidChangeNotification
                                                   object:clip];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (BOOL)isFlipped { return YES; }

- (void)scrolled:(NSNotification*)note {
    (void)note;
    self.needsDisplay = YES;
}

- (void)setErrorLines:(NSIndexSet*)lines {
    _errorLines = [lines copy] ?: [NSIndexSet indexSet];
    self.needsDisplay = YES;
}

- (void)setWarningLines:(NSIndexSet*)lines {
    _warningLines = [lines copy] ?: [NSIndexSet indexSet];
    self.needsDisplay = YES;
}

- (NSFont*)numberFont {
    NSTextView* text = (NSTextView*)self.clientView;
    CGFloat size = text.font != nil ? text.font.pointSize - 1 : 11;
    return [NSFont monospacedDigitSystemFontOfSize:MAX(9, size) weight:NSFontWeightRegular];
}

- (NSUInteger)lineCount {
    NSString* all = ((NSTextView*)self.clientView).string;
    NSUInteger lines = 1;
    NSUInteger length = all.length;
    for (NSUInteger i = 0; i < length; ++i)
        if ([all characterAtIndex:i] == '\n') ++lines;
    return lines;
}

- (void)remeasure {
    numberAttributes_ = @{
        NSFontAttributeName : [self numberFont],
        NSForegroundColorAttributeName : [NSColor secondaryLabelColor],
    };
    NSUInteger digits = 1;
    for (NSUInteger n = [self lineCount]; n >= 10; n /= 10) ++digits;
    if (digits < 3) digits = 3;
    if (digits == lastDigits_) return;
    lastDigits_ = digits;
    NSSize one = [@"8" sizeWithAttributes:numberAttributes_];
    self.ruleThickness = ceil(one.width * digits + 22);
}

- (void)textDidChange {
    [self remeasure];
    self.needsDisplay = YES;
}

- (void)drawHashMarksAndLabelsInRect:(NSRect)rect {
    (void)rect;
    NSTextView* text = (NSTextView*)self.clientView;
    NSLayoutManager* layout = text.layoutManager;
    NSTextContainer* container = text.textContainer;
    if (layout == nil || container == nil) return;

    [[NSColor textBackgroundColor] setFill];
    NSRectFill(self.bounds);
    [[NSColor separatorColor] setFill];
    NSRectFill(NSMakeRect(NSMaxX(self.bounds) - 1, NSMinY(self.bounds), 1,
                          NSHeight(self.bounds)));

    if (numberAttributes_ == nil) [self remeasure];

    NSString* all = text.string;
    NSRect visible = text.visibleRect;
    NSRange glyphs = [layout glyphRangeForBoundingRect:visible inTextContainer:container];
    NSRange chars = [layout characterRangeForGlyphRange:glyphs actualGlyphRange:NULL];

    // The number of the first line showing: the newlines above it, plus one.
    NSUInteger line = 1;
    for (NSUInteger i = 0; i < chars.location && i < all.length; ++i)
        if ([all characterAtIndex:i] == '\n') ++line;

    CGFloat inset = text.textContainerOrigin.y;
    CGFloat width = NSWidth(self.bounds);
    NSUInteger at = chars.location;
    NSUInteger end = NSMaxRange(chars);

    // Walk the lines that begin inside the visible characters.
    while (at <= end && at <= all.length) {
        NSRect fragment;
        if (at < all.length || all.length == 0) {
            if (all.length == 0) {
                fragment = layout.extraLineFragmentRect;
            } else {
                NSUInteger glyph = [layout glyphIndexForCharacterAtIndex:at];
                fragment = [layout lineFragmentRectForGlyphAtIndex:glyph effectiveRange:NULL];
            }
        } else {
            // The empty last line after a final newline.
            if (all.length == 0 || [all characterAtIndex:all.length - 1] != '\n') break;
            fragment = layout.extraLineFragmentRect;
            if (NSIsEmptyRect(fragment)) break;
        }

        NSPoint top = [self convertPoint:NSMakePoint(0, NSMinY(fragment) + inset) fromView:text];
        CGFloat height = NSHeight(fragment);

        NSColor* mark = nil;
        if ([_errorLines containsIndex:line]) mark = [NSColor systemRedColor];
        else if ([_warningLines containsIndex:line]) mark = [NSColor systemOrangeColor];
        if (mark != nil) {
            [[mark colorWithAlphaComponent:0.85] setFill];
            NSRect badge = NSMakeRect(2, top.y + 1, width - 6, MAX(1, height - 2));
            [[NSBezierPath bezierPathWithRoundedRect:badge xRadius:3 yRadius:3] fill];
        }

        NSString* number = [NSString stringWithFormat:@"%lu", (unsigned long)line];
        NSDictionary* attributes = numberAttributes_;
        if (mark != nil) {
            NSMutableDictionary* white = [numberAttributes_ mutableCopy];
            white[NSForegroundColorAttributeName] = [NSColor whiteColor];
            attributes = white;
        }
        NSSize size = [number sizeWithAttributes:attributes];
        [number drawAtPoint:NSMakePoint(width - size.width - 10,
                                        top.y + (height - size.height) / 2)
             withAttributes:attributes];

        if (all.length == 0) break;
        if (at >= all.length) break;
        NSRange lineRange = [all lineRangeForRange:NSMakeRange(at, 0)];
        NSUInteger next = NSMaxRange(lineRange);
        if (next == at) break;
        at = next;
        ++line;
        if (at == all.length && [all characterAtIndex:all.length - 1] != '\n') break;
    }
}

@end
