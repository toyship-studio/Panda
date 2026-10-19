//
// Created by Admin on 11.02.2022.
//

#pragma once

#include "Miren/Base.hpp"

#include <cstdint>

namespace Miren {

class OpenGLTexture {
public:
    OpenGLTexture();
    void create(TextureCreate &create);
    void update(Foundation::Memory mem);
    // A 2D region (z = 0, depth = 1) or a box of a 3D texture.
    void updateRegion(
        uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height, uint16_t depth, Foundation::Memory mem
    );
    void resize(uint32_t width, uint32_t height);
    // Rebuilds the mip chain from mip 0 (MAX_LEVEL is set at creation from numMips).
    void generateMipmaps();

    uint16_t getWidth() {
        return m_create.m_width;
    }

    uint16_t getHeight() {
        return m_create.m_height;
    }
    void terminate();

    inline uint32_t getId() {
        return m_id;
    }

    // GL_TEXTURE_2D / GL_TEXTURE_CUBE_MAP / GL_TEXTURE_3D — the binding target when sampling.
    inline uint32_t getTarget() {
        return m_target;
    }

    inline TextureFormat getFormat() {
        return m_create.m_format;
    }

    void readPixels(void *data);

private:
    TextureCreate m_create;
    uint32_t m_id;
    uint32_t m_target;
    uint32_t m_format;
    uint32_t m_type;
};

} // namespace Miren
