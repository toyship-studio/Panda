#include "App/DiskWatch.hpp"

#include "App/EditorLayer.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Foundation/Logger.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Panda {

namespace {

    // A file is written in pieces and a program writes several files at a go: the changes are
    // taken when nothing more was told for this long…
    constexpr double QUIET = 0.25;
    // …and no later than this after the first of them: changes that never rest are taken in portions.
    constexpr double LONGEST = 2.0;
    // How often the wait for the unsaved documents looks at them.
    constexpr double REOPEN_LOOK = 0.5;

    path_t realPath(const path_t &path) {
        std::error_code error;
        const path_t real = std::filesystem::weakly_canonical(path, error);
        return error ? path : real;
    }

    // What programs keep next to a file while they work on it: nothing of the project.
    bool isScrap(const path_t &path) {
        const std::string name = path.filename().string();
        return name.empty() || name.front() == '.' || name.back() == '~';
    }

    bool isUnder(const path_t &path, const path_t &folder) {
        const auto &text = path.native();
        const auto &root = folder.native();
        return text.size() > root.size() && text.compare(0, root.size(), root) == 0 &&
               text[root.size()] == path_t::preferred_separator;
    }

} // namespace

DiskWatch::DiskWatch(EditorLayer &app)
    : m_app(app) {}

DiskWatch::~DiskWatch() = default;

void DiskWatch::watch(const path_t &projectPath) {
    m_watcher.reset();
    m_waiting = false;
    m_newFiles = false;
    m_keymapChanged = false;
    m_projectSettingsChanged = false;
    m_reopenWaits = false;
    m_assets.clear();
    m_projectSettings.clear();
    m_keymap.clear();
    std::vector<std::filesystem::path> paths;
    // Of the editor: its settings (a document like any other) and its keymap.
    if (const path_t settings = m_app.documents().editorSettingsFile().path(); !settings.empty()) { paths.push_back(settings); }
    if (const path_t &keymap = m_app.commands().keymapPath(); !keymap.empty()) {
        paths.push_back(keymap);
        m_keymap = realPath(keymap);
    }
    // Of the project: what its documents and its registry read.
    if (!projectPath.empty()) {
        m_assets = realPath(projectPath / "Assets");
        m_projectSettings = realPath(m_app.documents().projectSettingsFile().path());
        paths.push_back(projectPath / "Assets");
        paths.push_back(m_app.documents().projectSettingsFile().path());
        paths.push_back(m_app.project().scriptsLibraryPath());
    }
    if (!paths.empty()) { m_watcher = std::make_unique<Fern::FileWatcher>(paths); }
}

void DiskWatch::note(const Fern::FileWatcher::Changes &changes) {
    bool any = changes.everything;
    bool newFiles = changes.everything && !m_assets.empty();
    bool keymap = changes.everything;
    bool projectSettings = changes.everything && !m_projectSettings.empty();
    AssetHandlerEditor *assets = AssetHandlerEditor::current();
    for (const std::filesystem::path &path : changes.paths) {
        if (isScrap(path)) { continue; }
        if (path == m_keymap) {
            any = keymap = true;
            continue;
        }
        if (path == m_projectSettings) { projectSettings = true; }
        if (m_assets.empty() || !isUnder(path, m_assets)) {
            any = true; // the settings of the editor, project.json, the script library: documents
            continue;
        }
        // What the change is to the asset registry, by its own rules: the content of a file is
        // seen by the documents by its time; news of the tree makes the registry read it again.
        switch (assets != nullptr ? assets->classifyDiskChange(path) : AssetHandlerEditor::DiskChange::Tree) {
            case AssetHandlerEditor::DiskChange::None: break;
            case AssetHandlerEditor::DiskChange::Content: any = true; break;
            case AssetHandlerEditor::DiskChange::Tree: any = newFiles = true; break;
        }
    }
    if (!any) { return; }
    if (!m_waiting) { m_sinceFirst = 0.0; }
    m_waiting = true;
    m_newFiles = m_newFiles || newFiles;
    m_keymapChanged = m_keymapChanged || keymap;
    m_projectSettingsChanged = m_projectSettingsChanged || projectSettings;
    m_sinceLast = 0.0;
}

void DiskWatch::update(double deltaTime) {
    if (!m_watcher) { return; }
    m_sinceLast += deltaTime;
    m_sinceFirst += deltaTime;
    Fern::FileWatcher::Changes changes;
    if (m_watcher->take(changes)) { note(changes); }
    if (!m_inFront || m_app.isPlaying() || m_pointerHeld || m_app.isModalOpen()) { return; }
    if (m_waiting && (m_sinceLast >= QUIET || m_sinceFirst >= LONGEST)) {
        const bool newFiles = m_newFiles;
        const bool keymap = m_keymapChanged;
        const bool projectSettings = m_projectSettingsChanged;
        m_waiting = false;
        m_newFiles = false;
        m_keymapChanged = false;
        m_projectSettingsChanged = false;
        if (keymap && m_app.commands().reloadKeymap()) { LOG_INFO_EDITOR("Reload: the keymap"); }
        m_app.documentActions().takeDiskChanges(newFiles);
        if (projectSettings) {
            // Its modules: one switched on opens the project again (below), one switched off
            // stays until a restart, and the log says so.
            m_reopenWaits = m_app.isReopenNeededForModules();
            m_reopenSaid = false;
            m_sinceReopenLook = REOPEN_LOOK; // the first look is now
            if (!m_reopenWaits) { m_app.warnOfModulesSwitchedOff(); }
        }
    }
    if (m_reopenWaits) { reopenForModules(deltaTime); }
}

void DiskWatch::reopenForModules(double deltaTime) {
    m_sinceReopenLook += deltaTime;
    if (m_sinceReopenLook < REOPEN_LOOK) { return; }
    m_sinceReopenLook = 0.0;
    if (!m_app.isReopenNeededForModules()) { // the file was changed back
        m_reopenWaits = false;
        return;
    }
    if (m_app.documents().hasUnsaved()) {
        if (!m_reopenSaid) {
            LOG_INFO_EDITOR("project.json switches a module on: the project opens again when everything is saved.");
            m_reopenSaid = true;
        }
        return;
    }
    // Closing the project and opening it begins this watch anew: nothing of it is touched after.
    m_app.reopenProjectForModules();
}

} // namespace Panda
