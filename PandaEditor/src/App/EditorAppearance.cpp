#include "App/EditorAppearance.hpp"

#include "Project/Documents/FileDocuments.hpp"
#include "Project/Plugins/PluginStore.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/System/SystemTools.hpp"

#include <Fern/Fern.hpp>
#include <Foundation/Logger.hpp>
#include <PandaUI/PandaUI.hpp>

namespace Panda {

EditorAppearance::EditorAppearance(EditorUI &ui, PluginStore &plugins, FileDocuments &documents)
    : m_ui(ui)
    , m_plugins(plugins)
    , m_documents(documents) {}

void EditorAppearance::load() {
    // Built-in plugins come from the editor resources, installed ones live next to the user data
    // (the store uses Plugins/ and registry/ inside it). Icons become textures through the UI host.
    m_plugins.initialize(
        {Fern::getStaticResourcesPath() / "plugins"}, SystemTools::userDataDirectory(),
        [this](const path_t &file) { return m_ui.loadImage(file); }
    );
    // An install, an update or a removal takes effect at once: the theme of the settings is read
    // again (its files may have changed), or the editor falls back to the built-in dark one; the
    // icons of the plugins replace or release the editor's.
    m_plugins.setOnInstalledChanged([this] {
        applyTheme();
        applyIcons();
    });
    applyIcons();
    applyTheme();
}

EditorUI::FontSettings EditorAppearance::fonts() const {
    const EditorAppearanceSettings &appearance = m_documents.editorSettings().appearance;
    EditorUI::FontSettings settings;
    settings.uiFamily = appearance.uiFont;
    settings.monoFamily = appearance.monoFont;
    settings.uiSize = appearance.uiFontSize;
    return settings;
}

void EditorAppearance::applyTheme() {
    const std::string themeId = currentThemeId();
    if (const EditorTheme *theme = m_plugins.findTheme(themeId)) {
        if (std::optional<PandaUI::Theme> loaded = EditorPlugins::loadTheme(theme->file)) {
            PandaUI::setTheme(*loaded); // Context re-reads the colors of the whole tree
            return;
        }
        LOG_ERROR_EDITOR("Theme %s can't be read: %s", themeId.c_str(), theme->file.string().c_str());
    }
    PandaUI::setTheme(PandaUI::Theme::dark());
}

void EditorAppearance::applyIcons() {
    std::vector<path_t> files; // install order: built-in plugins first, a later plugin wins a token
    for (const InstalledPlugin &plugin : m_plugins.installed()) {
        files.insert(files.end(), plugin.icons.begin(), plugin.icons.end());
    }
    m_ui.applyIconOverrides(files);
}

// The theme of an empty setting (a new user) or of one whose plugin is gone: Panda Dark, the
// built-in dark theme as a plugin theme (its file adds nothing to the base) — so the APPEARANCE
// section and the Plugins tab name the theme the editor shows, not nothing.
std::string EditorAppearance::currentThemeId() const {
    static const std::string DEFAULT_THEME = "dev.pandaengine.themes/panda-dark";
    const std::string &theme = m_documents.editorSettings().appearance.theme;
    return theme.empty() || m_plugins.findTheme(theme) == nullptr ? DEFAULT_THEME : theme;
}

void EditorAppearance::selectTheme(const std::string &themeId) {
    if (!m_plugins.findTheme(themeId)) { return; }
    m_documents.editorSettings().appearance.theme = themeId;
    m_documents.editorSettingsFile().save();
    applyTheme();
}

std::vector<std::string> EditorAppearance::themeNames() const {
    std::vector<std::string> names;
    for (const EditorTheme &theme : m_plugins.themes()) { names.push_back(theme.name); }
    return names;
}

int EditorAppearance::currentThemeIndex() const {
    const std::vector<EditorTheme> &themes = m_plugins.themes();
    const std::string current = currentThemeId();
    for (size_t i = 0; i < themes.size(); i++) {
        if (themes[i].id == current) { return static_cast<int>(i); }
    }
    return -1;
}

void EditorAppearance::selectThemeIndex(int index) {
    const std::vector<EditorTheme> &themes = m_plugins.themes();
    if (index < 0 || index >= static_cast<int>(themes.size())) { return; }
    selectTheme(themes[static_cast<size_t>(index)].id);
}

} // namespace Panda
