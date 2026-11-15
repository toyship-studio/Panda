#pragma once

#include <Panda/Base/Base.hpp>
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

// A module in the project set (context/plugins.md, decision 4): built-in SDK modules and modules
// of the project folder are enabled by default — an entry with enabled=false disables one (version —
// the one the project is configured for, empty = any available). The set is the truth for loading
// in the editor and for export.
struct ModuleRef final : public Rain::Codable {
    std::string id;
    std::string version;
    bool enabled = true;

    RAIN_FIELDS_BEGIN(ModuleRef)
    RAIN_FIELD(id)
    RAIN_FIELD(version)
    RAIN_FIELD(enabled)
    RAIN_FIELDS_END
};

// The sections of the project scope — `.Panda/project.json`, in version control (one JSON object
// of sections by name, the settings registry; the modules add their own sections the same way,
// EditorSettingsTypes registers these). Paths are relative to the project, with forward slashes.

// "world": the main world of the project. The editor opens it when the session has no worlds;
// the scene list and the start world of the export are ahead (context/roadmap.md).
struct ProjectWorldSettings final : public Rain::Codable {
    std::string startWorld;

    RAIN_FIELDS_BEGIN(ProjectWorldSettings)
    RAIN_FIELD(startWorld)
    RAIN_FIELDS_END
};

// "modules": the module set of the project (ModuleRef). An empty list — every module of the
// distribution is enabled.
struct ProjectModuleSettings final : public Rain::Codable {
    std::vector<ModuleRef> entries;

    RAIN_FIELDS_BEGIN(ProjectModuleSettings)
    RAIN_FIELD(entries)
    RAIN_FIELDS_END
};

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
// start world), and the one that was active.
struct ProjectSession final : public Rain::Codable {
    std::vector<SessionWorld> worlds;
    std::string activeWorld;

    RAIN_FIELDS_BEGIN(ProjectSession)
    RAIN_FIELD(worlds)
    RAIN_FIELD(activeWorld)
    RAIN_FIELDS_END
};

} // namespace Panda
