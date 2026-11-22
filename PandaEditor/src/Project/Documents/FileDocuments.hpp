#pragma once

#include "Project/Documents/AssetFile.hpp"
#include "Project/Documents/DocumentBackups.hpp"
#include "Project/Documents/SettingsFile.hpp"
#include "Project/Documents/WorldFile.hpp"
#include "Project/Settings/EditorSettings.hpp"
#include "Project/Settings/ProjectSettings.hpp"

#include <Panda/Prefabs/Prefabs.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Panda {

class AssetHandlerEditor;

// What the file documents tell the editor (EditorLayer).
class FileDocumentsOutput {
public:
    virtual ~FileDocumentsOutput() = default;
    // Another world became the active one: the editor shows it from now on.
    virtual void documentsDidActivateWorld() = 0;
    // The active world got another content: read from its file (opened into an untouched new
    // world, reloaded), the starter content, or another file (Save As).
    virtual void documentsDidLoadWorld() = 0;
    // A settings file and its store agree again — after a read (Reload, Don't Save, a revert) or
    // after Save: the editor applies what follows (the editor scope; the shader features of the
    // project). The first read of a file is not reported: whoever opens it applies it.
    virtual void documentsDidReadSettings(SettingsScope scope) = 0;
    virtual void documentsDidWriteSettings(SettingsScope scope) = 0;
    // The pose of the editor camera: it stays with the world it was looking at.
    virtual EditorCameraSettings documentsEditorCamera() = 0;
    virtual void documentsSetEditorCamera(const EditorCameraSettings &camera) = 0;
    // The active world was never shown and brings no camera: the editor finds a view of it.
    virtual void documentsLookAtWorld() = 0;
};

// The documents of one pass of Save All, Reload or the unsaved-changes question, in the order of
// Reload. The asset documents are views made for the pass and live here.
struct FileDocumentList final {
    std::vector<std::unique_ptr<AssetFile>> assets;
    std::vector<FileDocument *> all;
};

// The one place of the file documents (FileDocument.hpp): every file the editor holds in memory.
// - `settings.json` of the user: the editor scope (SettingsFile);
// - `.Panda/project.json` of the open project: the project scope (SettingsFile);
// - the open worlds of the project, a tab each, one of them active (WorldFile);
// - the loaded assets: the asset database owns them, their documents are views (AssetFile).
// The collection owns the settings files and the worlds, and follows up their reads and writes
// for the project (the start world, the asset registry) and for the editor (FileDocumentsOutput).
// Save, Reload and the unsaved-changes question are written once over list() (EditorLayer); the
// life of the project (ProjectLoader) opens and closes what is here.
class FileDocuments final {
public:
    explicit FileDocuments(FileDocumentsOutput *output);
    ~FileDocuments();

    // Every document that needs attention, in the order of Reload: the settings (they decide how
    // the rest is read: the shader features, the modules), the assets (the worlds reference them),
    // the worlds. An asset is listed when it has unsaved edits or its file has changed.
    FileDocumentList list();
    // Some document has edits its file does not: asked often, so nothing on disk is looked at.
    bool hasUnsaved();

    // ---- The settings ----
    SettingsFile &editorSettingsFile() {
        return m_editorSettings;
    }
    SettingsFile &projectSettingsFile() {
        return m_projectSettings;
    }
    // The editor's own settings of the two scopes, as fields (the sections of the modules are in
    // the store of the file: SettingsFile::settings).
    EditorSettings &editorSettings() {
        return m_editorSettings.settings().get<EditorSettings>();
    }
    ProjectSettings &projectSettings() {
        return m_projectSettings.settings().get<ProjectSettings>();
    }

    // ---- The open worlds ----
    // In the order of their tabs. At most one is active: the editor shows and edits it, Play
    // plays its copy, the export cooks it. A world is a document like any other: a project may
    // have none open (the tabs were closed, the project has no world yet) — activeWorld() is
    // null then and the editor shows nothing.
    const std::vector<std::unique_ptr<WorldFile>> &worlds() const {
        return m_worlds;
    }
    WorldFile *activeWorld() {
        return m_activeWorld;
    }
    WorldFile *findWorld(uint32_t id);
    WorldFile *findWorld(const path_t &path); // the open world of this file
    // A file or a folder of the project moved (AssetHandlerEditor::movePath): the open worlds and
    // prefabs in it follow it, and so does the start world of project.json — the file is written at
    // once when the settings have no other unsaved edit, else the next Save writes it with them.
    void fileMoved(const path_t &from, const path_t &to);
    bool isWorldFile(const FileDocument *file) const;
    // The editor camera stays with the world it was looking at; the new active world brings its own.
    void activateWorld(WorldFile &world);
    // A world file in a tab of its own: an open one is activated, another one is read into a new
    // document. nullptr — the file could not be read; nothing changes then.
    WorldFile *openWorld(const path_t &path);
    WorldFile &newWorld(); // a new world with the starter content in a tab of its own, active
    // Drops the world from memory (the unsaved-changes question is the caller's). Its neighbor
    // becomes the active one; after the last world none is.
    void closeWorld(WorldFile &world);

    // ---- Prefabs ----

    // Apply (context/prefabs.md): the part of what the instance at `root` differs in that the
    // scope names goes into its prefab file. Apply Forced takes the same places out of the other
    // instances too — on disk in the files of the project that are not open, in memory in the
    // open documents, which wait for Save. A document of the prefab, if open, shows the new file;
    // a world whose entities got other ids (its additions became parts) starts its undo history
    // again. false — nothing was applied, the log says why.
    bool applyToPrefab(Entity root, const Prefabs::Scope &scope, bool forced);

    // ---- The asset documents ----
    std::unique_ptr<AssetFile> assetFile(AssetId id); // a loaded asset of the project; null — not loaded

    // ---- The backups of the unsaved edits (DocumentBackups) ----
    // Every open world and prefab with unsaved edits gets a copy of its text as it is now, aside
    // from its file; one without them has none. Nothing is written for a world that did not change
    // since its last copy: the editor calls this before the game code runs (Play, a script build
    // loaded) and at the pauses of the work (EditorLayer::updateBackups). Save, Reload and closing a
    // world take its copy away, and so does the end of the session. How many copies were written.
    size_t backUp();
    // The unsaved edits a session of this project left behind (it crashed, or was killed): the
    // documents they are of, as a question names them; empty — none.
    std::vector<std::string> leftBackups() const;
    // They come back as unsaved edits of their documents — the open one of the file, else a new
    // document of it (a world that had no file: a new world) — and the first becomes the active
    // world. Save writes them; a file changed on disk since is asked about there.
    void restoreLeftBackups();
    void discardLeftBackups();

    // ---- The project (ProjectLoader) ----
    const path_t &projectPath() const {
        return m_projectPath;
    }
    // project.json of the project is read. The worlds come later (openWorlds), when the project is
    // ready for them: the modules, the asset registry, the script build.
    void openProject(const path_t &projectPath);
    // The worlds of the session, else the start world of the project, else none.
    void openWorlds(const ProjectSession &session);
    // The open worlds with the camera in each, for the session of the project.
    ProjectSession session();
    // Every world goes (the editor is told it shows none), the project scope is back to the defaults.
    void closeProject();
    // The same without a word to the editor: the exit, when the editor is gone (ProjectLoader's
    // destructor; the worlds go before the asset database closes).
    void releaseProject();
    void prepareComponentTypes(); // the modules registered components: the worlds get their pools
    void updateScriptFields();    // the script build was (re)loaded: the containers follow its manifest

private:
    friend class WorldFile;    // save and reload are followed up here
    friend class SettingsFile; // reads and writes are followed up here

    bool saveWorld(WorldFile &world);
    bool reloadWorld(WorldFile &world);
    void settingsRead(SettingsFile &file);
    void settingsWritten(SettingsFile &file);

    // A new world document at the end of the list (not active): empty, or with the starter content.
    WorldFile &addWorld(bool starterContent);
    // The document of the file: the open one, or a new document. Nothing is activated.
    // nullptr — the file could not be read.
    WorldFile *loadWorld(const path_t &path);
    bool readWorld(WorldFile &world, const path_t &path); // the file → the document; the active one is shown again
    bool saveWorldAs(WorldFile &world);
    // The world becomes the active one and brings its camera (the previous one is not asked);
    // null — no world is active from now on.
    void showWorld(WorldFile *world);
    // The first world of the project that gets a file becomes its start world.
    void syncStartWorld(const WorldFile &world);
    void registerWorldAsset(const path_t &path);
    std::string projectRelative(const path_t &path) const; // forward slashes; empty — outside the project

    FileDocumentsOutput *m_output;
    DocumentBackups m_backups;
    SettingsFile m_editorSettings;
    SettingsFile m_projectSettings;
    std::vector<std::unique_ptr<WorldFile>> m_worlds;
    WorldFile *m_activeWorld = nullptr; // one of m_worlds; null — no world is open
    uint32_t m_nextWorldId = 1;
    path_t m_projectPath;
};

} // namespace Panda
