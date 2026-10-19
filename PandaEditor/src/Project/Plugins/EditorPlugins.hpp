#pragma once

#include <Panda/Assets/Base/Path.hpp>
#include <PandaUI/Core/Theme.hpp>
#include <Rain/Rain.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Panda {

// An editor plugin is a data package (context/plugins.md): a folder with plugin.json and data/.
// Shared by all projects, read at editor startup; carries no code. Sources:
// built-in — editor resources (Resources/plugins/*), user ones —
// <userdata>/Panda/Plugins/*/plugin.json (stage 9 — installation from the registry).
struct PluginManifest final : public Rain::Codable {
    int manifestVersion = 1;
    std::string id;
    std::string name;
    std::string version;
    std::vector<std::string> authors;
    std::string description;
    std::vector<std::string> parts;

    RAIN_FIELDS_BEGIN(PluginManifest)
    RAIN_FIELD(manifestVersion)
    RAIN_FIELD(id)
    RAIN_FIELD(name)
    RAIN_FIELD(version)
    RAIN_FIELD(authors)
    RAIN_FIELD(description)
    RAIN_FIELD(parts)
    RAIN_FIELDS_END
};

// A theme is data/themes/<file>.json: a name, a base (dark | light) and colors on top of it by the
// names of the PandaUI::Theme fields ("#RRGGBB" or "#RRGGBBAA"). An unknown name is a warning.
struct ThemeFile final : public Rain::Codable {
    std::string name;
    std::string base = "dark";
    std::unordered_map<std::string, std::string> colors;

    RAIN_FIELDS_BEGIN(ThemeFile)
    RAIN_FIELD(name)
    RAIN_FIELD(base)
    RAIN_FIELD(colors)
    RAIN_FIELDS_END
};

struct EditorTheme final {
    std::string id;   // "<plugin id>/<file without extension>" — the key in the settings
    std::string name; // label in the View → Theme menu
    path_t file;
};

class EditorPlugins final {
public:
    // Reads the plugin manifests of the folders (every subfolder with a plugin.json) and their themes.
    void scan(const std::vector<path_t> &directories);
    const std::vector<EditorTheme> &themes() const {
        return m_themes;
    }
    const EditorTheme *findTheme(const std::string &id) const;
    // A theme from a file on top of the base; nullopt — the file cannot be read.
    static std::optional<PandaUI::Theme> loadTheme(const path_t &file);

private:
    struct Plugin {
        PluginManifest manifest;
        path_t directory;
    };
    std::vector<Plugin> m_plugins;
    std::vector<EditorTheme> m_themes;
};

} // namespace Panda
