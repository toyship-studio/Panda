#include "Panda/TerrainEditor/TerrainEditorForm.hpp"

#include "Panda/TerrainEditor/TerrainDialogs.hpp"
#include "Panda/TerrainEditor/TerrainDocumentOps.hpp"
#include "Panda/TerrainEditor/TerrainIcons.hpp"

#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <Panda/Terrain/Terrain2DAsset.hpp>
#include <Panda/Terrain/Terrain3DAsset.hpp>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace TerrainEditor {

using namespace Editor;
using namespace PandaUI;

namespace {

    // ------------------------------------------------------------------ form building blocks

    // A row of the layer list: kind icon + name + caption + visibility toggle. A click
    // on the row selects the layer (its settings open as a section below).
    class LayerRow final : public Control {
    public:
        LayerRow(
            const char *icon, const std::string &title, const std::string &caption, bool enabled,
            bool selected, std::function<void()> onSelect, std::function<void(bool)> onToggle
        )
            : m_selected(selected)
            , m_enabled(enabled)
            , m_onSelect(std::move(onSelect)) {
            setCursor(Cursor::PointingHand);
            layout().setFlexDirection(FlexDirection::Row);
            layout().setAlignItems(Align::Center);
            layout().setGap(10.f);
            layout().setPadding(Edge::Horizontal, 8.f);
            layout().setPadding(Edge::Vertical, 5.f);
            surface().setCornerRadius(6.f);

            m_icon = makeIcon(icon, 30.f);
            addSubview(m_icon);

            auto texts = std::make_shared<Panel>();
            texts->setBackgroundColor(Color(0x00000000));
            texts->setUserInteractionEnabled(false);
            texts->layout().setFlexDirection(FlexDirection::Column);
            texts->layout().setFlexGrow(1.f);
            texts->layout().setFlexShrink(1.f);
            auto titleLabel = std::make_shared<Label>(title);
            titleLabel->setFont(Typo::Body);
            titleLabel->setTextRole(enabled ? TextRole::Primary : TextRole::Disabled);
            texts->addSubview(titleLabel);
            auto captionLabel = std::make_shared<Label>(caption);
            captionLabel->setFont(Typo::Meta);
            captionLabel->setTextRole(TextRole::Dim);
            texts->addSubview(captionLabel);
            addSubview(texts);

            auto eye = std::make_shared<Toggle>();
            eye->setOn(enabled);
            eye->setTooltip("Layer visibility");
            eye->setOnValueChanged([onToggle = std::move(onToggle)](Toggle &t) {
                if (onToggle) { onToggle(t.isOn()); }
            });
            addSubview(eye);
            applyStyle();
        }

    private:
        void performPrimaryAction() override {
            if (m_onSelect) { m_onSelect(); }
        }
        void controlStateChanged(ControlState, ControlState) override {
            applyStyle();
        }
        void themeChanged() override {
            applyStyle();
        }
        void applyStyle() {
            const Theme &t = Theme::current();
            m_icon->setTintColor(!m_enabled ? t.textDisabled : (m_selected ? t.accentText : t.textMuted));
            if (m_selected) {
                setBackgroundColor(t.accentSoft);
            } else {
                setBackgroundColor(hasControlState(ControlState::Hovered) ? t.rowHover : Color(0x00000000));
            }
        }

        bool m_selected;
        bool m_enabled;
        std::shared_ptr<ImageView> m_icon;
        std::function<void()> m_onSelect;
    };

    // Editing a copy of the data + sending the intent — the common framework of the form rows.
    template<typename Data>
    struct Editing {
        std::shared_ptr<Data> data;
        std::function<void()> emit;
    };

    template<typename Data>
    void addNumber(
        CollapsibleSection &section, const Editing<Data> &editing, const char *title, float min, float max,
        float value, std::function<void(Data &, float)> apply
    ) {
        auto field = std::make_shared<NumberField>();
        field->setFont(Typo::Value);
        field->setRange(min, max);
        field->setValue(value);
        field->setOnValueChanged([editing, apply = std::move(apply)](NumberField &f) {
            apply(*editing.data, f.getValue());
            editing.emit();
        });
        section.addContent(std::make_shared<PropertyRow>(title, field));
    }

    template<typename Data>
    void addToggle(
        CollapsibleSection &section, const Editing<Data> &editing, const char *title, bool value,
        std::function<void(Data &, bool)> apply
    ) {
        auto toggle = std::make_shared<Toggle>();
        toggle->setOn(value);
        toggle->setOnValueChanged([editing, apply = std::move(apply)](Toggle &t) {
            apply(*editing.data, t.isOn());
            editing.emit();
        });
        section.addContent(std::make_shared<PropertyRow>(title, toggle, false));
    }

    template<typename Data>
    void addColor(
        CollapsibleSection &section, const Editing<Data> &editing, const char *title, const Panda::Color &value,
        std::function<void(Data &, const Panda::Color &)> apply
    ) {
        auto well = std::make_shared<ColorWell>(Color(value.r, value.g, value.b, value.a));
        well->setOnColorChanged([editing, apply = std::move(apply)](ColorWell &w) {
            const Color c = w.getColor();
            apply(*editing.data, Panda::Color(c.r, c.g, c.b, c.a));
            editing.emit();
        });
        section.addContent(std::make_shared<PropertyRow>(title, well, false));
    }

    void addSlider(
        CollapsibleSection &section, const char *title, float min, float max, float value,
        std::function<void(float)> apply
    ) {
        auto slider = std::make_shared<Slider>();
        slider->setRange(min, max);
        slider->setValue(value);
        slider->setOnValueChanged([apply = std::move(apply)](Slider &s) { apply(s.getValue()); });
        section.addContent(std::make_shared<PropertyRow>(title, slider));
    }

    void addCombobox(
        CollapsibleSection &section, const char *title, std::vector<std::string> items, int selected,
        std::function<void(int)> apply
    ) {
        auto combobox = std::make_shared<Combobox>();
        combobox->setItems(std::move(items));
        combobox->setSelectedIndex(selected);
        combobox->setOnSelectionChanged([apply = std::move(apply)](Combobox &c) { apply(c.selectedIndex()); });
        section.addContent(std::make_shared<PropertyRow>(title, combobox));
    }

    const char *grassIcon(const Panda::GrassLayer &layer) {
        return layer.kind == Panda::GrassKind::Tuft ? Ic::FloraTuft : Ic::FloraGrassMesh;
    }

    std::string grassCaption(const Panda::GrassLayer &layer) {
        static const char *placements[] = {"procedural", "painted", "procedural + brush"};
        const char *kind = layer.kind == Panda::GrassKind::Tuft ? "Tuft" : "Mesh";
        return std::string(kind) + " · " + placements[std::min<uint32_t>(static_cast<uint32_t>(layer.placement), 2u)];
    }

    // The first channel of the painted mask not occupied by other painted layers; -1 — all occupied.
    int freeMaskChannel(const Panda::FloraSettings &flora, size_t except) {
        uint32_t used = 0;
        for (size_t i = 0; i < flora.grass.size(); i++) {
            if (i != except && flora.grass[i].placement != Panda::GrassPlacement::Procedural) {
                used |= 1u << flora.grass[i].maskChannel;
            }
        }
        for (uint32_t channel = 0; channel < Panda::TerrainData2D::FLORA_MASK_CHANNELS; channel++) {
            if (!(used & (1u << channel))) { return static_cast<int>(channel); }
        }
        return -1;
    }

    // ------------------------------------------------------------------ 2D sections

    void buildTerrainSection2D(View &container, uint32_t assetId, const TerrainFormHost &host) {
        auto section = makeFormSection("TERRAIN");
        Panda::AssetRef<Panda::Terrain2DAsset> asset{Panda::AssetId(assetId)};
        const Panda::TerrainData2D &data = asset->getData();
        char size[128];
        std::snprintf(
            size, sizeof(size), "%u × %u cells · %g m  (%g × %g m)", data.cellsX, data.cellsZ, data.cellSize,
            data.cellsX * data.cellSize, data.cellsZ * data.cellSize
        );
        auto sizeLabel = std::make_shared<Label>(size);
        sizeLabel->setFont(Typo::Value);
        sizeLabel->setTextRole(TextRole::Secondary);
        section->addContent(std::make_shared<PropertyRow>("Size", sizeLabel, false));

        const uint32_t cellsX = data.cellsX;
        const uint32_t cellsZ = data.cellsZ;
        const float cellSize = data.cellSize;
        auto resize = makeFormButton("Resize…", ButtonStyle::Neutral, nullptr);
        resize->setOnClick([host, assetId, cellsX, cellsZ, cellSize](Button &button) {
            showResizeTerrain2DDialog(button.getContext(), cellsX, cellsZ, cellSize, [host, assetId](uint32_t x, uint32_t z) {
                resizeTerrain2D(host, Panda::AssetId(assetId), x, z);
            });
        });
        auto import = makeFormButton("Import Heightmap…", ButtonStyle::Neutral, nullptr);
        import->setOnClick([host, assetId](Button &button) {
            showImportHeightmapDialog(button.getContext(), [host, assetId](const std::string &path, float heightScale) {
                importHeightmap(host, Panda::AssetId(assetId), path, heightScale);
            });
        });
        section->addContent(makeFormButtonRow({resize, import}));
        container.addSubview(section);
    }

    // One active brush tool and only its parameters. The target of the Grass/Trees brushes is
    // the layer selected in the list below.
    void buildToolSection(
        View &container, const TerrainFormHost &host, const Panda::FloraSettings *flora, bool voxels
    ) {
        auto section = makeFormSection("TOOL");
        Panda::TerrainTool *tool = host.tool;
        Panda::TerrainBrushSettings *brush = &tool->settings();
        const auto emit = [tool] { tool->settingsChanged(); };

        std::vector<std::string> names = {"Off", "Sculpt"};
        std::vector<Panda::TerrainToolKind> kinds = {Panda::TerrainToolKind::None, Panda::TerrainToolKind::Sculpt};
        if (!voxels) {
            names.insert(names.end(), {"Paint", "Grass", "Trees"});
            kinds.insert(
                kinds.end(),
                {Panda::TerrainToolKind::Paint, Panda::TerrainToolKind::Grass, Panda::TerrainToolKind::Trees}
            );
        }
        const auto current = std::find(kinds.begin(), kinds.end(), brush->tool);
        auto tools = std::make_shared<SegmentedControl>();
        tools->setSegments(names);
        tools->setSelectedIndex(current == kinds.end() ? 0 : static_cast<int>(current - kinds.begin()));
        tools->layout().setMargin(Edge::Vertical, 4.f);
        tools->setOnSelectionChanged([brush, emit, kinds, rebuild = host.rebuild](SegmentedControl &control) {
            const int index = control.selectedIndex();
            if (index < 0 || index >= static_cast<int>(kinds.size())) { return; }
            brush->tool = kinds[index];
            emit();
            rebuild(); // each tool has its own set of parameters
        });
        section->addContent(tools);

        if (brush->tool != Panda::TerrainToolKind::None) {
            addSlider(*section, "Radius", 1.f, 50.f, brush->radius, [brush, emit](float value) {
                brush->radius = value;
                emit();
            });
        }
        switch (brush->tool) {
            case Panda::TerrainToolKind::None:
                section->addContent(makeFormHint("Pick a tool to edit the terrain in the viewport."));
                break;
            case Panda::TerrainToolKind::Sculpt:
                if (voxels) {
                    addCombobox(
                        *section, "Mode", {"Sphere", "Raise", "Flatten"}, static_cast<int>(brush->sculptMode3d),
                        [brush, emit](int index) {
                            brush->sculptMode3d = static_cast<Panda::TerrainBrushMode3D>(index);
                            emit();
                        }
                    );
                } else {
                    addCombobox(
                        *section, "Mode", {"Raise", "Flatten", "Smooth"},
                        std::min(static_cast<int>(brush->sculptMode), 2), [brush, emit](int index) {
                            brush->sculptMode = static_cast<Panda::TerrainBrushMode2D>(index);
                            emit();
                        }
                    );
                }
                addCombobox(*section, "Direction", {"Add", "Subtract"}, brush->subtract ? 1 : 0, [brush, emit](int index) {
                    brush->subtract = index == 1;
                    emit();
                });
                addSlider(*section, "Strength", 0.5f, 20.f, brush->strength, [brush, emit](float value) {
                    brush->strength = value;
                    emit();
                });
                break;
            case Panda::TerrainToolKind::Paint:
                // No semantic names: the soil layers are numbered, the meaning is set by the user.
                addCombobox(
                    *section, "Soil Layer", {"Layer 1", "Layer 2", "Layer 3", "Layer 4"}, brush->paintLayer - 1,
                    [brush, emit](int index) {
                        brush->paintLayer = index + 1;
                        emit();
                    }
                );
                addSlider(*section, "Strength", 0.5f, 20.f, brush->paintStrength, [brush, emit](float value) {
                    brush->paintStrength = value;
                    emit();
                });
                break;
            case Panda::TerrainToolKind::Grass: {
                const int selected = host.state->selectedGrass;
                const bool paintable = flora != nullptr && selected >= 0 &&
                                       selected < static_cast<int>(flora->grass.size()) &&
                                       flora->grass[selected].placement != Panda::GrassPlacement::Procedural;
                if (!paintable) {
                    section->addContent(makeFormHint(
                        "Select a grass layer with Painted or Procedural + Brush placement in the list below."
                    ));
                    break;
                }
                auto target = std::make_shared<Label>(flora->grass[selected].name);
                target->setFont(Typo::Body);
                section->addContent(std::make_shared<PropertyRow>("Layer", target, false));
                addCombobox(*section, "Direction", {"Add", "Erase"}, brush->grassErase ? 1 : 0, [brush, emit](int index) {
                    brush->grassErase = index == 1;
                    emit();
                });
                addSlider(*section, "Strength", 0.2f, 8.f, brush->grassStrength, [brush, emit](float value) {
                    brush->grassStrength = value;
                    emit();
                });
                break;
            }
            case Panda::TerrainToolKind::Trees: {
                const Panda::TreeLayer *layer = nullptr;
                if (flora != nullptr) {
                    for (const Panda::TreeLayer &candidate : flora->trees) {
                        if (candidate.id == host.state->selectedTree) { layer = &candidate; }
                    }
                }
                if (layer == nullptr) {
                    section->addContent(makeFormHint("Select a tree layer in the list below."));
                    break;
                }
                auto target = std::make_shared<Label>(layer->name);
                target->setFont(Typo::Body);
                section->addContent(std::make_shared<PropertyRow>("Layer", target, false));
                addCombobox(*section, "Direction", {"Plant", "Erase"}, brush->treeErase ? 1 : 0, [brush, emit, rebuild = host.rebuild](int index) {
                    brush->treeErase = index == 1;
                    emit();
                    rebuild(); // planting and erasing have different parameters
                });
                if (brush->treeErase) {
                    auto all = std::make_shared<Toggle>();
                    all->setOn(brush->treeEraseAll);
                    all->setOnValueChanged([brush, emit](Toggle &t) {
                        brush->treeEraseAll = t.isOn();
                        emit();
                    });
                    section->addContent(std::make_shared<PropertyRow>("All Layers", all, false));
                } else {
                    addSlider(*section, "Spacing (m)", 1.f, 40.f, brush->treeSpacing, [brush, emit](float value) {
                        brush->treeSpacing = value;
                        emit();
                    });
                    addSlider(*section, "Rate (1/s)", 1.f, 60.f, brush->treeRate, [brush, emit](float value) {
                        brush->treeRate = value;
                        emit();
                    });
                }
                break;
            }
        }
        container.addSubview(section);
    }

    void buildGrassList(View &container, const TerrainFormHost &host, const Editing<Panda::FloraSettings> &editing) {
        auto section = makeFormSection("GRASS & BUSHES");
        const Panda::FloraSettings &flora = *editing.data;
        TerrainEditorState *state = host.state;
        for (size_t i = 0; i < flora.grass.size(); i++) {
            const Panda::GrassLayer &layer = flora.grass[i];
            section->addContent(std::make_shared<LayerRow>(
                grassIcon(layer), layer.name, grassCaption(layer), layer.enabled,
                state->selectedGrass == static_cast<int>(i),
                [state, i, rebuild = host.rebuild] {
                    state->selectedGrass = static_cast<int>(i); // also the Grass brush target (bindBrush)
                    rebuild();
                },
                [editing, i, rebuild = host.rebuild](bool enabled) {
                    editing.data->grass[i].enabled = enabled;
                    editing.emit();
                    rebuild();
                }
            ));
        }
        if (flora.grass.empty()) {
            section->addContent(makeFormHint("No layers. Grass and bushes fill a layer by rules or a painted mask."));
        }
        auto add = makeFormButton("Add Layer…", ButtonStyle::AccentOutline, nullptr);
        add->setOnClick([editing, state, rebuild = host.rebuild](Button &button) {
            showAddGrassLayerDialog(button.getContext(), [editing, state, rebuild](Panda::GrassLayer layer) {
                editing.data->grass.push_back(std::move(layer));
                state->selectedGrass = static_cast<int>(editing.data->grass.size()) - 1; // new layer opens right away
                editing.emit();
                rebuild();
            });
        });
        section->addContent(makeFormButtonRow({add}));
        container.addSubview(section);
    }

    void buildGrassLayerSection(
        View &container, uint32_t assetId, const TerrainFormHost &host, const Editing<Panda::FloraSettings> &editing
    ) {
        const int index = host.state->selectedGrass;
        if (index < 0 || index >= static_cast<int>(editing.data->grass.size())) { return; }
        const size_t i = static_cast<size_t>(index);
        const Panda::GrassLayer &layer = editing.data->grass[i];
        auto section = makeFormSection("GRASS LAYER · " + layer.name);
        // Field edits go into layer i of the settings copy.
        const auto number = [&](const char *title, float min, float max, float value, void (*apply)(Panda::GrassLayer &, float)) {
            addNumber<Panda::FloraSettings>(
                *section, editing, title, min, max, value,
                [i, apply](Panda::FloraSettings &flora, float v) { apply(flora.grass[i], v); }
            );
        };
        auto nameField = std::make_shared<TextField>();
        nameField->setText(layer.name);
        nameField->setOnSubmit([editing, i, rebuild = host.rebuild](TextField &f) {
            editing.data->grass[i].name = f.getText();
            editing.emit();
            rebuild(); // captions of the list row and of the section
        });
        section->addContent(std::make_shared<PropertyRow>("Name", nameField));

        const bool meshKind = layer.kind == Panda::GrassKind::Mesh;
        if (meshKind) {
            // The imported mesh as a whole + a material (empty = auto-materials of the primitives).
            addAssetRow(*section, "Mesh", "mesh", static_cast<uint32_t>(layer.mesh.getId()), [editing, i](uint32_t id) {
                editing.data->grass[i].mesh = assetRefOf(id);
                editing.emit();
            });
            addAssetRow(*section, "Material Override", "material", static_cast<uint32_t>(layer.meshMaterial.getId()),
                [editing, i](uint32_t id) {
                    editing.data->grass[i].meshMaterial = assetRefOf(id);
                    editing.emit();
                }
            );
        }

        // Placement: rules, brush, or rules with the brush on top. The mask channel is assigned
        // automatically — the first free one; on a mode change the channel is re-encoded
        // (what has been painted is preserved).
        const bool painted = layer.placement == Panda::GrassPlacement::Painted;
        addCombobox(
            *section, "Placement", {"Procedural", "Painted", "Procedural + Brush"},
            std::min(static_cast<int>(layer.placement), 2),
            [editing, i, host, assetId, rebuild = host.rebuild](int selected) {
                Panda::GrassLayer &edited = editing.data->grass[i];
                const Panda::GrassPlacement from = edited.placement;
                const auto to = static_cast<Panda::GrassPlacement>(selected);
                if (to == from) { return; }
                if (to != Panda::GrassPlacement::Procedural && from == Panda::GrassPlacement::Procedural) {
                    const int channel = freeMaskChannel(*editing.data, i);
                    if (channel < 0) { // all mask channels are taken — the painted mode is unavailable
                        rebuild();
                        return;
                    }
                    edited.maskChannel = static_cast<uint32_t>(channel);
                }
                edited.placement = to;
                editing.emit();
                if (to != Panda::GrassPlacement::Procedural) {
                    convertFloraMask(
                        host, Panda::AssetId(assetId), edited.maskChannel, static_cast<uint32_t>(from),
                        static_cast<uint32_t>(to)
                    );
                }
                rebuild(); // the layer's set of fields depends on the placement
            }
        );

        if (meshKind) {
            number("Spacing (m)", 0.2f, 200.f, layer.spacing, [](Panda::GrassLayer &l, float v) { l.spacing = v; });
        } else {
            number("Density", 0.f, 1.f, layer.density, [](Panda::GrassLayer &l, float v) { l.density = v; });
        }
        number("Scale Min", 0.05f, 3.f, layer.scaleMin, [](Panda::GrassLayer &l, float v) { l.scaleMin = v; });
        number("Scale Max", 0.05f, 3.f, layer.scaleMax, [](Panda::GrassLayer &l, float v) { l.scaleMax = v; });
        if (!meshKind) {
            number("Wind Influence", 0.f, 1.f, layer.windInfluence, [](Panda::GrassLayer &l, float v) { l.windInfluence = v; });
            number("Brownian", 0.f, 3.f, layer.brownian, [](Panda::GrassLayer &l, float v) { l.brownian = v; });
        }
        if (!painted) { // Painted: the brush is a direct intent, soil/height/patches do not filter
            addCombobox(
                *section, "Soil Layer", {"Any", "Layer 1", "Layer 2", "Layer 3", "Layer 4"},
                static_cast<int>(std::min(layer.soilLayer, 4u)), [editing, i](int selected) {
                    editing.data->grass[i].soilLayer = static_cast<uint32_t>(std::max(selected, 0));
                    editing.emit();
                }
            );
            number("Min Height", -1000.f, 1000.f, layer.minHeight, [](Panda::GrassLayer &l, float v) { l.minHeight = v; });
            number("Max Height", -1000.f, 1000.f, layer.maxHeight, [](Panda::GrassLayer &l, float v) { l.maxHeight = v; });
            number("Noise Coverage", 0.f, 1.f, layer.noiseCoverage, [](Panda::GrassLayer &l, float v) { l.noiseCoverage = v; });
        }
        number("Max Slope", 0.f, 90.f, layer.maxSlopeDeg, [](Panda::GrassLayer &l, float v) { l.maxSlopeDeg = v; });
        if (meshKind) {
            addToggle<Panda::FloraSettings>(*section, editing, "Cast Shadows", layer.castShadows, [i](Panda::FloraSettings &flora, bool v) {
                flora.grass[i].castShadows = v;
            });
        }
        TerrainEditorState *state = host.state;
        auto remove = makeFormButton("Delete Layer", ButtonStyle::Neutral, [editing, state, i, rebuild = host.rebuild] {
            editing.data->grass.erase(editing.data->grass.begin() + static_cast<std::ptrdiff_t>(i));
            state->selectedGrass = -1;
            editing.emit();
            rebuild();
        });
        section->addContent(makeFormButtonRow({remove}));
        container.addSubview(section);
    }

    void buildTreeList(View &container, const Panda::Terrain2DAsset &asset, const TerrainFormHost &host, const Editing<Panda::FloraSettings> &editing) {
        auto section = makeFormSection("TREES");
        const Panda::FloraSettings &flora = *editing.data;
        TerrainEditorState *state = host.state;
        // The number of points per layer — in a single pass.
        std::vector<size_t> counts(flora.trees.size(), 0);
        asset.getTrees().forEach([&](const Panda::TreeInstance &tree) {
            for (size_t i = 0; i < flora.trees.size(); i++) {
                if (flora.trees[i].id == tree.layer) { counts[i]++; }
            }
        });
        for (size_t i = 0; i < flora.trees.size(); i++) {
            const Panda::TreeLayer &layer = flora.trees[i];
            const uint32_t layerId = layer.id;
            section->addContent(std::make_shared<LayerRow>(
                layer.canopyDensity > 0.f ? Ic::FloraTree : Ic::FloraRock, layer.name,
                std::to_string(counts[i]) + (counts[i] == 1 ? " tree" : " trees"), layer.enabled,
                state->selectedTree == layerId,
                [state, layerId, rebuild = host.rebuild] {
                    state->selectedTree = layerId; // also the Trees brush target (bindBrush)
                    rebuild();
                },
                [editing, i, rebuild = host.rebuild](bool enabled) {
                    editing.data->trees[i].enabled = enabled;
                    editing.emit();
                    rebuild();
                }
            ));
        }
        if (flora.trees.empty()) {
            section->addContent(makeFormHint("No layers. Trees are placed point by point: by the Trees brush or Scatter."));
        }
        auto add = makeFormButton("Add Layer…", ButtonStyle::AccentOutline, nullptr);
        add->setOnClick([editing, state, rebuild = host.rebuild](Button &button) {
            showAddTreeLayerDialog(button.getContext(), [editing, state, rebuild](Panda::TreeLayer layer) {
                uint32_t maxId = 0;
                for (const Panda::TreeLayer &existing : editing.data->trees) { maxId = std::max(maxId, existing.id); }
                layer.id = maxId + 1;
                state->selectedTree = layer.id; // new layer opens right away
                editing.data->trees.push_back(std::move(layer));
                editing.emit();
                rebuild();
            });
        });
        section->addContent(makeFormButtonRow({add}));
        container.addSubview(section);
    }

    void buildTreeLayerSection(
        View &container, uint32_t assetId, const TerrainFormHost &host, const Editing<Panda::FloraSettings> &editing
    ) {
        size_t i = editing.data->trees.size();
        for (size_t k = 0; k < editing.data->trees.size(); k++) {
            if (editing.data->trees[k].id == host.state->selectedTree) { i = k; }
        }
        if (i == editing.data->trees.size()) { return; }
        const Panda::TreeLayer &layer = editing.data->trees[i];
        const uint32_t layerId = layer.id;
        auto section = makeFormSection("TREE LAYER · " + layer.name);
        const auto number = [&](const char *title, float min, float max, float value, void (*apply)(Panda::TreeLayer &, float)) {
            addNumber<Panda::FloraSettings>(
                *section, editing, title, min, max, value,
                [i, apply](Panda::FloraSettings &flora, float v) { apply(flora.trees[i], v); }
            );
        };

        auto nameField = std::make_shared<TextField>();
        nameField->setText(layer.name);
        nameField->setOnSubmit([editing, i, rebuild = host.rebuild](TextField &f) {
            editing.data->trees[i].name = f.getText();
            editing.emit();
            rebuild();
        });
        section->addContent(std::make_shared<PropertyRow>("Name", nameField));

        addAssetRow(*section, "Mesh", "mesh", static_cast<uint32_t>(layer.mesh.getId()), [editing, i](uint32_t id) {
            editing.data->trees[i].mesh = assetRefOf(id);
            editing.emit();
        });
        addAssetRow(*section, "Material Override", "material", static_cast<uint32_t>(layer.meshMaterial.getId()),
            [editing, i](uint32_t id) {
                editing.data->trees[i].meshMaterial = assetRefOf(id);
                editing.emit();
            }
        );
        // Beyond Tree Distance (Settings → Flora) — a billboard with a baked atlas (color+normals, auto).
        addToggle<Panda::FloraSettings>(*section, editing, "Imposter", layer.imposter, [i](Panda::FloraSettings &flora, bool v) {
            flora.trees[i].imposter = v;
        });
        // Crown density for the occlusion volume (0 — does not occlude).
        number("Canopy Density", 0.f, 1.f, layer.canopyDensity, [](Panda::TreeLayer &l, float v) { l.canopyDensity = v; });
        // Scale spread of new trees (brush, Scatter); standing trees do not change.
        number("Scale Min", 0.05f, 5.f, layer.scaleMin, [](Panda::TreeLayer &l, float v) { l.scaleMin = v; });
        number("Scale Max", 0.05f, 5.f, layer.scaleMax, [](Panda::TreeLayer &l, float v) { l.scaleMax = v; });

        const std::string layerName = layer.name;
        auto scatter = makeFormButton("Scatter…", ButtonStyle::Neutral, nullptr);
        scatter->setOnClick([host, assetId, layerId, layerName](Button &button) {
            showScatterTreesDialog(
                button.getContext(), layerName,
                [host, assetId, layerId](const Panda::TreeScatterRules &rules, bool replaceExisting) {
                    scatterTrees(host, Panda::AssetId(assetId), layerId, rules, replaceExisting);
                }
            );
        });
        auto clear = makeFormButton("Clear Trees", ButtonStyle::Neutral, [host, assetId, layerId] {
            clearTrees(host, Panda::AssetId(assetId), layerId);
        });
        section->addContent(makeFormButtonRow({scatter, clear}));

        TerrainEditorState *state = host.state;
        auto remove = makeFormButton("Delete Layer", ButtonStyle::Neutral, [editing, state, i, rebuild = host.rebuild] {
            // The layer's points are taken along by editFlora — in one undo command with the layer.
            editing.data->trees.erase(editing.data->trees.begin() + static_cast<std::ptrdiff_t>(i));
            state->selectedTree = 0;
            editing.emit();
            rebuild();
        });
        section->addContent(makeFormButtonRow({remove}));
        container.addSubview(section);
    }

    // The terrain's grass palette: the ONLY input of the far-view grain mosaic and the color of the
    // Tuft blades.
    void buildGrainSection(View &container, const Editing<Panda::FloraSettings> &editing) {
        auto section = makeFormSection("GRASS PALETTE");
        const Panda::GrassGrain &grain = editing.data->grain;
        section->addContent(makeFormHint(
            "Colors of Tuft blades and of the far grain mosaic that replaces grass at a distance. "
            "Nothing else affects the mosaic: grains over Soil on splat layer 1. Colors are albedo; "
            "set the meadow darkness with Brightness, not with dark colors."
        ));
        addColor<Panda::FloraSettings>(*section, editing, "Color Root", grain.root, [](Panda::FloraSettings &f, const Panda::Color &c) { f.grain.root = c; });
        addColor<Panda::FloraSettings>(*section, editing, "Color Tip", grain.tip, [](Panda::FloraSettings &f, const Panda::Color &c) { f.grain.tip = c; });
        // The third color: "dry" plants break up the uniformity.
        addColor<Panda::FloraSettings>(*section, editing, "Color Extra", grain.extra, [](Panda::FloraSettings &f, const Panda::Color &c) { f.grain.extra = c; });
        // Soil between the grains: the remainder of the mosaic coverage.
        addColor<Panda::FloraSettings>(*section, editing, "Color Soil", grain.soil, [](Panda::FloraSettings &f, const Panda::Color &c) { f.grain.soil = c; });
        addNumber<Panda::FloraSettings>(*section, editing, "Extra Share", 0.f, 1.f, grain.extraShare, [](Panda::FloraSettings &f, float v) { f.grain.extraShare = v; });
        addNumber<Panda::FloraSettings>(*section, editing, "Grain Coverage", 0.f, 1.f, grain.coverage, [](Panda::FloraSettings &f, float v) { f.grain.coverage = v; });
        // Meadow brightness — a multiplier of the palette albedo (linear): the colors keep the hue,
        // a dark meadow is set here, not with black colors.
        addNumber<Panda::FloraSettings>(*section, editing, "Brightness", 0.f, 1.f, grain.brightness, [](Panda::FloraSettings &f, float v) { f.grain.brightness = v; });
        container.addSubview(section);
    }

    // Binding the brush to the tab: the target is ITS asset and the layers selected in ITS lists.
    // The canonical brush state is shared across the editor (it may have come from another tab) —
    // a mismatch is fixed and reported.
    void bindBrush(Panda::AssetId id, const TerrainFormHost &host, bool voxels) {
        TerrainEditorState &state = *host.state;
        if (state.assetId != id) { state = TerrainEditorState{id}; }
        Panda::TerrainBrushSettings &brush = host.tool->settings();
        Panda::TerrainToolKind tool = brush.tool;
        // A voxel terrain has a single tool — relief.
        if (voxels && tool != Panda::TerrainToolKind::None) { tool = Panda::TerrainToolKind::Sculpt; }
        const bool changed = brush.tool != tool || brush.grassLayer != state.selectedGrass ||
                             brush.treeLayer != state.selectedTree;
        brush.assetId = static_cast<uint32_t>(id); // the emit of any form edit will carry the target
        brush.tool = tool;
        brush.grassLayer = state.selectedGrass;
        brush.treeLayer = state.selectedTree;
        if (changed && brush.tool != Panda::TerrainToolKind::None) { host.tool->settingsChanged(); }
    }

} // namespace

void buildTerrainEditor2D(View &container, Panda::AssetId id, const TerrainFormHost &host) {
    Panda::AssetRef<Panda::Terrain2DAsset> asset{id};
    if (!asset.isValid() || !asset->getData().isValid()) { return; }
    const uint32_t assetId = static_cast<uint32_t>(id);
    bindBrush(id, host, false);
    // A copy of the vegetation settings: edits are sent as a whole (editFlora puts them into the
    // asset with an undo command).
    Editing<Panda::FloraSettings> editing;
    editing.data = std::make_shared<Panda::FloraSettings>(asset->getFloraSettings());
    editing.emit = [host, id, data = editing.data] { editFlora(host, id, *data); };

    buildTerrainSection2D(container, assetId, host);
    buildToolSection(container, host, editing.data.get(), false);
    buildGrassList(container, host, editing);
    buildGrassLayerSection(container, assetId, host, editing);
    buildTreeList(container, *asset.get(), host, editing);
    buildTreeLayerSection(container, assetId, host, editing);
    buildGrainSection(container, editing);
}

void buildTerrainEditor3D(View &container, Panda::AssetId id, const TerrainFormHost &host) {
    Panda::AssetRef<Panda::Terrain3DAsset> asset{id};
    if (!asset.isValid() || !asset->getData().isValid()) { return; }
    const uint32_t assetId = static_cast<uint32_t>(id);
    bindBrush(id, host, true);

    auto section = makeFormSection("TERRAIN");
    const Panda::TerrainData3D &data = asset->getData();
    char size[128];
    std::snprintf(size, sizeof(size), "%u × %u × %u voxels · %g m", data.cellsX, data.cellsY, data.cellsZ, data.cellSize);
    auto sizeLabel = std::make_shared<Label>(size);
    sizeLabel->setFont(Typo::Value);
    sizeLabel->setTextRole(TextRole::Secondary);
    section->addContent(std::make_shared<PropertyRow>("Size", sizeLabel, false));
    const uint32_t cellsX = data.cellsX;
    const uint32_t cellsY = data.cellsY;
    const uint32_t cellsZ = data.cellsZ;
    auto resize = makeFormButton("Resize…", ButtonStyle::Neutral, nullptr);
    resize->setOnClick([host, assetId, cellsX, cellsY, cellsZ](Button &button) {
        showResizeTerrain3DDialog(
            button.getContext(), cellsX, cellsY, cellsZ, [host, assetId](uint32_t x, uint32_t y, uint32_t z) {
                resizeTerrain3D(host, Panda::AssetId(assetId), x, y, z);
            }
        );
    });
    section->addContent(makeFormButtonRow({resize}));
    container.addSubview(section);

    buildToolSection(container, host, nullptr, true);
}

TerrainAssetEditor::TerrainAssetEditor(const Editor::AssetEditorContext &context, bool voxels)
    : m_context(context)
    , m_voxels(voxels) {}

void TerrainAssetEditor::build(View &container) {
    TerrainFormHost host;
    host.tool = &terrainTool();
    host.state = &m_state;
    host.rebuild = m_context.rebuild;
    host.edited = m_context.edited;
    if (m_voxels) {
        buildTerrainEditor3D(container, m_context.asset, host);
    } else {
        buildTerrainEditor2D(container, m_context.asset, host);
    }
}

} // namespace TerrainEditor
