//
// Created by Admin on 11.02.2022.
//

#pragma once

#include "Fern/GraphicsContext/GraphicsContext.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

@class EAGLContext;
@class CAEAGLLayer;

class GLES_Context final : public Fern::GraphicsContext {
public:
    GLES_Context(CAEAGLLayer *eaglLayer);
    ~GLES_Context() override;
    void lockDrawingContext() override;
    void unlockDrawingContext() override;
    void setCurrent() override;
    void releaseDrawingContext() override;
    void swapBuffers() override;
    void update() override;
    uint32_t getDefaultFrameBufferId() override;

private:
    void refreshDrawableSizeFromLayer();
    void resizeDrawableIfNeeded(bool force);

    EAGLContext *m_context;
    CAEAGLLayer *m_layer;
    uint32_t m_colorRenderBuffer;
    uint32_t m_depthRenderBuffer;
    uint32_t m_frameBuffer;
    int m_width;
    int m_height;
    std::atomic_int m_pendingWidth;
    std::atomic_int m_pendingHeight;
    std::atomic_bool m_needsDrawableUpdate;
    std::mutex m_mutex;
};
