#pragma once

#include <Foundation/PlatformDetection.hpp>

#include <cstdint>

#ifdef PLATFORM_WINDOWS

#    include "Miren/Base.hpp"
#    include "Miren/VertexBufferLayoutData.hpp"

#    include <string>
#    include <unordered_map>
#    include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11Buffer;
struct ID3D11InputLayout;
struct ID3D11ShaderResourceView;
struct ID3D11SamplerState;
struct ID3D10Blob;

namespace Miren {

// One shader program: VS + PS compiled from HLSL source at creation time.
// Reflection maps cbuffer/texture/sampler names to bind slots per stage, so the
// engine-side setUbo("MATERIAL_FIELDS", ...) / setTexture("albedoTexture", ...)
// calls resolve directly (HLSL keeps the original names, unlike spirv-cross).
class D3D11Shader final {
public:
    D3D11Shader();

    void create(ID3D11Device *device, ProgramCreate create);
    void terminate();
    bool isValid() const;

    void bind(ID3D11DeviceContext *deviceContext);
    void setUbo(ID3D11Device *device, ID3D11DeviceContext *deviceContext, const char *name, const void *data, size_t size);
    void setTexture(
        ID3D11DeviceContext *deviceContext,
        const char *name,
        ID3D11ShaderResourceView *view,
        ID3D11SamplerState *sampler
    );
    // Input layouts depend on the VS signature; one is cached per (vertex layout, instance
    // layout) pair. Instance elements map to signature parameters after the vertex ones and
    // are fetched from input slot 1 with per-instance step.
    ID3D11InputLayout *getInputLayout(
        ID3D11Device *device,
        uint16_t layoutId,
        const VertexBufferLayoutData &layout,
        uint16_t instanceLayoutId = MIREN_INVALID_HANDLE,
        const VertexBufferLayoutData *instanceLayout = nullptr
    );

private:
    struct StageSlots {
        int vertex = -1;
        int pixel = -1;
    };

    const StageSlots *findSlots(const std::unordered_map<std::string, StageSlots> &map, const char *name, const char *kind);

    ID3D11VertexShader *m_vertexShader;
    ID3D11PixelShader *m_pixelShader;
    ID3D10Blob *m_vertexBytecode; // kept for CreateInputLayout
    std::unordered_map<std::string, StageSlots> m_constantBuffers;
    std::unordered_map<std::string, StageSlots> m_textures;
    std::unordered_map<std::string, StageSlots> m_samplers;
    std::unordered_map<std::string, ID3D11Buffer *> m_uboBuffers;
    std::unordered_map<uint32_t, ID3D11InputLayout *> m_inputLayouts; // (layoutId << 16) | instanceLayoutId
    std::vector<std::string> m_missingInputLog; // names already reported, log once
};

} // namespace Miren

#endif
