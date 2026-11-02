//
// Created by Admin on 11.02.2022.
//

#include "GLFW_GLContext.hpp"

namespace Fern {

GLFW_GLContext::GLFW_GLContext(GLFWwindow *windowHandle)
    : m_windowHandle(windowHandle) {
    glfwMakeContextCurrent(m_windowHandle);
    // Enable v-sync
    glfwSwapInterval(1);
    glfwMakeContextCurrent(nullptr);
}

void GLFW_GLContext::setCurrent() {
    glfwMakeContextCurrent(m_windowHandle);
}

void GLFW_GLContext::releaseDrawingContext() {
    glfwMakeContextCurrent(nullptr);
}

void GLFW_GLContext::swapBuffers() {
    glfwSwapBuffers(m_windowHandle);
}

void GLFW_GLContext::update() {}

uint32_t GLFW_GLContext::getDefaultFrameBufferId() {
    return 0;
}

void *GLFW_GLContext::getProcAddress(const char *name) {
    return reinterpret_cast<void *>(glfwGetProcAddress(name));
}

} // namespace Fern