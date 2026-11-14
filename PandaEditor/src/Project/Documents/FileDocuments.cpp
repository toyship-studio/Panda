#include "Project/Documents/FileDocuments.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "Project/Settings/EditorSettingsTypes.hpp"
#include "Project/System/SystemTools.hpp"

#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>

#include <filesystem>

namespace Panda {

namespace {

    AssetHandlerEditor *assetHandler() {
        return static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    }

    bool isSubpath(const path_t &path, const path_t &base) {
        if (path.empty() || base.empty()) { return false; }
        const path_t relative = std::filesystem::relative(path, base);
        return !relative.empty() && relative.native()[0] != '.';
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
        world.sort();
        ScriptEngine *scripts = GameContext::getScriptEngine();
        if (scripts && scripts->isLoaded()) { world.updateScriptsAndFields(); }
        // The starter content is not an edit: an untouched new world closes without a question
        // and gives its place to a file that is opened.
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
    // The editor always has a world to show: without a project it is this empty one. The output
    // is not told — it is being constructed and takes the world from activeWorld().
    m_activeWorld = &addWorld(/*starterContent*/ false);
}

FileDocuments::~FileDocuments() = default;

FileDocumentList FileDocuments::list() {
    FileDocumentList list;
    list.all.push_back(&m_editorSettings);
    if (m_projectPath.empty()) { return list; }
    list.all.push_back(&m_projectSettings);
    if (AssetHandlerEditor *handler = assetHandler()) {
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

/* ------------------------------ The settings ------------------------------ */

void FileDocuments::settingsRead(SettingsFile &file) {
    // A project.json without a start world gets the open one again (it is written with the next Save).
    if (&file == &m_projectSettings) { syncStartWorld(*m_activeWorld); }
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
    m_activeWorld->setCamera(m_output->documentsEditorCamera()); // the camera stays with the world it was looking at
    showWorld(world);
}

void FileDocuments::showWorld(WorldFile &world) {
    m_activeWorld = &world;
    m_output->documentsDidActivateWorld();
    if (world.hasCamera()) { m_output->documentsSetEditorCamera(world.camera()); }
}

WorldFile &FileDocuments::newWorld() {
    // An empty world (a project that had nothing to open) is filled in place.
    if (m_activeWorld->path().empty() && !m_activeWorld->isDirty() && m_activeWorld->world().isEmpty()) {
        fillStarterContent(m_activeWorld->world());
        m_output->documentsDidLoadWorld();
        return *m_activeWorld;
    }
    WorldFile &world = addWorld(/*starterContent*/ true);
    activateWorld(world);
    return world;
}

WorldFile *FileDocuments::openWorld(const path_t &path) {
    if (path.extension() != ".world") {
        LOG_ERROR_EDITOR("Cannot open world from unsupported file: %s", path.string().c_str());
        return nullptr;
    }
    if (!isSubpath(path, m_projectPath)) {
        LOG_ERROR_EDITOR("World must be inside the project directory: %s", path.string().c_str());
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
    // An untouched new world gives its place to the file; otherwise the file gets a document of
    // its own.
    const bool reuse = m_activeWorld->path().empty() && !m_activeWorld->isDirty();
    WorldFile *world = reuse ? m_activeWorld : &addWorld(/*starterContent*/ false);
    if (!readWorld(*world, path)) {
        if (!reuse) { m_worlds.pop_back(); }
        return nullptr;
    }
    return world;
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
    // The last world: a new one takes its place.
    if (m_worlds.size() == 1) { addWorld(/*starterContent*/ true); }
    // The neighbor becomes active: the next tab, the previous one after the last.
    if (&world == m_activeWorld) { showWorld(*m_worlds[index + 1 < m_worlds.size() ? index + 1 : index - 1]); }
    m_worlds.erase(m_worlds.begin() + static_cast<std::ptrdiff_t>(index));
}

bool FileDocuments::saveWorld(WorldFile &world) {
    if (world.path().empty()) { return saveWorldAs(world); }
    if (!world.write(world.path())) { return false; }
    syncStartWorld(world);
    registerWorldAsset(world.path());
    return true;
}

bool FileDocuments::saveWorldAs(WorldFile &world) {
    std::optional<path_t> chosen =
        SystemTools::saveFileDialog("World\0*.world\0", (m_projectPath / "Assets").string().c_str(), "world.world");
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
    syncStartWorld(world);
    registerWorldAsset(path);
    if (&world == m_activeWorld) { m_output->documentsDidLoadWorld(); }
    return true;
}

void FileDocuments::syncStartWorld(const WorldFile &world) {
    ProjectWorldSettings &settings = m_projectSettings.settings().get<ProjectWorldSettings>();
    if (settings.startWorld.empty()) { settings.startWorld = projectRelative(world.path()); }
}

void FileDocuments::registerWorldAsset(const path_t &path) {
    AssetHandlerEditor *handler = assetHandler();
    if (!handler || !std::filesystem::exists(path) || !isSubpath(path, m_projectPath)) { return; }
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
    AssetHandlerEditor *handler = assetHandler();
    if (!handler || !handler->isLoaded(id) || !handler->getRegistry().contains(id)) { return nullptr; }
    return std::make_unique<AssetFile>(*handler, id, handler->isAssetChangedOnDisk(id));
}

/* ------------------------------ The project ------------------------------ */

void FileDocuments::openProject(const path_t &projectPath) {
    m_projectPath = projectPath;
    m_projectSettings.open(projectPath / ".Panda" / "project.json");
}

void FileDocuments::openWorlds(const ProjectSession &session) {
    WorldFile *active = nullptr;
    for (const SessionWorld &entry : session.worlds) {
        const path_t path = m_projectPath / entry.path;
        if (entry.path.empty() || !std::filesystem::exists(path)) { continue; }
        WorldFile *world = loadWorld(path);
        if (world == nullptr) { continue; }
        world->setCamera(entry.camera);
        // The first one is read into the empty world, which is already shown.
        if (world == m_activeWorld) { m_output->documentsSetEditorCamera(entry.camera); }
        if (entry.path == session.activeWorld) { active = world; }
    }
    if (m_activeWorld->path().empty()) { // the session had nothing to open
        const std::string &startWorld = m_projectSettings.settings().get<ProjectWorldSettings>().startWorld;
        const path_t startPath = startWorld.empty() ? path_t() : m_projectPath / startWorld;
        if (!startPath.empty() && !std::filesystem::exists(startPath)) {
            LOG_WARN_EDITOR("The start world of the project does not exist: %s (project.json, \"world\")", startWorld.c_str());
        }
        if (startPath.empty() || !std::filesystem::exists(startPath) || loadWorld(startPath) == nullptr) { newWorld(); }
    }
    if (active != nullptr && active != m_activeWorld) { showWorld(*active); }
}

ProjectSession FileDocuments::session() {
    const EditorCameraSettings camera = m_output->documentsEditorCamera();
    m_activeWorld->setCamera(camera);
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
    // The editor is shown the empty world before the others go: nothing of it points into a world
    // that is gone.
    std::vector<std::unique_ptr<WorldFile>> gone = std::move(m_worlds);
    m_worlds.clear();
    showWorld(addWorld(/*starterContent*/ false));
    m_projectSettings.open({});
    m_projectPath.clear();
}

void FileDocuments::releaseProject() {
    m_activeWorld = nullptr;
    m_worlds.clear();
    m_activeWorld = &addWorld(/*starterContent*/ false);
    m_projectSettings.open({});
    m_projectPath.clear();
}

void FileDocuments::prepareComponentTypes() {
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        world->world().prepareComponentTypes();
    }
}

void FileDocuments::updateScriptFields() {
    for (const std::unique_ptr<WorldFile> &world : m_worlds) {
        world->world().updateScriptsAndFields();
    }
}

} // namespace Panda
