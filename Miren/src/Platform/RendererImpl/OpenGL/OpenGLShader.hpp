//
// Created by Admin on 11.02.2022.
//

#pragma once

#include "Miren/VertexBufferLayoutData.hpp"
#include "Miren/Base.hpp"

#include <string>
#include <unordered_map>

namespace Miren {

class OpenGLShader {
public:
    OpenGLShader();
    void create(ProgramCreate create);
    void terminate();
    void bind();
    void unbind();
    void setUbo(const char *name, void *value, size_t size);
    // target — GL_TEXTURE_2D / GL_TEXTURE_CUBE_MAP / GL_TEXTURE_3D of the texture
    // (OpenGLTexture::getTarget).
    void setTexture(const char *name, uint32_t textureId, uint32_t target);
    // baseLocation — the first attribute location of this stream (instance attributes go after
    // the vertex ones); divisor 0 = per-vertex, 1 = per-instance (glVertexAttribDivisor).
    void bindAttributes(
        VertexBufferLayoutData &layout, intptr_t baseVertex, uint32_t baseLocation = 0, uint32_t divisor = 0
    );
    bool isValid();

private:
    static std::string getOpenGlUboName(const char *name);
    static std::string getOpenGlTextureName(const char *name);
    uint32_t m_id;
    std::unordered_map<std::string, uint32_t> m_uboIndices;
    std::unordered_map<std::string, uint32_t> m_uboBindings;
    std::unordered_map<std::string, uint32_t> m_textureBindings;
    std::unordered_map<std::string, int> m_uniformLocationCache;
    std::unordered_map<std::string, uint32_t> m_uboBuffers;
    static bool checkCompileErrors(uint32_t shader, const std::string &type);
    uint32_t getUboIndex(const char *name);
    uint32_t getUboBinding(const char *name);
    int getTextureBinding(const char *name);
    int getTextureLocation(const char *name);
};

} // namespace Miren
