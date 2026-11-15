#pragma once

#include <Miren/Miren.hpp>
#include <Panda/UI/PandaUIFernHost.hpp>
#include <Panda/UI/PandaUIMirenRenderer.hpp>
#include <PandaUI/Rendering/Font.hpp>
#include <PandaUI/Runtime/Context.hpp>

#include <filesystem>
#include <array>
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

// The UI of the editor itself (the chrome): its PandaUI context, the fonts and the icons it
// draws with, a second window for the Settings. The input of the windows and the drawing are
// the engine's (PandaUIFernHost, PandaUIMirenRenderer).
class EditorUI final {
public:
    void initialize(Fern::Window *window);
    void shutdown();

    void setRootView(std::shared_ptr<PandaUI::View> view);
    // Runs layout/animation and draws the chrome: the last pass of the frame, into the window
    // (every frame). Whatever the chrome shows of this frame — the viewports, the previews — is
    // drawn before it.
    void update(double deltaTime);
    bool handleEvent(Fern::Event *event); // Fern → Context
    // A gesture made in place of the user is in flight (EditorMcp: `pointer`): the real mouse is
    // not heard until it ends, so a hand that happens to move does not tear the gesture.
    void setPointerOverridden(bool overridden) {
        m_pointerOverridden = overridden;
    }
    // The chrome as it was last drawn, once more as a pass into another target (a screenshot of
    // the window: a texture of the size of the window in pixels).
    void renderTo(const Miren::ViewDescription &target);

    // The fonts of the chrome. uiFamily / monoFamily: a family installed on this machine (Fern::Fonts)
    // that plays the Sans / Mono role; empty — the bundled Inter Display / JetBrains Mono. uiSize: the
    // body size; the UI is designed at 16 and every text scales by uiSize / 16. A glyph the chosen face
    // lacks comes from the bundled one (the fallback chain of the atlases).
    struct FontSettings {
        std::string uiFamily;
        std::string monoFamily;
        float uiSize = 16.f;
        bool operator==(const FontSettings &other) const = default;
    };
    void loadFonts(const FontSettings &settings); // blocks: the fonts are needed by the first frame
    void applyFonts(const FontSettings &settings);     // a settings change: reloads what differs, the texts re-measure
    // The face of a font asset of the project in the chrome (the preview of the font tab): rasterized
    // once from the bytes, registered as the family "asset:<id>" with the bundled Sans as the fallback.
    PandaUI::FontFamily fontFamilyForAsset(uint32_t assetId, const std::vector<uint8_t> &bytes);
    // A face installed on this machine under a PandaUI family of the caller's choice (the gallery's
    // "Fantasy" skin wants a serif): the closest face of the weight, rasterized once, the bundled
    // Sans of that weight for the glyphs it lacks. False — the family is not installed or its file
    // cannot be read; nothing is registered then.
    bool loadInstalledFace(const std::string &installedFamily, int weightClass, PandaUI::FontFamily as);
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

    // A second window of the editor with a UI tree of its own (the Settings window): a modal
    // window of Fern over the main one, its own Context over the renderer, the fonts and the
    // theme of the chrome. One at a time; opening while one is open answers its context.
    PandaUI::Context *openAuxiliaryWindow(const char *title, PandaUI::Size size, std::shared_ptr<PandaUI::View> root);
    // The UI tree goes at once, the native window a few frames later: frames in flight draw into it.
    void closeAuxiliaryWindow();
    Fern::Window *auxiliaryWindow() const; // nullptr — none is open
    PandaUI::Context *auxiliaryContext();
    void renderAuxiliaryTo(const Miren::ViewDescription &target); // like renderTo, the tree of the second window

    // The texture a viewport renders into, as a PandaUI texture for its ImageView. The handle is
    // stable across resizes, so it is wrapped once; the texture stays the engine's.
    PandaUI::TextureHandle wrapNativeTexture(Miren::TextureHandle native, PandaUI::Size size) {
        return m_renderer.wrapNativeTexture(native, size);
    }

    // A single image from the bundle resources (a path from Resources/, e.g. "ui/start/Logo.png") — for
    // one-off chrome images; icon sets are loaded by loadEditorIcons as a batch on the scheduler.
    PandaUI::TextureHandle loadImage(const std::filesystem::path &path) {
        return m_renderer.createTextureFromStaticResource(path);
    }
    // A texture from RGBA8 pixels drawn at run time (the plates of the gallery skin); destroyTexture
    // releases what loadImage or createTexture made.
    PandaUI::TextureHandle createTexture(const unsigned char *pixels, uint16_t width, uint16_t height) {
        return m_renderer.createTextureRGBA8(pixels, width, height);
    }
    void destroyTexture(PandaUI::TextureHandle texture) {
        m_renderer.destroyTexture(texture);
    }

private:
    PandaUIMirenRenderer m_renderer;
    PandaUIFernHost m_platform;
    PandaUI::Context m_context;
    struct AuxiliaryWindow {
        Fern::Window *window = nullptr;
        PandaUIFernHost platform;
        PandaUI::Context context;
    };
    std::unique_ptr<AuxiliaryWindow> m_auxiliary;
    struct RetiredWindow {
        Fern::Window *window = nullptr;
        uint32_t frame = 0; // the frame after which nothing in flight draws into it
    };
    std::vector<RetiredWindow> m_retiredWindows;
    void updateAuxiliary(double deltaTime);
    Fern::Window *m_window = nullptr;
    bool m_initialized = false;
    bool m_pointerOverridden = false;

    // The font atlases are kept alive as long as the Context is alive: the bundled faces of a role and,
    // when a setting names an installed family, its faces (by weight: Regular, Medium, Bold).
    struct FontRole {
        std::array<std::shared_ptr<PandaUI::FontAtlas>, 3> bundled;
        std::array<std::shared_ptr<PandaUI::FontAtlas>, 3> custom;
        std::string family; // the installed family loaded into custom; empty — none
    };
    struct FontJob;
    FontRole m_sansRole, m_monoRole;
    FontSettings m_fontSettings;
    struct AssetFont {
        uint64_t hash = 0; // of the bytes: a reimported font (Reload) gets a fresh atlas under the same family
        std::shared_ptr<PandaUI::FontAtlas> atlas; // null — the bytes are not a face the rasterizer reads
    };
    std::unordered_map<uint32_t, AssetFont> m_assetFonts; // by asset id
    std::vector<std::shared_ptr<PandaUI::FontAtlas>> m_installedFaces; // loadInstalledFace: alive with the Context
    bool m_bundledFontsLoaded = false;
    void loadBundledFonts();
    void loadCustomFamily(FontRole &role, const std::string &family);
    void rasterizeFonts(std::vector<FontJob> &jobs); // on the scheduler; blocks until the atlases exist
    void bindFonts();                                 // the roles into the contexts, the text scale
    void bindFonts(PandaUI::Context &context);

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
