// The two spellings of text the window deals in: NSString on the AppKit
// side, UTF-8 char* on the core's (winforms/bridge.h). Every crossing goes
// through one of these, so an embedded nil or a NULL from the core is
// handled in one place rather than at every call.
#ifndef MACOS_TEXT_H
#define MACOS_TEXT_H

#import <Foundation/Foundation.h>

#include <string>

#include "bridge.h"

// What the core said, as an NSString; never nil.
static inline NSString* Str(const char* text) {
    if (text == NULL) return @"";
    NSString* made = [NSString stringWithUTF8String:text];
    return made != nil ? made : @"";
}

// The same for a string the core allocated and hands over to be freed.
static inline NSString* Take(char* text) {
    NSString* made = Str(text);
    if (text != NULL) ride_free(text);
    return made;
}

// An NSString as the core wants it. The pointer lives as long as the
// autorelease pool, which is the call it is handed to.
static inline const char* Utf8(NSString* text) {
    if (text == nil) return "";
    const char* bytes = text.UTF8String;
    return bytes != NULL ? bytes : "";
}

// A copy that can cross to another thread.
static inline std::string StdString(NSString* text) { return std::string(Utf8(text)); }

#endif
