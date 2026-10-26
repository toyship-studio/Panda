#include "UI/Host/EditorUI.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>

#include <Fern/Fern.hpp>
#include <Fern/Fonts/FontCatalog.hpp>
#include <Foundation/Logger.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <PandaUI/Core/View.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace Panda {

namespace {
    PandaUI::TextureHandle makeFontAtlasTexture(void *userData, const PandaUI::FontAtlas::AtlasData &atlas) {
        return static_cast<PandaUIImplMiren *>(userData)->createTextureRGBA8(atlas.rgbaPixels, atlas.width, atlas.height);
    }
} // namespace

void EditorUI::initialize(Fern::Window *window, Miren::ViewId viewId) {
    if (m_initialized) { return; }
    m_viewId = viewId;
    m_renderer.init();
    m_platform.init(window, m_context); // synchronizes the Context viewport size from the window
    m_initialized = true;
}

void EditorUI::shutdown() {
    if (!m_initialized) { return; }
    m_platform.shutdown();
    m_renderer.shutdown();
    m_initialized = false;
}

void EditorUI::setRootView(std::shared_ptr<PandaUI::View> view) {
    m_context.setRootView(std::move(view));
}

void EditorUI::update(double deltaTime) {
    if (!m_initialized) { return; }
    // The editor chrome is drawn ON TOP of the scene every frame: update rebuilds the draw-list on changes,
    // while the cached draw-list is submitted always (otherwise on "clean" frames the scene would show
    // through the UI).
    m_context.update(deltaTime);
    m_renderer.render(m_context.getDrawList(), m_viewId);
}

bool EditorUI::handleEvent(Fern::Event *event) {
    if (!m_initialized) { return false; }
    return m_platform.handleEvent(event);
}

namespace {
    constexpr float kFontPixelSize = 40.f; // the atlas is rasterized larger than the text and scaled down: sharp at 2x
    constexpr int kWeightCount = 3;
    const PandaUI::FontWeight kWeights[kWeightCount] = {PandaUI::FontWeight::Regular, PandaUI::FontWeight::Medium, PandaUI::FontWeight::Bold};
    const int kWeightClasses[kWeightCount] = {400, 500, 700}; // OS/2 usWeightClass of the three roles
    const char *const kBundledSans[kWeightCount] = {
        "default-fonts/Inter/InterDisplay-Regular.ttf", "default-fonts/Inter/InterDisplay-Medium.ttf",
        "default-fonts/Inter/InterDisplay-Bold.ttf"};
    const char *const kBundledMono[kWeightCount] = {
        "default-fonts/JetBrainsMono/JetBrainsMono-Regular.ttf", "default-fonts/JetBrainsMono/JetBrainsMono-Medium.ttf",
        "default-fonts/JetBrainsMono/JetBrainsMono-Bold.ttf"};

    std::vector<unsigned char> readWholeFile(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) { return {}; }
        const std::streamsize size = file.tellg();
        if (size <= 0) { return {}; }
        std::vector<unsigned char> bytes(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char *>(bytes.data()), size);
        return bytes;
    }
} // namespace

// One face to rasterize and the slots it fills (a face may serve several weights).
struct EditorUI::FontJob {
    std::vector<unsigned char> bytes;
    int faceIndex = 0;
    std::vector<std::shared_ptr<PandaUI::FontAtlas> *> targets;
};

void EditorUI::loadFonts(const FontSettings &settings) {
    if (!m_bundledFontsLoaded) {
        loadBundledFonts();
        m_bundledFontsLoaded = true;
    }
    loadCustomFamily(m_sansRole, settings.uiFamily);
    loadCustomFamily(m_monoRole, settings.monoFamily);
    m_fontSettings = settings;
    bindFonts();
}

void EditorUI::applyFonts(const FontSettings &settings) {
    if (settings == m_fontSettings) { return; }
    if (settings.uiFamily != m_sansRole.family) { loadCustomFamily(m_sansRole, settings.uiFamily); }
    if (settings.monoFamily != m_monoRole.family) { loadCustomFamily(m_monoRole, settings.monoFamily); }
    m_fontSettings = settings;
    bindFonts();
    m_context.fontsChanged(); // every text re-measures and redraws
}

void EditorUI::rasterizeFonts(std::vector<FontJob> &jobs) {
    // CPU pack on the workers (stb is thread-safe, the faces are independent), the GPU texture upload
    // on the main thread in the completion. Blocks: the fonts are needed by the next frame.
    PandaAsync::Scheduler &scheduler = Panda::Application::get()->getScheduler();
    uint32_t count = 0;
    for (FontJob &job : jobs) {
        if (job.bytes.empty()) { continue; }
        count++;
        const unsigned char *memory = job.bytes.data();
        const uint32_t size = static_cast<uint32_t>(job.bytes.size());
        const int faceIndex = job.faceIndex;
        scheduler.submit<PandaUI::FontAtlas::CpuAtlas>(
            PandaAsync::JobDesc{"font.rasterize"},
            [memory, size, faceIndex](PandaAsync::Context &) {
                return PandaUI::FontAtlas::rasterizeFromMemory(memory, size, kFontPixelSize, faceIndex);
            },
            [this, &job](PandaUI::FontAtlas::CpuAtlas &&cpu) {
                std::shared_ptr<PandaUI::FontAtlas> atlas =
                    PandaUI::FontAtlas::createFromCpuAtlas(std::move(cpu), makeFontAtlasTexture, &m_renderer);
                for (std::shared_ptr<PandaUI::FontAtlas> *target : job.targets) { *target = atlas; }
            }
        );
    }
    scheduler.waitAll();
    scheduler.processCompletions(PandaAsync::CompletionBudget{count, 0.0});
}

void EditorUI::loadBundledFonts() {
    std::vector<FontJob> jobs;
    jobs.reserve(kWeightCount * 2);
    for (int w = 0; w < kWeightCount; ++w) {
        for (int role = 0; role < 2; ++role) {
            const char *path = role == 0 ? kBundledSans[w] : kBundledMono[w];
            Panda::FileData data = Panda::AssetImporterBase::loadBinaryData(path);
            FontJob job;
            if (data.memory.data && data.size > 0) {
                const auto *bytes = static_cast<const unsigned char *>(data.memory.data);
                job.bytes.assign(bytes, bytes + data.size);
            } else {
                LOG_ERROR("BUNDLED FONT NOT FOUND: %s", path);
            }
            data.memory.release();
            job.targets.push_back(role == 0 ? &m_sansRole.bundled[w] : &m_monoRole.bundled[w]);
            jobs.push_back(std::move(job));
        }
    }
    rasterizeFonts(jobs);
    // Mono falls back to Sans for the glyphs it lacks (the bundled JetBrains Mono has them all, an
    // installed mono font may not).
    for (int w = 0; w < kWeightCount; ++w) {
        if (m_monoRole.bundled[w]) { m_monoRole.bundled[w]->setFallback(m_sansRole.bundled[w]); }
    }
}

void EditorUI::loadCustomFamily(FontRole &role, const std::string &family) {
    role.custom = {};
    role.family = family;
    if (family.empty()) { return; }
    // The closest installed face of every weight; faces are rasterized once and shared between the
    // weights they serve (a family with one file serves all three).
    std::vector<FontJob> jobs;
    std::vector<std::string> keys;
    for (int w = 0; w < kWeightCount; ++w) {
        const std::optional<Fern::Fonts::Face> face = Fern::Fonts::find(family, kWeightClasses[w]);
        if (!face) {
            LOG_WARN_EDITOR("Font family \"%s\" is not installed: the bundled font is used", family.c_str());
            return;
        }
        const std::string key = face->file.string() + "#" + std::to_string(face->index);
        size_t index = keys.size();
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) { index = i; }
        }
        if (index == keys.size()) {
            keys.push_back(key);
            FontJob job;
            job.bytes = readWholeFile(face->file);
            job.faceIndex = face->index;
            if (job.bytes.empty()) { LOG_WARN_EDITOR("Font file cannot be read: %s", face->file.string().c_str()); }
            jobs.push_back(std::move(job));
        }
        jobs[index].targets.push_back(&role.custom[w]);
    }
    rasterizeFonts(jobs);
    // What the installed face lacks (Cyrillic, ✓, …) is drawn with the bundled face of the role.
    for (int w = 0; w < kWeightCount; ++w) {
        if (role.custom[w]) { role.custom[w]->setFallback(role.bundled[w]); }
    }
}

void EditorUI::bindFonts() {
    for (int w = 0; w < kWeightCount; ++w) {
        const std::shared_ptr<PandaUI::FontAtlas> &sans = m_sansRole.custom[w] ? m_sansRole.custom[w] : m_sansRole.bundled[w];
        const std::shared_ptr<PandaUI::FontAtlas> &mono = m_monoRole.custom[w] ? m_monoRole.custom[w] : m_monoRole.bundled[w];
        m_context.setFontAtlas(sans, kWeights[w], PandaUI::FontFamily::Sans);
        m_context.setFontAtlas(mono, kWeights[w], PandaUI::FontFamily::Mono);
    }
    m_context.setTextScale(m_fontSettings.uiSize > 0.f ? m_fontSettings.uiSize / 16.f : 1.f);
}

void EditorUI::loadEditorIcons() {
    // The icon library = all PNGs of the listed folders (icon name = file name): a new icon is
    // a new file, no list is maintained in code. Package icons arrive later (loadIconFolders).
    // Blocks here: the panels read the library when the controller is built right after.
    std::vector<path_t> folders;
    for (const char *folder : {"ui/editor-icons", "ui/file-icons"}) {
        folders.push_back(Fern::getStaticResourcesPath() / folder);
    }
    Editor::setIcons(loadIconLibrary(folders));
    rememberIconSources(folders);
}

void EditorUI::loadIconFolders(const std::vector<path_t> &folders) {
    Editor::addIcons(loadIconLibrary(folders));
    rememberIconSources(folders);
    // A plugin may override the icons of a module that arrived just now.
    if (!m_iconOverrideFiles.empty()) { applyIconOverrides(m_iconOverrideFiles); }
}

void EditorUI::rememberIconSources(const std::vector<path_t> &folders) {
    for (const path_t &folder : folders) {
        std::error_code error;
        for (const std::filesystem::directory_entry &file : std::filesystem::directory_iterator(folder, error)) {
            if (file.path().extension() == ".png") { m_iconSources[file.path().stem().string()] = file.path(); }
        }
    }
}

void EditorUI::applyIconOverrides(const std::vector<path_t> &files) {
    m_iconOverrideFiles = files;
    std::unordered_map<std::string, path_t> wanted; // token → the plugin file that should show
    for (const path_t &file : files) { wanted[file.stem().string()] = file; }

    // What has to change: a token whose override differs from the one in place, and a token whose
    // override went away (back to its source).
    std::vector<std::pair<std::string, path_t>> changes;
    for (const auto &[token, file] : wanted) {
        const auto current = m_iconOverrides.find(token);
        if (current == m_iconOverrides.end() || current->second != file) { changes.emplace_back(token, file); }
    }
    for (const auto &[token, file] : m_iconOverrides) {
        if (wanted.count(token) == 0) {
            const auto source = m_iconSources.find(token);
            changes.emplace_back(token, source == m_iconSources.end() ? path_t() : source->second);
        }
    }
    if (changes.empty()) { return; }

    // Decoding in parallel like the startup load; the textures change on the main thread.
    PandaAsync::Scheduler &scheduler = Panda::Application::get()->getScheduler();
    Editor::IconLibrary added;
    uint32_t count = 0;
    for (const auto &[token, file] : changes) {
        if (file.empty()) { continue; } // a plugin-only icon whose plugin is gone: nothing to go back to
        count++;
        scheduler.submit<Miren::TextureCreate>(
            PandaAsync::JobDesc{"icon.decode"},
            [file](PandaAsync::Context &) { return Panda::AssetImporterBase::load2DTextureFile(file); },
            [this, &added, token](Miren::TextureCreate &&create) {
                const PandaUI::TextureHandle existing = Editor::icons().get(token);
                if (existing) {
                    if (!m_renderer.replaceTexture(existing, std::move(create))) {
                        LOG_ERROR_EDITOR("Icon %s could not be replaced", token.c_str());
                    }
                } else {
                    added.set(token, m_renderer.createTextureFromDecoded(std::move(create)));
                }
            }
        );
    }
    scheduler.waitAll();
    scheduler.processCompletions(PandaAsync::CompletionBudget{count, 0.0});
    Editor::addIcons(added);
    m_iconOverrides.clear();
    for (const auto &[token, file] : wanted) { m_iconOverrides[token] = file; }
    LOG_INFO_EDITOR("Icons: %zu plugin icon(s) applied", wanted.size());
}

Editor::IconLibrary EditorUI::loadIconLibrary(const std::vector<path_t> &folders) {
    // Decoding runs in parallel on the scheduler (stb decoding is thread-safe), the GPU textures
    // are created on the main thread in the completion.
    PandaAsync::Scheduler &scheduler = Panda::Application::get()->getScheduler();
    Editor::IconLibrary library;
    uint32_t iconCount = 0;
    for (const path_t &folder : folders) {
        std::error_code error;
        std::filesystem::directory_iterator files(folder, error);
        if (error) {
            LOG_ERROR("EDITOR ICONS FOLDER NOT FOUND: %s", folder.string().c_str());
            continue;
        }
        for (const std::filesystem::directory_entry &file : files) {
            if (file.path().extension() != ".png") { continue; }
            const std::string name = file.path().stem().string();
            const path_t path = file.path();
            iconCount++;
            scheduler.submit<Miren::TextureCreate>(
                PandaAsync::JobDesc{"icon.decode"},
                [path](PandaAsync::Context &) { return Panda::AssetImporterBase::load2DTextureFile(path); },
                [this, &library, name](Miren::TextureCreate &&create) {
                    library.set(name, m_renderer.createTextureFromDecoded(std::move(create)));
                }
            );
        }
    }
    scheduler.waitAll();
    scheduler.processCompletions(PandaAsync::CompletionBudget{iconCount, 0.0});
    return library;
}

} // namespace Panda
