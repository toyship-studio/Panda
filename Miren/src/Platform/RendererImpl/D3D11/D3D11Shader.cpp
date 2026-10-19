#include "D3D11Shader.hpp"

#ifdef PLATFORM_WINDOWS

#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    ifndef UNICODE
#        define UNICODE
#    endif
#    include <windows.h>
#    include <d3d11.h>
#    include <d3dcompiler.h>

#    include <algorithm>

namespace Miren {

namespace {

    ID3DBlob *compileStage(const void *source, size_t sourceSize, const char *target) {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#    ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#    else
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#    endif
        ID3DBlob *bytecode = nullptr;
        ID3DBlob *errors = nullptr;
        const HRESULT result =
            D3DCompile(source, sourceSize, nullptr, nullptr, nullptr, "main", target, flags, 0, &bytecode, &errors);
        if (FAILED(result)) {
            const char *message = errors ? static_cast<const char *>(errors->GetBufferPointer()) : "no error output";
            LOG_ERROR("SHADER COMPILATION ERROR (%s):\n%s", target, message);
            LOG_ERROR_EDITOR("SHADER COMPILATION ERROR (%s):\n%s", target, message);
            if (errors) { errors->Release(); }
            if (bytecode) { bytecode->Release(); }
            return nullptr;
        }
        if (errors) { errors->Release(); }
        return bytecode;
    }

    DXGI_FORMAT elementFormat(const VertexBufferElement &element) {
        switch (element.type) {
            case BufferElementType::Float:
                switch (element.count) {
                    case 1:
                        return DXGI_FORMAT_R32_FLOAT;
                    case 2:
                        return DXGI_FORMAT_R32G32_FLOAT;
                    case 3:
                        return DXGI_FORMAT_R32G32B32_FLOAT;
                    case 4:
                        return DXGI_FORMAT_R32G32B32A32_FLOAT;
                }
                break;
            case BufferElementType::UnsignedInt:
                switch (element.count) {
                    case 1:
                        return DXGI_FORMAT_R32_UINT;
                    case 2:
                        return DXGI_FORMAT_R32G32_UINT;
                    case 3:
                        return DXGI_FORMAT_R32G32B32_UINT;
                    case 4:
                        return DXGI_FORMAT_R32G32B32A32_UINT;
                }
                break;
            case BufferElementType::Int:
                switch (element.count) {
                    case 1:
                        return DXGI_FORMAT_R32_SINT;
                    case 2:
                        return DXGI_FORMAT_R32G32_SINT;
                    case 3:
                        return DXGI_FORMAT_R32G32B32_SINT;
                    case 4:
                        return DXGI_FORMAT_R32G32B32A32_SINT;
                }
                break;
            case BufferElementType::UnsignedByte:
                switch (element.count) {
                    case 4:
                        return element.normalized ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UINT;
                    case 1:
                        return element.normalized ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8_UINT;
                    case 2:
                        return element.normalized ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8G8_UINT;
                }
                break;
            default:
                break;
        }
        PND_ASSERT(false, "Unsupported vertex element format for D3D11");
        return DXGI_FORMAT_UNKNOWN;
    }

} // namespace

D3D11Shader::D3D11Shader()
    : m_vertexShader(nullptr)
    , m_pixelShader(nullptr)
    , m_vertexBytecode(nullptr) {}

void D3D11Shader::create(ID3D11Device *device, ProgramCreate create) {
    PND_ASSERT(m_vertexShader == nullptr, "PROGRAM ALREADY CREATED");
    ID3DBlob *vertexBytecode = compileStage(create.m_vertexBinary.data, create.m_vertexBinarySize, "vs_5_0");
    create.m_vertexBinary.release();
    ID3DBlob *pixelBytecode = compileStage(create.m_fragmentBinary.data, create.m_fragmentBinarySize, "ps_5_0");
    create.m_fragmentBinary.release();
    if (!vertexBytecode || !pixelBytecode) {
        if (vertexBytecode) { vertexBytecode->Release(); }
        if (pixelBytecode) { pixelBytecode->Release(); }
        return;
    }

    ID3D11VertexShader *vertexShader = nullptr;
    ID3D11PixelShader *pixelShader = nullptr;
    if (FAILED(device->CreateVertexShader(
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(), nullptr, &vertexShader
        )) ||
        FAILED(device->CreatePixelShader(
            pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(), nullptr, &pixelShader
        ))) {
        LOG_ERROR("D3D11: Failed to create shader objects");
        if (vertexShader) { vertexShader->Release(); }
        vertexBytecode->Release();
        pixelBytecode->Release();
        return;
    }
    m_vertexShader = vertexShader;
    m_pixelShader = pixelShader;
    m_vertexBytecode = vertexBytecode;

    // Reflection: resource name -> bind slot, separately per stage.
    ID3DBlob *stages[2] = {vertexBytecode, pixelBytecode};
    for (int stage = 0; stage < 2; stage++) {
        ID3D11ShaderReflection *reflection = nullptr;
        if (FAILED(D3DReflect(
                stages[stage]->GetBufferPointer(),
                stages[stage]->GetBufferSize(),
                IID_ID3D11ShaderReflection,
                reinterpret_cast<void **>(&reflection)
            ))) {
            continue;
        }
        D3D11_SHADER_DESC shaderDesc = {};
        reflection->GetDesc(&shaderDesc);
        for (UINT i = 0; i < shaderDesc.BoundResources; i++) {
            D3D11_SHADER_INPUT_BIND_DESC bindDesc = {};
            reflection->GetResourceBindingDesc(i, &bindDesc);
            std::unordered_map<std::string, StageSlots> *target = nullptr;
            switch (bindDesc.Type) {
                case D3D_SIT_CBUFFER:
                    target = &m_constantBuffers;
                    break;
                case D3D_SIT_TEXTURE:
                    target = &m_textures;
                    break;
                case D3D_SIT_SAMPLER:
                    target = &m_samplers;
                    break;
                default:
                    break;
            }
            if (!target) { continue; }
            StageSlots &slots = (*target)[bindDesc.Name];
            if (stage == 0) {
                slots.vertex = static_cast<int>(bindDesc.BindPoint);
            } else {
                slots.pixel = static_cast<int>(bindDesc.BindPoint);
            }
        }
        reflection->Release();
    }
    pixelBytecode->Release();
}

void D3D11Shader::terminate() {
    for (auto &pair : m_inputLayouts) {
        if (pair.second) { pair.second->Release(); }
    }
    m_inputLayouts.clear();
    for (auto &pair : m_uboBuffers) {
        if (pair.second) { pair.second->Release(); }
    }
    m_uboBuffers.clear();
    m_constantBuffers.clear();
    m_textures.clear();
    m_samplers.clear();
    m_missingInputLog.clear();
    if (m_vertexBytecode) {
        m_vertexBytecode->Release();
        m_vertexBytecode = nullptr;
    }
    if (m_pixelShader) {
        m_pixelShader->Release();
        m_pixelShader = nullptr;
    }
    if (m_vertexShader) {
        m_vertexShader->Release();
        m_vertexShader = nullptr;
    }
}

bool D3D11Shader::isValid() const {
    return m_vertexShader != nullptr;
}

void D3D11Shader::bind(ID3D11DeviceContext *deviceContext) {
    deviceContext->VSSetShader(m_vertexShader, nullptr, 0);
    deviceContext->PSSetShader(m_pixelShader, nullptr, 0);
}

const D3D11Shader::StageSlots *D3D11Shader::findSlots(
    const std::unordered_map<std::string, StageSlots> &map, const char *name, const char *kind
) {
    auto it = map.find(name);
    if (it != map.end()) { return &it->second; }
    if (std::find(m_missingInputLog.begin(), m_missingInputLog.end(), name) == m_missingInputLog.end()) {
        m_missingInputLog.push_back(name);
        LOG_ERROR("SHADER %s %s NOT FOUND", kind, name);
    }
    return nullptr;
}

void D3D11Shader::setUbo(
    ID3D11Device *device, ID3D11DeviceContext *deviceContext, const char *name, const void *data, size_t size
) {
    const StageSlots *slots = findSlots(m_constantBuffers, name, "UBO");
    if (!slots) { return; }

    ID3D11Buffer *&buffer = m_uboBuffers[name];
    if (buffer) {
        D3D11_BUFFER_DESC existingDesc = {};
        buffer->GetDesc(&existingDesc);
        if (existingDesc.ByteWidth < size) {
            buffer->Release();
            buffer = nullptr;
        }
    }
    if (!buffer) {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = static_cast<UINT>((size + 15) & ~static_cast<size_t>(15));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&desc, nullptr, &buffer))) {
            LOG_ERROR("D3D11: Failed to create constant buffer %s", name);
            buffer = nullptr;
            return;
        }
    }

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(deviceContext->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, data, size);
        deviceContext->Unmap(buffer, 0);
    }
    if (slots->vertex >= 0) { deviceContext->VSSetConstantBuffers(static_cast<UINT>(slots->vertex), 1, &buffer); }
    if (slots->pixel >= 0) { deviceContext->PSSetConstantBuffers(static_cast<UINT>(slots->pixel), 1, &buffer); }
}

void D3D11Shader::setTexture(
    ID3D11DeviceContext *deviceContext, const char *name, ID3D11ShaderResourceView *view, ID3D11SamplerState *sampler
) {
    const StageSlots *textureSlots = findSlots(m_textures, name, "TEXTURE");
    if (!textureSlots) { return; }
    if (textureSlots->vertex >= 0) {
        deviceContext->VSSetShaderResources(static_cast<UINT>(textureSlots->vertex), 1, &view);
    }
    if (textureSlots->pixel >= 0) {
        deviceContext->PSSetShaderResources(static_cast<UINT>(textureSlots->pixel), 1, &view);
    }

    // The engine's HLSL convention pairs "name" textures with "nameSampler" states.
    const std::string samplerName = std::string(name) + "Sampler";
    auto samplerIt = m_samplers.find(samplerName);
    if (samplerIt == m_samplers.end()) { return; }
    if (samplerIt->second.vertex >= 0) {
        deviceContext->VSSetSamplers(static_cast<UINT>(samplerIt->second.vertex), 1, &sampler);
    }
    if (samplerIt->second.pixel >= 0) {
        deviceContext->PSSetSamplers(static_cast<UINT>(samplerIt->second.pixel), 1, &sampler);
    }
}

ID3D11InputLayout *D3D11Shader::getInputLayout(
    ID3D11Device *device,
    uint16_t layoutId,
    const VertexBufferLayoutData &layout,
    uint16_t instanceLayoutId,
    const VertexBufferLayoutData *instanceLayout
) {
    const uint32_t cacheKey = (static_cast<uint32_t>(layoutId) << 16) | instanceLayoutId;
    auto it = m_inputLayouts.find(cacheKey);
    if (it != m_inputLayouts.end()) { return it->second; }
    if (!m_vertexBytecode) { return nullptr; }

    ID3D11ShaderReflection *reflection = nullptr;
    if (FAILED(D3DReflect(
            m_vertexBytecode->GetBufferPointer(),
            m_vertexBytecode->GetBufferSize(),
            IID_ID3D11ShaderReflection,
            reinterpret_cast<void **>(&reflection)
        ))) {
        return nullptr;
    }
    D3D11_SHADER_DESC shaderDesc = {};
    reflection->GetDesc(&shaderDesc);

    // Vertex layout elements map to the VS signature by order (the GL backend
    // matched them by attribute location the same way). Layout elements beyond
    // the signature are simply not fetched.
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    UINT offset = 0;
    for (int i = 0; i < layout.m_elementsCount; i++) {
        const VertexBufferElement &element = layout.m_elements[i];
        if (static_cast<UINT>(i) < shaderDesc.InputParameters) {
            D3D11_SIGNATURE_PARAMETER_DESC parameterDesc = {};
            reflection->GetInputParameterDesc(static_cast<UINT>(i), &parameterDesc);
            D3D11_INPUT_ELEMENT_DESC desc = {};
            desc.SemanticName = parameterDesc.SemanticName;
            desc.SemanticIndex = parameterDesc.SemanticIndex;
            desc.Format = elementFormat(element);
            desc.InputSlot = 0;
            desc.AlignedByteOffset = offset;
            desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            elements.push_back(desc);
        }
        offset += element.count * VertexBufferElement::getSizeOfType(element.type);
    }
    if (instanceLayout) {
        // Instance elements continue the VS signature after the vertex ones; fetched
        // from slot 1, stepping once per instance.
        UINT instanceOffset = 0;
        for (int i = 0; i < instanceLayout->m_elementsCount; i++) {
            const VertexBufferElement &element = instanceLayout->m_elements[i];
            const UINT parameter = static_cast<UINT>(layout.m_elementsCount + i);
            if (parameter < shaderDesc.InputParameters) {
                D3D11_SIGNATURE_PARAMETER_DESC parameterDesc = {};
                reflection->GetInputParameterDesc(parameter, &parameterDesc);
                D3D11_INPUT_ELEMENT_DESC desc = {};
                desc.SemanticName = parameterDesc.SemanticName;
                desc.SemanticIndex = parameterDesc.SemanticIndex;
                desc.Format = elementFormat(element);
                desc.InputSlot = 1;
                desc.AlignedByteOffset = instanceOffset;
                desc.InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
                desc.InstanceDataStepRate = 1;
                elements.push_back(desc);
            }
            instanceOffset += element.count * VertexBufferElement::getSizeOfType(element.type);
        }
    }

    ID3D11InputLayout *inputLayout = nullptr;
    if (elements.empty() ||
        FAILED(device->CreateInputLayout(
            elements.data(),
            static_cast<UINT>(elements.size()),
            m_vertexBytecode->GetBufferPointer(),
            m_vertexBytecode->GetBufferSize(),
            &inputLayout
        ))) {
        LOG_ERROR(
            "D3D11: Failed to create input layout (layout elements: %d, shader inputs: %u)",
            layout.m_elementsCount,
            shaderDesc.InputParameters
        );
        for (const D3D11_INPUT_ELEMENT_DESC &element : elements) {
            LOG_ERROR(
                "D3D11:   element %s%u format %d offset %u",
                element.SemanticName,
                element.SemanticIndex,
                (int)element.Format,
                element.AlignedByteOffset
            );
        }
        inputLayout = nullptr;
    }
    reflection->Release();
    m_inputLayouts[cacheKey] = inputLayout;
    return inputLayout;
}

} // namespace Miren

#endif
