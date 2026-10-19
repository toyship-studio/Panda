#pragma once

#include "Panda/Extensions/AssetKinds.hpp"
#include "Panda/Extensions/ComponentTypes.hpp"
#include "Panda/Extensions/ResourceProviders.hpp"
#include "Panda/Extensions/ScriptHostApis.hpp"
#include "Panda/Extensions/Settings.hpp"
#include "Panda/Extensions/WorldSystems.hpp"

namespace Panda {

// Engine registries (context/plugins.md): extension points through which the core learns about
// contributions — its own and the plugins'. The owner is GameContext (getExtensions); core
// contributions are registered on creation, plugins add theirs at application start, before
// the first world.
class Extensions final {
public:
    Extensions();
    Extensions(const Extensions &) = delete;
    Extensions &operator=(const Extensions &) = delete;

    ComponentTypeRegistry components;
    AssetKindRegistry assetKinds;
    WorldSystemRegistry worldSystems;
    SettingsRegistry settings;
    ResourceProviderRegistry resourceProviders;
    ShaderTemplateRegistry shaderTemplates;
    MeshLayoutRegistry meshLayouts;
    ScriptHostApiRegistry scriptHostApis;
};

} // namespace Panda
