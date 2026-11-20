#include "Project/Documents/FileDocuments.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Settings/EditorSettingsTypes.hpp"
#include "Project/System/SystemTools.hpp"

#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/Prefabs/Prefabs.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Panda {

namespace {


    bool isSubpath(const path_t &path, const path_t &base) {
        if (path.empty() || base.empty()) { return false; }
        std::error_code error;
        const path_t relative = std::filesystem::relative(path, base, error);
        // Inside the folder: the way there does not go up and is not the folder itself.
        return !error && !relative.empty() && relative != "." && *relative.begin() != "..";
    }

    // Starter content of a new world: camera, sun, reference sprite, sky.
    void fillStarterContent(World &world) {
        Entity camera = world.instantiateEntity();
        camera.setName("Camera");
        camera.getTransform().setPosition({0.f, 0.f, 4.f});
        camera.addComponent<CameraComponent>();

        Entity sun = world.instantiateEntity();
        sun.setName("Directional Light");
        sun.getTransform().setRotationEuler({-40.f, -30.f, 0.f});
        sun.addComponent<DirectionalLightComponent>().intensity = 1.0f;

        Entity sprite = world.instantiateEntity();
        sprite.setName("Orange Sprite");
        sprite.addComponent<SpriteRendererComponent>().color = {1.0f, 0.5f, 0.2f, 1.0f};

        Entity sky = world.instantiateEntity();
        sky.setName("Sky");
        sky.addComponent<SkyComponent>();
        ScriptEngine *scripts = GameContext::getScriptEngine();
        if (scripts && scripts->isLoaded()) { world.updateScriptsAndFields(); }
        // The starter content is not an edit: an untouched new world closes without a question.
        world.setChanged(false);
    }

} // namespace

FileDocuments::FileDocuments(FileDocumentsOutput *output)
    : m_output(output)
    , m_editorSettings(*this, SettingsScope::Editor, "the editor settings")
    , m_projectSettings(*this, SettingsScope::Project, "the project settings") {
    // The sections of the editor's own files (the editor and the project scopes) before the stores
    // are made; the defaults hold until the files are opened.
    registerEditorSettingsTypes(GameContext::getExtensions().settings);
    m_editorSettings.open({});
    m_projectSettings.open({});
}

FileDocuments::~FileDocuments() = default;

FileDocumentList FileDocuments::list() {
    FileDocumentList list;
    list.all.push_back(&m_editorSettings);
    if (m_projectPath.empty()) { return list; }
    list.all.push_back(&m_projectSettings);
    if (AssetHandlerEditor *handler = AssetHandlerEditor::current()) {
        for (AssetId id : handler->loadedProjectAssets()) {
            const bool changedOnDisk = handler->isAssetChangedOnDisk(id);
            if (!changedOnDisk && !handler->documents().isAssetChanged(id)) { continue; }
            list.assets.push_back(std::make_unique<AssetFile>(*handler, id, changedOnDisk));
            list.all.push_back(list.assets.back().get());
        }
    }
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        list.all.push_back(world.get());
    }
    return list;
}

bool FileDocuments::hasUnsaved() {
    if (m_editorSettings.isDirty()) { return true; }
    if (m_projectPath.empty()) { return false; }
    if (m_projectSettings.isDirty()) { return true; }
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        if (world->isDirty()) { return true; }
    }
    if (AssetHandlerEditor *handler = AssetHandlerEditor::current()) {
        for (AssetId id : handler->loadedProjectAssets()) {
            if (handler->documents().isAssetChanged(id)) { return true; }
        }
    }
    return false;
}

/* ------------------------------ The settings ------------------------------ */

void FileDocuments::settingsRead(SettingsFile &file) {
    // A project.json without a start world gets the open one again (it is written with the next Save).
    if (&file == &m_projectSettings && m_activeWorld != nullptr) { syncStartWorld(*m_activeWorld); }
    m_output->documentsDidReadSettings(file.scope());
}

void FileDocuments::settingsWritten(SettingsFile &file) {
    m_output->documentsDidWriteSettings(file.scope());
}

/* ------------------------------ The open worlds ------------------------------ */

WorldFile *FileDocuments::findWorld(uint32_t id) {
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        if (world->id() == id) { return world.get(); }
    }
    return nullptr;
}

WorldFile *FileDocuments::findWorld(const path_t &path) {
    if (path.empty()) { return nullptr; }
    // One document per file, however the path is spelled (a dialog, the asset registry and the
    // session each build it their own way).
    const path_t normal = path.lexically_normal();
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        if (world->path().empty()) { continue; }
        std::error_code errorCode;
        if (world->path().lexically_normal() == normal || std::filesystem::equivalent(world->path(), path, errorCode)) {
            return world.get();
        }
    }
    return nullptr;
}

bool FileDocuments::isWorldFile(const FileDocument *file) const {
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        if (world.get() == file) { return true; }
    }
    return false;
}

WorldFile &FileDocuments::addWorld(bool starterContent) {
    m_worlds.push_back(std::make_unique<WorldFile>(*this, m_nextWorldId++));
    WorldFile &world = *m_worlds.back();
    if (starterContent) { fillStarterContent(world.world()); }
    return world;
}

void FileDocuments::activateWorld(WorldFile &world) {
    if (&world == m_activeWorld) { return; }
    // The camera stays with the world it was looking at.
    if (m_activeWorld != nullptr) { m_activeWorld->setCamera(m_output->documentsEditorCamera()); }
    showWorld(&world);
}

void FileDocuments::showWorld(WorldFile *world) {
    m_activeWorld = world;
    m_output->documentsDidActivateWorld();
    if (world == nullptr) { return; }
    if (world->hasCamera()) {
        m_output->documentsSetEditorCamera(world->camera());
    } else {
        m_output->documentsLookAtWorld();
    }
}

WorldFile &FileDocuments::newWorld() {
    WorldFile &world = addWorld(/*starterContent*/ true);
    activateWorld(world);
    return world;
}

WorldFile *FileDocuments::openWorld(const path_t &path) {
    if (path.extension() != ".world" && !WorldFile::isPrefabFile(path)) {
        LOG_ERROR_EDITOR("%s is neither a world nor a prefab and cannot be opened as one.", path.string().c_str());
        return nullptr;
    }
    if (!isSubpath(path, m_projectPath)) {
        LOG_ERROR_EDITOR("%s is outside the folder of the project and cannot be opened.", path.string().c_str());
        return nullptr;
    }
    WorldFile *world = loadWorld(path);
    if (world == nullptr) { return nullptr; }
    registerWorldAsset(path);
    activateWorld(*world);
    return world;
}

WorldFile *FileDocuments::loadWorld(const path_t &path) {
    if (WorldFile *open = findWorld(path)) { return open; }
    WorldFile &world = addWorld(/*starterContent*/ false);
    if (!readWorld(world, path)) {
        m_worlds.pop_back();
        return nullptr;
    }
    return &world;
}

bool FileDocuments::readWorld(WorldFile &world, const path_t &path) {
    if (!world.read(path)) { return false; }
    syncStartWorld(world);
    if (&world == m_activeWorld) { m_output->documentsDidLoadWorld(); }
    return true;
}

bool FileDocuments::reloadWorld(WorldFile &world) {
    return !world.path().empty() && readWorld(world, path_t(world.path()));
}

void FileDocuments::closeWorld(WorldFile &world) {
    size_t index = 0;
    while (index < m_worlds.size() && m_worlds[index].get() != &world) { ++index; }
    if (index == m_worlds.size()) { return; }
    // The neighbor becomes active — the next tab, the previous one after the last — and after the
    // last world none is. The editor is told before the world goes: nothing of it points into a
    // world that is gone.
    if (&world == m_activeWorld) {
        WorldFile *neighbor = m_worlds.size() == 1 ? nullptr : m_worlds[index + 1 < m_worlds.size() ? index + 1 : index - 1].get();
        showWorld(neighbor);
    }
    m_worlds.erase(m_worlds.begin() + static_cast<std::ptrdiff_t>(index));
}

bool FileDocuments::applyToPrefab(Entity root, const Prefabs::Scope &scope, bool forced) {
    AssetHandlerEditor *handler = AssetHandlerEditor::current();
    World *world = root.isValid() ? root.getWorld() : nullptr;
    const AssetId prefab = Prefabs::prefabOf(root);
    if (handler == nullptr || world == nullptr || prefab == AssetId(0)) { return false; }
    const path_t path = handler->documentFilePath(prefab);
    const std::string name = handler->getAssetName(prefab);
    // The prefab is written as its file has it: edits of it that are not saved yet would be lost
    // under the new file, or written with it unasked.
    WorldFile *document = findWorld(path);
    if (document != nullptr && document->isDirty()) {
        LOG_WARN_EDITOR("%s has unsaved edits: save it (or close it without saving), then apply.", name.c_str());
        return false;
    }
    const auto readText = [](const path_t &file, std::string &text) {
        std::ifstream input(file);
        if (!input.is_open()) { return false; }
        std::stringstream content;
        content << input.rdbuf();
        text = content.str();
        return true;
    };
    std::string text;
    if (!readText(path, text)) {
        LOG_ERROR_EDITOR("%s could not be read: nothing was applied.", name.c_str());
        return false;
    }
    const Prefabs::Applied applied = Prefabs::apply(root, scope, text);
    if (!applied.any()) {
        if (!applied.refused) { LOG_INFO_EDITOR("Nothing to apply: the instance differs from %s in nothing there.", name.c_str()); }
        return false;
    }
    {
        std::ofstream output(path);
        if (!output.is_open()) {
            LOG_ERROR_EDITOR("%s could not be written: nothing was applied.", name.c_str());
            return false;
        }
        output << applied.file;
    }
    // Apply Forced: the worlds and prefabs that are not open lose the same places on disk — before
    // the prefabs are read again, so that a prefab that nests this one is read as it is now.
    int rewritten = 0;
    if (forced) {
        for (const auto &[id, info] : handler->getRegistry()) {
            if ((info.type != AssetType::WORLD && info.type != AssetType::PREFAB) || id == prefab) { continue; }
            const path_t file = handler->documentFilePath(id);
            std::string content;
            std::string stripped;
            if (file.empty() || findWorld(file) != nullptr || !readText(file, content)) { continue; }
            if (!Prefabs::stripApplied(applied, content, stripped)) { continue; }
            std::ofstream(file) << stripped;
            rewritten++;
        }
    }
    // The open documents follow the prefab (and lose the places, when forced) in memory.
    handler->prefabChanged(prefab, forced ? &applied : nullptr);
    if (document != nullptr) { reloadWorld(*document); }
    world->setChanged(); // its file holds the difference the instance has no more
    if (applied.reidentified) { world->getCommandHistory().clear(); }
    if (rewritten > 0) {
        LOG_INFO_EDITOR(
            "Applied to %s; %d other file%s of the project lost the overrides of the same places.", name.c_str(), rewritten,
            rewritten == 1 ? "" : "s"
        );
    } else {
        LOG_INFO_EDITOR("Applied to %s.", name.c_str());
    }
    return true;
}

bool FileDocuments::saveWorld(WorldFile &world) {
    if (world.path().empty()) { return saveWorldAs(world); }
    if (!world.write(world.path())) { return false; }
    syncStartWorld(world);
    registerWorldAsset(world.path());
    // A saved prefab reaches its instances in the open worlds at once.
    AssetHandlerEditor *handler = AssetHandlerEditor::current();
    if (world.isPrefab() && handler != nullptr) {
        if (const AssetId prefab = handler->getAssetId(world.path())) { handler->prefabChanged(prefab); }
    }
    return true;
}

bool FileDocuments::saveWorldAs(WorldFile &world) {
    const path_t assets = m_projectPath / AssetHandlerEditor::ASSETS_FOLDER;
    std::optional<path_t> chosen = SystemTools::saveFileDialog("World\0*.world\0", assets.string().c_str(), "world.world");
    if (!chosen.has_value()) { return false; }
    path_t path = chosen.value();
    if (path.extension() != ".world") { path.replace_extension(".world"); }
    // One document per file: a world saved over the file of another open world would leave two
    // documents of one file, each overwriting the other.
    if (WorldFile *open = findWorld(path); open != nullptr && open != &world) {
        LOG_ERROR_EDITOR("%s is open in another tab: close it or choose another name", path.filename().string().c_str());
        return false;
    }
    if (!world.write(path)) { return false; }
    if (!isSubpath(path, assets)) {
        LOG_WARN_EDITOR(
            "%s is saved outside the Assets folder of the project: it is a file the editor edits, not an asset — "
            "a script cannot be given it and the game carries it only as the world it starts in.",
            path.filename().string().c_str()
        );
    }
    syncStartWorld(world);
    registerWorldAsset(path);
    if (&world == m_activeWorld) { m_output->documentsDidLoadWorld(); }
    return true;
}

void FileDocuments::syncStartWorld(const WorldFile &world) {
    if (world.isPrefab()) { return; } // a game starts in a world
    std::string &startWorld = projectSettings().world.startWorld;
    if (startWorld.empty()) { startWorld = projectRelative(world.path()); }
}

void FileDocuments::registerWorldAsset(const path_t &path) {
    AssetHandlerEditor *handler = AssetHandlerEditor::current();
    // An asset is a file of the Assets folder (AssetHandlerEditor): a world elsewhere in the
    // project is edited like any other and nothing can refer to it.
    if (!handler || !std::filesystem::exists(path) || !isSubpath(path, handler->assetsPath())) { return; }
    if (handler->getAssetId(path) || !handler->canImport(path)) { return; }
    handler->registerAsset(path);
}

std::string FileDocuments::projectRelative(const path_t &path) const {
    if (!isSubpath(path, m_projectPath)) { return {}; }
    // generic_string — forward slashes: the project files are portable between Windows and macOS.
    return std::filesystem::relative(path, m_projectPath).generic_string();
}

/* ------------------------------ The asset documents ------------------------------ */

std::unique_ptr<AssetFile> FileDocuments::assetFile(AssetId id) {
    AssetHandlerEditor *handler = AssetHandlerEditor::current();
    if (!handler || !handler->isLoaded(id) || !handler->getRegistry().contains(id)) { return nullptr; }
    return std::make_unique<AssetFile>(*handler, id, handler->isAssetChangedOnDisk(id));
}

/* ------------------------------ The project ------------------------------ */

void FileDocuments::openProject(const path_t &projectPath) {
    m_projectPath = projectPath;
    m_projectSettings.open(projectPath / ".Panda" / "project.json");
    // The open documents are where the instances of a changed prefab are (context/prefabs.md).
    if (AssetHandlerEditor *handler = AssetHandlerEditor::current()) {
        handler->setInstanceWorlds([this] {
            std::vector<World *> worlds;
            for (const std::unique_ptr<WorldFile> &world : m_worlds) { worlds.push_back(&world->world()); }
            return worlds;
        });
    }
}

void FileDocuments::openWorlds(const ProjectSession &session) {
    WorldFile *active = nullptr;
    for (const SessionWorld &entry : session.worlds) {
        const path_t path = m_projectPath / entry.path;
        if (entry.path.empty() || !std::filesystem::exists(path)) { continue; }
        WorldFile *world = loadWorld(path);
        if (world == nullptr) { continue; }
        // A session that names a world without a camera (the one a new project comes with, one
        // written by hand): no rotation is not a pose, and the world gets the camera of a new one.
        const auto &rotation = entry.camera.rotation;
        if (rotation.x != 0.f || rotation.y != 0.f || rotation.z != 0.f || rotation.w != 0.f) { world->setCamera(entry.camera); }
        if (active == nullptr || entry.path == session.activeWorld) { active = world; }
    }
    // A session with nothing to open shows no world: a world opens from the Content Browser,
    // File → New → World makes one.
    if (active != nullptr) { showWorld(active); }
}

ProjectSession FileDocuments::session() {
    const EditorCameraSettings camera = m_output->documentsEditorCamera();
    if (m_activeWorld != nullptr) { m_activeWorld->setCamera(camera); }
    ProjectSession session;
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        SessionWorld entry;
        entry.path = projectRelative(world->path());
        if (entry.path.empty()) { continue; } // a world without a file is not a part of the session
        // A world that was never shown remembers where the camera stood when the session ended.
        entry.camera = world->hasCamera() ? world->camera() : camera;
        if (world.get() == m_activeWorld) { session.activeWorld = entry.path; }
        session.worlds.push_back(std::move(entry));
    }
    return session;
}

void FileDocuments::closeProject() {
    // The editor is shown no world before the worlds go: nothing of it points into a world that
    // is gone.
    showWorld(nullptr);
    m_worlds.clear();
    m_projectSettings.open({});
    m_projectPath.clear();
}

void FileDocuments::releaseProject() {
    m_activeWorld = nullptr;
    m_worlds.clear();
    m_projectSettings.open({});
    m_projectPath.clear();
}

void FileDocuments::prepareComponentTypes() {
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        world->world().prepareComponentTypes();
    }
}

void FileDocuments::updateScriptFields() {
    Prefabs::scriptsReloaded(); // the prefabs the instances are compared with follow the same classes
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        world->world().updateScriptsAndFields();
    }
}

} // namespace Panda
