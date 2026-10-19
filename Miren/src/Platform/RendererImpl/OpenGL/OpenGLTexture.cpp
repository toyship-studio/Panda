//
// Created by Admin on 11.02.2022.
//

#include "OpenGLTexture.hpp"
#include "Texture/TextureFormat.hpp"

#include "OpenGLBase.hpp"

namespace Miren {

OpenGLTexture::OpenGLTexture()
    : m_id(-1)
    , m_create()
    , m_format(0)
    , m_type(0) {}

void OpenGLTexture::create(TextureCreate &create) {
    PND_ASSERT(m_id == -1, "TEXTURE ALREADY CREATED");
    m_create = create;
    MIREN_LOG("CREATE TEXTURE, w: %d, h: %d", create.m_width, create.m_height);
    GL_CALL(glGenTextures(1, &m_id));

    const bool volume = create.is3D();
    GLenum target = volume ? GL_TEXTURE_3D : (create.m_isCubeMap ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D);
    m_target = target;
    GL_CALL(glBindTexture(target, m_id));
    GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));

    m_format = s_textureFormat[create.m_format].m_fmt;
    GLenum internalFormat = s_textureFormat[create.m_format].m_internalFmt;
    m_type = s_textureFormat[create.m_format].m_type;

    uint8_t *data = (uint8_t *)create.m_data.data;
    if (volume) {
        // Volume: width×height slices in a row, no mips.
        GL_CALL(glTexImage3D(
            GL_TEXTURE_3D, 0, m_format, create.m_width, create.m_height, create.m_depth, 0, internalFormat, m_type,
            data
        ));
    } else {
        GLenum imageTarget = m_create.m_isCubeMap ? GL_TEXTURE_CUBE_MAP_POSITIVE_X : GL_TEXTURE_2D;
        const uint16_t numSides = create.m_isCubeMap ? 6 : 1;
        size_t imageSize = create.bytesPerTexel() * create.m_width * create.m_height;
        for (uint16_t side = 0; side < numSides; ++side) {
            GL_CALL(glTexImage2D(
                imageTarget, 0, m_format, create.m_width, create.m_height, 0, internalFormat, m_type, data
            ));
            imageTarget++;
            data += imageSize;
        }
    }
    create.m_data.release();

    if (create.m_numMips > 0 && !volume) {
        GL_CALL(glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, create.m_numMips));
        GL_CALL(glGenerateMipmap(target));
    }

    switch (create.m_wrap) {
        case CLAMP:
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
            break;
        case REPEAT:
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_REPEAT));
            break;
        case CLAMP_TO_BORDER: {
#if defined(GL_CLAMP_TO_BORDER) && defined(GL_TEXTURE_BORDER_COLOR)
            // The border is white: the shadow map beyond the edge reads as "lit".
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER));
            const GLfloat white[4] = {1.f, 1.f, 1.f, 1.f};
            GL_CALL(glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, white));
#else
            // OpenGL ES 3.0 has no core border-color support.
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
#endif
            break;
        }
    }
    GL_CALL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, openGLFiltering(create.m_minFiltering)));
    GL_CALL(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, openGLFiltering(create.m_magFiltering)));
    // Comparison sampler (shadow map): sampler2DShadow from SampleCmp compares ref with the texel.
    if (create.m_compare == COMPARE_LESS_EQUAL) {
        GL_CALL(glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE));
        GL_CALL(glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL));
    }
}

void OpenGLTexture::update(Foundation::Memory mem) {
    PND_ASSERT(m_id != -1, "TEXTURE IS NOT CREATED");
    GL_CALL(glBindTexture(m_target, m_id));
    GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
    GLenum internalFormat = s_textureFormat[m_create.m_format].m_internalFmt;
    if (m_create.is3D()) {
        GL_CALL(glTexSubImage3D(
            m_target, 0, 0, 0, 0, m_create.m_width, m_create.m_height, m_create.m_depth, internalFormat, m_type,
            mem.data
        ));
    } else {
        GL_CALL(glTexSubImage2D(
            m_target, 0, 0, 0, m_create.m_width, m_create.m_height, internalFormat, m_type, mem.data
        ));
    }
    mem.release();
}

void OpenGLTexture::updateRegion(
    uint16_t x, uint16_t y, uint16_t z, uint16_t width, uint16_t height, uint16_t depth, Foundation::Memory mem
) {
    PND_ASSERT(m_id != -1, "TEXTURE IS NOT CREATED");
    GL_CALL(glBindTexture(m_target, m_id));
    GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1)); // tightly packed region data (row = width, slice = width×height)
    GLenum internalFormat = s_textureFormat[m_create.m_format].m_internalFmt;
    if (m_create.is3D()) {
        GL_CALL(glTexSubImage3D(m_target, 0, x, y, z, width, height, depth, internalFormat, m_type, mem.data));
    } else {
        GL_CALL(glTexSubImage2D(m_target, 0, x, y, width, height, internalFormat, m_type, mem.data));
    }
    mem.release();
}

void OpenGLTexture::generateMipmaps() {
    PND_ASSERT(m_id != -1, "TEXTURE IS NOT CREATED");
    if (m_create.m_numMips == 0) { return; }
    GL_CALL(glBindTexture(m_target, m_id));
    GL_CALL(glGenerateMipmap(m_target));
}

void OpenGLTexture::resize(uint32_t width, uint32_t height) {
    PND_ASSERT(m_id != -1, "TEXTURE IS NOT CREATED");
    if (m_create.is3D()) {
        LOG_ERROR("OPENGL: 3D texture resize is not supported");
        return;
    }
    terminate();
    m_create.m_width = width;
    m_create.m_height = height;
    create(m_create);
}

void OpenGLTexture::terminate() {
    PND_ASSERT(m_id != -1, "TEXTURE ALREADY DELETED");
    GL_CALL(glDeleteTextures(1, &m_id));
    m_id = -1;
}

void OpenGLTexture::readPixels(void *data) {
    GL_CALL(glBindTexture(m_target, m_id));
#ifdef PLATFORM_DESKTOP
    GL_CALL(glPixelStorei(GL_PACK_ALIGNMENT, 1));
    const GLenum format = s_textureFormat[m_create.m_format].m_internalFmt;
    GL_CALL(glGetTexImage(m_target, 0, format, m_type, data));
#else
    LOG_ERROR("READ TEXTURE NOT SUPPORTED IN GLES");
#endif
}

} // namespace Miren
