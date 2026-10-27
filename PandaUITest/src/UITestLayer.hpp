#pragma once

#include "PandaUIImplFern.hpp"
#include "PandaUIImplMiren.hpp"
#include "UI/Demo/SkinAssets.hpp"

#include <Fern/Window/Menu.hpp>
#include <Panda/Application/Layer.hpp>

#include <memory>
#include <vector>

class UITestLayer final : public Panda::Layer {
public:
    UITestLayer();

    void onAttach() override;
    void onDetach() override;
    void onUpdate(double deltaTime) override;
    void onEvent(Fern::Event *event) override;

private:
    struct DemoWindow {
        Fern::Window *window = nullptr;
        PandaUI::Context context;
        PandaUIImplFern platform;
        uint16_t viewId = 0;
        bool ownsWindow = false;
    };

    Fern::MenuBar buildMenuBar();
    void createMainWindow();
    void openSecondaryWindow();
    void openColorSchemeWindow();
    void closeWindow(Fern::Window *window);
    DemoWindow *findWindow(Fern::Window *window);
    void syncViewport(DemoWindow &demoWindow);
    void applyFonts(PandaUI::Context &context); // sets Regular/Medium/Bold on the window's context

    std::vector<std::unique_ptr<DemoWindow>> m_windows;
    PandaUIImplMiren m_uiRenderer;
    std::shared_ptr<PandaUI::FontAtlas> m_defaultFont;
    std::shared_ptr<PandaUI::FontAtlas> m_mediumFont;
    std::shared_ptr<PandaUI::FontAtlas> m_boldFont;
    std::shared_ptr<PandaUI::FontAtlas> m_monoFont;
    std::shared_ptr<PandaUI::FontAtlas> m_monoMediumFont;
    std::shared_ptr<PandaUI::FontAtlas> m_monoBoldFont;
    std::shared_ptr<PandaUI::FontAtlas> m_displayFont; // a serif face of the system for the "Fantasy" skin; null — none
    PandaUI::TextureHandle m_shipLogoTexture;
    Demo::SkinAssets m_skinAssets;
    uint16_t m_nextViewId = 1;
};
