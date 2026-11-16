#pragma once

#include "UI/Host/EditorUI.hpp"

#include <string>
#include <vector>

namespace Panda {

class FileDocuments;
class PluginStore;

// The look of the editor: the theme and the icons of the installed plugins (a plugin is data — a
// theme, a set of icons), the fonts of the APPEARANCE settings. The settings of the editor scope
// say what, the plugin store has the files, the UI host applies them.
class EditorAppearance final {
public:
    EditorAppearance(EditorUI &ui, PluginStore &plugins, FileDocuments &documents);

    // At startup, before the panels are built — they read the theme and take their icons while
    // being built: the plugins are read (the built-in ones from the resources, the user's from
    // the user data), their icons and the theme of the settings are applied. An install, an
    // update or a removal of a plugin applies at once from then on.
    void load();
    // The fonts of the settings, for the UI host (EditorUI::loadFonts, applyFonts).
    EditorUI::FontSettings fonts() const;

    std::string currentThemeId() const;
    void selectTheme(const std::string &id); // applies and saves
    // The APPEARANCE section of the editor settings chooses a theme by index.
    std::vector<std::string> themeNames() const;
    int currentThemeIndex() const;
    void selectThemeIndex(int index);

private:
    void applyTheme(); // the theme of the settings; the built-in dark one when it is gone or cannot be read
    void applyIcons(); // the icons of the installed plugins over the editor's

    EditorUI &m_ui;
    PluginStore &m_plugins;
    FileDocuments &m_documents;
};

} // namespace Panda
