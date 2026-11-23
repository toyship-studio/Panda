#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/Base/Path.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/ShaderAsset.hpp"

#include <Foundation/GraphicsAPI.hpp>
#include <Miren/Miren.hpp>

#include <string>
#include <vector>

namespace Panda {

class AssetHandler;

// Static resources of contributions (context/plugins.md): a provider creates its assets after
// the core (StaticResources::initStaticResources) and releases them before it. Ids of static
// assets are persisted in projects (the shader of a .mat) — each contribution has its own
// stable ones.
struct ResourceProvider final {
    std::string name;
    void (*init)(AssetHandler &handler) = nullptr;
    void (*release)() = nullptr;
};

// A built-in material shader: the name in the editor picker and the material carrying the
// fields template (built-in shaders have no SPIRV reflection — the fields of a .mat are
// checked against the template on load and on a shader change). Registration order = picker order.
struct ShaderTemplate final {
    AssetId shader = 0;
    std::string displayName;
    AssetRef<MaterialAsset> material;
};

// An engine shader that reads the scene feature blocks (ShaderFeatures.hpp): the lit templates,
// their variants, the sky, the lit shaders of a module. Such a shader is compiled for every project
// with the project's feature set — the editor cooks it into the project cache, the export into the
// game — and reloaded in place under its static id (ShaderAsset::reload); the bundle holds the
// build-time bake with every feature. vertex/fragment — the sources relative to the resources root
// ("default-shaders/default_lit.vert.hlsl"), shipped next to the baked programs.
struct SceneShader final {
    AssetRef<ShaderAsset> shader;
    std::string vertex;
    std::string fragment;
};

// A contribution's own vertex layout: for it the renderer takes its programs for the shadow depth
// pass and the IDS pass instead of the general ones.
struct MeshLayoutPrograms final {
    Miren::VertexLayoutHandle layout{MIREN_INVALID_HANDLE};
    AssetRef<ShaderAsset> shadowDepth;
    AssetRef<MaterialAsset> ids;
};

class ResourceProviderRegistry final {
public:
    // While the core static resources are alive (setLive), a new provider is initialized
    // immediately — a plugin may arrive after startup (project plugins on project open).
    void add(ResourceProvider provider);
    const std::vector<ResourceProvider> &providers() const {
        return m_providers;
    }
    // Set by initStaticResources (after init of all providers), cleared by deinitStaticResources.
    void setLive(AssetHandler *handler) {
        m_liveHandler = handler;
    }

private:
    std::vector<ResourceProvider> m_providers;
    AssetHandler *m_liveHandler = nullptr;
};

// Live together with the static resources: filled in initStaticResources (the core, then
// providers), cleared in deinitStaticResources.
class ShaderTemplateRegistry final {
public:
    void add(ShaderTemplate shaderTemplate);
    void clear() {
        m_templates.clear();
    }
    const std::vector<ShaderTemplate> &templates() const {
        return m_templates;
    }
    const ShaderTemplate *find(AssetId shader) const;

private:
    std::vector<ShaderTemplate> m_templates;
};

// Live together with the static resources: filled by initStaticResources (the core) and the
// providers (the modules), cleared in deinitStaticResources.
class SceneShaderRegistry final {
public:
    void add(AssetRef<ShaderAsset> shader, std::string vertex, std::string fragment);
    void clear() {
        m_shaders.clear();
    }
    const std::vector<SceneShader> &shaders() const {
        return m_shaders;
    }
    // The logical project-relative source path of an engine shader: `engine/<resource path>`. The
    // cache and the cooked files of the project use it like the path of a project shader
    // (AssetImporterBase::getCookedShaderAssetPath).
    static path_t projectSourcePath(const std::string &resourcePath);
    // Every scene shader from the cooked pairs of a project (`<cookedRoot>/<backend>/engine/…`):
    // the editor passes `<project>/.Panda/Shaders/cooked`, the runtime `<data>/Shaders/cooked`.
    // A missing pair is logged and the shader keeps its program.
    void reloadCooked(const path_t &cookedRoot, Foundation::GraphicsAPI backend) const;

private:
    std::vector<SceneShader> m_shaders;
};

class MeshLayoutRegistry final {
public:
    void add(MeshLayoutPrograms programs);
    void clear() {
        m_layouts.clear();
    }
    const MeshLayoutPrograms *find(Miren::VertexLayoutHandle layout) const;

private:
    std::vector<MeshLayoutPrograms> m_layouts;
};

} // namespace Panda
