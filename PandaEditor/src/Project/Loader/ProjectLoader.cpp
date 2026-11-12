#include "ProjectLoader.hpp"
#include "Panda/Serialization/SettingsMapper.hpp"
#include "Panda/Serialization/WorldMapper.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Project/Assets/ShaderCook.hpp"
#include "Project/Modules/ProjectModules.hpp"
#include "Project/Settings/EditorSettingsTypes.hpp"
#include "Project/System/SystemTools.hpp"
#include "AssetHandlerEditor.hpp"

#include <Panda/Renderer/ShaderFeatures.hpp>

#include <fstream>
#include <sstream>

namespace Panda {

bool isSubpath(const path_t &path, const path_t &base) {
    if (path.empty() || base.empty()) { return false; }
    auto rel = std::filesystem::relative(path, base);
    return !rel.empty() && rel.native()[0] != '.';
}

namespace {

    // The editor's own files: the user data directory (not the application bundle, which an
    // update replaces). Empty — the environment has no home: the settings live in memory only.
    path_t editorDataDirectory() {
        path_t directory = SystemTools::userDataDirectory();
        if (directory.empty()) { return {}; }
        std::error_code errorCode;
        std::filesystem::create_directories(directory, errorCode);
        return errorCode ? path_t() : directory;
    }

    path_t projectDataDirectory(const path_t &projectPath) {
        path_t directory = projectPath / ".Panda";
        std::error_code errorCode;
        std::filesystem::create_directories(directory, errorCode);
        return directory;
    }

    template<typename T>
    bool readJson(const path_t &path, Rain::JsonDecoder &decoder, T &data) {
        std::ifstream file(path);
        if (!file.is_open()) { return false; }
        Rain::Decoder *decoderPtr = &decoder;
        return decoderPtr->decode(file, data);
    }

    template<typename T>
    bool writeJson(const path_t &path, Rain::JsonEncoder &encoder, const T &data) {
        std::ofstream file(path);
        if (!file.is_open()) { return false; }
        Rain::Encoder *encoderPtr = &encoder;
        encoderPtr->encode(file, data);
        return true;
    }

} // namespace

// The open world: `.world` of the project. Reload is opening the same file again.
class ProjectLoader::WorldFile final : public FileDocument {
public:
    explicit WorldFile(ProjectLoader &loader)
        : m_loader(loader) {}
    std::string title() const override {
        const path_t &path = m_loader.m_worldPath;
        return path.empty() ? "the new world" : "world \"" + path.stem().string() + "\"";
    }
    path_t path() const override {
        return m_loader.m_worldPath;
    }
    bool isDirty() const override {
        return m_loader.m_world->isChanged();
    }
    bool isChangedOnDisk() const override {
        return m_loader.m_worldStamp.isStale(m_loader.m_worldPath);
    }
    bool save() override {
        return m_loader.saveWorld();
    }
    bool reload() override {
        return !m_loader.m_worldPath.empty() && m_loader.openWorld(path_t(m_loader.m_worldPath));
    }

private:
    ProjectLoader &m_loader;
};

// `.Panda/project.json`: the project scope of the settings.
class ProjectLoader::ProjectSettingsFile final : public FileDocument {
public:
    explicit ProjectSettingsFile(ProjectLoader &loader)
        : m_loader(loader) {}
    std::string title() const override {
        return "the project settings";
    }
    path_t path() const override {
        return m_loader.projectSettingsPath();
    }
    bool isDirty() const override {
        return m_loader.m_projectSettingsDirty;
    }
    bool isChangedOnDisk() const override {
        return m_loader.m_projectSettingsStamp.isStale(path());
    }
    bool save() override {
        return m_loader.saveProjectSettings();
    }
    bool reload() override {
        return m_loader.reloadProjectSettings();
    }

private:
    ProjectLoader &m_loader;
};

// `settings.json` of the user: the editor scope. Every edit is written at once, so the memory is
// never ahead of the file.
class ProjectLoader::EditorSettingsFile final : public FileDocument {
public:
    explicit EditorSettingsFile(ProjectLoader &loader)
        : m_loader(loader) {}
    std::string title() const override {
        return "the editor settings";
    }
    path_t path() const override {
        return m_loader.editorSettingsPath();
    }
    bool isDirty() const override {
        return false;
    }
    bool isChangedOnDisk() const override {
        return m_loader.m_editorSettingsStamp.isStale(path());
    }
    bool save() override {
        m_loader.saveEditorSettings();
        return true;
    }
    bool reload() override {
        return m_loader.reloadEditorSettings();
    }

private:
    ProjectLoader &m_loader;
};

ProjectLoader::ProjectLoader(World *world, ProjectLoaderOutput *output)
    : m_output(output)
    , m_world(world)
    , m_jsonEncoder(true)
    , m_jsonDecoder()
    , m_editorSettings()
    , m_session()
    , m_projectSettings()
    , m_projectSession()
    , m_projectPath()
    , m_worldPath()
    , m_assetHandler(nullptr)
    , m_worldFile(std::make_unique<WorldFile>(*this))
    , m_projectSettingsFile(std::make_unique<ProjectSettingsFile>(*this))
    , m_editorSettingsFile(std::make_unique<EditorSettingsFile>(*this)) {
    registerEditorSettingsTypes(GameContext::getExtensions().settings);
    SettingsMapper::reset(SettingsScope::Editor, m_editorSettings);
    SettingsMapper::reset(SettingsScope::Project, m_projectSettings);
    GameContext::setScriptEngine(&m_scriptEngine);
    m_assetHandler = static_cast<AssetHandlerEditor *>(GameContext::getAssetHandler());
    StaticResources::initStaticResources();
}

ProjectLoader::~ProjectLoader() {
    saveSession();
    saveEditorSettings(false);
    saveProjectSettings(false);
    m_assetHandler->closeProject();
    Panda::StaticResources::deinitStaticResources();
    GameContext::setScriptEngine(nullptr);
}

void ProjectLoader::loadInitialData() {
    const path_t directory = editorDataDirectory();
    if (directory.empty()) {
        LOG_WARN("NO USER DATA DIRECTORY: THE EDITOR SETTINGS ARE NOT PERSISTED");
    } else {
        std::ifstream settings(directory / "settings.json");
        if (settings.is_open()) {
            if (!SettingsMapper::load(SettingsScope::Editor, m_editorSettings, m_jsonDecoder, settings)) {
                LOG_ERROR("EDITOR SETTINGS FILE IS BROKEN: %s", (directory / "settings.json").string().c_str());
            }
        } else {
            LOG_INFO("EDITOR SETTINGS NOT FOUND");
        }
        m_editorSettingsStamp.sync(directory / "settings.json");
        if (!readJson(directory / "session.json", m_jsonDecoder, m_session)) { m_session = EditorSession(); }
    }
    if (m_session.hasOpenedProject) { loadRecentProject(); }
}

void ProjectLoader::saveSession() {
    const path_t directory = editorDataDirectory();
    if (directory.empty()) { return; }
    m_session.hasOpenedProject = !m_projectPath.empty();
    if (!writeJson(directory / "session.json", m_jsonEncoder, m_session)) {
        LOG_ERROR("CANNOT WRITE THE EDITOR SESSION AT %s", (directory / "session.json").string().c_str());
    }
}

void ProjectLoader::saveEditorSettings(bool overwriteDiskEdits) {
    const path_t path = editorSettingsPath();
    if (path.empty()) { return; }
    if (!overwriteDiskEdits && m_editorSettingsStamp.isStale(path)) {
        // The file is the contract: an edit made outside the editor and not taken by Reload yet is
        // not overwritten by a save nobody asked for.
        LOG_WARN_EDITOR("EDITOR SETTINGS: settings.json was changed on disk, the editor's copy is not written");
        return;
    }
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("CANNOT WRITE THE EDITOR SETTINGS AT %s", path.string().c_str());
        return;
    }
    SettingsMapper::save(SettingsScope::Editor, m_editorSettings, m_jsonEncoder, file);
    file.close();
    m_editorSettingsStamp.sync(path);
}

bool ProjectLoader::reloadEditorSettings() {
    const path_t path = editorSettingsPath();
    std::ifstream file(path);
    if (!file.is_open()) { return false; }
    // A broken file leaves the scope as it is: decoded into a scratch store first.
    TypeContext scratch;
    SettingsMapper::reset(SettingsScope::Editor, scratch);
    if (!SettingsMapper::load(SettingsScope::Editor, scratch, m_jsonDecoder, file)) {
        LOG_ERROR_EDITOR("EDITOR SETTINGS FILE IS BROKEN: %s", path.string().c_str());
        return false;
    }
    SettingsMapper::copy(SettingsScope::Editor, scratch, m_editorSettings);
    m_editorSettingsStamp.sync(path);
    m_output->loaderDidReloadEditorSettings();
    return true;
}

path_t ProjectLoader::editorSettingsPath() const {
    const path_t directory = editorDataDirectory();
    return directory.empty() ? path_t() : directory / "settings.json";
}

path_t ProjectLoader::projectSettingsPath() const {
    return m_projectPath.empty() ? path_t() : projectDataDirectory(m_projectPath) / "project.json";
}

void ProjectLoader::saveWindowState(LastOpenedProjectWindowState state) {
    m_session.windowState = state;
}

void ProjectLoader::saveDockLayout(DockLayoutState state) {
    m_session.dockLayout = std::move(state);
}

const DockLayoutState &ProjectLoader::getDockLayout() {
    return m_session.dockLayout;
}

void ProjectLoader::loadRecentProject() {
    if (m_session.recentProjects.empty()) { return; }
    auto &recentProject = m_session.recentProjects.front();
    openProject(recentProject.path);
}

void ProjectLoader::openProject(const path_t &path) {
    if (!std::filesystem::exists(path)) {
        LOG_INFO("PROJECT NOT FOUND AT PATH %s", path.string().c_str());
        return;
    }
    if (hasOpenedProject()) {
        // Project switch: save the settings of the old project and release its world BEFORE the old
        // script build is unloaded in reloadScriptsDll — otherwise script instances would be
        // destroyed by the code of an already unloaded library. The registry of the old project is
        // saved and closed by the handler's closeProject; the UI is not notified (no flicker of the
        // start screen) — the panels are updated by loaderDidLoadProject of the new project.
        saveProjectSettings(false);
        m_world->clear();
        m_assetHandler->closeProject();
    }
    m_projectPath = path;
    path_t projectName = path.filename();
    m_worldPath.clear();
    const path_t dataDirectory = projectDataDirectory(m_projectPath);
    // The sections of the project scope and the project session.
    {
        SettingsMapper::reset(SettingsScope::Project, m_projectSettings);
        std::ifstream file(dataDirectory / "project.json");
        if (!file.is_open()) {
            LOG_INFO("PROJECT SETTINGS NOT FOUND");
        } else if (!SettingsMapper::load(SettingsScope::Project, m_projectSettings, m_jsonDecoder, file)) {
            LOG_ERROR_EDITOR("PROJECT SETTINGS FILE IS BROKEN: %s", (dataDirectory / "project.json").string().c_str());
        }
        if (!readJson(dataDirectory / "session.json", m_jsonDecoder, m_projectSession)) {
            m_projectSession = ProjectSession();
        }
    }
    m_projectSettingsStamp.sync(projectSettingsPath());
    m_projectSettingsDirty = false;
    // The module set of the project (ProjectModules): the contributions are needed before the asset
    // registry and the world. Engine parts are not unloaded — the modules of the previous project
    // stay, new ones are added; the live world gets pools for the new component types.
    if (ProjectModules::load(m_projectSettings.get<ProjectModuleSettings>()) > 0) {
        m_world->prepareComponentTypes();
        // The sections of the new modules were read as foreign values: they get their types now.
        SettingsMapper::adoptForeign(SettingsScope::Project, m_projectSettings);
        SettingsMapper::adoptForeign(SettingsScope::Editor, m_editorSettings);
    }
    m_output->setEditorCameraSettings(m_projectSession.camera);
    applyShaderFeatures();
    // First switch the asset registry and the scripts to the new project, then notify the UI:
    // loaderDidLoadProject rescans the asset panels — they must already see the NEW registry.
    finalizeOpenedProject();
    // The scene shaders of the engine for the project's shader features (the project shaders
    // compile for them as they load).
    ShaderCook::cookAndReloadSceneShaders(m_projectPath);
    m_output->loaderDidLoadProject(projectName.string(), m_projectPath);
    LOG_INFO("LOADED PROJECT AT PATH %s", m_projectPath.string().c_str());
    // The world of the last session, else the start world of the project, else a new one.
    const std::string &lastWorld = m_projectSession.lastWorld;
    const std::string &startWorld = m_projectSettings.get<ProjectWorldSettings>().startWorld;
    if (!lastWorld.empty() && std::filesystem::exists(m_projectPath / lastWorld)) {
        loadWorld(m_projectPath / lastWorld);
        return;
    }
    if (!startWorld.empty()) {
        loadWorld(m_projectPath / startWorld);
        return;
    }
    createDefaultWorld();
}

namespace {

    // .Panda/scripts-manifest.json — an export of the script build's reflection for external
    // tools (AI agent, code editor): class names, fields, their types and fieldId —
    // without reading C++ and computing the FNV hash by hand. Written on every (re)load of
    // the scripts; the engine does NOT read it — the file is outbound only.
    struct ScriptFieldManifestDto : public Rain::Codable {
        uint32_t fieldId = 0;
        std::string name;
        std::string type;

        RAIN_FIELDS_BEGIN(ScriptFieldManifestDto)
        RAIN_FIELD(fieldId)
        RAIN_FIELD(name)
        RAIN_FIELD(type)
        RAIN_FIELDS_END
    };

    struct ScriptClassManifestDto : public Rain::Codable {
        std::string name;
        // The key name = scriptFields, as in .world (and "fields" is reserved by Rain).
        std::vector<ScriptFieldManifestDto> scriptFields;

        RAIN_FIELDS_BEGIN(ScriptClassManifestDto)
        RAIN_FIELD(name)
        RAIN_FIELD(scriptFields)
        RAIN_FIELDS_END
    };

    struct ScriptsManifestDto : public Rain::Codable {
        std::vector<ScriptClassManifestDto> scripts;

        RAIN_FIELDS_BEGIN(ScriptsManifestDto)
        RAIN_FIELD(scripts)
        RAIN_FIELDS_END
    };

    const char *scriptFieldTypeName(Scripting::ScriptFieldType type) {
        switch (type) {
            case Scripting::ScriptFieldType::INTEGER: return "integer";
            case Scripting::ScriptFieldType::FLOAT: return "float";
            case Scripting::ScriptFieldType::ENTITY: return "entity";
            case Scripting::ScriptFieldType::TEXTURE: return "texture";
            case Scripting::ScriptFieldType::MATERIAL: return "material";
            case Scripting::ScriptFieldType::WORLD: return "world";
            case Scripting::ScriptFieldType::FONT: return "font";
            case Scripting::ScriptFieldType::UNKNOWN: break;
        }
        return "unknown";
    }

    void writeScriptsManifest(
        const path_t &path, const Panda::ScriptBundleManifest &manifest, Rain::JsonEncoder &encoder
    ) {
        ScriptsManifestDto dto;
        dto.scripts.reserve(manifest.classes.size());
        for (const auto &scriptClass : manifest.classes) {
            ScriptClassManifestDto &classDto = dto.scripts.emplace_back();
            classDto.name = scriptClass.name;
            for (const auto &field : scriptClass.fields) {
                classDto.scriptFields.push_back(
                    {{}, field.handle, field.name, scriptFieldTypeName(field.type)}
                );
            }
        }
        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("CANNOT WRITE SCRIPTS MANIFEST AT PATH %s", path.string().c_str());
            return;
        }
        Rain::Encoder *encoderPtr = &encoder;
        encoderPtr->encode(file, dto);
    }

} // namespace

ScriptEngineConfig ProjectLoader::makeScriptsConfig() {
    const std::string projectName = m_projectPath.filename().string();
    return {
        m_projectPath / "Scripts" / projectName / "bin",
        projectName,
        m_projectPath / ".Panda" / "ScriptCache"
    };
}

bool ProjectLoader::isScriptsChangedOnDisk() {
    if (!hasOpenedProject()) { return false; }
    std::error_code errorCode;
    const auto diskTime =
        std::filesystem::last_write_time(ScriptEngine::libraryFilePath(makeScriptsConfig()), errorCode);
    if (errorCode) { return false; } // no library — nothing to load
    // The library exists but is not loaded (it was absent at startup) — there is something to pick up.
    if (!m_scriptEngine.isLoaded()) { return true; }
    return diskTime > m_scriptsDllTime;
}

void ProjectLoader::reloadScriptsDll() {
    const ScriptEngineConfig config = makeScriptsConfig();
    m_scriptEngine.reload(config);
    {
        // Even on failure: do not hammer the same broken library with every Reload.
        std::error_code errorCode;
        const auto diskTime =
            std::filesystem::last_write_time(ScriptEngine::libraryFilePath(config), errorCode);
        if (!errorCode) { m_scriptsDllTime = diskTime; }
    }
    if (m_scriptEngine.isLoaded()) {
        m_world->updateScriptsAndFields();
        writeScriptsManifest(
            m_projectPath / ".Panda" / "scripts-manifest.json", m_scriptEngine.getManifest(),
            m_jsonEncoder
        );
        LOG_INFO_EDITOR("SUCCESSFULLY LOADED SCRIPTING ASSEMBLY");
    } else {
        LOG_ERROR_EDITOR("ERROR LOADING SCRIPTING ASSEMBLY");
    }
}

bool ProjectLoader::hasOpenedProject() {
    return !m_projectPath.empty();
}

const path_t &ProjectLoader::getOpenedProjectPath() {
    return m_projectPath;
}

const path_t &ProjectLoader::getOpenedWorldPath() {
    return m_worldPath;
}

void ProjectLoader::createProject(const std::string &name, const path_t &path) {
    m_projectCreator.createProject(name, path);
    openProject(path);
}

void ProjectLoader::openCppProject() {
    std::string name = m_projectPath.filename().string();
    SystemTools::openCppProject(m_projectPath / "Scripts" / name);
}

void ProjectLoader::showCppProject() {
    std::string name = m_projectPath.filename().string();
    SystemTools::open(m_projectPath / "Scripts" / name);
}

void ProjectLoader::appendRecentProject() {
    if (m_projectPath.empty()) { return; }
    RecentProject recentProject;
    recentProject.path = m_projectPath.generic_string();
    recentProject.name = m_projectPath.filename().string();
    auto &recentList = m_session.recentProjects;
    auto existing = std::find(recentList.begin(), recentList.end(), recentProject);
    if (existing != recentList.end()) {
        // Move existing recent project to last
        std::rotate(recentList.begin(), existing, existing + 1);
        return;
    }
    recentList.insert(recentList.begin(), recentProject);
}

void ProjectLoader::finalizeOpenedProject() {
    m_assetHandler->openProject(m_projectPath);
    reloadScriptsDll();
    appendRecentProject();
    saveSession();
}

const std::vector<RecentProject> &ProjectLoader::getRecentProjectsList() {
    return m_session.recentProjects;
}

void ProjectLoader::newWorld() {
    m_worldPath.clear();
    m_projectSession.lastWorld.clear();
    createDefaultWorld();
}

bool ProjectLoader::openWorld(const path_t &path) {
    if (path.extension() != ".world") {
        LOG_ERROR_EDITOR("Cannot open world from unsupported file: %s", path.string().c_str());
        return false;
    }
    if (!isSubpath(path, m_projectPath)) {
        LOG_ERROR_EDITOR("World must be inside the project directory: %s", path.string().c_str());
        return false;
    }
    if (!loadWorld(path)) { return false; }
    registerCurrentWorldAsset();
    return true;
}

bool ProjectLoader::saveWorld() {
    if (m_worldPath.empty()) { return saveWorldAs(); }
    if (!writeWorld(m_worldPath)) { return false; }
    syncWorldPaths();
    registerCurrentWorldAsset();
    return true;
}

bool ProjectLoader::writeWorld(const path_t &path) {
    std::ofstream file(path);
    if (file.is_open()) {
        WorldMapper::save(*m_world, m_jsonEncoder, file);
        file.close();
        m_world->setChanged(false);
        m_worldStamp.sync(path);
        return true;
    } else {
        LOG_ERROR("ERROR SAVING WORLD AT PATH %s", path.string().c_str());
        return false;
    }
}

bool ProjectLoader::loadWorld(const path_t &path) {
    if (path.empty()) {
        createDefaultWorld();
        return false;
    }
    std::ifstream file(path);
    if (file.is_open()) {
        const bool decoded = WorldMapper::load(*m_world, m_jsonDecoder, file);
        file.close();
        // Broken JSON: the current world and m_worldPath are NOT touched — fix the file and
        // repeat Reload; an accidental Save does not silently overwrite the broken file.
        if (!decoded) {
            LOG_ERROR_EDITOR("WORLD FILE IS BROKEN (JSON parse error): %s", path.string().c_str());
            return false;
        }
    } else {
        LOG_INFO("CANNOT LOAD WORLD AT PATH %s", path.string().c_str());
        m_worldPath.clear();
        m_projectSession.lastWorld.clear();
        createDefaultWorld();
        return false;
    }
    m_worldPath = path;
    m_worldStamp.sync(path);
    syncWorldPaths();
    if (m_scriptEngine.isLoaded()) { m_world->updateScriptsAndFields(); }
    LOG_INFO("LOADED WORLD AT PATH %s", m_worldPath.string().c_str());
    m_output->loaderDidLoadWorld();
    return true;
}

bool ProjectLoader::saveWorldAs() {
    std::optional<path_t> optionalPath =
        SystemTools::saveFileDialog("World\0*.world\0", (m_projectPath / "Assets").string().c_str(), "world.world");
    if (!optionalPath.has_value()) { return false; }
    path_t previousPath = m_worldPath;
    m_worldPath = optionalPath.value();
    if (m_worldPath.extension() != ".world") { m_worldPath.replace_extension(".world"); }
    if (!writeWorld(m_worldPath)) {
        m_worldPath = previousPath;
        return false;
    }
    syncWorldPaths();
    registerCurrentWorldAsset();
    m_output->loaderDidLoadWorld();
    return true;
}

void ProjectLoader::registerCurrentWorldAsset() {
    if (!std::filesystem::exists(m_worldPath)) { return; }
    if (!isSubpath(m_worldPath, m_projectPath)) { return; }
    if (m_assetHandler->getAssetId(m_worldPath)) { return; }
    if (!m_assetHandler->canImport(m_worldPath)) { return; }
    m_assetHandler->registerAsset(m_worldPath);
}

namespace {

    // Starter content of a new world: camera, sun, reference sprite, sky.
    void fillDefaultWorld(World &world) {
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
        world.setChanged(true);
        world.sort();
    }

} // namespace

void ProjectLoader::createDefaultWorld() {
    m_world->clear(); // the default settings too
    fillDefaultWorld(*m_world);
    if (m_scriptEngine.isLoaded()) { m_world->updateScriptsAndFields(); }
    m_output->loaderDidCreateNewWorld();
}

void ProjectLoader::syncWorldPaths() {
    std::string relative;
    if (isSubpath(m_worldPath, m_projectPath)) {
        // generic_string — forward slashes: the project files are portable between Windows and macOS.
        relative = std::filesystem::relative(m_worldPath, m_projectPath).generic_string();
    }
    m_projectSession.lastWorld = relative;
    ProjectWorldSettings &world = m_projectSettings.get<ProjectWorldSettings>();
    if (world.startWorld.empty()) { world.startWorld = relative; } // the first world of the project
}

void ProjectLoader::closeProject() {
    saveProjectSettings(false);
    m_assetHandler->closeProject();
    SettingsMapper::reset(SettingsScope::Project, m_projectSettings);
    m_projectSession = ProjectSession();
    m_worldPath.clear();
    m_projectPath.clear();
    saveSession();
    m_output->loaderDidLoadCloseProject();
}

void ProjectLoader::removeRecentProject(int index) {
    LOG_INFO("REMOVE PROJECT AT INDEX %d", index);
    m_session.recentProjects.erase(std::next(m_session.recentProjects.begin(), index));
}

LastOpenedProjectWindowState ProjectLoader::getLastWindowState() {
    return m_session.windowState;
}

bool ProjectLoader::saveProjectSettings(bool overwriteDiskEdits) {
    if (m_projectPath.empty()) { return false; }
    const path_t dataDirectory = projectDataDirectory(m_projectPath);
    syncWorldPaths();
    bool saved = true;
    if (!overwriteDiskEdits && m_projectSettingsStamp.isStale(projectSettingsPath())) {
        // The file is the contract: an edit made outside the editor and not taken by Reload yet
        // is not overwritten by a save nobody asked for (closing or switching the project).
        LOG_WARN_EDITOR("PROJECT SETTINGS: project.json was changed on disk, the editor's copy is not written");
    } else {
        std::ofstream file(dataDirectory / "project.json");
        if (file.is_open()) {
            SettingsMapper::save(SettingsScope::Project, m_projectSettings, m_jsonEncoder, file);
        } else {
            LOG_ERROR("CANNOT WRITE THE PROJECT SETTINGS AT %s", (dataDirectory / "project.json").string().c_str());
            saved = false;
        }
        file.close();
        m_projectSettingsStamp.sync(projectSettingsPath());
        if (saved) { m_projectSettingsDirty = false; }
    }
    m_projectSession.camera = m_output->getEditorCameraSettings();
    if (!writeJson(dataDirectory / "session.json", m_jsonEncoder, m_projectSession)) {
        LOG_ERROR("CANNOT WRITE THE PROJECT SESSION AT %s", (dataDirectory / "session.json").string().c_str());
    }
    // A saved change of the shader features recompiles the shaders of the project for the set.
    if (applyShaderFeatures()) {
        ShaderCook::cookAndReloadSceneShaders(m_projectPath);
        m_assetHandler->reloadLoadedShaders();
    }
    return saved;
}

bool ProjectLoader::reloadProjectSettings() {
    if (m_projectPath.empty()) { return false; }
    const path_t path = projectSettingsPath();
    // A broken file leaves the scope as it is: decoded into a scratch store first. A file that
    // is gone is the default settings.
    TypeContext scratch;
    SettingsMapper::reset(SettingsScope::Project, scratch);
    std::ifstream file(path);
    if (file.is_open() && !SettingsMapper::load(SettingsScope::Project, scratch, m_jsonDecoder, file)) {
        LOG_ERROR_EDITOR("PROJECT SETTINGS FILE IS BROKEN: %s", path.string().c_str());
        return false;
    }
    SettingsMapper::copy(SettingsScope::Project, scratch, m_projectSettings);
    m_projectSettingsStamp.sync(path);
    m_projectSettingsDirty = false;
    syncWorldPaths(); // the open world stays the start world when the file has none
    if (applyShaderFeatures()) {
        ShaderCook::cookAndReloadSceneShaders(m_projectPath);
        m_assetHandler->reloadLoadedShaders();
    }
    return true;
}

bool ProjectLoader::applyShaderFeatures() {
    const ProjectRenderingSettings &rendering = m_projectSettings.get<ProjectRenderingSettings>();
    std::string unknown;
    const ShaderFeatureSet features = ShaderFeatureSet::fromNames(rendering.features, &unknown);
    if (!unknown.empty()) {
        LOG_WARN_EDITOR("PROJECT SETTINGS: unknown shader features are ignored: %s", unknown.c_str());
    }
    const bool changed = features != ShaderFeatures::enabled();
    ShaderFeatures::setEnabled(features);
    return changed;
}

} // namespace Panda
