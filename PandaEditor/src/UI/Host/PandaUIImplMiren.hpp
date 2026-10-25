#pragma once

#include <Miren/Miren.hpp>
#include <PandaUI/PandaUI.hpp>

#include <cstdint>
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
    // GPU phase of texture loading from ALREADY decoded data: the decode (load2DTexture) can be done
    // on a worker, while this part only on the main thread (Miren is single-threaded).
    // createTextureFromStaticResource = decode + it.
    PandaUI::TextureHandle createTextureFromDecoded(Miren::TextureCreate create);
    // Shows an ALREADY existing Miren texture (e.g. the scene FBO color attachment) without a copy:
    // registers it under a PandaUI id for ImageView. The texture does NOT change ownership —
    // shutdown does not delete it (the owner is the engine). Seam #1 of the migration: the live 3D
    // scene frame in the viewport panel. Call on FBO creation/resize.
    PandaUI::TextureHandle wrapNativeTexture(Miren::TextureHandle native, PandaUI::Size size);
    // Puts new pixels behind an existing handle: every view holding the handle draws them from the
    // next frame (icon plugins swap the editor icons live). The old native texture is deleted
    // unless it is a wrapped foreign one. False — the handle is unknown or the pixels are empty.
    bool replaceTexture(PandaUI::TextureHandle handle, Miren::TextureCreate create);
    void render(const PandaUI::DrawList &drawList, uint16_t viewId);

private:
    struct Data;
    std::unique_ptr<Data> m_data;
};
