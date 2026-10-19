#pragma once

#include <Miren/Miren.hpp>
#include <PandaUI/PandaUI.hpp>

#include <filesystem>
#include <memory>

class PandaUIImplMiren final {
public:
    PandaUIImplMiren();
    ~PandaUIImplMiren();

    void init();
    void shutdown();
    PandaUI::TextureHandle createTextureRGBA8(const unsigned char *pixels, uint16_t width, uint16_t height);
    PandaUI::TextureHandle createTextureFromStaticResource(const std::filesystem::path &path);
    // Shows an ALREADY existing Miren texture (e.g. the color attachment of the scene FBO) without a copy:
    // registers it under a PandaUI id for ImageView. Ownership of the texture is NOT transferred — shutdown
    // does not delete it (the owner is the engine). Migration seam #1: the live frame of the 3D scene in
    // the viewport panel. Call it on FBO creation/resize.
    PandaUI::TextureHandle wrapNativeTexture(Miren::TextureHandle native, PandaUI::Size size);
    void render(const PandaUI::DrawList &drawList, uint16_t viewId);

private:
    struct Data;
    std::unique_ptr<Data> m_data;
};
