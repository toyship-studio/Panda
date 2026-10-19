#pragma once

#import "Fern/Window/Window.hpp"
#import "GraphicsContext/OSX_GLContext.h"

@class NativeWindow;
@class NSMutableArray;

class CocoaWindow final : public Fern::Window {
public:
    CocoaWindow(
        const char *title,
        Fern::Rect rect,
        Fern::WindowState state,
        Fern::WindowType windowType,
        Fern::WindowFlags flags,
        NSOpenGLContext *sharedContext = nil
    );
    ~CocoaWindow() override;
    void setTitle(const char *title) override;
    Fern::WindowState getState() override;
    void setState(Fern::WindowState state) override;
    void setModifiedState(bool isUnsavedChanges) override;
    bool getModifiedState() override;
    void setFlags(Fern::WindowFlags flags) override;
    void setSize(Fern::Size size) override;
    Fern::Size getSize() override;
    Fern::Size getDpi() override;
    Fern::EdgeInsets getSafeAreaInsets() override;
    Fern::Vec2 getCursorPosition() override;
    bool setProgressBar(float progress) override;
    bool endProgressBar() override;
    void setMenuBar(const Fern::MenuBar &menuBar) override;
    Fern::GraphicsContext *getDrawingContext() override;
    NativeWindow *getNative();
    NSOpenGLContext *getNativeGLContext();
    void flushPendingMenuBar();
    bool canClose() const;
    void didBecomeKey();
    void didResignKey();
    void didExitFullScreen();

private:
    void applyFlagsNow(Fern::WindowFlags flags);
    void applySizeNow(Fern::Size size);
    void applyMenuBarNow(const Fern::MenuBar &menuBar);

    Fern::WindowState m_nextState;
    Fern::WindowType m_windowType;
    Fern::WindowFlags m_flags;
    Fern::WindowFlags m_pendingFlags;
    Fern::Size m_pendingSize;
    Fern::MenuBar m_pendingMenuBar;
    bool m_isReady;
    bool m_isCursorLocked;
    bool m_isClosable;
    bool m_allowsMinimize;
    bool m_allowsMaximize;
    bool m_hasPendingFlags;
    bool m_hasPendingSize;
    bool m_hasPendingMenuBar;
    NativeWindow *m_handle;
    NSMutableArray *m_menuTargets;
    Fern::GraphicsContext *m_graphicsContext;
};
