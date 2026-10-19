#pragma once

#include "Panda/Assets/Base/AssetRef.hpp"
#include "Panda/Assets/MaterialAsset.hpp"
#include "Panda/Assets/ShaderAsset.hpp"

#include <Miren/Miren.hpp>

#include <string>
#include <vector>

namespace Panda {

class AssetHandler;

// Static resources of contributions (context/plugins.md): a provider creates its assets after
// the core (StaticResources::initStaticResources) and releases them before it. Ids of static
// assets are persisted in projects (the materials' meta.shader) — each contribution has its own
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
