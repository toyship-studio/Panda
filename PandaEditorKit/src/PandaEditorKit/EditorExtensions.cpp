#include "PandaEditorKit/EditorExtensions.hpp"

#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>

#include <filesystem>

namespace Editor {

void ComponentInspectorRegistry::add(ComponentInspector inspector) {
    m_inspectors.push_back(std::move(inspector));
}

const ComponentInspector *ComponentInspectorRegistry::find(std::string_view component) const {
    for (const ComponentInspector &inspector : m_inspectors) {
        if (inspector.component == component) { return &inspector; }
    }
    return nullptr;
}

bool ComponentInspectorRegistry::has(const ComponentInspector &inspector, Panda::Entity entity) const {
    if (!entity.isValid()) { return false; }
    const Panda::ComponentType *type = Panda::GameContext::getExtensions().components.find(inspector.component);
    return type != nullptr && entity.hasComponent(type->typeId);
}

void AssetKindUiRegistry::add(AssetKindUi kind) {
    m_kinds.push_back(std::move(kind));
}

const AssetKindUi *AssetKindUiRegistry::find(std::string_view kind) const {
    for (const AssetKindUi &entry : m_kinds) {
        if (entry.kind == kind) { return &entry; }
    }
    return nullptr;
}

Panda::path_t uniqueAssetPath(const Panda::path_t &directory, const std::string &baseName, const std::string &extension) {
    Panda::path_t path = directory / (baseName + extension);
    for (int counter = 2; std::filesystem::exists(path); ++counter) {
        path = directory / (baseName + " " + std::to_string(counter) + extension);
    }
    return path;
}

ViewportTool &ViewportToolRegistry::add(std::unique_ptr<ViewportTool> tool) {
    m_tools.push_back(std::move(tool));
    return *m_tools.back();
}

bool ViewportToolRegistry::anyDriving() const {
    for (const std::unique_ptr<ViewportTool> &tool : m_tools) {
        if (tool->isDriving()) { return true; }
    }
    return false;
}

void DocumentTypeRegistry::add(DocumentType type) {
    m_types.push_back(std::move(type));
}

const DocumentType *DocumentTypeRegistry::find(std::string_view id) const {
    for (const DocumentType &type : m_types) {
        if (type.id == id) { return &type; }
    }
    return nullptr;
}

EditorExtensions &editorExtensions() {
    static EditorExtensions extensions;
    return extensions;
}

} // namespace Editor
