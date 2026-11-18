#include "ProjectLoader.hpp"
#include "Panda/Serialization/SettingsMapper.hpp"
#include "Panda/GameLogic/GameContext.hpp"
#include "Panda/Assets/StaticResources.hpp"
#include "Project/Assets/ShaderCook.hpp"
#include "Project/Modules/ProjectModules.hpp"
#include "Project/System/SystemTools.hpp"
#include "AssetHandlerEditor.hpp"

#include <Panda/Renderer/ShaderFeatures.hpp>

#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace Panda {

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

ProjectLoader::ProjectLoader(FileDocuments &documents, ProjectLoaderOutput *output)
    : m_documents(documents)
    , m_output(output)
    , m_jsonEncoder(true)
    , m_jsonDecoder()
    , m_session()
    , m_assetHandler(nullptr) {
    GameContext::setScriptEngine(&m_scriptEngine);
    m_assetHandler = AssetHandlerEditor::current();
    StaticResources::initStaticResources();
}

ProjectLoader::~ProjectLoader() {
    // The editor is gone by now: nothing is written (saveOnExit did it) and nobody is told. The
    // worlds go before the asset database closes — they reference its assets.
    m_documents.releaseProject();
    m_assetHandler->closeProject();
    Panda::StaticResources::deinitStaticResources();
    GameContext::setScriptEngine(nullptr);
}

void ProjectLoader::loadInitialData(bool openLastProject, const path_t &project) {
    const path_t directory = editorDataDirectory();
    if (directory.empty()) { LOG_WARN("There is no user data directory: the settings of the editor are not kept."); }
    m_documents.editorSettingsFile().open(directory.empty() ? path_t() : directory / "settings.json");
    if (!directory.empty() && !readJson(directory / "session.json", m_jsonDecoder, m_session)) {
        m_session = EditorSession();
    }
    if (!project.empty()) {
        openProject(project);
    } else if (openLastProject && m_session.hasOpenedProject) {
        loadRecentProject();
    }
}

void ProjectLoader::saveOnExit() {
    saveSession();
    m_documents.editorSettingsFile().saveUnasked();
    saveProject();
}

void ProjectLoader::saveSession() {
    const path_t directory = editorDataDirectory();
    if (directory.empty()) { return; }
    m_session.hasOpenedProject = hasOpenedProject();
    // Several instances share the file (one project per window): the recent projects another
    // instance has written since this one read the session are kept, after this instance's own.
    EditorSession onDisk;
    if (readJson(directory / "session.json", m_jsonDecoder, onDisk)) {
        for (const RecentProject &project : onDisk.recentProjects) {
            auto &recent = m_session.recentProjects;
            const bool forgotten =
                std::find(m_forgottenProjects.begin(), m_forgottenProjects.end(), project) != m_forgottenProjects.end();
            if (!forgotten && std::find(recent.begin(), recent.end(), project) == recent.end()) { recent.push_back(project); }
        }
    }
    if (!writeJson(directory / "session.json", m_jsonEncoder, m_session)) {
        LOG_ERROR("The session of the editor could not be written to %s.", (directory / "session.json").string().c_str());
    }
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
        LOG_INFO("There is no project at %s.", path.string().c_str());
        return;
    }
    if (hasOpenedProject()) {
        // Project switch: the old project goes BEFORE its script build is unloaded in
        // reloadScriptsDll — otherwise script instances would be destroyed by the code of an
        // already unloaded library. The start screen is not shown in between — the panels are
        // updated by loaderDidLoadProject of the new project.
        releaseOpenedProject();
    }
    // project.json of the project (a document) and its session.
    m_documents.openProject(path);
    if (!readJson(projectDataDirectory(path) / "session.json", m_jsonDecoder, m_projectSession)) {
        m_projectSession = ProjectSession();
    }
    // The module set of the project (ProjectModules): the contributions are needed before the asset
    // registry and the worlds. Engine parts are not unloaded — the modules of the previous project
    // stay, new ones are added; the live worlds get pools for the new component types.
    if (ProjectModules::load(m_documents.projectSettings().get<ProjectModuleSettings>()) > 0) {
        m_documents.prepareComponentTypes();
        // The sections of the new modules were read as foreign values: they get their types now.
        SettingsMapper::adoptForeign(SettingsScope::Project, m_documents.projectSettings());
        SettingsMapper::adoptForeign(SettingsScope::Editor, m_documents.editorSettings());
    }
    applyShaderFeatures();
    // First switch the asset registry and the scripts to the new project, then notify the UI:
    // loaderDidLoadProject rescans the asset panels — they must already see the NEW registry.
    finalizeOpenedProject();
    // The scene shaders of the engine for the project's shader features (the project shaders
    // compile for them as they load).
    ShaderCook::cookAndReloadSceneShaders(path);
    m_output->loaderDidLoadProject(path.filename().string(), path);
    LOG_INFO("Opened the project at %s.", path.string().c_str());
    m_documents.openWorlds(m_projectSession);
}

namespace {

    // .Panda/scripts-manifest.json — an export of the script build's reflection for external
    // tools (AI agent, code editor): class names, fields, their types and fieldId —
    // without reading C++ and computing the FNV hash by hand. Written on every (re)load of
    // the scripts; the engine does NOT read it — the file is outbound only.
    struct ScriptFieldManifestDto : public Rain::Codable {
        uint32_t fieldId = 0;
        std::string name;
        std::string type; // what one value of the field is
        bool list = false; // the field is a list: its "value" in a world is an array of them

        RAIN_FIELDS_BEGIN(ScriptFieldManifestDto)
        RAIN_FIELD(fieldId)
        RAIN_FIELD(name)
        RAIN_FIELD(type)
        RAIN_FIELD(list)
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
            case Scripting::ScriptFieldType::MESH: return "mesh";
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
                    {{}, field.handle, field.name, scriptFieldTypeName(field.type), field.list}
                );
            }
        }
        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("The scripts manifest could not be written to %s.", path.string().c_str());
            return;
        }
        Rain::Encoder *encoderPtr = &encoder;
        encoderPtr->encode(file, dto);
    }

} // namespace

ScriptEngineConfig ProjectLoader::makeScriptsConfig() {
    const std::string projectName = projectPath().filename().string();
    return {
        projectPath() / "Scripts" / projectName / "bin",
        projectName,
        projectPath() / ".Panda" / "ScriptCache"
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
    return diskTime != m_scriptsDllTime; // another build, a newer one or one put back
}

namespace {

    // The classes the sources of a script project register: REGISTER_SCRIPT(Name) → the file. The
    // build folders and what an IDE keeps next to the sources are not sources.
    std::unordered_map<std::string, path_t> readScriptSources(const path_t &scriptsFolder) {
        std::unordered_map<std::string, path_t> sources;
        std::error_code error;
        if (!std::filesystem::is_directory(scriptsFolder, error)) { return sources; }
        constexpr uintmax_t MAX_SOURCE_SIZE = 2 * 1024 * 1024;
        const std::string macro = "REGISTER_SCRIPT";
        for (auto it = std::filesystem::recursive_directory_iterator(scriptsFolder, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            const std::string name = it->path().filename().string();
            if (it->is_directory(error)) {
                if (name == "bin" || name == "build" || name.rfind("cmake-build", 0) == 0 || name.rfind('.', 0) == 0) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            const std::string extension = it->path().extension().string();
            if (extension != ".cpp" && extension != ".hpp" && extension != ".h" && extension != ".cc") { continue; }
            if (it->file_size(error) > MAX_SOURCE_SIZE) { continue; }
            std::ifstream file(it->path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            for (size_t at = text.find(macro); at != std::string::npos; at = text.find(macro, at + macro.size())) {
                size_t cursor = at + macro.size();
                while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) { cursor++; }
                if (cursor >= text.size() || text[cursor] != '(') { continue; }
                cursor++;
                while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) { cursor++; }
                size_t end = cursor;
                while (end < text.size() &&
                       (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_' || text[end] == ':')) {
                    end++;
                }
                if (end == cursor) { continue; }
                // The engine knows a class by its bare name; the first file that registers it stands.
                std::string className = text.substr(cursor, end - cursor);
                if (const size_t scope = className.rfind("::"); scope != std::string::npos) {
                    className = className.substr(scope + 2);
                }
                sources.emplace(std::move(className), it->path());
            }
        }
        return sources;
    }

} // namespace

path_t ProjectLoader::scriptSource(const std::string &className) {
    if (!hasOpenedProject()) { return {}; }
    if (!m_scriptSourcesRead) {
        m_scriptSources = readScriptSources(projectPath() / "Scripts");
        m_scriptSourcesRead = true;
    }
    const auto found = m_scriptSources.find(className);
    return found == m_scriptSources.end() ? path_t() : found->second;
}

void ProjectLoader::reloadScriptsDll() {
    m_scriptSourcesRead = false; // a build was made: the sources may have changed with it
    const ScriptEngineConfig config = makeScriptsConfig();
    {
        // A project whose scripts were never built is not broken: there is nothing to load yet.
        // One line says what to do; the engine is not asked for a library that is not there.
        std::error_code errorCode;
        if (!std::filesystem::exists(ScriptEngine::libraryFilePath(config), errorCode)) {
            if (!m_scriptEngine.isLoaded()) {
                LOG_WARN_EDITOR(
                    "The scripts of the project are not built yet, so the game cannot play. Build them in the CMake "
                    "panel (Scripts, Build): the editor loads them when the build is done."
                );
            }
            return;
        }
    }
    m_scriptEngine.reload(config);
    {
        // Even on failure: do not hammer the same broken library with every Reload.
        std::error_code errorCode;
        const auto diskTime =
            std::filesystem::last_write_time(ScriptEngine::libraryFilePath(config), errorCode);
        if (!errorCode) { m_scriptsDllTime = diskTime; }
    }
    if (m_scriptEngine.isLoaded()) {
        m_documents.updateScriptFields();
        writeScriptsManifest(
            projectPath() / ".Panda" / "scripts-manifest.json", m_scriptEngine.getManifest(),
            m_jsonEncoder
        );
        LOG_INFO_EDITOR("Scripts loaded.");
    } else {
        LOG_ERROR_EDITOR("The scripts could not be loaded: build them in the CMake panel and reload.");
    }
    m_output->loaderDidReloadScripts();
}

bool ProjectLoader::hasOpenedProject() {
    return !projectPath().empty();
}

const path_t &ProjectLoader::getOpenedProjectPath() {
    return projectPath();
}

void ProjectLoader::createProject(const std::string &name, const path_t &path) {
    m_projectCreator.createProject(name, path);
    openProject(path);
}

void ProjectLoader::appendRecentProject() {
    if (projectPath().empty()) { return; }
    RecentProject recentProject;
    recentProject.path = projectPath().generic_string();
    recentProject.name = projectPath().filename().string();
    // Opened again, it is a recent project again.
    m_forgottenProjects.erase(
        std::remove(m_forgottenProjects.begin(), m_forgottenProjects.end(), recentProject), m_forgottenProjects.end()
    );
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
    m_assetHandler->openProject(projectPath());
    reloadScriptsDll();
    appendRecentProject();
    saveSession();
}

const std::vector<RecentProject> &ProjectLoader::getRecentProjectsList() {
    return m_session.recentProjects;
}

void ProjectLoader::releaseOpenedProject() {
    m_scriptSourcesRead = false;
    m_scriptSources.clear();
    saveProject();
    m_output->loaderWillCloseProject();
    m_documents.closeProject(); // the worlds go before the assets they reference
    m_assetHandler->closeProject();
}

void ProjectLoader::closeProject() {
    releaseOpenedProject();
    m_projectSession = ProjectSession();
    saveSession();
    m_output->loaderDidLoadCloseProject();
}

void ProjectLoader::removeRecentProject(int index) {
    auto &recent = m_session.recentProjects;
    if (index < 0 || index >= static_cast<int>(recent.size())) { return; }
    m_forgottenProjects.push_back(recent[static_cast<size_t>(index)]);
    recent.erase(std::next(recent.begin(), index));
}

LastOpenedProjectWindowState ProjectLoader::getLastWindowState() {
    return m_session.windowState;
}

void ProjectLoader::saveProject() {
    if (!hasOpenedProject()) { return; }
    // project.json is written only when it is not newer than what the editor read: an edit made on
    // disk and not taken by Reload yet stays.
    m_documents.projectSettingsFile().saveUnasked();
    ProjectSession session = m_documents.session(); // the open worlds
    session.exportTarget = m_projectSession.exportTarget;
    session.exportConfiguration = m_projectSession.exportConfiguration;
    m_projectSession = std::move(session);
    const path_t sessionPath = projectDataDirectory(projectPath()) / "session.json";
    if (!writeJson(sessionPath, m_jsonEncoder, m_projectSession)) {
        LOG_ERROR("The session of the project could not be written to %s.", sessionPath.string().c_str());
    }
}

void ProjectLoader::applyProjectSettings() {
    // A changed set of shader features recompiles the shaders of the project for it.
    if (hasOpenedProject() && applyShaderFeatures()) {
        ShaderCook::cookAndReloadSceneShaders(projectPath());
        m_assetHandler->reloadLoadedShaders();
    }
}

bool ProjectLoader::applyShaderFeatures() {
    const ProjectRenderingSettings &rendering = m_documents.projectSettings().get<ProjectRenderingSettings>();
    std::string unknown;
    const ShaderFeatureSet features = ShaderFeatureSet::fromNames(rendering.features, &unknown);
    if (!unknown.empty()) {
        LOG_WARN_EDITOR("Project settings: unknown shader features are ignored: %s", unknown.c_str());
    }
    const bool changed = features != ShaderFeatures::enabled();
    ShaderFeatures::setEnabled(features);
    return changed;
}

} // namespace Panda
