#import "CocoaWindowDelegate.h"
#import "CocoaWindow.h"
#import "WonderView.h"

using namespace Fern;

namespace Fern {
EventQueue* getEventQueue();
}

@implementation CocoaWindowDelegate

- (instancetype)initWithCocoa:(CocoaWindow *)initWindow {
    self = [super init];
    if (self != nil) {
        window = initWindow;
    }
    return self;
}

- (void)postWindowSize {
    CGRect frame = [[window->getNative() contentView] frame];
    uint32_t width = frame.size.width;
    uint32_t height = frame.size.height;
    getEventQueue()->postSizeEvent(width, height, window);
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
    window->didBecomeKey();
    getEventQueue()->postWindowFocusEvent(true, window);
}

- (void)windowDidResignKey:(NSNotification *)notification {
    window->didResignKey();
    // The releases of the keys held across the loss (Cmd+Tab, a system shortcut that takes the
    // keyboard) never reach the window: the view posts them itself, then the focus goes.
    NSView *view = [window->getNative() contentView];
    if ([view isKindOfClass:[WonderView class]]) { [(WonderView *)view releaseHeldKeys]; }
    getEventQueue()->postWindowFocusEvent(false, window);
}

- (void)windowDidMiniaturize:(NSNotification *)notification {
    getEventQueue()->postWindowMinimizedEvent(true, window);
}

- (void)windowDidDeminiaturize:(NSNotification *)notification {
    getEventQueue()->postWindowMinimizedEvent(false, window);
}

- (BOOL)windowShouldClose:(id)sender {
    if (!window->canClose()) { return NO; }
    getEventQueue()->postWindowCloseRequest(window);
    return NO;
}

- (void)windowDidResize:(NSNotification *)notification {
    [self postWindowSize];
}

- (void)windowDidChangeOcclusionState:(NSNotification *)notification {
    [self postWindowSize];
}

- (void)windowDidEnterFullScreen:(NSNotification *)notification {
    [self postWindowSize];
}

- (void)windowDidExitFullScreen:(NSNotification *)notification {
    window->didExitFullScreen();
    [self postWindowSize];
}

@end
