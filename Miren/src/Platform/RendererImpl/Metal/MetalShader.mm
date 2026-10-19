#include "MetalShader.hpp"
#include "MetalRendererConstants.hpp"

#if defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)

#    import <Metal/Metal.h>

#    include <algorithm>
#    include <cstring>

namespace Miren {

namespace {

    id<MTLFunction> compileStage(id<MTLDevice> device, const void *source, size_t sourceSize, const char *kind) {
        NSString *code = [[[NSString alloc] initWithBytes:source
                                                   length:sourceSize
                                                 encoding:NSUTF8StringEncoding] autorelease];
        if (code == nil) {
            LOG_ERROR("METAL: Shader source is not valid UTF-8 (%s)", kind);
            return nil;
        }
        MTLCompileOptions *options = [[[MTLCompileOptions alloc] init] autorelease];
        options.languageVersion = MTLLanguageVersion2_1; // in sync with the cook (--msl-version 20100)
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:code options:options error:&error];
        if (library == nil) {
            const char *message = error ? error.localizedDescription.UTF8String : "no error output";
            LOG_ERROR("SHADER COMPILATION ERROR (%s):\n%s", kind, message);
            LOG_ERROR_EDITOR("SHADER COMPILATION ERROR (%s):\n%s", kind, message);
            return nil;
        }
        // spirv-cross renames main → main0 (main is reserved in MSL).
        id<MTLFunction> function = [library newFunctionWithName:@"main0"];
        if (function == nil) { function = [library newFunctionWithName:@"main"]; }
        if (function == nil) {
            LOG_ERROR("METAL: Entry point main0 not found (%s)", kind);
            LOG_ERROR_EDITOR("METAL: Entry point main0 not found (%s)", kind);
        }
        [library release];
        return function;
    }

    MTLVertexFormat vertexFormat(const VertexBufferElement &element) {
        switch (element.type) {
            case BufferElementType::Float:
                switch (element.count) {
                    case 1:
                        return MTLVertexFormatFloat;
                    case 2:
                        return MTLVertexFormatFloat2;
                    case 3:
                        return MTLVertexFormatFloat3;
                    case 4:
                        return MTLVertexFormatFloat4;
                }
                break;
            case BufferElementType::UnsignedInt:
                switch (element.count) {
                    case 1:
                        return MTLVertexFormatUInt;
                    case 2:
                        return MTLVertexFormatUInt2;
                    case 3:
                        return MTLVertexFormatUInt3;
                    case 4:
                        return MTLVertexFormatUInt4;
                }
                break;
            case BufferElementType::Int:
                switch (element.count) {
                    case 1:
                        return MTLVertexFormatInt;
                    case 2:
                        return MTLVertexFormatInt2;
                    case 3:
                        return MTLVertexFormatInt3;
                    case 4:
                        return MTLVertexFormatInt4;
                }
                break;
            case BufferElementType::UnsignedByte:
                switch (element.count) {
                    case 2:
                        return element.normalized ? MTLVertexFormatUChar2Normalized : MTLVertexFormatUChar2;
                    case 4:
                        return element.normalized ? MTLVertexFormatUChar4Normalized : MTLVertexFormatUChar4;
                }
                break;
            default:
                break;
        }
        PND_ASSERT(false, "Unsupported vertex element format for Metal");
        return MTLVertexFormatInvalid;
    }

    bool isIntegerPixelFormat(uint32_t format) {
        switch (static_cast<MTLPixelFormat>(format)) {
            case MTLPixelFormatR8Sint:
            case MTLPixelFormatR8Uint:
            case MTLPixelFormatR32Sint:
            case MTLPixelFormatR32Uint:
                return true;
            default:
                return false;
        }
    }

} // namespace

bool MetalShader::PipelineKey::operator<(const PipelineKey &other) const {
    if (layoutId != other.layoutId) { return layoutId < other.layoutId; }
    if (instanceLayoutId != other.instanceLayoutId) { return instanceLayoutId < other.instanceLayoutId; }
    if (blend != other.blend) { return blend < other.blend; }
    if (colorWrite != other.colorWrite) { return colorWrite < other.colorWrite; }
    if (colorCount != other.colorCount) { return colorCount < other.colorCount; }
    if (depthFormat != other.depthFormat) { return depthFormat < other.depthFormat; }
    for (uint32_t i = 0; i < MAX_FRAMEBUFFER_ATTACHMENTS; i++) {
        if (colorFormats[i] != other.colorFormats[i]) { return colorFormats[i] < other.colorFormats[i]; }
    }
    return false;
}

MetalShader::MetalShader()
    : m_vertexFunction(nullptr)
    , m_fragmentFunction(nullptr)
    , m_pipelines()
    , m_reflected(false) {}

void MetalShader::create(void *device, ProgramCreate create) {
    PND_ASSERT(m_vertexFunction == nullptr, "PROGRAM ALREADY CREATED");
    id<MTLFunction> vertexFunction =
        compileStage((id<MTLDevice>)device, create.m_vertexBinary.data, create.m_vertexBinarySize, "VERTEX");
    create.m_vertexBinary.release();
    id<MTLFunction> fragmentFunction =
        compileStage((id<MTLDevice>)device, create.m_fragmentBinary.data, create.m_fragmentBinarySize, "FRAGMENT");
    create.m_fragmentBinary.release();
    if (vertexFunction == nil || fragmentFunction == nil) {
        if (vertexFunction) { [vertexFunction release]; }
        if (fragmentFunction) { [fragmentFunction release]; }
        return;
    }
    m_vertexFunction = (void *)vertexFunction;
    m_fragmentFunction = (void *)fragmentFunction;
}

void MetalShader::terminate() {
    for (auto &pair : m_pipelines) {
        if (pair.second) { [(id)pair.second release]; }
    }
    m_pipelines.clear();
    m_buffers.clear();
    m_textures.clear();
    m_samplers.clear();
    m_missingInputLog.clear();
    m_reflected = false;
    if (m_fragmentFunction) {
        [(id)m_fragmentFunction release];
        m_fragmentFunction = nullptr;
    }
    if (m_vertexFunction) {
        [(id)m_vertexFunction release];
        m_vertexFunction = nullptr;
    }
}

bool MetalShader::isValid() const {
    return m_vertexFunction != nullptr;
}

void MetalShader::captureReflection(void *reflectionPtr) {
    if (m_reflected || reflectionPtr == nullptr) { return; }
    m_reflected = true;
    MTLRenderPipelineReflection *reflection = (MTLRenderPipelineReflection *)reflectionPtr;

    const auto record = [&](MTLBindingType type, NSUInteger index, NSString *name, bool isVertex) {
        std::unordered_map<std::string, StageSlots> *target = nullptr;
        switch (type) {
            case MTLBindingTypeBuffer:
                // The vertex stream slot is not a shader resource.
                if (index == METAL_VERTEX_STREAM_BUFFER_INDEX) { return; }
                target = &m_buffers;
                break;
            case MTLBindingTypeTexture:
                target = &m_textures;
                break;
            case MTLBindingTypeSampler:
                target = &m_samplers;
                break;
            default:
                return;
        }
        StageSlots &slots = (*target)[name.UTF8String];
        if (isVertex) {
            slots.vertex = static_cast<int>(index);
        } else {
            slots.fragment = static_cast<int>(index);
        }
    };

#    if defined(PLATFORM_IOS)
    if (@available(iOS 16.0, *)) {
#    endif
        for (id<MTLBinding> binding in reflection.vertexBindings) {
            record(binding.type, binding.index, binding.name, true);
        }
        for (id<MTLBinding> binding in reflection.fragmentBindings) {
            record(binding.type, binding.index, binding.name, false);
        }
#    if defined(PLATFORM_IOS)
    } else {
        // iOS < 16: the old reflection API (MTLArgument); the enum values match MTLBindingType.
#        pragma clang diagnostic push
#        pragma clang diagnostic ignored "-Wdeprecated-declarations"
        for (MTLArgument *argument in reflection.vertexArguments) {
            record(static_cast<MTLBindingType>(argument.type), argument.index, argument.name, true);
        }
        for (MTLArgument *argument in reflection.fragmentArguments) {
            record(static_cast<MTLBindingType>(argument.type), argument.index, argument.name, false);
        }
#        pragma clang diagnostic pop
    }
#    endif
}

void *MetalShader::getPipeline(
    void *devicePtr,
    const PipelineKey &key,
    const VertexBufferLayoutData &layout,
    const VertexBufferLayoutData *instanceLayout
) {
    auto it = m_pipelines.find(key);
    if (it != m_pipelines.end()) { return it->second; }
    if (!isValid()) { return nullptr; }
    id<MTLDevice> device = (id<MTLDevice>)devicePtr;

    MTLVertexDescriptor *vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
    uint32_t offset = 0;
    for (uint32_t i = 0; i < layout.m_elementsCount; i++) {
        const VertexBufferElement &element = layout.m_elements[i];
        vertexDescriptor.attributes[i].format = vertexFormat(element);
        vertexDescriptor.attributes[i].offset = offset;
        vertexDescriptor.attributes[i].bufferIndex = METAL_VERTEX_STREAM_BUFFER_INDEX;
        offset += element.count * VertexBufferElement::getSizeOfType(element.type);
    }
    vertexDescriptor.layouts[METAL_VERTEX_STREAM_BUFFER_INDEX].stride = layout.m_stride;
    vertexDescriptor.layouts[METAL_VERTEX_STREAM_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerVertex;
    if (instanceLayout) {
        // The instance stream attributes go after the vertex ones (the locations in the shader
        // continue the numbering).
        uint32_t instanceOffset = 0;
        for (uint32_t i = 0; i < instanceLayout->m_elementsCount; i++) {
            const VertexBufferElement &element = instanceLayout->m_elements[i];
            const uint32_t attribute = layout.m_elementsCount + i;
            vertexDescriptor.attributes[attribute].format = vertexFormat(element);
            vertexDescriptor.attributes[attribute].offset = instanceOffset;
            vertexDescriptor.attributes[attribute].bufferIndex = METAL_INSTANCE_STREAM_BUFFER_INDEX;
            instanceOffset += element.count * VertexBufferElement::getSizeOfType(element.type);
        }
        vertexDescriptor.layouts[METAL_INSTANCE_STREAM_BUFFER_INDEX].stride = instanceLayout->m_stride;
        vertexDescriptor.layouts[METAL_INSTANCE_STREAM_BUFFER_INDEX].stepFunction =
            MTLVertexStepFunctionPerInstance;
    }

    MTLRenderPipelineDescriptor *descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = (id<MTLFunction>)m_vertexFunction;
    descriptor.fragmentFunction = (id<MTLFunction>)m_fragmentFunction;
    descriptor.vertexDescriptor = vertexDescriptor;
    for (uint32_t i = 0; i < key.colorCount; i++) {
        descriptor.colorAttachments[i].pixelFormat = static_cast<MTLPixelFormat>(key.colorFormats[i]);
        if (!key.colorWrite) { descriptor.colorAttachments[i].writeMask = MTLColorWriteMaskNone; }
        // Integer attachments (id picking) are not blended — Metal validates this,
        // unlike D3D11, where the common blend for all targets was silently ignored.
        if (key.blend && !isIntegerPixelFormat(key.colorFormats[i])) {
            descriptor.colorAttachments[i].blendingEnabled = YES;
            descriptor.colorAttachments[i].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            descriptor.colorAttachments[i].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            descriptor.colorAttachments[i].rgbBlendOperation = MTLBlendOperationAdd;
            descriptor.colorAttachments[i].sourceAlphaBlendFactor = MTLBlendFactorOne;
            descriptor.colorAttachments[i].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            descriptor.colorAttachments[i].alphaBlendOperation = MTLBlendOperationAdd;
        }
    }
    if (key.depthFormat != MTLPixelFormatInvalid) {
        const MTLPixelFormat depthFormat = static_cast<MTLPixelFormat>(key.depthFormat);
        descriptor.depthAttachmentPixelFormat = depthFormat;
        // A stencil format exists only for the combined depth/stencil (Miren's DEPTH24STENCIL8 =
        // Depth32Float_Stencil8); Depth32Float (shadow map) has no stencil, and its format in
        // the PSO must be Invalid.
        const bool hasStencil = depthFormat == MTLPixelFormatDepth32Float_Stencil8;
        descriptor.stencilAttachmentPixelFormat = hasStencil ? depthFormat : MTLPixelFormatInvalid;
    }

    NSError *error = nil;
    MTLRenderPipelineReflection *reflection = nil;
    // BindingInfo == the former ArgumentInfo (the same value 1<<0), the constant is available
    // since macOS 13 / iOS 16; on iOS with deployment 14 the numerically equal old flag is used.
#    if defined(PLATFORM_IOS)
    const MTLPipelineOption reflectionOptions =
        MTLPipelineOptionBufferTypeInfo | static_cast<MTLPipelineOption>(1 << 0);
#    else
    const MTLPipelineOption reflectionOptions = MTLPipelineOptionBindingInfo | MTLPipelineOptionBufferTypeInfo;
#    endif
    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:descriptor
                                                                               options:reflectionOptions
                                                                            reflection:&reflection
                                                                                 error:&error];
    [descriptor release];
    if (pipeline == nil) {
        const char *message = error ? error.localizedDescription.UTF8String : "no error output";
        LOG_ERROR("METAL: Failed to create pipeline state:\n%s", message);
        LOG_ERROR_EDITOR("METAL: Failed to create pipeline state:\n%s", message);
        m_pipelines[key] = nullptr;
        return nullptr;
    }
    captureReflection((void *)reflection);
    m_pipelines[key] = (void *)pipeline;
    return (void *)pipeline;
}

const MetalShader::StageSlots *MetalShader::findSlots(
    const std::unordered_map<std::string, StageSlots> &map, const char *name, const char *kind, bool logMissing
) {
    auto it = map.find(name);
    if (it != map.end()) { return &it->second; }
    // dxc names the cbuffer type "type.NAME": if the variable name did not survive to MSL,
    // spirv-cross uses the sanitized type name (as in the GLSL path).
    const std::string typeName = std::string("type_") + name;
    it = map.find(typeName);
    if (it != map.end()) { return &it->second; }
    if (!logMissing) { return nullptr; }
    if (std::find(m_missingInputLog.begin(), m_missingInputLog.end(), name) == m_missingInputLog.end()) {
        m_missingInputLog.push_back(name);
        std::string available;
        for (const auto &pair : map) {
            if (!available.empty()) { available += ", "; }
            available += pair.first;
        }
        LOG_ERROR("SHADER %s %s NOT FOUND (available: %s)", kind, name, available.c_str());
    }
    return nullptr;
}

const MetalShader::StageSlots *MetalShader::findBuffer(const char *name) {
    return findSlots(m_buffers, name, "UBO", true);
}

const MetalShader::StageSlots *MetalShader::findTexture(const char *name) {
    return findSlots(m_textures, name, "TEXTURE", true);
}

const MetalShader::StageSlots *MetalShader::findSamplerForTexture(const char *textureName) {
    // The engine's HLSL convention pairs "name" textures with "nameSampler" states.
    const std::string samplerName = std::string(textureName) + "Sampler";
    const StageSlots *slots = findSlots(m_samplers, samplerName.c_str(), "SAMPLER", false);
    if (slots) { return slots; }
    // A SamplerState shared by several textures: the shader's only sampler.
    if (m_samplers.size() == 1) { return &m_samplers.begin()->second; }
    return nullptr;
}

} // namespace Miren

#endif
