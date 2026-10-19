//
// Created by Admin on 17.02.2022.
//

#import "UIKitWindow.h"
#import "../AppDelegate.h"
#ifdef PANDA_GRAPHICS_METAL
#import "GraphicsContext/IOS_MetalContext.h"
#else
#import "GraphicsContext/GLES_Context.h"
#endif

#import <UIKit/UIKit.h>

namespace {
UIKitWindow *s_mainWindow = nullptr;
}

UIKitWindow::UIKitWindow() {
    m_size = {};
    m_dpi = {1.f, 1.f};
    m_safeAreaInsets = {};
    m_context = nullptr;
    void (^createContext)(void) = ^{
        s_mainWindow = this;
        refreshMetricsFromView();
        auto delegate = (WonderAppDelegate*)[UIApplication sharedApplication].delegate;
        UIViewController *rootViewController = delegate.window.rootViewController;
#ifdef PANDA_GRAPHICS_METAL
        m_context = new IOS_MetalContext((CAMetalLayer*)[rootViewController view].layer);
#else
        m_context = new GLES_Context((CAEAGLLayer*)[rootViewController view].layer);
#endif
    };
    if ([NSThread isMainThread]) {
        createContext();
    } else {
        dispatch_sync(dispatch_get_main_queue(), createContext);
    }
}

UIKitWindow::~UIKitWindow() {
    if (s_mainWindow == this) { s_mainWindow = nullptr; }
    delete m_context;
    m_context = nullptr;
}

void UIKitWindow::refreshMetricsFromView() {
    if (![NSThread isMainThread]) { return; }

    auto delegate = (WonderAppDelegate*)[UIApplication sharedApplication].delegate;
    UIView *view = delegate.window.rootViewController.view;
    const CGFloat scale = view.contentScaleFactor > 0 ? view.contentScaleFactor : UIScreen.mainScreen.nativeScale;
    CGSize size = view.bounds.size;
    UIEdgeInsets insets = view.safeAreaInsets;

    m_size = Fern::Size(static_cast<float>(size.width), static_cast<float>(size.height));
    m_dpi = Fern::Size(static_cast<float>(scale), static_cast<float>(scale));
    m_safeAreaInsets = Fern::EdgeInsets(
        static_cast<float>(insets.top),
        static_cast<float>(insets.left),
        static_cast<float>(insets.right),
        static_cast<float>(insets.bottom)
    );

    if (m_context) { m_context->update(); }
}

Fern::EdgeInsets UIKitWindow::getSafeAreaInsets() {
    return m_safeAreaInsets;
}

UIKitWindow *FernIOSRefreshMainWindowMetrics() {
    if (s_mainWindow) { s_mainWindow->refreshMetricsFromView(); }
    return s_mainWindow;
}
