#pragma once

#include "UI/Host/PandaUIImplFern.hpp"
#include "UI/Host/PandaUIImplMiren.hpp"

#include <Miren/Miren.hpp>
#include <PandaUI/Rendering/Font.hpp>
#include <PandaUI/Runtime/Context.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Fern {
struct Event;
class Window;
} // namespace Fern

namespace PandaUI {
class View;
}

namespace Editor {
class IconLibrary;
}

namespace Panda {

// The C++-side PandaUI host for the editor chrome. Owns the Context, renders its draw-list through Miren and
// feeds it with Fern input. Tech debt: the render/input temporarily duplicate the engine's
// PandaUIImpl(runtime) — to be unified engine-side as a separate step.
class EditorUI final {
public:
    void initialize(Fern::Window *window, Miren::ViewId viewId);
    void shutdown();

    void setRootView(std::shared_ptr<PandaUI::View> view);
    void update(double deltaTime);        // runs layout/animation + submits the draw-list to Miren (every frame)
    bool handleEvent(Fern::Event *event); // Fern → Context

    void loadFonts();       // Inter Display + JetBrains Mono (Regular/Medium/Bold) into Context — like UITestLayer
    void loadEditorIcons(); // editor-icons + file-icons → the global Editor::IconLibrary (panel icons)
    // Folders of the packages' PNG icons (absolute paths; EditorExtensions::iconFolders) — into the same
    // library, on top of what is already loaded.
    void loadIconFolders(const std::vector<std::filesystem::path> &folders);
    // The icons of the installed icon plugins (data/icons/<token>.png, in install order: a later file
    // wins). A known token keeps its handle and gets the new pixels, so every icon already on screen
    // changes at once; a token no plugin overrides any more goes back to its own file; a token the
    // editor does not have becomes a new icon. Called at startup and after every change of the
    // installed plugins.
    void applyIconOverrides(const std::vector<std::filesystem::path> &files);

    PandaUI::Context &context() {
        return m_context;
    }

    // Wrap an engine FBO texture (scene color attachment) as a PandaUI texture for the viewport ImageView
    // (migration seam #1). The handle is stable across resizes, so wrap once. Ownership stays with the engine.
    PandaUI::TextureHandle wrapNativeTexture(Miren::TextureHandle native, PandaUI::Size size) {
        return m_renderer.wrapNativeTexture(native, size);
    }

    // A single image from the bundle resources (a path from Resources/, e.g. "ui/start/Logo.png") — for
    // one-off chrome images; icon sets are loaded by loadEditorIcons as a batch on the scheduler.
    PandaUI::TextureHandle loadImage(const std::filesystem::path &path) {
        return m_renderer.createTextureFromStaticResource(path);
    }

private:
    PandaUIImplMiren m_renderer;
    PandaUIImplFern m_platform;
    PandaUI::Context m_context;
    Miren::ViewId m_viewId = 0;
    bool m_initialized = false;

    // The font atlases are kept alive as long as the Context is alive.
    std::shared_ptr<PandaUI::FontAtlas> m_fontRegular, m_fontMedium, m_fontBold;
    std::shared_ptr<PandaUI::FontAtlas> m_monoRegular, m_monoMedium, m_monoBold;

    // All PNGs of the folders (absolute paths) → a library: decoding on the scheduler, textures on
    // the main thread; blocks until ready.
    Editor::IconLibrary loadIconLibrary(const std::vector<std::filesystem::path> &folders);
    void rememberIconSources(const std::vector<std::filesystem::path> &folders);
    // token → the file of the editor or of a module (what an override goes back to)
    std::unordered_map<std::string, std::filesystem::path> m_iconSources;
    // token → the plugin file in place of the source right now
    std::unordered_map<std::string, std::filesystem::path> m_iconOverrides;
    std::vector<std::filesystem::path> m_iconOverrideFiles; // the last applyIconOverrides input
};

} // namespace Panda
