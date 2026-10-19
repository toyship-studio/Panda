#include "UITestLayer.hpp"

#include "UI/Demo/ColorSchemeEditorView.hpp"
#include "UI/Demo/DemoAppView.hpp"
#include "UI/Demo/PageHeader.hpp"

#include <Fern/Events/MenuEvents.hpp>
#include <Fern/Events/WindowEvents.hpp>
#include <Fern/Fern.hpp>
#include <Fern/Window/Window.hpp>
#include <Miren/Miren.hpp>
#include <Panda.hpp>
#include <Panda/Assets/Base/AssetImporterBase.hpp>

#include <algorithm>
#include <memory>

namespace {

enum UITestMenuCommand : Fern::MenuCommandId {
    Quit = 1,
    ToggleFullscreen = 2,
    ThemeDark = 3,
    ThemeLight = 4,
    ColorScheme = 5,
};

PandaUI::TextureHandle createFontAtlasTexture(void *userData, const PandaUI::FontAtlas::AtlasData &atlas) {
    auto *renderer = static_cast<PandaUIImplMiren *>(userData);
    return renderer->createTextureRGBA8(atlas.rgbaPixels, atlas.width, atlas.height);
}

} // namespace

UITestLayer::UITestLayer()
    : Panda::Layer("PandaUITest") {}

void UITestLayer::onAttach() {
    m_uiRenderer.init();

    // Three Inter Display weights (as in PandaEditor): Regular — body, Medium — small text, Bold —
    // headings. The atlas is rasterized large (40px): UI font sizes 11..22pt, on retina up to ~44px
    // the text stays crisp.
    auto loadFont = [this](const char *path) {
        Panda::FileData data = Panda::AssetImporterBase::loadBinaryData(path);
        auto font = PandaUI::FontAtlas::createFromMemory(
            data.memory.data, data.size, 40.f, createFontAtlasTexture, &m_uiRenderer
        );
        data.memory.release();
        return font;
    };
    m_defaultFont = loadFont("default-fonts/Inter/InterDisplay-Regular.ttf");
    m_mediumFont = loadFont("default-fonts/Inter/InterDisplay-Medium.ttf");
    m_boldFont = loadFont("default-fonts/Inter/InterDisplay-Bold.ttf");
    // Monospaced (JetBrains Mono) — numbers, paths, IDs, axes, FPS, versions, hotkeys (design.md §2).
    m_monoFont = loadFont("default-fonts/JetBrainsMono/JetBrainsMono-Regular.ttf");
    m_monoMediumFont = loadFont("default-fonts/JetBrainsMono/JetBrainsMono-Medium.ttf");
    m_monoBoldFont = loadFont("default-fonts/JetBrainsMono/JetBrainsMono-Bold.ttf");

    m_shipLogoTexture = m_uiRenderer.createTextureFromStaticResource("demo-ui/ship_logo.png");

    createMainWindow();
}

void UITestLayer::onDetach() {
    for (auto it = m_windows.rbegin(); it != m_windows.rend(); ++it) {
        DemoWindow &demoWindow = **it;
        demoWindow.platform.shutdown();
        if (demoWindow.ownsWindow && demoWindow.window) { Fern::disposeWindow(demoWindow.window); }
    }
    m_windows.clear();
    m_defaultFont.reset();
    m_mediumFont.reset();
    m_boldFont.reset();
    m_monoFont.reset();
    m_monoMediumFont.reset();
    m_monoBoldFont.reset();
    m_uiRenderer.shutdown();
}

void UITestLayer::applyFonts(PandaUI::Context &context) {
    context.setFontAtlas(m_defaultFont); // Sans Regular (base / fallback)
    context.setFontAtlas(m_mediumFont, PandaUI::FontWeight::Medium);
    context.setFontAtlas(m_boldFont, PandaUI::FontWeight::Bold);
    context.setFontAtlas(m_monoFont, PandaUI::FontWeight::Regular, PandaUI::FontFamily::Mono);
    context.setFontAtlas(m_monoMediumFont, PandaUI::FontWeight::Medium, PandaUI::FontFamily::Mono);
    context.setFontAtlas(m_monoBoldFont, PandaUI::FontWeight::Bold, PandaUI::FontFamily::Mono);
}

void UITestLayer::onUpdate(double deltaTime) {
    for (std::unique_ptr<DemoWindow> &demoWindow : m_windows) {
        // On-demand: resubmit the window only if its UI changed during the tick.
        if (demoWindow->context.update(deltaTime)) {
            m_uiRenderer.render(demoWindow->context.getDrawList(), demoWindow->viewId);
        }
    }
}

void UITestLayer::onEvent(Fern::Event *event) {
    if (event->type == Fern::EventType::WindowCloseRequest &&
        event->getWindow() != Panda::Application::get()->getMainWindow()) {
        event->isHandled = true;
        closeWindow(event->getWindow());
        return;
    }

    if (event->type == Fern::EventType::WindowResize) {
        if (DemoWindow *demoWindow = findWindow(event->getWindow())) {
            demoWindow->window->getDrawingContext()->update();
            syncViewport(*demoWindow);
        }
    }

    for (std::unique_ptr<DemoWindow> &demoWindow : m_windows) {
        if (demoWindow->platform.handleEvent(event)) {
            event->isHandled = true;
            return;
        }
    }

    if (event->type == Fern::EventType::MenuCommand) {
        const auto *menuEvent = static_cast<const Fern::MenuCommandEvent *>(event);
        if (menuEvent->commandId == UITestMenuCommand::Quit) {
            event->isHandled = true;
            Panda::Application::get()->close();
        } else if (menuEvent->commandId == UITestMenuCommand::ToggleFullscreen) {
            event->isHandled = true;
            Fern::Window *window = event->getWindow() ? event->getWindow() : Panda::Application::get()->getMainWindow();
            window->setState(
                window->getState() == Fern::WindowState::WindowStateFullScreen
                    ? Fern::WindowState::WindowStateNormal
                    : Fern::WindowState::WindowStateFullScreen
            );
        } else if (menuEvent->commandId == UITestMenuCommand::ThemeDark) {
            event->isHandled = true;
            PandaUI::setTheme(PandaUI::Theme::dark());
        } else if (menuEvent->commandId == UITestMenuCommand::ThemeLight) {
            event->isHandled = true;
            PandaUI::setTheme(PandaUI::Theme::light());
        } else if (menuEvent->commandId == UITestMenuCommand::ColorScheme) {
            event->isHandled = true;
            openColorSchemeWindow();
        }
    }
}

Fern::MenuBar UITestLayer::buildMenuBar() {
    Fern::MenuBar menuBar;

    auto &fileMenu = menuBar.addMenu("File");
    fileMenu.addItem("Quit", UITestMenuCommand::Quit, {Fern::Key::KEY_Q, Fern::MenuShortcut::Primary});

    auto &viewMenu = menuBar.addMenu("View");
    viewMenu.addItem("Toggle Fullscreen", UITestMenuCommand::ToggleFullscreen);
    viewMenu.addItem("Theme: Dark", UITestMenuCommand::ThemeDark);
    viewMenu.addItem("Theme: Light", UITestMenuCommand::ThemeLight);
    viewMenu.addItem("Color Scheme…", UITestMenuCommand::ColorScheme);

    return menuBar;
}

void UITestLayer::createMainWindow() {
    Fern::Window *window = Panda::Application::get()->getMainWindow();
    window->setMenuBar(buildMenuBar());

    auto demoWindow = std::make_unique<DemoWindow>();
    demoWindow->window = window;
    demoWindow->viewId = 0;
    demoWindow->ownsWindow = false;
    applyFonts(demoWindow->context);
    demoWindow->platform.init(window, demoWindow->context);
    demoWindow->context.setRootView(
        demoWindow->context.makeView<Demo::DemoAppView>(m_shipLogoTexture, [this] { openSecondaryWindow(); })
    );
    syncViewport(*demoWindow);
    m_windows.push_back(std::move(demoWindow));
}

void UITestLayer::openSecondaryWindow() {
    Fern::Rect rect(120.f + 26.f * m_windows.size(), 96.f + 26.f * m_windows.size(), 560.f, 420.f);
    Fern::Window *window = Fern::createWindow(
        "PandaUI — Secondary Window", rect, Fern::WindowState::WindowStateNormal, Fern::WindowType::Normal,
        FERN_WINDOW_FLAGS_DEFAULT
    );
    window->setMenuBar(buildMenuBar());

    auto demoWindow = std::make_unique<DemoWindow>();
    demoWindow->window = window;
    demoWindow->viewId = m_nextViewId++;
    demoWindow->ownsWindow = true;
    applyFonts(demoWindow->context);
    demoWindow->platform.init(window, demoWindow->context);

    auto root = demoWindow->context.makeView<PandaUI::Panel>();
    root->setBackgroundColor(PandaUI::theme().windowBackground);
    root->layout().setFlexDirection(PandaUI::FlexDirection::Column);
    root->layout().setPadding(24.f);
    root->addSubview(std::make_shared<Demo::PageHeader>(
        "Secondary Window", "A separate window with its own PandaUI::Context. Several windows work at the same time."
    ));
    demoWindow->context.setRootView(root);

    syncViewport(*demoWindow);
    m_windows.push_back(std::move(demoWindow));
}

void UITestLayer::openColorSchemeWindow() {
    Fern::Rect rect(140.f + 26.f * m_windows.size(), 110.f + 26.f * m_windows.size(), 420.f, 560.f);
    Fern::Window *window = Fern::createWindow(
        "PandaUI — Color Scheme", rect, Fern::WindowState::WindowStateNormal, Fern::WindowType::Normal,
        FERN_WINDOW_FLAGS_DEFAULT
    );
    window->setMenuBar(buildMenuBar());

    auto demoWindow = std::make_unique<DemoWindow>();
    demoWindow->window = window;
    demoWindow->viewId = m_nextViewId++;
    demoWindow->ownsWindow = true;
    applyFonts(demoWindow->context);
    demoWindow->platform.init(window, demoWindow->context);
    demoWindow->context.setRootView(demoWindow->context.makeView<Demo::ColorSchemeEditorView>());

    syncViewport(*demoWindow);
    m_windows.push_back(std::move(demoWindow));
}

void UITestLayer::closeWindow(Fern::Window *window) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(), [window](const std::unique_ptr<DemoWindow> &demoWindow) {
        return demoWindow->window == window;
    });
    if (it == m_windows.end() || !(*it)->ownsWindow) { return; }

    (*it)->platform.shutdown();
    Fern::disposeWindow((*it)->window);
    m_windows.erase(it);
}

UITestLayer::DemoWindow *UITestLayer::findWindow(Fern::Window *window) {
    for (std::unique_ptr<DemoWindow> &demoWindow : m_windows) {
        if (demoWindow->window == window) { return demoWindow.get(); }
    }
    return nullptr;
}

void UITestLayer::syncViewport(DemoWindow &demoWindow) {
    Fern::Size windowSize = demoWindow.window->getSize();
    Fern::Size dpi = demoWindow.window->getDpi();
    Miren::setViewGraphicsContext(demoWindow.viewId, demoWindow.window->getDrawingContext());
    Miren::setViewport(
        demoWindow.viewId,
        Miren::Rect(
            0, 0, static_cast<int>(windowSize.width * dpi.width), static_cast<int>(windowSize.height * dpi.height)
        )
    );
    demoWindow.platform.syncViewport();
}
