//
// Created by Admin on 10.03.2022.
//

#include "Miren/Miren_p.hpp"

namespace Miren {

// MARK: - PUBLIC METHODS IMPL

static Context *s_context = nullptr;

void initialize(Fern::GraphicsContext *ctx) {
    MIREN_LOG("MIREN INIT BEGIN");
    MIREN_LOG("ALLOCATING MIREN CONTEXT, {} BYTES", sizeof(Context));
    MIREN_LOG("FRAME DATA SIZE: {} BYTES", sizeof(Frame));
    s_context = F_NEW(Foundation::getAllocator(), Context);
    s_context->init(ctx);
    MIREN_LOG("MIREN INIT END");
}

void terminate() {
    MIREN_LOG("MIREN SHUTDOWN BEGIN");
    s_context->shutdown();
    F_DELETE(Foundation::getAllocator(), s_context);
    s_context = nullptr;
    MIREN_LOG("MIREN SHUTDOWN END");
}

FrameBufferHandle createFrameBuffer(FrameBufferSpecification specification) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->createFrameBuffer(specification);
}

uint32_t readFrameBuffer(FrameBufferHandle handle, int attachIndex, int x, int y, int width, int height, void *data) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->readFrameBuffer(handle, attachIndex, x, y, width, height, data);
}

void deleteFrameBuffer(FrameBufferHandle handle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->deleteFrameBuffer(handle);
}

ProgramHandle createProgram(ProgramCreate create) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->createProgram(create);
}

void deleteProgram(ProgramHandle handle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->deleteProgram(handle);
}

TextureHandle createTexture(TextureCreate create) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    PND_ASSERT_F(
        create.m_width > 0 && create.m_height > 0, "INVALID TEXTURE SIZE w: %d, h: %d", create.m_width, create.m_height
    );
    PND_ASSERT(!(create.is3D() && create.m_isCubeMap), "TEXTURE CANNOT BE BOTH 3D AND CUBE");
    PND_ASSERT(!(create.is3D() && create.m_numMips > 0), "3D TEXTURE HAS NO MIPS");
    PND_ASSERT(!(create.is3D() && create.isDepthFormat()), "3D TEXTURE CANNOT BE DEPTH");
    return s_context->createTexture(create);
}

void updateTexture(TextureHandle handle, Foundation::Memory mem) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->updateTexture(handle, mem);
}

void updateTextureRegion(
    TextureHandle handle, uint16_t x, uint16_t y, uint16_t width, uint16_t height,
    Foundation::Memory mem
) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    PND_ASSERT_F(width > 0 && height > 0, "INVALID TEXTURE REGION w: %d, h: %d", width, height);
    s_context->updateTextureRegion(handle, x, y, 0, width, height, 1, mem);
}

void updateTextureRegion3D(
    TextureHandle handle, uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height,
    uint16_t depth, Foundation::Memory mem
) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    PND_ASSERT_F(
        width > 0 && height > 0 && depth > 0, "INVALID TEXTURE REGION w: %d, h: %d, d: %d", width, height, depth
    );
    s_context->updateTextureRegion(handle, x, y, z, width, height, depth, mem);
}

void resizeTexture(TextureHandle handle, uint32_t width, uint32_t height) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    PND_ASSERT_F(width > 0 && height > 0, "INVALID TEXTURE SIZE w: %d, h: %d", width, height);
    s_context->resizeTexture(handle, width, height);
}

void generateTextureMipmaps(TextureHandle handle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->generateTextureMipmaps(handle);
}

void deleteTexture(TextureHandle handle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->deleteTexture(handle);
}

IndexBufferHandle createIndexBuffer(Foundation::Memory indices, BufferElementType elementType, size_t count) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->createIndexBuffer(indices, elementType, count);
}

IndexBufferHandle createDynamicIndexBuffer(Foundation::Memory indices, BufferElementType elementType, size_t count) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->createDynamicIndexBuffer(indices, elementType, count);
}

void updateDynamicIndexBuffer(IndexBufferHandle handle, Foundation::Memory indices, size_t count) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->updateDynamicIndexBuffer(handle, indices, count);
}

void deleteIndexBuffer(IndexBufferHandle handle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->deleteIndexBuffer(handle);
}

VertexBufferHandle createVertexBuffer(Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->createVertexBuffer(data, size, layoutHandle);
}

VertexBufferHandle createDynamicVertexBuffer(Foundation::Memory data, uint32_t size, VertexLayoutHandle layoutHandle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->createDynamicVertexBuffer(data, size, layoutHandle);
}

void updateDynamicVertexBuffer(VertexBufferHandle handle, Foundation::Memory data, uint32_t size) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->updateDynamicVertexBuffer(handle, data, size);
}

void deleteVertexBuffer(VertexBufferHandle handle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->deleteVertexBuffer(handle);
}

VertexLayoutHandle createVertexLayout(VertexBufferLayoutData data) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->createVertexLayout(data);
}

void deleteVertexLayout(VertexLayoutHandle handle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->deleteVertexLayout(handle);
}

uint32_t readTexture(TextureHandle handle, void *data) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->readTexture(handle, Foundation::Memory(data, nullptr, [](void *, void *) {}));
}

uint32_t readTexture(TextureHandle handle, Foundation::Memory destination) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->readTexture(handle, destination);
}

void allocTransientVertexBuffer(TransientVertexBuffer *buffer, uint32_t size) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->allocTransientVertexBuffer(buffer, size);
}

void allocTransientIndexBuffer(TransientIndexBuffer *buffer, uint32_t count) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->allocTransientIndexBuffer(buffer, count);
}

bool renderFrame() {
    // PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    if (!s_context) { return false; }
    return s_context->renderFrame();
}

GpuStats getGpuStats() {
    return s_context ? s_context->getGpuStats() : GpuStats{};
}

uint32_t frame() {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->frame();
}

uint32_t getFrameNumber() {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    return s_context->getFrameNumber();
}

void setViewClear(ViewId id, uint32_t color) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setViewClear(id, color);
}

void setViewClearEnabled(ViewId id, bool enabled) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setViewClearEnabled(id, enabled);
}

void setViewClearAttachments(ViewId id, std::vector<Clear> clear) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setViewClearAttachments(id, clear);
}

void setViewClearDepth(ViewId id, bool enabled, float depth) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setViewClearDepth(id, enabled, depth);
}

void setViewport(ViewId id, Rect rect) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setViewport(id, rect);
}

void setViewGraphicsContext(ViewId id, Fern::GraphicsContext *ctx) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setViewGraphicsContext(id, ctx);
}

void setViewFrameBuffer(ViewId id, FrameBufferHandle frameBuffer) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setViewFrameBuffer(id, frameBuffer);
}

void setState(uint32_t state) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setState(state);
}

void setScissorRect(Rect rect) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setScissorRect(rect);
}

void setDepthBias(float constant, float slope) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setDepthBias(constant, slope);
}

void setVertexBuffer(VertexBufferHandle handle, intptr_t offset) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setVertexBuffer(handle, offset);
}

void setIndexBuffer(IndexBufferHandle handle, intptr_t offset, size_t count) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setIndexBuffer(handle, offset, count);
}

void addInputUniformBuffer(std::string_view name, const void *value, size_t size) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->addInput(name, RenderPassInputType::UniformBuffer, value, size);
}

void addInputTexture(std::string_view name, TextureHandle handle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->addInput(name, RenderPassInputType::ImageSampler2D, &handle, sizeof(TextureHandle));
}

void setVertexLayout(VertexLayoutHandle handle) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setVertexLayout(handle);
}

void setInstanceBuffer(VertexBufferHandle handle, VertexLayoutHandle layoutHandle, intptr_t offset, uint32_t count) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->setInstanceBuffer(handle, layoutHandle, offset, count);
}

void discard() {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->discard();
}

void submit(ViewId id, ProgramHandle program) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->submit(id, program);
}

void touch(ViewId id) {
    PND_ASSERT(s_context != nullptr, "MIREN NOT INITIALIZED");
    s_context->discard();
    s_context->submit(id, ProgramHandle());
}

} // namespace Miren
