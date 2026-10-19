#include "Panda/TerrainEditor/TerrainEditorExtensions.hpp"

#include "Panda/TerrainEditor/TerrainEditorForm.hpp"
#include "Panda/TerrainEditor/TerrainIcons.hpp"
#include "Panda/TerrainEditor/TerrainTool.hpp"

#include <PandaEditorKit/ComponentCommands.hpp>
#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <Foundation/Foundation.hpp>
#include <Miren/Miren.hpp>
#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/GameLogic/World.hpp>
#include <Panda/Terrain/FloraDistances.hpp>
#include <Panda/Terrain/FloraSettings.hpp>
#include <Panda/Terrain/TerrainComponents.hpp>
#include <Panda/Terrain/TerrainData2D.hpp>
#include <Panda/Terrain/TerrainData3D.hpp>
#include <Panda/Terrain/TerrainFlora.hpp>
#include <Panda/Terrain/TerrainLodSettings.hpp>
#include <Panda/Terrain/TerrainResources.hpp>
#include <Panda/Terrain/TerrainSerialization.hpp>
#include <Panda/Terrain/TerrainTrees.hpp>
#include <Panda/Terrain/TerrainVertex.hpp>

#include <fstream>
#include <memory>
#include <unordered_set>

namespace TerrainEditor {

using namespace Editor;
using namespace PandaUI;

namespace {

    Panda::TerrainTool *s_tool = nullptr; // the viewport tool — owned by the editor registry

    // ---- Terrain 2D / 3D inspector sections: a reference to the asset and the material, physics.
    // Landscape data (heights/flora/brushes) is edited in the ASSET TAB (the document model) —
    // the Open Terrain Editor button opens it.

    void addOpenTerrainEditorButton(CollapsibleSection &section, uint32_t terrainAssetId) {
        if (terrainAssetId == 0) { return; } // no terrain assigned — nothing to edit
        auto open = makeFormButton("Open Terrain Editor", ButtonStyle::AccentOutline, [terrainAssetId] {
            editorHost().selectAsset(Panda::AssetId(terrainAssetId));
        });
        section.addContent(makeFormButtonRow({open}));
    }

    template<typename Component>
    void buildTerrainComponentSection(
        CollapsibleSection &section, const ComponentInspectorContext &context, const char *terrainKind
    ) {
        Panda::Entity primary = context.entities.front();
        auto data = std::make_shared<Component>(primary.getComponent<Component>());
        Panda::World *world = &context.world;
        const std::vector<Panda::Entity> entities = context.entities;
        auto emit = [world, entities, data] { commitComponent<Component>(*world, entities, *data); };
        addOpenTerrainEditorButton(section, static_cast<uint32_t>(data->terrain.getId()));
        addAssetRow(section, "Terrain", terrainKind, static_cast<uint32_t>(data->terrain.getId()), [data, emit](uint32_t id) {
            data->terrain = assetRefOf(id);
            emit();
        });
        // An empty material = the module's built-in terrain material (the sky pattern).
        addAssetRow(section, "Material", "material", static_cast<uint32_t>(data->material.getId()), [data, emit](uint32_t id) {
            data->material = assetRefOf(id);
            emit();
        });
        auto physicsToggle = std::make_shared<Toggle>();
        physicsToggle->setOn(data->physicsEnabled);
        physicsToggle->setOnValueChanged([data, emit](Toggle &t) {
            data->physicsEnabled = t.isOn();
            emit();
        });
        section.addContent(std::make_shared<PropertyRow>("Physics", physicsToggle, false));
    }

    template<typename Component>
    void setTerrainComponentPresence(Panda::World &world, const std::vector<Panda::Entity> &entities, bool add) {
        toggleComponentPresence<Component>(world, std::unordered_set<Panda::Entity>(entities.begin(), entities.end()), add);
    }

    // ---- Settings sections: the module's world settings (the "terrain" and "flora" sections of
    // .world, World::context<T>) — LOD/ring distances and flora ranges, meters. The store is the
    // context of the world the editor shows.

    std::shared_ptr<NumberField> numberField(float min, float max, float value, std::function<void(float)> apply) {
        auto field = std::make_shared<NumberField>();
        field->setFont(Typo::Value);
        field->setRange(min, max);
        field->setValue(value);
        field->setOnValueChanged([apply = std::move(apply)](NumberField &f) { apply(f.getValue()); });
        return field;
    }

    void buildTerrainSettings(CollapsibleSection &section, Panda::TypeContext &store) {
        Panda::TypeContext *w = &store;
        const Panda::TerrainLodSettings &lod = store.get<Panda::TerrainLodSettings>();
        section.addContent(std::make_shared<PropertyRow>(
            "LOD 1 Distance", numberField(8.f, 4096.f, lod.lod1, [w](float v) { w->get<Panda::TerrainLodSettings>().lod1 = v; })
        ));
        section.addContent(std::make_shared<PropertyRow>(
            "LOD 2 Distance", numberField(8.f, 8192.f, lod.lod2, [w](float v) { w->get<Panda::TerrainLodSettings>().lod2 = v; })
        ));
        section.addContent(std::make_shared<PropertyRow>(
            "Draw Distance",
            numberField(0.f, 16384.f, lod.drawDistance, [w](float v) { w->get<Panda::TerrainLodSettings>().drawDistance = v; })
        ));
        // Width of the LOD grid rings (tiles): rings 2–4 — from the nearest to the farthest.
        section.addContent(std::make_shared<PropertyRow>(
            "Ring 2 Width", numberField(0.f, 256.f, static_cast<float>(lod.ring2Width), [w](float v) {
                w->get<Panda::TerrainLodSettings>().ring2Width = static_cast<int>(v);
            })
        ));
        section.addContent(std::make_shared<PropertyRow>(
            "Ring 3 Width", numberField(0.f, 256.f, static_cast<float>(lod.ring3Width), [w](float v) {
                w->get<Panda::TerrainLodSettings>().ring3Width = static_cast<int>(v);
            })
        ));
        section.addContent(std::make_shared<PropertyRow>(
            "Ring 4 Width", numberField(0.f, 256.f, static_cast<float>(lod.ring4Width), [w](float v) {
                w->get<Panda::TerrainLodSettings>().ring4Width = static_cast<int>(v);
            })
        ));
    }

    void buildFloraSettings(CollapsibleSection &section, Panda::TypeContext &store) {
        Panda::TypeContext *w = &store;
        const Panda::FloraDistances &flora = store.get<Panda::FloraDistances>();
        section.addContent(std::make_shared<PropertyRow>(
            "Grass Distance", numberField(10.f, 1000.f, flora.grass, [w](float v) { w->get<Panda::FloraDistances>().grass = v; })
        ));
        // Width of the grass dissolve zone at the far boundary.
        section.addContent(std::make_shared<PropertyRow>(
            "Grass Dissolve",
            numberField(0.f, 1000.f, flora.grassDissolve, [w](float v) { w->get<Panda::FloraDistances>().grassDissolve = v; })
        ));
        section.addContent(std::make_shared<PropertyRow>(
            "Tree Distance", numberField(10.f, 4000.f, flora.tree, [w](float v) { w->get<Panda::FloraDistances>().tree = v; })
        ));
        section.addContent(std::make_shared<PropertyRow>(
            "Imposter Distance",
            numberField(0.f, 16000.f, flora.imposter, [w](float v) { w->get<Panda::FloraDistances>().imposter = v; })
        ));
    }

    // ---- "New Terrain" / "New Terrain 3D": an empty terrain is created flat (no noise, no generation).

    Panda::path_t createTerrain2D(const Panda::path_t &directory) {
        const Panda::path_t path = uniqueAssetPath(directory, "Terrain", ".terrain");
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR_EDITOR("CAN'T CREATE TERRAIN FILE %s", path.string().c_str());
            return {};
        }
        // cellSize 2: a height detail of 1 m is excessive for the relief, while memory grows
        // quadratically — the same meters with a grid four times smaller.
        Panda::TerrainSerialization::write2D(
            file, Panda::TerrainData2D::makeFlat(128, 128, 2.f), Panda::FloraSettings{}, Panda::TerrainTrees{}
        );
        return path;
    }

    Panda::path_t createTerrain3D(const Panda::path_t &directory) {
        const Panda::path_t path = uniqueAssetPath(directory, "Terrain3D", ".terrain");
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR_EDITOR("CAN'T CREATE TERRAIN FILE %s", path.string().c_str());
            return {};
        }
        Panda::TerrainSerialization::write3D(file, Panda::TerrainData3D::makeFlat(128, 48, 128, 1.f, 8));
        return path;
    }

    // ---- Per-frame flora statistics (visible blocks, plant instances, reseeds).
    void collectFloraStatistics(uint32_t *values) {
        const Panda::TerrainFlora::FrameStats stats = Panda::TerrainFlora::consumeFrameStats();
        values[0] = stats.visibleBlocks;
        values[1] = stats.instances;
        values[2] = stats.rebuilds;
    }

    // ---- Terrain material previews: the sphere shows pure layer 1. The convention of a layered
    // shader: the set of fields layer1..4Texture / layerNColor, the weights are the vertex color
    // (fragColor); it works both for built-in shaders (fields from the template) and for user ones
    // (from SPIRV reflection). For a 2D terrain the weights live in the splatmap, and the vertex
    // is thin (TerrainVertex).

    bool usesLayerWeights(Panda::MaterialAsset &material) {
        bool hasFirstTexture = false;
        bool hasLastColor = false;
        for (const Panda::MaterialField &field : material.getInputs().inputs) {
            if (field.name == "layer1Texture") { hasFirstTexture = true; }
            if (field.name == "layer4Color") { hasLastColor = true; }
        }
        return hasFirstTexture && hasLastColor;
    }

    bool isThinTerrainMaterial(Panda::MaterialAsset &material) {
        return usesLayerWeights(material) &&
               material.getShaderAsset().getId() == Panda::Terrain::Resources::shader2D.getId();
    }

    Miren::VertexLayoutHandle thinVertexLayout() {
        return Panda::Terrain::Resources::vertexLayout;
    }

    Foundation::Memory packThinVertices(const std::vector<Panda::Vertex> &vertices, uint32_t &outSize) {
        std::vector<Panda::TerrainVertex> thin;
        thin.reserve(vertices.size());
        for (const Panda::Vertex &vertex : vertices) { thin.emplace_back(vertex.pos, vertex.normal, vertex.uv); }
        outSize = static_cast<uint32_t>(sizeof(Panda::TerrainVertex) * thin.size());
        return Foundation::Memory::copying(thin.data(), outSize);
    }

    // Splatmap placeholder: a single texel with the weight of layer 1 (lives until the process ends).
    void bindSplatmapStub() {
        static Miren::TextureHandle s_splatmap = [] {
            Foundation::Memory data = Foundation::Memory::alloc(sizeof(uint32_t));
            *static_cast<uint32_t *>(data.data) = 0x000000FF; // RGBA8: R=255 — layer 1
            Miren::TextureCreate create;
            create.m_data = data;
            return Miren::createTexture(create);
        }();
        Miren::addInputTexture("splatmap", s_splatmap);
    }

    std::unique_ptr<AssetEditor> createEditor2D(const AssetEditorContext &context) {
        return std::make_unique<TerrainAssetEditor>(context, /*voxels*/ false);
    }
    std::unique_ptr<AssetEditor> createEditor3D(const AssetEditorContext &context) {
        return std::make_unique<TerrainAssetEditor>(context, /*voxels*/ true);
    }

} // namespace

Panda::TerrainTool &terrainTool() {
    PND_ASSERT(s_tool != nullptr, "Terrain editor part is not registered");
    return *s_tool;
}

void registerExtensions(EditorExtensions &extensions) {
    extensions.iconFolders.push_back("terrain-icons"); // the module's editor/resources/terrain-icons → the editor bundle

    ComponentInspector terrain2d;
    terrain2d.component = "terrain2dComponent";
    terrain2d.displayName = "Terrain 2D";
    terrain2d.icon = Ic::Terrain;
    terrain2d.setPresence = setTerrainComponentPresence<Panda::Terrain2DComponent>;
    terrain2d.build = [](CollapsibleSection &section, const ComponentInspectorContext &context) {
        buildTerrainComponentSection<Panda::Terrain2DComponent>(section, context, "terrain2d");
    };
    extensions.componentInspectors.add(std::move(terrain2d));

    ComponentInspector terrain3d;
    terrain3d.component = "terrain3dComponent";
    terrain3d.displayName = "Terrain 3D";
    terrain3d.icon = Ic::Terrain;
    terrain3d.setPresence = setTerrainComponentPresence<Panda::Terrain3DComponent>;
    terrain3d.build = [](CollapsibleSection &section, const ComponentInspectorContext &context) {
        buildTerrainComponentSection<Panda::Terrain3DComponent>(section, context, "terrain3d");
    };
    extensions.componentInspectors.add(std::move(terrain3d));

    const Color terrainColor(0x86A65BFF);
    extensions.assetKinds.add({"terrain2d", Ic::Terrain, terrainColor, createEditor2D});
    extensions.assetKinds.add({"terrain3d", Ic::Terrain, terrainColor, createEditor3D});

    extensions.assetCreators.push_back({"New Terrain", createTerrain2D});      // flat 128×128 heightmap
    extensions.assetCreators.push_back({"New Terrain 3D", createTerrain3D});   // voxel, flat ground

    s_tool = static_cast<Panda::TerrainTool *>(&extensions.viewportTools.add(std::make_unique<Panda::TerrainTool>()));

    extensions.settingsSections.push_back({Panda::SettingsScope::World, "TERRAIN", buildTerrainSettings});
    extensions.settingsSections.push_back({Panda::SettingsScope::World, "FLORA", buildFloraSettings});

    extensions.statistics.push_back({"FLORA", {"Blocks", "Plants", "Rebuilds"}, collectFloraStatistics});

    // Preview: 2D — a thin sphere + the splatmap placeholder; the other layered ones — weights
    // (1,0,0,0) in the vertex color.
    MaterialPreview thin;
    thin.matches = isThinTerrainMaterial;
    thin.vertexColor = Panda::Color(1.f, 0.f, 0.f, 0.f);
    thin.vertexLayout = thinVertexLayout;
    thin.packVertices = packThinVertices;
    thin.bindInputs = bindSplatmapStub;
    extensions.materialPreviews.push_back(thin);
    MaterialPreview layered;
    layered.matches = usesLayerWeights;
    layered.vertexColor = Panda::Color(1.f, 0.f, 0.f, 0.f);
    extensions.materialPreviews.push_back(layered);
}

} // namespace TerrainEditor
