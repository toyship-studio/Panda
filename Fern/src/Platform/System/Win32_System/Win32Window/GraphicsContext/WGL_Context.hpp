#pragma once

#include "Fern/GraphicsContext/GraphicsContext.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#    define UNICODE
#endif
#include <windows.h>

#include <cstdint>

namespace Fern {

class WGL_Context final : public GraphicsContext {
public:
    WGL_Context(HWND window, WGL_Context *sharedContext);
    ~WGL_Context() override;
    void setCurrent() override;
    void releaseDrawingContext() override;
    void swapBuffers() override;
    void update() override;
    uint32_t getDefaultFrameBufferId() override;

    HGLRC getHandle() {
        return m_context;
    }

private:
    HWND m_window;
    HDC m_deviceContext;
    HGLRC m_context;
};

} // namespace Fern
