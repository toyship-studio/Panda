#pragma once

#include <Miren/Miren.hpp>
#include <PandaUI/Core/TextureHandle.hpp>
#include <PandaUI/Rendering/DrawList.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace Panda {

// PandaUI drawn with Miren: the two programs of the UI (textured triangles and the analytic
// rounded rectangles), the textures behind the PandaUI handles and the submit of a draw list
// into a view. The one implementation for every host of a PandaUI context — the game UI of the
// script bundle (GameUI) and the UI of an application built on the engine (the editor: EditorUI).
// A host owns an instance: its textures live and die with it.
class PandaUIMirenRenderer final {
public:
    PandaUIMirenRenderer();
    ~PandaUIMirenRenderer();

    void init();
    void shutdown();

    PandaUI::TextureHandle createTextureRGBA8(const uint8_t *pixels, uint16_t width, uint16_t height);
    // From decoded pixels: the decode (AssetImporterBase::load2DTexture) may run on a worker,
    // this part runs on the main thread — Miren is called from one thread.
    PandaUI::TextureHandle createTexture(Miren::TextureCreate create);
    // A picture of the application resources ("ui/start/Logo.png"): decode + createTexture.
    PandaUI::TextureHandle createTextureFromStaticResource(const std::filesystem::path &path);
    // A texture that exists already (the color target a viewport renders into) under a PandaUI
    // handle, without a copy. It does not change hands: destroyTexture and shutdown forget it and
    // leave it alive.
    PandaUI::TextureHandle wrapNativeTexture(Miren::TextureHandle native, PandaUI::Size size);
    // New pixels behind an existing handle: every view holding the handle draws them from the next
    // frame (the icon plugins of the editor swap the icons live). false — the handle is unknown
    // or the pixels are empty.
    bool replaceTexture(PandaUI::TextureHandle handle, Miren::TextureCreate create);
    // The views holding the handle draw white from the next frame. An unknown handle is ignored.
    void destroyTexture(PandaUI::TextureHandle handle);

    void render(const PandaUI::DrawListView &drawList, Miren::ViewId view);
    void render(const PandaUI::DrawList &drawList, Miren::ViewId view) {
        render(drawList.view(), view);
    }

private:
    struct Data;
    std::unique_ptr<Data> m_data;
};

} // namespace Panda
