//
// Created by Admin on 13.03.2022.
//

#pragma once

#include "Miren/Base.hpp"
#include "Miren/MirenStates.hpp"
#include "RenderPassInputs.hpp"

#include <string_view>

namespace Miren {

struct RenderPass {
    RenderPass()
        : m_isSubmitted(false)
        , m_state(MIREN_STATE_CULL_FACE | MIREN_STATE_DEPTH_TEST)
        , m_numIndices(0)
        , m_textureBindingsCount(0)
        , m_indicesOffset(0)
        , m_verticesOffset(0)
        , m_instancesOffset(0)
        , m_numInstances(1)
        , m_viewId(0)
        , m_shader(MIREN_INVALID_HANDLE)
        , m_indexBuffer(MIREN_INVALID_HANDLE)
        , m_vertexBuffer(MIREN_INVALID_HANDLE)
        , m_vertexLayout(MIREN_INVALID_HANDLE)
        , m_instanceBuffer(MIREN_INVALID_HANDLE)
        , m_instanceLayout(MIREN_INVALID_HANDLE)
        , m_scissorRect(Rect::zero())
        , m_depthBiasConstant(0.f)
        , m_depthBiasSlope(0.f)
        , m_inputs(2000) {}

    RenderPass(RenderPass &&other) = default;

    RenderPass &operator=(RenderPass &&other) = default;

    void reset() {
        m_isSubmitted = false;
        m_state = MIREN_STATE_CULL_FACE | MIREN_STATE_DEPTH_TEST;
        m_numIndices = 0;
        m_textureBindingsCount = 0;
        m_indicesOffset = 0;
        m_verticesOffset = 0;
        m_instancesOffset = 0;
        m_numInstances = 1;
        m_viewId = 0;
        m_shader = MIREN_INVALID_HANDLE;
        m_indexBuffer = MIREN_INVALID_HANDLE;
        m_vertexBuffer = MIREN_INVALID_HANDLE;
        m_vertexLayout = MIREN_INVALID_HANDLE;
        m_instanceBuffer = MIREN_INVALID_HANDLE;
        m_instanceLayout = MIREN_INVALID_HANDLE;
        m_scissorRect = Rect::zero();
        m_depthBiasConstant = 0.f;
        m_depthBiasSlope = 0.f;
        m_inputs.reset();
    }

    void addInput(std::string_view name, RenderPassInputType type, const void *value, size_t size) {
        PassInput input(name, type, const_cast<void *>(value), size);
        m_inputs.writeInput(input);
    }

    bool m_isSubmitted;
    uint32_t m_numIndices;
    intptr_t m_indicesOffset;
    intptr_t m_verticesOffset;
    ProgramHandle m_shader;
    IndexBufferHandle m_indexBuffer;
    VertexBufferHandle m_vertexBuffer;
    VertexLayoutHandle m_vertexLayout;
    // Instancing: a second vertex stream with a per-instance step. Invalid buffer = a regular draw.
    VertexBufferHandle m_instanceBuffer;
    VertexLayoutHandle m_instanceLayout;
    intptr_t m_instancesOffset;
    uint32_t m_numInstances;
    RenderPassInputs m_inputs;
    uint32_t m_textureBindingsCount;
    Rect m_scissorRect;
    // Depth bias (shadow pass against acne); 0/0 — disabled.
    float m_depthBiasConstant;
    float m_depthBiasSlope;
    ViewId m_viewId;
    uint32_t m_state;
};

} // namespace Miren
