#pragma once

#include "Panda/Base/Base.hpp"
#include "Base/Asset.hpp"
#include "Base/Path.hpp"
#include "Base/AssetImporterBase.hpp"

#include <Miren/Miren.hpp>
#include <cstdlib>

namespace Panda {

class TextureAsset : public Asset {
public:
    TextureAsset()
        : m_mirenHandle(MIREN_INVALID_HANDLE)
        , Asset(AssetType::TEXTURE) {}

    explicit TextureAsset(const path_t &path)
        : Asset(AssetType::TEXTURE) {
        auto textureCreate = AssetImporterBase::load2DTexture(path);
        textureCreate.m_wrap = Miren::CLAMP;
        textureCreate.m_magFiltering = Miren::NEAREST;
        textureCreate.m_minFiltering = Miren::NEAREST_MIPMAP_LINEAR;
        textureCreate.m_numMips = 3;
        create(textureCreate);
    }

    TextureAsset(Miren::TextureCreate textureCreate)
        : Asset(AssetType::TEXTURE) {
        create(textureCreate);
    }

    TextureAsset(TextureAsset &&other) {
        m_mirenHandle = other.m_mirenHandle;
        m_type = other.m_type;
        m_size = other.m_size;
        m_format = other.m_format;
        other.m_mirenHandle = MIREN_INVALID_HANDLE;
    }

    ~TextureAsset() {
        if (m_mirenHandle.isValid()) { Miren::deleteTexture(m_mirenHandle); }
    }

    // Recreating the GPU texture UNDER THE SAME asset (the id and all AssetRefs to it stay
    // valid): system textures (splatmap on resize, grain mosaics on a spec change)
    // change size/content without changing identity.
    void create(const Miren::TextureCreate &create) {
        if (m_mirenHandle.isValid()) { Miren::deleteTexture(m_mirenHandle); }
        m_mirenHandle = Miren::createTexture(create);
        m_size = Size(create.m_width, create.m_height);
        m_format = create.m_format;
    }
    // Upload of mip 0 as a whole (the memory is handed over to Miren).
    void update(Foundation::Memory pixels) {
        Miren::updateTexture(m_mirenHandle, pixels);
    }
    // A rectangle of mip 0 — tightly packed width×height texels (brushes).
    void updateRegion(uint16_t x, uint16_t y, uint16_t width, uint16_t height, Foundation::Memory pixels) {
        Miren::updateTextureRegion(m_mirenHandle, x, y, width, height, pixels);
    }

    Size getSize() const {
        return m_size;
    }

    inline Miren::TextureHandle getMirenHandle() {
        return m_mirenHandle;
    }

    // Pixel format of the GPU texture: consumers learn from it about the hardware sRGB decode
    // on sampling (RGBA8_SRGB → linear color); the editor's meta settings are already baked
    // into it.
    Miren::TextureFormat getFormat() const {
        return m_format;
    }

    bool operator==(const TextureAsset &o) {
        return o.m_mirenHandle.id == m_mirenHandle.id;
    }

private:
    Miren::TextureHandle m_mirenHandle;
    Size m_size;
    Miren::TextureFormat m_format = Miren::TextureFormat::RGBA8;
};

} // namespace Panda