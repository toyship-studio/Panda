#import "CocoaWindow.h"
#import "CocoaWindowDelegate.h"
#import "WonderView.h"
#import "Fern/Events/EventQueue.hpp"
#ifdef PANDA_GRAPHICS_METAL
#import "GraphicsContext/OSX_MetalContext.h"
#endif

#import <Cocoa/Cocoa.h>

using namespace Fern;

namespace Fern {
void pollEvents();
EventQueue *getEventQueue();
}

@interface FernMenuTarget : NSObject
- (instancetype)initWithWindow:(Fern::Window *)window commandId:(Fern::MenuCommandId)commandId;
- (void)performMenuCommand:(id)sender;
@end

@implementation FernMenuTarget {
    Fern::Window *m_window;
    Fern::MenuCommandId m_commandId;
}

- (instancetype)initWithWindow:(Fern::Window *)window commandId:(Fern::MenuCommandId)commandId {
    self = [super init];
    if (self) {
        m_window = window;
        m_commandId = commandId;
    }
    return self;
}

- (void)performMenuCommand:(id)sender {
    Fern::getEventQueue()->postMenuCommandEvent(m_commandId, m_window);
}

@end

namespace {

NSString *makeNSString(const std::string &value) {
    return [NSString stringWithUTF8String:value.c_str()];
}

NSString *keyEquivalentForShortcut(Fern::MenuShortcut shortcut) {
    if (!shortcut.isValid()) { return @""; }

    const int key = static_cast<int>(shortcut.key);
    if (key >= static_cast<int>(Fern::Key::KEY_A) && key <= static_cast<int>(Fern::Key::KEY_Z)) {
        unichar character = static_cast<unichar>('a' + key - static_cast<int>(Fern::Key::KEY_A));
        return [NSString stringWithCharacters:&character length:1];
    }
    if (key >= static_cast<int>(Fern::Key::KEY_0) && key <= static_cast<int>(Fern::Key::KEY_9)) {
        unichar character = static_cast<unichar>('0' + key - static_cast<int>(Fern::Key::KEY_0));
        return [NSString stringWithCharacters:&character length:1];
    }

    switch (shortcut.key) {
        case Fern::Key::KEY_COMMA: return @",";
        case Fern::Key::KEY_MINUS: return @"-";
        case Fern::Key::KEY_PERIOD: return @".";
        case Fern::Key::KEY_SLASH: return @"/";
        case Fern::Key::KEY_SEMICOLON: return @";";
        case Fern::Key::KEY_EQUAL: return @"=";
        case Fern::Key::KEY_LEFT_BRACKET: return @"[";
        case Fern::Key::KEY_BACKSLASH: return @"\\";
        case Fern::Key::KEY_RIGHT_BRACKET: return @"]";
        case Fern::Key::KEY_GRAVE_ACCENT: return @"`";
        default: return @"";
    }
}

NSEventModifierFlags modifierMaskForShortcut(Fern::MenuShortcut shortcut) {
    NSEventModifierFlags mask = 0;
    if (shortcut.modifiers & Fern::MenuShortcut::Shift) { mask |= NSEventModifierFlagShift; }
    if (shortcut.modifiers & Fern::MenuShortcut::Control) { mask |= NSEventModifierFlagControl; }
    if (shortcut.modifiers & Fern::MenuShortcut::Alt) { mask |= NSEventModifierFlagOption; }
    if (shortcut.modifiers & Fern::MenuShortcut::Super) { mask |= NSEventModifierFlagCommand; }
    if (shortcut.modifiers & Fern::MenuShortcut::Primary) { mask |= NSEventModifierFlagCommand; }
    return mask;
}

void addMenuItems(NSMenu *menu, const std::vector<Fern::MenuItem> &items, Fern::Window *window, NSMutableArray *targets) {
    for (const Fern::MenuItem &item : items) {
        switch (item.getType()) {
            case Fern::MenuItemType::Separator:
                [menu addItem:[NSMenuItem separatorItem]];
                break;
            case Fern::MenuItemType::Command: {
                Fern::MenuShortcut shortcut = item.getShortcut();
                NSMenuItem *nativeItem = [[NSMenuItem alloc] initWithTitle:makeNSString(item.getTitle())
                                                                     action:@selector(performMenuCommand:)
                                                              keyEquivalent:keyEquivalentForShortcut(shortcut)];
                [nativeItem setKeyEquivalentModifierMask:modifierMaskForShortcut(shortcut)];
                [nativeItem setEnabled:item.isEnabled()];
                [nativeItem setState:item.isChecked() ? NSControlStateValueOn : NSControlStateValueOff];
                FernMenuTarget *target = [[FernMenuTarget alloc] initWithWindow:window commandId:item.getCommandId()];
                [nativeItem setTarget:target];
                [targets addObject:target];
                [target release];
                [menu addItem:nativeItem];
                [nativeItem release];
                break;
            }
            case Fern::MenuItemType::Submenu: {
                NSMenuItem *nativeItem = [[NSMenuItem alloc] initWithTitle:makeNSString(item.getTitle())
                                                                     action:nil
                                                              keyEquivalent:@""];
                NSMenu *submenu = [[NSMenu alloc] initWithTitle:makeNSString(item.getTitle())];
                [submenu setAutoenablesItems:NO];
                addMenuItems(submenu, item.getChildren(), window, targets);
                [nativeItem setSubmenu:submenu];
                [submenu release];
                [nativeItem setEnabled:item.isEnabled()];
                [menu addItem:nativeItem];
                [nativeItem release];
                break;
            }
        }
    }
}

void addApplicationMenu(NSMenu *mainMenu) {
    NSString *appName = [[NSProcessInfo processInfo] processName];
    NSMenuItem *appMenuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:appName];
    [appMenu setAutoenablesItems:NO];
    NSString *quitTitle = [NSString stringWithFormat:@"Quit %@", appName];
    NSMenuItem *quitItem = [[NSMenuItem alloc] initWithTitle:quitTitle action:@selector(terminate:) keyEquivalent:@"q"];
    [quitItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [appMenu addItem:quitItem];
    [quitItem release];
    [appMenuItem setSubmenu:appMenu];
    [mainMenu addItem:appMenuItem];
    [appMenu release];
    [appMenuItem release];
}

Fern::WindowFlags normalizeWindowFlags(Fern::WindowType windowType, Fern::WindowFlags flags) {
    if (windowType == Fern::WindowType::Popup) {
        flags &= ~(FERN_WINDOW_FLAG_RESIZABLE | FERN_WINDOW_FLAG_CLOSABLE | FERN_WINDOW_FLAG_MINIMIZABLE |
                   FERN_WINDOW_FLAG_MAXIMIZABLE);
        flags |= FERN_WINDOW_FLAG_ALWAYS_ON_TOP;
    } else if (windowType == Fern::WindowType::Modal) {
        flags |= FERN_WINDOW_FLAG_ALWAYS_ON_TOP;
    }
    return flags;
}

NSUInteger styleMaskForWindow(Fern::WindowType windowType, Fern::WindowFlags flags) {
    if (windowType == Fern::WindowType::Popup) { return NSWindowStyleMaskBorderless; }

    NSUInteger styleMask = NSWindowStyleMaskTitled;
    if (Fern::hasWindowFlag(flags, FERN_WINDOW_FLAG_CLOSABLE)) { styleMask |= NSWindowStyleMaskClosable; }
    if (Fern::hasWindowFlag(flags, FERN_WINDOW_FLAG_MINIMIZABLE)) { styleMask |= NSWindowStyleMaskMiniaturizable; }
    if (Fern::hasWindowFlag(flags, FERN_WINDOW_FLAG_RESIZABLE)) { styleMask |= NSWindowStyleMaskResizable; }
    return styleMask;
}

NSWindowCollectionBehavior collectionBehaviorForWindow(Fern::WindowType windowType, Fern::WindowFlags flags) {
    if (windowType == Fern::WindowType::Popup) {
        return NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorTransient;
    }
    if (Fern::hasWindowFlag(flags, FERN_WINDOW_FLAG_RESIZABLE) &&
        Fern::hasWindowFlag(flags, FERN_WINDOW_FLAG_MAXIMIZABLE)) {
        return NSWindowCollectionBehaviorFullScreenPrimary | NSWindowCollectionBehaviorManaged;
    }
    return NSWindowCollectionBehaviorManaged;
}

NSWindowLevel levelForWindow(Fern::WindowType windowType, Fern::WindowFlags flags) {
    if (windowType == Fern::WindowType::Modal) { return NSModalPanelWindowLevel; }
    if (windowType == Fern::WindowType::Popup || Fern::hasWindowFlag(flags, FERN_WINDOW_FLAG_ALWAYS_ON_TOP)) {
        return NSFloatingWindowLevel;
    }
    return NSNormalWindowLevel;
}

void applyStandardButtonState(NSWindow *window, Fern::WindowFlags flags) {
    NSButton *zoomButton = [window standardWindowButton:NSWindowZoomButton];
    if (zoomButton) { [zoomButton setEnabled:Fern::hasWindowFlag(flags, FERN_WINDOW_FLAG_MAXIMIZABLE)]; }
    NSButton *miniaturizeButton = [window standardWindowButton:NSWindowMiniaturizeButton];
    if (miniaturizeButton) { [miniaturizeButton setEnabled:Fern::hasWindowFlag(flags, FERN_WINDOW_FLAG_MINIMIZABLE)]; }
    NSButton *closeButton = [window standardWindowButton:NSWindowCloseButton];
    if (closeButton) { [closeButton setEnabled:Fern::hasWindowFlag(flags, FERN_WINDOW_FLAG_CLOSABLE)]; }
}

} // namespace

@interface NativeWindow : NSWindow
- (BOOL)isFullScreen;
@end

@implementation NativeWindow

- (BOOL)isWindowResizable {
    return (([self styleMask] & NSWindowStyleMaskResizable) == NSWindowStyleMaskResizable);
}

- (BOOL)isFullScreen {
    return (([self styleMask] & NSWindowStyleMaskFullScreen) == NSWindowStyleMaskFullScreen);
}

- (BOOL)canBecomeKeyWindow {
    // Required for NSWindowStyleMaskBorderless windows
    return YES;
}

- (BOOL)canBecomeMainWindow {
    return YES;
}

@end

CocoaWindow::CocoaWindow(
    const char *title,
    Fern::Rect _rect,
    Fern::WindowState state,
    Fern::WindowType windowType,
    Fern::WindowFlags flags,
    NSOpenGLContext *sharedContext
)
    : m_nextState(WindowState::WindowStateNone)
    , m_windowType(windowType)
    , m_flags(normalizeWindowFlags(windowType, flags))
    , m_pendingFlags(0)
    , m_pendingSize()
    , m_pendingMenuBar()
    , m_isReady(false)
    , m_isCursorLocked(false)
    , m_isClosable(Fern::hasWindowFlag(m_flags, FERN_WINDOW_FLAG_CLOSABLE) && windowType != Fern::WindowType::Popup)
    , m_allowsMinimize(
          Fern::hasWindowFlag(m_flags, FERN_WINDOW_FLAG_MINIMIZABLE) && windowType != Fern::WindowType::Popup
      )
    , m_allowsMaximize(
          Fern::hasWindowFlag(m_flags, FERN_WINDOW_FLAG_MAXIMIZABLE) && windowType != Fern::WindowType::Popup
      )
    , m_hasPendingFlags(false)
    , m_hasPendingSize(false)
    , m_hasPendingMenuBar(false)
    , m_handle(nil)
    , m_menuTargets(nil)
    , m_graphicsContext(nullptr) {
    NSRect rect = CGRectMake(_rect.origin.x, _rect.origin.y, _rect.size.width, _rect.size.height);

    NSUInteger styleMask = styleMaskForWindow(windowType, m_flags);

    NativeWindow* window = [[NativeWindow alloc] initWithContentRect:rect
                                                   styleMask:styleMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];
    [window setCollectionBehavior:collectionBehaviorForWindow(windowType, m_flags)];
    [window setLevel:levelForWindow(windowType, m_flags)];
    [window setTitle:@(title)];
    applyStandardButtonState(window, m_flags);
    CocoaWindowDelegate* delegate = [[CocoaWindowDelegate alloc] initWithCocoa:this];
    [window setDelegate:delegate];
    [window setHasShadow:NO];
    [window center];
    [window setBackgroundColor:[NSColor blackColor]];
    WonderView* gameView = [[WonderView alloc] init];
    gameView.eventWindow = this;
    [gameView setWantsLayer:YES];
    [window setContentView:gameView];
    [gameView release];
    [window makeFirstResponder:gameView];
    [window setAcceptsMouseMovedEvents:YES];
    [window setRestorable:NO];
    if ([window respondsToSelector:@selector(setTabbingMode:)]) {
        [window setTabbingMode:NSWindowTabbingModeDisallowed];
    }
    [window makeKeyAndOrderFront:nil];
    m_menuTargets = [[NSMutableArray alloc] init];
#ifdef PANDA_GRAPHICS_METAL
    (void)sharedContext; // Metal shares resources via the shared device, GL sharing is not needed
    m_graphicsContext = new OSX_MetalContext([window contentView]);
#else
    m_graphicsContext = new OSX_GLContext([window contentView], sharedContext);
#endif
    this->m_handle = window;
}

CocoaWindow::~CocoaWindow() {
    delete m_graphicsContext;
    m_graphicsContext = nullptr;
    [m_handle orderOut:nil];
    [m_handle setDelegate:nil];
    [m_handle close];
    [m_handle release];
    m_handle = nil;
    [m_menuTargets removeAllObjects];
    [m_menuTargets release];
    m_menuTargets = nil;
    // HACK: Allow Cocoa to catch up before returning
    pollEvents();
}

void CocoaWindow::setTitle(const char *title) {
    [m_handle setTitle:@(title)];
}

WindowState CocoaWindow::getState() {
    NSUInteger masks = m_handle.styleMask;
    if (masks & NSWindowStyleMaskFullScreen) {
        return WindowState::WindowStateFullScreen;
    }
    if (m_handle.isMiniaturized) {
        return WindowState::WindowStateMinimized;
    }
    if (m_handle.isZoomed) {
        return WindowState::WindowStateMaximized;
    }
    return WindowState::WindowStateNormal;
}

void CocoaWindow::setState(Fern::WindowState state) {
    if (!m_isReady) {
        m_nextState = state;
        return;
    }
    switch (state) {
        case WindowState::WindowStateNone:
            break;
        case WindowState::WindowStateMinimized:
            if (!m_allowsMinimize) { break; }
            [m_handle miniaturize:nil];
            break;
        case WindowState::WindowStateMaximized:
            if (!m_allowsMaximize) { break; }
            if (![m_handle isZoomed]) {
                [m_handle zoom:nil];
            }
            break;
        case WindowState::WindowStateFullScreen: {
            if (!m_allowsMaximize) { break; }
            const NSUInteger masks = m_handle.styleMask;
            if (!(masks & NSWindowStyleMaskFullScreen)) {
                [m_handle toggleFullScreen:nil];
            }
            break;
        }
        case WindowState::WindowStateNormal:
        default: {
            const NSUInteger masks = m_handle.styleMask;
            if (masks & NSWindowStyleMaskFullScreen) {
                [m_handle toggleFullScreen:nil];
            } else if (m_handle.isMiniaturized) {
                [m_handle deminiaturize:nil];
            } else if (m_handle.isZoomed) {
                [m_handle zoom:nil];
            }
        }
        break;
    }
    m_nextState = WindowState::WindowStateNone;
}

bool CocoaWindow::getModifiedState() {
    return m_handle.documentEdited;
}

void CocoaWindow::setModifiedState(bool isUnsavedChanges) {
    m_handle.documentEdited = isUnsavedChanges;
}

void CocoaWindow::setFlags(Fern::WindowFlags flags) {
    flags = normalizeWindowFlags(m_windowType, flags);
    if ([m_handle styleMask] & NSWindowStyleMaskFullScreen) {
        m_pendingFlags = flags;
        m_hasPendingFlags = true;
        return;
    }
    applyFlagsNow(flags);
}

void CocoaWindow::setSize(Fern::Size size) {
    if ([m_handle styleMask] & NSWindowStyleMaskFullScreen) {
        m_pendingSize = size;
        m_hasPendingSize = true;
        return;
    }
    applySizeNow(size);
}

void CocoaWindow::applyFlagsNow(Fern::WindowFlags flags) {
    m_flags = normalizeWindowFlags(m_windowType, flags);
    m_isClosable = Fern::hasWindowFlag(m_flags, FERN_WINDOW_FLAG_CLOSABLE) && m_windowType != Fern::WindowType::Popup;
    m_allowsMinimize =
        Fern::hasWindowFlag(m_flags, FERN_WINDOW_FLAG_MINIMIZABLE) && m_windowType != Fern::WindowType::Popup;
    m_allowsMaximize =
        Fern::hasWindowFlag(m_flags, FERN_WINDOW_FLAG_MAXIMIZABLE) && m_windowType != Fern::WindowType::Popup;
    [m_handle setStyleMask:styleMaskForWindow(m_windowType, m_flags)];
    [m_handle setCollectionBehavior:collectionBehaviorForWindow(m_windowType, m_flags)];
    [m_handle setLevel:levelForWindow(m_windowType, m_flags)];
    applyStandardButtonState(m_handle, m_flags);
}

void CocoaWindow::applySizeNow(Fern::Size size) {
    NSRect contentRect = [m_handle contentRectForFrameRect:[m_handle frame]];
    contentRect.origin.y += contentRect.size.height - size.height;
    contentRect.size = NSMakeSize(size.width, size.height);
    [m_handle setFrame:[m_handle frameRectForContentRect:contentRect]
                        display:YES];
    [m_handle center];
}

Fern::Size CocoaWindow::getSize() {
    const NSRect contentRect = [[m_handle contentView] frame];
    float width = (float) contentRect.size.width;
    float height = (float) contentRect.size.height;
    return {width, height};
}

Fern::Size CocoaWindow::getDpi() {
    NSScreen* screen = [NSScreen mainScreen];
    float scale = (float) screen.backingScaleFactor;
    return {scale, scale};
}

Fern::EdgeInsets CocoaWindow::getSafeAreaInsets() {
    return {};
}

Vec2 CocoaWindow::getCursorPosition() {
    NSRect  originalFrame = [m_handle frame];
    NSPoint location      = [m_handle mouseLocationOutsideOfEventStream];
    NSRect  adjustFrame   = [m_handle contentRectForFrameRect: originalFrame];

    int32_t x = location.x;
    int32_t y = int32_t(adjustFrame.size.height) - int32_t(location.y);

    // clamp within the range of the window
    x = Foundation::clamp(x, 0, int32_t(adjustFrame.size.width) );
    y = Foundation::clamp(y, 0, int32_t(adjustFrame.size.height) );
    return Vec2(x, y);
}

bool CocoaWindow::setProgressBar(float progress) {
    return true;
}

bool CocoaWindow::endProgressBar() {
    return true;
}

void CocoaWindow::setMenuBar(const Fern::MenuBar &menuBar) {
    m_pendingMenuBar = menuBar;
    m_hasPendingMenuBar = true;
}

void CocoaWindow::flushPendingMenuBar() {
    if (!m_hasPendingMenuBar) { return; }
    m_hasPendingMenuBar = false;
    applyMenuBarNow(m_pendingMenuBar);
}

void CocoaWindow::applyMenuBarNow(const Fern::MenuBar &menuBar) {
    [m_menuTargets removeAllObjects];

    NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@""];
    [mainMenu setAutoenablesItems:NO];
    addApplicationMenu(mainMenu);
    for (const Fern::MenuItem &menu : menuBar.getMenus()) {
        NSMenuItem *nativeItem = [[NSMenuItem alloc] initWithTitle:makeNSString(menu.getTitle())
                                                            action:nil
                                                     keyEquivalent:@""];
        NSMenu *submenu = [[NSMenu alloc] initWithTitle:makeNSString(menu.getTitle())];
        [submenu setAutoenablesItems:NO];
        addMenuItems(submenu, menu.getChildren(), this, m_menuTargets);
        [nativeItem setSubmenu:submenu];
        [submenu release];
        [nativeItem setEnabled:menu.isEnabled()];
        [mainMenu addItem:nativeItem];
        [nativeItem release];
    }
    [NSApp setMainMenu:mainMenu];
    [mainMenu release];
}

GraphicsContext* CocoaWindow::getDrawingContext() {
    return m_graphicsContext;
}

NSOpenGLContext *CocoaWindow::getNativeGLContext() {
#ifdef PANDA_GRAPHICS_METAL
    return nil;
#else
    return static_cast<OSX_GLContext *>(m_graphicsContext)->getNativeContext();
#endif
}

NativeWindow* CocoaWindow::getNative() {
    return m_handle;
}

bool CocoaWindow::canClose() const {
    return m_isClosable;
}

void CocoaWindow::didExitFullScreen() {
    if (m_hasPendingFlags) {
        m_hasPendingFlags = false;
        applyFlagsNow(m_pendingFlags);
    }
    if (m_hasPendingSize) {
        m_hasPendingSize = false;
        applySizeNow(m_pendingSize);
    }
}

void CocoaWindow::didBecomeKey() {
    m_isReady = true;
    setState(m_nextState);
}

void CocoaWindow::didResignKey() {
    m_isReady = false;
}
