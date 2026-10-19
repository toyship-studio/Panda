#pragma once

#import "Fern/GraphicsContext/GraphicsContext.hpp"
#import <Cocoa/Cocoa.h>

#include <mutex>

class OSX_GLContext final : public Fern::GraphicsContext {
public:
    OSX_GLContext(NSView *view, NSOpenGLContext *shareContext = nil);
    ~OSX_GLContext() override;
    void lockDrawingContext() override;
    void unlockDrawingContext() override;
    void setCurrent() override;
    void releaseDrawingContext() override;
    void swapBuffers() override;
    void update() override;
    uint32_t getDefaultFrameBufferId() override;
    NSOpenGLContext *getNativeContext();

private:
    NSOpenGLContext *m_context;
    std::mutex m_mutex;
};
