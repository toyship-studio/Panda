#pragma once

#include <Panda/Base/Base.hpp>
#include <Panda/Renderer/ShaderFeatures.hpp>
#include <Rain/Rain.hpp>

#include <string>
#include <vector>

namespace Panda {

struct EditorCameraSettings : public Rain::Codable {
    Vec3 position;
    Quat rotation;

    RAIN_FIELDS_BEGIN(EditorCameraSettings)
    RAIN_FIELD(position)
    RAIN_FIELD(rotation)
    RAIN_FIELDS_END

    void reset() {
        position = {0, 0, 0};
        rotation = {0, 0, 0, 0};
    }
};

// `.Panda/project.json` (in version control): the settings of the project the editor owns are
// the keys of the file, each a field of ProjectSettings (EditorSettingsTypes registers them in
// the order the file is written in); the modules add their sections to the same file through
// the settings registry. Paths are relative to the project, with forward slashes.

// "mcp": the MCP server of the editor for this project (App/EditorMcp, PandaSDK/docs/zed-and-agents.md):
// an AI agent that works in the project folder looks at the editor and drives it through it. The
// port is the one the client configuration of the project names (`.mcp.json`:
// http://127.0.0.1:<port>/mcp), so it lives with the project; two projects open at once need
// different ports. Off — the editor listens to nothing.
struct ProjectMcpSettings final : public Rain::Codable {
    bool enabled = false;
    int port = 7717;

    RAIN_FIELDS_BEGIN(ProjectMcpSettings)
    RAIN_FIELD(enabled)
    RAIN_FIELD(port)
    RAIN_FIELDS_END
};

// The settings of the project the editor owns, as fields: `projectSettings().name`.
struct ProjectSettings final {
    // "name": the name of the project. What the editor calls the project (the window, the recent
    // projects, `status`), and what names the files that come from it: the folder of its scripts
    // (Scripts/<name>) and the library they build, the game the export builds (<name>.app,
    // <name>.exe) and the folder that game keeps its saves in. None of those paths is written
    // anywhere: they follow from the name, the folder of the project may be called anything. A
    // folder whose project.json does not name it is not opened (ProjectLoader::readName).
    std::string name;
    // "startWorld": the world the game starts in; it carries what that world reaches
    // (context/export.md). The editor opens it when the session has no worlds.
    std::string startWorld;
    // "shaderFeatures": the shader features of the project (ShaderFeatures.hpp). Without the key
    // — every feature of the catalog, so a project keeps its look; the template writes the lean
    // set. A change applies on save (the shaders of the project recompile); the export writes the
    // set into the game's manifest.
    std::vector<std::string> shaderFeatures = ShaderFeatureSet::all().names();
    // "disabledModules": the modules of the distribution the project switches off, by id
    // (ProjectModules); every other one is on.
    std::vector<std::string> disabledModules;
    ProjectMcpSettings mcp;
};

// A world the session had open: its file (relative to the project) and where the editor camera
// stood in it.
struct SessionWorld final : public Rain::Codable {
    std::string path;
    EditorCameraSettings camera;

    RAIN_FIELDS_BEGIN(SessionWorld)
    RAIN_FIELD(path)
    RAIN_FIELD(camera)
    RAIN_FIELDS_END
};

// The session of the project — `.Panda/session.json`, not in version control: where the user
// was. The worlds open at the end of the last session, in the order of their tabs (none — the
// project opens with no world), and the one that was active; what the Export window was last set to — the target
// ("macos", "ios", "ios-device", "windows": the platform of its build folder) and the
// configuration ("debug", "release"); empty — the defaults of the machine.
struct ProjectSession final : public Rain::Codable {
    std::vector<SessionWorld> worlds;
    std::string activeWorld;
    std::string exportTarget;
    std::string exportConfiguration;

    RAIN_FIELDS_BEGIN(ProjectSession)
    RAIN_FIELD(worlds)
    RAIN_FIELD(activeWorld)
    RAIN_FIELD(exportTarget)
    RAIN_FIELD(exportConfiguration)
    RAIN_FIELDS_END
};

} // namespace Panda
