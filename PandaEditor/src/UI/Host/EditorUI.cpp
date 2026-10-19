#include "UI/Host/EditorUI.hpp"

#include <PandaEditorKit/Widgets/Icons.hpp>

#include <Fern/Fern.hpp>
#include <Foundation/Logger.hpp>
#include <Panda/Application/Application.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>
#include <PandaUI/Core/View.hpp>

#include <filesystem>
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

void EditorUI::loadFonts() {
    struct Spec {
        const char *path;
        std::shared_ptr<PandaUI::FontAtlas> *slot;
        PandaUI::FontWeight weight;
        PandaUI::FontFamily family;
    };
    const Spec specs[] = {
        {"default-fonts/Inter/InterDisplay-Regular.ttf", &m_fontRegular, PandaUI::FontWeight::Regular,
         PandaUI::FontFamily::Sans},
        {"default-fonts/Inter/InterDisplay-Medium.ttf", &m_fontMedium, PandaUI::FontWeight::Medium,
         PandaUI::FontFamily::Sans},
        {"default-fonts/Inter/InterDisplay-Bold.ttf", &m_fontBold, PandaUI::FontWeight::Bold,
         PandaUI::FontFamily::Sans},
        {"default-fonts/JetBrainsMono/JetBrainsMono-Regular.ttf", &m_monoRegular, PandaUI::FontWeight::Regular,
         PandaUI::FontFamily::Mono},
        {"default-fonts/JetBrainsMono/JetBrainsMono-Medium.ttf", &m_monoMedium, PandaUI::FontWeight::Medium,
         PandaUI::FontFamily::Mono},
        {"default-fonts/JetBrainsMono/JetBrainsMono-Bold.ttf", &m_monoBold, PandaUI::FontWeight::Bold, PandaUI::FontFamily::Mono},
    };
    constexpr int kCount = static_cast<int>(sizeof(specs) / sizeof(specs[0]));

    // Rasterization of 6 atlases on the shared scheduler: CPU pack on workers (stb is thread-safe, the fonts
    // are independent), the GPU texture upload — on the main thread in the completion. Blocks here: the
    // fonts are needed by the first frame.
    PandaAsync::Scheduler &scheduler = Panda::Application::get()->getScheduler();
    std::vector<Panda::FileData> data;
    data.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        data.push_back(Panda::AssetImporterBase::loadBinaryData(specs[i].path));
        const void *memory = data[i].memory.data;
        const uint32_t size = data[i].size;
        const Spec &spec = specs[i];
        scheduler.submit<PandaUI::FontAtlas::CpuAtlas>(
            PandaAsync::JobDesc{"font.rasterize"},
            [memory, size](PandaAsync::Context &) {
                return PandaUI::FontAtlas::rasterizeFromMemory(memory, size, 40.f);
            },
            [this, &spec](PandaUI::FontAtlas::CpuAtlas &&cpu) {
                std::shared_ptr<PandaUI::FontAtlas> atlas =
                    PandaUI::FontAtlas::createFromCpuAtlas(std::move(cpu), makeFontAtlasTexture, &m_renderer);
                *spec.slot = atlas;
                m_context.setFontAtlas(atlas, spec.weight, spec.family);
            }
        );
    }
    scheduler.waitAll();
    scheduler.processCompletions(PandaAsync::CompletionBudget{static_cast<uint32_t>(kCount), 0.0});
    for (int i = 0; i < kCount; ++i) { data[i].memory.release(); }
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
}

void EditorUI::loadIconFolders(const std::vector<path_t> &folders) {
    Editor::addIcons(loadIconLibrary(folders));
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
