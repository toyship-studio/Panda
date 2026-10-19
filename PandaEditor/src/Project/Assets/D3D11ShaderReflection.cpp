#include "D3D11ShaderReflection.hpp"

#ifdef PLATFORM_WINDOWS

#    include <Foundation/Logger.hpp>

#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    ifndef UNICODE
#        define UNICODE
#    endif
#    include <windows.h>
#    include <d3d11shader.h>
#    include <d3dcompiler.h>

#    include <fstream>
#    include <sstream>

namespace Panda {

namespace {

    std::string variableTypeName(ID3D11ShaderReflectionType *type) {
        D3D11_SHADER_TYPE_DESC typeDesc = {};
        if (!type || FAILED(type->GetDesc(&typeDesc))) { return {}; }
        if (typeDesc.Class == D3D_SVC_SCALAR && typeDesc.Type == D3D_SVT_FLOAT) { return "float"; }
        if (typeDesc.Class == D3D_SVC_SCALAR && typeDesc.Type == D3D_SVT_UINT) { return "uint"; }
        if (typeDesc.Class == D3D_SVC_VECTOR && typeDesc.Type == D3D_SVT_FLOAT && typeDesc.Columns == 4) {
            return "vec4";
        }
        return {};
    }

} // namespace

ShaderSpirvReflectionData reflectD3D11FragmentShader(const path_t &fragmentSourcePath) {
    ShaderSpirvReflectionData reflection;

    std::ifstream file(fragmentSourcePath, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR_EDITOR("Error reading shader source %s", fragmentSourcePath.string().c_str());
        return reflection;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string source = buffer.str();

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    ID3DBlob *bytecode = nullptr;
    ID3DBlob *errors = nullptr;
    if (FAILED(D3DCompile(
            source.data(), source.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", flags, 0, &bytecode, &errors
        ))) {
        const char *message = errors ? static_cast<const char *>(errors->GetBufferPointer()) : "no error output";
        LOG_ERROR_EDITOR("SHADER COMPILATION ERROR (reflection):\n%s", message);
        if (errors) { errors->Release(); }
        if (bytecode) { bytecode->Release(); }
        return reflection;
    }
    if (errors) { errors->Release(); }

    ID3D11ShaderReflection *shaderReflection = nullptr;
    if (FAILED(D3DReflect(
            bytecode->GetBufferPointer(),
            bytecode->GetBufferSize(),
            IID_ID3D11ShaderReflection,
            reinterpret_cast<void **>(&shaderReflection)
        ))) {
        bytecode->Release();
        return reflection;
    }

    D3D11_SHADER_DESC shaderDesc = {};
    shaderReflection->GetDesc(&shaderDesc);

    for (UINT i = 0; i < shaderDesc.BoundResources; i++) {
        D3D11_SHADER_INPUT_BIND_DESC bindDesc = {};
        shaderReflection->GetResourceBindingDesc(i, &bindDesc);
        if (bindDesc.Type == D3D_SIT_TEXTURE) {
            ShaderImageInfo image;
            image.type = "texture2D";
            image.name = bindDesc.Name;
            image.set = 0;
            image.binding = static_cast<int>(bindDesc.BindPoint);
            reflection.separate_images.push_back(image);
        } else if (bindDesc.Type == D3D_SIT_SAMPLER) {
            ShaderSamplerInfo sampler;
            sampler.type = "sampler";
            sampler.name = bindDesc.Name;
            sampler.set = 0;
            sampler.binding = static_cast<int>(bindDesc.BindPoint);
            reflection.separate_samplers.push_back(sampler);
        } else if (bindDesc.Type == D3D_SIT_CBUFFER) {
            ID3D11ShaderReflectionConstantBuffer *constantBuffer =
                shaderReflection->GetConstantBufferByName(bindDesc.Name);
            D3D11_SHADER_BUFFER_DESC bufferDesc = {};
            if (!constantBuffer || FAILED(constantBuffer->GetDesc(&bufferDesc))) { continue; }

            // Mirror the spirv-cross --reflect layout the mapper expects:
            // ubos[i].name = "type.<NAME>", ubos[i].type is a key into `types`.
            const std::string typeKey = std::string("_d3d11_") + bufferDesc.Name;
            ShaderUniformBufferInfo ubo;
            ubo.type = typeKey;
            ubo.name = std::string("type.") + bufferDesc.Name;
            ubo.block_size = static_cast<int>(bufferDesc.Size);
            ubo.set = 0;
            ubo.binding = static_cast<int>(bindDesc.BindPoint);
            reflection.ubos.push_back(ubo);

            ShaderTypeInfo typeInfo;
            typeInfo.name = ubo.name;
            for (UINT v = 0; v < bufferDesc.Variables; v++) {
                ID3D11ShaderReflectionVariable *variable = constantBuffer->GetVariableByIndex(v);
                D3D11_SHADER_VARIABLE_DESC variableDesc = {};
                if (!variable || FAILED(variable->GetDesc(&variableDesc))) { continue; }
                ShaderTypeMemberInfo member;
                member.name = variableDesc.Name;
                member.type = variableTypeName(variable->GetType());
                member.offset = static_cast<int>(variableDesc.StartOffset);
                typeInfo.members.push_back(member);
            }
            reflection.types[typeKey] = typeInfo;
        }
    }

    shaderReflection->Release();
    bytecode->Release();
    return reflection;
}

} // namespace Panda

#endif
