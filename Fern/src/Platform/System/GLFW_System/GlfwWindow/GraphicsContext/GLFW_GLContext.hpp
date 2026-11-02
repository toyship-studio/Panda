//
// Created by Admin on 11.02.2022.
//

#pragma once

#include "Fern/GraphicsContext/GraphicsContext.hpp"

#define GLFW_INCLUDE_NONE // the context loads no GL function itself: the renderer does, through getProcAddress
#include <GLFW/glfw3.h>

#include <cstdint>

namespace Fern {

// GLFW based context
class GLFW_GLContext : public GraphicsContext {
public:
    GLFW_GLContext(GLFWwindow *windowHandle);
    void setCurrent() override;
    void releaseDrawingContext() override;
    void swapBuffers() override;
    void update() override;
    uint32_t getDefaultFrameBufferId() override;
    void *getProcAddress(const char *name) override;

private:
    GLFWwindow *m_windowHandle;
};

} // namespace Fern
