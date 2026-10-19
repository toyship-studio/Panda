//
//  WonderView.mm
//
//  Created by Andreichev Mikhail on 07.02.2025.
//

#import "WonderView.h"
#import "Fern/Events/EventQueue.hpp"
#ifdef PANDA_GRAPHICS_METAL
#import <QuartzCore/CAMetalLayer.h>
#endif

using namespace Fern;

namespace Fern {
EventQueue* getEventQueue();
bool isCursorLocked();
}

struct WonderHelper {
    Fern::Key keycodes[256];
};

static const NSRange kEmptyRange = { NSNotFound, 0 };

static void fillKeyTable(WonderHelper& helper) {
    memset(helper.keycodes, -1, sizeof(helper.keycodes));

    helper.keycodes[0x1D] = Fern::Key::KEY_0;
    helper.keycodes[0x12] = Fern::Key::KEY_1;
    helper.keycodes[0x13] = Fern::Key::KEY_2;
    helper.keycodes[0x14] = Fern::Key::KEY_3;
    helper.keycodes[0x15] = Fern::Key::KEY_4;
    helper.keycodes[0x17] = Fern::Key::KEY_5;
    helper.keycodes[0x16] = Fern::Key::KEY_6;
    helper.keycodes[0x1A] = Fern::Key::KEY_7;
    helper.keycodes[0x1C] = Fern::Key::KEY_8;
    helper.keycodes[0x19] = Fern::Key::KEY_9;
    helper.keycodes[0x00] = Fern::Key::KEY_A;
    helper.keycodes[0x0B] = Fern::Key::KEY_B;
    helper.keycodes[0x08] = Fern::Key::KEY_C;
    helper.keycodes[0x02] = Fern::Key::KEY_D;
    helper.keycodes[0x0E] = Fern::Key::KEY_E;
    helper.keycodes[0x03] = Fern::Key::KEY_F;
    helper.keycodes[0x05] = Fern::Key::KEY_G;
    helper.keycodes[0x04] = Fern::Key::KEY_H;
    helper.keycodes[0x22] = Fern::Key::KEY_I;
    helper.keycodes[0x26] = Fern::Key::KEY_J;
    helper.keycodes[0x28] = Fern::Key::KEY_K;
    helper.keycodes[0x25] = Fern::Key::KEY_L;
    helper.keycodes[0x2E] = Fern::Key::KEY_M;
    helper.keycodes[0x2D] = Fern::Key::KEY_N;
    helper.keycodes[0x1F] = Fern::Key::KEY_O;
    helper.keycodes[0x23] = Fern::Key::KEY_P;
    helper.keycodes[0x0C] = Fern::Key::KEY_Q;
    helper.keycodes[0x0F] = Fern::Key::KEY_R;
    helper.keycodes[0x01] = Fern::Key::KEY_S;
    helper.keycodes[0x11] = Fern::Key::KEY_T;
    helper.keycodes[0x20] = Fern::Key::KEY_U;
    helper.keycodes[0x09] = Fern::Key::KEY_V;
    helper.keycodes[0x0D] = Fern::Key::KEY_W;
    helper.keycodes[0x07] = Fern::Key::KEY_X;
    helper.keycodes[0x10] = Fern::Key::KEY_Y;
    helper.keycodes[0x06] = Fern::Key::KEY_Z;

    helper.keycodes[0x27] = Fern::Key::KEY_APOSTROPHE;
    helper.keycodes[0x2A] = Fern::Key::KEY_BACKSLASH;
    helper.keycodes[0x2B] = Fern::Key::KEY_COMMA;
    helper.keycodes[0x18] = Fern::Key::KEY_EQUAL;
    helper.keycodes[0x32] = Fern::Key::KEY_GRAVE_ACCENT;
    helper.keycodes[0x21] = Fern::Key::KEY_LEFT_BRACKET;
    helper.keycodes[0x1B] = Fern::Key::KEY_MINUS;
    helper.keycodes[0x2F] = Fern::Key::KEY_PERIOD;
    helper.keycodes[0x1E] = Fern::Key::KEY_RIGHT_BRACKET;
    helper.keycodes[0x29] = Fern::Key::KEY_SEMICOLON;
    helper.keycodes[0x2C] = Fern::Key::KEY_SLASH;
    helper.keycodes[0x0A] = Fern::Key::KEY_WORLD_1;

    helper.keycodes[0x33] = Fern::Key::KEY_BACKSPACE;
    helper.keycodes[0x39] = Fern::Key::KEY_CAPS_LOCK;
    helper.keycodes[0x75] = Fern::Key::KEY_DELETE;
    helper.keycodes[0x7D] = Fern::Key::KEY_DOWN;
    helper.keycodes[0x77] = Fern::Key::KEY_END;
    helper.keycodes[0x24] = Fern::Key::KEY_ENTER;
    helper.keycodes[0x35] = Fern::Key::KEY_ESCAPE;
    helper.keycodes[0x7A] = Fern::Key::KEY_F1;
    helper.keycodes[0x78] = Fern::Key::KEY_F2;
    helper.keycodes[0x63] = Fern::Key::KEY_F3;
    helper.keycodes[0x76] = Fern::Key::KEY_F4;
    helper.keycodes[0x60] = Fern::Key::KEY_F5;
    helper.keycodes[0x61] = Fern::Key::KEY_F6;
    helper.keycodes[0x62] = Fern::Key::KEY_F7;
    helper.keycodes[0x64] = Fern::Key::KEY_F8;
    helper.keycodes[0x65] = Fern::Key::KEY_F9;
    helper.keycodes[0x6D] = Fern::Key::KEY_F10;
    helper.keycodes[0x67] = Fern::Key::KEY_F11;
    helper.keycodes[0x6F] = Fern::Key::KEY_F12;
    helper.keycodes[0x69] = Fern::Key::KEY_PRINT_SCREEN;
    helper.keycodes[0x6B] = Fern::Key::KEY_F14;
    helper.keycodes[0x71] = Fern::Key::KEY_F15;
    helper.keycodes[0x6A] = Fern::Key::KEY_F16;
    helper.keycodes[0x40] = Fern::Key::KEY_F17;
    helper.keycodes[0x4F] = Fern::Key::KEY_F18;
    helper.keycodes[0x50] = Fern::Key::KEY_F19;
    helper.keycodes[0x5A] = Fern::Key::KEY_F20;
    helper.keycodes[0x73] = Fern::Key::KEY_HOME;
    helper.keycodes[0x72] = Fern::Key::KEY_INSERT;
    helper.keycodes[0x7B] = Fern::Key::KEY_LEFT;
    helper.keycodes[0x3A] = Fern::Key::KEY_LEFT_ALT;
    helper.keycodes[0x3B] = Fern::Key::KEY_LEFT_CONTROL;
    helper.keycodes[0x38] = Fern::Key::KEY_LEFT_SHIFT;
    helper.keycodes[0x37] = Fern::Key::KEY_LEFT_SUPER;
    helper.keycodes[0x6E] = Fern::Key::KEY_MENU;
    helper.keycodes[0x47] = Fern::Key::KEY_NUM_LOCK;
    helper.keycodes[0x79] = Fern::Key::KEY_PAGE_DOWN;
    helper.keycodes[0x74] = Fern::Key::KEY_PAGE_UP;
    helper.keycodes[0x7C] = Fern::Key::KEY_RIGHT;
    helper.keycodes[0x3D] = Fern::Key::KEY_RIGHT_ALT;
    helper.keycodes[0x3E] = Fern::Key::KEY_RIGHT_CONTROL;
    helper.keycodes[0x3C] = Fern::Key::KEY_RIGHT_SHIFT;
    helper.keycodes[0x36] = Fern::Key::KEY_RIGHT_SUPER;
    helper.keycodes[0x31] = Fern::Key::KEY_SPACE;
    helper.keycodes[0x30] = Fern::Key::KEY_TAB;
    helper.keycodes[0x7E] = Fern::Key::KEY_UP;

    helper.keycodes[0x52] = Fern::Key::KEY_KP_0;
    helper.keycodes[0x53] = Fern::Key::KEY_KP_1;
    helper.keycodes[0x54] = Fern::Key::KEY_KP_2;
    helper.keycodes[0x55] = Fern::Key::KEY_KP_3;
    helper.keycodes[0x56] = Fern::Key::KEY_KP_4;
    helper.keycodes[0x57] = Fern::Key::KEY_KP_5;
    helper.keycodes[0x58] = Fern::Key::KEY_KP_6;
    helper.keycodes[0x59] = Fern::Key::KEY_KP_7;
    helper.keycodes[0x5B] = Fern::Key::KEY_KP_8;
    helper.keycodes[0x5C] = Fern::Key::KEY_KP_9;
    helper.keycodes[0x45] = Fern::Key::KEY_KP_ADD;
    helper.keycodes[0x41] = Fern::Key::KEY_KP_DECIMAL;
    helper.keycodes[0x4B] = Fern::Key::KEY_KP_DIVIDE;
    helper.keycodes[0x4C] = Fern::Key::KEY_KP_ENTER;
    helper.keycodes[0x51] = Fern::Key::KEY_KP_EQUAL;
    helper.keycodes[0x43] = Fern::Key::KEY_KP_MULTIPLY;
    helper.keycodes[0x4E] = Fern::Key::KEY_KP_SUBTRACT;
}

static Fern::Key cocoaKeyToPandaKey(WonderHelper& helper, unsigned int key) {
    if (key >= sizeof(helper.keycodes) / sizeof(helper.keycodes[0])) {
        return Fern::Key::KEY_UNKNOWN;
    }
    return helper.keycodes[key];
}

static bool isKnownKey(Fern::Key key) {
    return key != Fern::Key::KEY_UNKNOWN;
}

static NSUInteger pandaKeyToCocoaModifierFlag(Fern::Key key) {
    switch (key) {
        case Fern::Key::KEY_LEFT_SHIFT:
        case Fern::Key::KEY_RIGHT_SHIFT:
            return NSEventModifierFlagShift;
        case Fern::Key::KEY_LEFT_CONTROL:
        case Fern::Key::KEY_RIGHT_CONTROL:
            return NSEventModifierFlagControl;
        case Fern::Key::KEY_LEFT_ALT:
        case Fern::Key::KEY_RIGHT_ALT:
            return NSEventModifierFlagOption;
        case Fern::Key::KEY_LEFT_SUPER:
        case Fern::Key::KEY_RIGHT_SUPER:
            return NSEventModifierFlagCommand;
        case Fern::Key::KEY_CAPS_LOCK:
            return NSEventModifierFlagCapsLock;
        default: return 0;
    }
}

@implementation WonderView {
    WonderHelper m_helper;
    BOOL m_multiTouchScroll;
}

@synthesize eventWindow;

- (instancetype)init {
    self = [super init];
    if (self != nil) {
        markedText = [[NSMutableAttributedString alloc] init];
        fillKeyTable(m_helper);
        m_multiTouchScroll = NO;
        self.ignoreMouseEvents = 0;
    }
    NSLog(@"WonderView created");
    return self;
}

- (void)dealloc {
    [super dealloc];
    [markedText release];
    NSLog(@"WonderView deallocated");
}

#ifdef PANDA_GRAPHICS_METAL
// Metal backend: the view's backing layer is a CAMetalLayer (created on setWantsLayer:YES,
// before OSX_MetalContext is created).
- (CALayer *)makeBackingLayer {
    return [CAMetalLayer layer];
}
#endif

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)canBecomeKeyView {
    return YES;
}

- (BOOL)wantsUpdateLayer {
    return YES;
}

// MARK: - Mouse

- (void)mouseMoved:(NSEvent *)event {
    // NOTE: The returned location uses base 0,1 not 0,0
    const NSPoint pos = [event locationInWindow];
    double xpos = pos.x;
    double ypos = self.frame.size.height - pos.y;
    double deltaX = [event deltaX];
    double deltaY = [event deltaY];
    // If the cursor was moved programmatically, the deltas are not needed
    if (self.ignoreMouseEvents) {
        deltaX = 0;
        deltaY = 0;
        self.ignoreMouseEvents--;
    }
    getEventQueue()->postMouseEvent(xpos, ypos, deltaX, deltaY, static_cast<Fern::Window *>(self.eventWindow));
}

- (void)mouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}

- (void)rightMouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}

- (void)mouseDown:(NSEvent *)event {
    getEventQueue()->postMouseButtonEvent(MouseButton::LEFT, true, static_cast<Fern::Window *>(self.eventWindow));
}

- (void)mouseUp:(NSEvent *)event {
    getEventQueue()->postMouseButtonEvent(MouseButton::LEFT, false, static_cast<Fern::Window *>(self.eventWindow));
}

- (void)rightMouseDown:(NSEvent *)event {
    getEventQueue()->postMouseButtonEvent(MouseButton::RIGHT, true, static_cast<Fern::Window *>(self.eventWindow));
}

- (void)rightMouseUp:(NSEvent *)event {
    getEventQueue()->postMouseButtonEvent(MouseButton::RIGHT, false, static_cast<Fern::Window *>(self.eventWindow));
}

- (void)otherMouseDown:(NSEvent *)event {
    getEventQueue()->postMouseButtonEvent(MouseButton::MIDDLE, true, static_cast<Fern::Window *>(self.eventWindow));
}

- (void)otherMouseUp:(NSEvent *)event {
    getEventQueue()->postMouseButtonEvent(MouseButton::MIDDLE, false, static_cast<Fern::Window *>(self.eventWindow));
}

- (void)scrollWheel:(NSEvent *)event {
    double deltaX = event.scrollingDeltaX;
    double deltaY = event.scrollingDeltaY;
    const bool isMomentum = event.momentumPhase != NSEventPhaseNone;
    const bool isGestureEnded = event.phase == NSEventPhaseEnded || event.phase == NSEventPhaseCancelled;
    if (event.phase == NSEventPhaseBegan) {
        m_multiTouchScroll = true;
    } else if (isGestureEnded) {
        m_multiTouchScroll = false;
    }
    if ([event hasPreciseScrollingDeltas]) {
        deltaX *= 0.025;
        deltaY *= 0.025;
    }
    getEventQueue()->postScrollEvent(
        deltaX,
        deltaY,
        m_multiTouchScroll || [event hasPreciseScrollingDeltas],
        isMomentum,
        isGestureEnded,
        static_cast<Fern::Window *>(self.eventWindow)
    );
}

// MARK: - Keys

- (void)keyDown:(NSEvent *)event {
    Fern::Key key = cocoaKeyToPandaKey(m_helper, [event keyCode]);
    if (isKnownKey(key)) {
        getEventQueue()->postKeyEvent(key, true, static_cast<Fern::Window *>(self.eventWindow), [event isARepeat]);
    }
    [self interpretKeyEvents:@[event]];
}

- (void)flagsChanged:(NSEvent *)event {
    Fern::Key key = cocoaKeyToPandaKey(m_helper, [event keyCode]);
    if (!isKnownKey(key)) {
        return;
    }
    const NSUInteger keyFlag = pandaKeyToCocoaModifierFlag(key);
    const NSUInteger modifierFlags = [event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask;
    bool down = keyFlag & modifierFlags;
    getEventQueue()->postKeyEvent(key, down, static_cast<Fern::Window *>(self.eventWindow));
}

- (void)keyUp:(NSEvent *)event {
    Fern::Key key = cocoaKeyToPandaKey(m_helper, [event keyCode]);
    if (!isKnownKey(key)) {
        return;
    }
    getEventQueue()->postKeyEvent(key, false, static_cast<Fern::Window *>(self.eventWindow));
}

// MARK: Text input client

- (BOOL)hasMarkedText {
    return [markedText length] > 0;
}

- (NSRange)markedRange {
    if ([markedText length] > 0) {
        return NSMakeRange(0, [markedText length] - 1);
    } else {
        return kEmptyRange;
    }
}

- (NSRange)selectedRange {
    return kEmptyRange;
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
    [markedText release];
    if ([string isKindOfClass:[NSAttributedString class]]) {
        markedText = [[NSMutableAttributedString alloc] initWithAttributedString:string];
    } else {
        markedText = [[NSMutableAttributedString alloc] initWithString:string];
    }
}

- (void)unmarkText {
    [[markedText mutableString] setString:@""];
}

- (NSArray*)validAttributesForMarkedText {
    return [NSArray array];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:(NSRangePointer)actualRange {
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    return 0;
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actualRange {
    const NSRect frame = [self frame];
    return NSMakeRect(frame.origin.x, frame.origin.y, 0.0, 0.0);
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    NSString* characters;
    if ([string isKindOfClass:[NSAttributedString class]]) {
        characters = [string string];
    } else {
        characters = (NSString*) string;
    }

    NSRange range = NSMakeRange(0, [characters length]);
    while (range.length) {
        uint32_t codepoint = 0;
        if ([characters getBytes:&codepoint
                       maxLength:sizeof(codepoint)
                      usedLength:NULL
                        encoding:NSUTF32StringEncoding
                         options:0
                           range:range
                  remainingRange:&range]) {
            if (codepoint >= 0xf700 && codepoint <= 0xf7ff) {
                continue;
            }
            getEventQueue()->postCharEvent(codepoint, static_cast<Fern::Window *>(self.eventWindow));
        }
    }
}

- (void)doCommandBySelector:(SEL)selector {}

@end
