#include "Project/Plugins/EditorPlugins.hpp"

#include <Foundation/Foundation.hpp>
#include <Rain/Coders/JsonDecoder.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace Panda {

namespace {

    template<typename T>
    bool readJson(const path_t &file, T &out) {
        std::ifstream stream(file);
        if (!stream.is_open()) { return false; }
        Rain::JsonDecoder json;
        Rain::Decoder &decoder = json;
        decoder.decode(stream, out);
        return true;
    }

    // "#RRGGBB" / "#RRGGBBAA" → Color; false — cannot be parsed.
    bool parseColor(const std::string &text, PandaUI::Color &out) {
        if (text.size() != 7 && text.size() != 9) { return false; }
        if (text[0] != '#') { return false; }
        char *end = nullptr;
        const unsigned long value = std::strtoul(text.c_str() + 1, &end, 16);
        if (end == nullptr || *end != '\0') { return false; }
        const uint32_t rgba = text.size() == 7 ? static_cast<uint32_t>((value << 8) | 0xFF) : static_cast<uint32_t>(value);
        out = PandaUI::Color(rgba);
        return true;
    }

    struct ThemeField {
        const char *name;
        PandaUI::Color PandaUI::Theme::*member;
    };

    // Theme field names = member names of PandaUI::Theme.
    const ThemeField THEME_FIELDS[] = {
        {"windowBackground", &PandaUI::Theme::windowBackground},
        {"surface", &PandaUI::Theme::surface},
        {"surfaceVariant", &PandaUI::Theme::surfaceVariant},
        {"input", &PandaUI::Theme::input},
        {"inputDeep", &PandaUI::Theme::inputDeep},
        {"textPrimary", &PandaUI::Theme::textPrimary},
        {"textSecondary", &PandaUI::Theme::textSecondary},
        {"textMuted", &PandaUI::Theme::textMuted},
        {"textDim", &PandaUI::Theme::textDim},
        {"textDisabled", &PandaUI::Theme::textDisabled},
        {"accent", &PandaUI::Theme::accent},
        {"accentHover", &PandaUI::Theme::accentHover},
        {"accentPressed", &PandaUI::Theme::accentPressed},
        {"onAccent", &PandaUI::Theme::onAccent},
        {"accentSecondary", &PandaUI::Theme::accentSecondary},
        {"accentText", &PandaUI::Theme::accentText},
        {"accentSoft", &PandaUI::Theme::accentSoft},
        {"accentMutedSurface", &PandaUI::Theme::accentMutedSurface},
        {"accentMutedBorder", &PandaUI::Theme::accentMutedBorder},
        {"control", &PandaUI::Theme::control},
        {"controlHover", &PandaUI::Theme::controlHover},
        {"controlPressed", &PandaUI::Theme::controlPressed},
        {"border", &PandaUI::Theme::border},
        {"borderMed", &PandaUI::Theme::borderMed},
        {"borderSoft", &PandaUI::Theme::borderSoft},
        {"borderFaint", &PandaUI::Theme::borderFaint},
        {"borderStrong", &PandaUI::Theme::borderStrong},
        {"focusRing", &PandaUI::Theme::focusRing},
        {"knob", &PandaUI::Theme::knob},
        {"textSelection", &PandaUI::Theme::textSelection},
        {"selection", &PandaUI::Theme::selection},
        {"selectionInactive", &PandaUI::Theme::selectionInactive},
        {"rowHover", &PandaUI::Theme::rowHover},
        {"success", &PandaUI::Theme::success},
        {"warning", &PandaUI::Theme::warning},
        {"danger", &PandaUI::Theme::danger},
    };

} // namespace

std::vector<std::string> InstalledPlugin::provides() const {
    std::vector<std::string> result;
    if (!themes.empty()) { result.push_back("themes"); }
    return result;
}

void EditorPlugins::scan(const std::vector<path_t> &builtInDirectories, const path_t &userDirectory) {
    m_plugins.clear();
    m_themes.clear();
    auto folders = [](const path_t &root) {
        std::vector<path_t> result;
        std::error_code error;
        if (!std::filesystem::is_directory(root, error)) { return result; }
        for (const auto &entry : std::filesystem::directory_iterator(root, error)) {
            // A dev plugin is a link: directory_iterator follows it for is_directory.
            if (entry.is_directory(error) && std::filesystem::exists(entry.path() / "plugin.json", error)) {
                result.push_back(entry.path());
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    };
    for (const path_t &root : builtInDirectories) {
        for (const path_t &folder : folders(root)) { scanFolder(folder, true); }
    }
    if (!userDirectory.empty()) {
        for (const path_t &folder : folders(userDirectory)) { scanFolder(folder, false); }
    }
}

void EditorPlugins::scanFolder(const path_t &folder, bool builtIn) {
    InstalledPlugin plugin;
    if (!readManifest(folder / "plugin.json", plugin.manifest) || plugin.manifest.id.empty()) {
        LOG_ERROR_EDITOR("Plugin manifest is missing or invalid: %s", folder.string().c_str());
        return;
    }
    const bool duplicate = std::any_of(m_plugins.begin(), m_plugins.end(), [&](const InstalledPlugin &known) {
        return known.manifest.id == plugin.manifest.id;
    });
    if (duplicate) { return; } // a user folder does not override a built-in with the same id
    plugin.directory = folder;
    plugin.builtIn = builtIn;
    plugin.dev = !builtIn && isDevLink(folder);
    // Plugin themes: data/themes/*.json, in alphabetical order of the files.
    std::error_code error;
    std::vector<path_t> themeFiles;
    const path_t themesDir = folder / "data" / "themes";
    for (const auto &entry : std::filesystem::directory_iterator(themesDir, error)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") { themeFiles.push_back(entry.path()); }
    }
    std::sort(themeFiles.begin(), themeFiles.end());
    for (const path_t &file : themeFiles) {
        ThemeFile theme;
        if (!readJson(file, theme)) { continue; }
        EditorTheme entry;
        entry.id = plugin.manifest.id + "/" + file.stem().string();
        entry.name = theme.name.empty() ? file.stem().string() : theme.name;
        entry.file = file;
        plugin.themes.push_back(entry);
        m_themes.push_back(std::move(entry));
    }
    m_plugins.push_back(std::move(plugin));
}

const InstalledPlugin *EditorPlugins::find(const std::string &id) const {
    for (const InstalledPlugin &plugin : m_plugins) {
        if (plugin.manifest.id == id) { return &plugin; }
    }
    return nullptr;
}

const EditorTheme *EditorPlugins::findTheme(const std::string &id) const {
    for (const EditorTheme &theme : m_themes) {
        if (theme.id == id) { return &theme; }
    }
    return nullptr;
}

bool EditorPlugins::readManifest(const path_t &file, PluginManifest &out) {
    return readJson(file, out);
}

bool EditorPlugins::isDevLink(const path_t &folder) {
    std::error_code error;
    if (std::filesystem::is_symlink(folder, error)) { return true; }
    // Windows: a junction is not reported as a symbolic link by every runtime; the store leaves a
    // marker next to it (<Plugins>/.dev/<id>).
    return std::filesystem::exists(folder.parent_path() / ".dev" / folder.filename(), error);
}

std::optional<PandaUI::Theme> EditorPlugins::loadTheme(const path_t &file) {
    ThemeFile data;
    if (!readJson(file, data)) { return std::nullopt; }
    PandaUI::Theme theme = data.base == "light" ? PandaUI::Theme::light() : PandaUI::Theme::dark();
    for (const auto &[name, text] : data.colors) {
        const ThemeField *field = nullptr;
        for (const ThemeField &candidate : THEME_FIELDS) {
            if (name == candidate.name) { field = &candidate; }
        }
        if (field == nullptr) {
            LOG_WARN_EDITOR("Theme %s: unknown color %s", file.filename().string().c_str(), name.c_str());
            continue;
        }
        PandaUI::Color color;
        if (!parseColor(text, color)) {
            LOG_WARN_EDITOR("Theme %s: bad color %s = %s", file.filename().string().c_str(), name.c_str(), text.c_str());
            continue;
        }
        theme.*(field->member) = color;
    }
    return theme;
}

} // namespace Panda
