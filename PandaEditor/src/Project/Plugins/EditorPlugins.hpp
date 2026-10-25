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
// Shared by all projects and carries no code. Sources: built-in — editor resources
// (Resources/plugins/*), installed — <userdata>/Panda/Plugins/<id> (the plugin store,
// context/infrastructure.md section 3; a dev plugin is a link to the developer's folder).
struct PluginManifest final : public Rain::Codable {
    int manifestVersion = 1;
    std::string id;
    std::string name;
    std::string version;
    std::vector<std::string> authors;
    std::string description;
    std::string category;
    std::vector<std::string> tags;
    std::string license;
    std::string engine; // the engine version range (Project/Plugins/Semver)
    std::vector<std::string> parts;
    std::unordered_map<std::string, std::string> links; // repository, homepage, issues
    bool unlisted = false;

    RAIN_FIELDS_BEGIN(PluginManifest)
    RAIN_FIELD(manifestVersion)
    RAIN_FIELD(id)
    RAIN_FIELD(name)
    RAIN_FIELD(version)
    RAIN_FIELD(authors)
    RAIN_FIELD(description)
    RAIN_FIELD(category)
    RAIN_FIELD(tags)
    RAIN_FIELD(license)
    RAIN_FIELD(engine)
    RAIN_FIELD(parts)
    RAIN_FIELD(links)
    RAIN_FIELD(unlisted)
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
    std::string name; // the label in the Plugins tab and the APPEARANCE section
    path_t file;
};

// A plugin found in the folders: its manifest, where it is and what it brings.
struct InstalledPlugin final {
    PluginManifest manifest;
    path_t directory;
    bool builtIn = false; // shipped in the editor resources: listed, never removed
    bool dev = false;     // a link to the developer's folder (Install Dev Plugin)
    std::vector<EditorTheme> themes;
    // data/icons/<name>.png: editor icons by token name (white alpha masks, EditorUI::applyIconOverrides).
    std::vector<path_t> icons;

    // What the plugin provides, derived from its data folders ("themes", "icons").
    std::vector<std::string> provides() const;
};

class EditorPlugins final {
public:
    // Reads the plugin manifests of the folders (every subfolder with a plugin.json) and their
    // themes. The built-in folders come first; a user plugin with the id of a built-in one is
    // skipped.
    void scan(const std::vector<path_t> &builtInDirectories, const path_t &userDirectory);

    const std::vector<InstalledPlugin> &plugins() const {
        return m_plugins;
    }
    const InstalledPlugin *find(const std::string &id) const;
    const std::vector<EditorTheme> &themes() const {
        return m_themes;
    }
    const EditorTheme *findTheme(const std::string &id) const;

    // A theme from a file on top of the base; nullopt — the file cannot be read.
    static std::optional<PandaUI::Theme> loadTheme(const path_t &file);
    // plugin.json of a folder; false — missing or unreadable.
    static bool readManifest(const path_t &file, PluginManifest &out);
    // True when the folder is a dev plugin link (a symbolic link, or the dev marker of the store).
    static bool isDevLink(const path_t &folder);

private:
    void scanFolder(const path_t &folder, bool builtIn);

    std::vector<InstalledPlugin> m_plugins;
    std::vector<EditorTheme> m_themes;
};

} // namespace Panda
