#include "Panda/TerrainEditor/TerrainDialogs.hpp"

#include "Panda/TerrainEditor/TerrainIcons.hpp"

#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

namespace TerrainEditor {

using namespace Editor;
using namespace PandaUI;

namespace {

    std::shared_ptr<Panel> makeForm() {
        auto form = std::make_shared<Panel>();
        form->setBackgroundColor(Color(0x00000000));
        form->layout().setFlexDirection(FlexDirection::Column);
        form->layout().setGap(6.f);
        return form;
    }

    // A number row of the dialog form: the value is written into state by reference (state lives
    // in the form's shared_ptr and outlives the button callback).
    template<typename T>
    void addNumberRow(Panel &form, const char *title, float min, float max, T &target, int decimals = -1) {
        auto field = std::make_shared<NumberField>();
        field->setFont(Typo::Value);
        field->setRange(min, max);
        field->setDecimals(decimals);
        field->setValue(static_cast<float>(target));
        T *value = &target;
        field->setOnValueChanged([value](NumberField &f) { *value = static_cast<T>(f.getValue()); });
        form.addSubview(std::make_shared<PropertyRow>(title, field));
    }

    // ---------------------------------------------------------------- layer templates

    // Template card: kind icon + title + description.
    class TemplateCard final : public Control {
    public:
        TemplateCard(const char *icon, const char *title, const char *description, std::function<void()> onPick)
            : m_onPick(std::move(onPick)) {
            setCursor(Cursor::PointingHand);
            layout().setFlexDirection(FlexDirection::Column);
            layout().setAlignItems(Align::Center);
            layout().setFlexGrow(1.f);
            layout().setFlexBasis(0.f); // cards of a row have equal width
            layout().setPadding(12.f);
            layout().setGap(8.f);
            surface().setCornerRadius(8.f);
            surface().setBorderWidth(1.f);

            m_icon = makeIcon(icon, 64.f);
            addSubview(m_icon);
            auto titleLabel = std::make_shared<Label>(title);
            titleLabel->setFont(Typo::Body.withWeight(FontWeight::Medium));
            addSubview(titleLabel);
            auto descriptionLabel = std::make_shared<Label>(description);
            descriptionLabel->setFont(Typo::Meta);
            descriptionLabel->setTextRole(TextRole::Muted);
            descriptionLabel->setNumberOfLines(0);
            descriptionLabel->setTextAlignment(TextAlignment::Center);
            descriptionLabel->layout().setWidth(Length::percent(100.f));
            addSubview(descriptionLabel);
            applyStyle();
        }

        void setPicked(bool picked) {
            m_picked = picked;
            applyStyle();
        }

    private:
        void performPrimaryAction() override {
            if (m_onPick) { m_onPick(); }
        }
        void controlStateChanged(ControlState, ControlState) override {
            applyStyle();
        }
        void themeChanged() override {
            applyStyle();
        }
        void applyStyle() {
            const Theme &t = Theme::current();
            const bool hot = hasControlState(ControlState::Hovered) || hasControlState(ControlState::Focused);
            m_icon->setTintColor(m_picked ? t.accentText : (hot ? t.textPrimary : t.textMuted));
            setBackgroundColor(m_picked ? t.accentSoft : (hot ? t.control : t.input));
            surface().setBorderColor(m_picked ? t.accent : (hot ? t.borderStrong : t.borderMed));
        }

        std::function<void()> m_onPick;
        std::shared_ptr<ImageView> m_icon;
        bool m_picked = false;
    };

    template<typename Layer>
    struct LayerTemplate {
        const char *icon;
        const char *title;
        const char *description;
        Layer layer; // template defaults (the name is the initial value of the Name field)
    };

    // The common "template + name" dialog of both lists.
    template<typename Layer>
    void showAddLayerDialog(
        Context *context, const char *title, std::vector<LayerTemplate<Layer>> templates,
        std::function<void(Layer)> onAdd
    ) {
        if (!context || templates.empty()) { return; }
        struct State {
            std::vector<LayerTemplate<Layer>> templates;
            std::vector<TemplateCard *> cards; // live in the dialog; card callbacks are called while it is open
            size_t picked = 0;
            bool nameEdited = false;
        };
        auto state = std::make_shared<State>();
        state->templates = std::move(templates);

        auto dialog = std::make_shared<Dialog>(title);
        dialog->setContentWidth(180.f * static_cast<float>(state->templates.size()) + 40.f);

        auto body = makeForm();
        body->layout().setGap(14.f);
        auto cardRow = std::make_shared<Panel>();
        cardRow->setBackgroundColor(Color(0x00000000));
        cardRow->layout().setFlexDirection(FlexDirection::Row);
        cardRow->layout().setGap(10.f);
        body->addSubview(cardRow);

        auto nameField = std::make_shared<TextField>();
        nameField->setText(state->templates.front().layer.name);
        body->addSubview(std::make_shared<PropertyRow>("Name", nameField));
        TextField *name = nameField.get();
        nameField->setOnTextChanged([state](TextField &) { state->nameEdited = true; });

        for (size_t i = 0; i < state->templates.size(); i++) {
            const LayerTemplate<Layer> &entry = state->templates[i];
            auto card = std::make_shared<TemplateCard>(entry.icon, entry.title, entry.description, [state, name, i] {
                state->picked = i;
                for (size_t k = 0; k < state->cards.size(); k++) { state->cards[k]->setPicked(k == i); }
                // The name follows the template until the user enters their own.
                if (!state->nameEdited) {
                    name->setText(state->templates[i].layer.name);
                    state->nameEdited = false; // setText calls onTextChanged
                }
            });
            card->setPicked(i == 0);
            state->cards.push_back(card.get());
            cardRow->addSubview(card);
        }
        dialog->setBody(body);

        dialog->addAction("Cancel", DialogActionRole::Cancel);
        dialog->addAction("Add Layer", DialogActionRole::Primary, [state, name, onAdd = std::move(onAdd)] {
            Layer layer = state->templates[state->picked].layer;
            if (!name->getText().empty()) { layer.name = name->getText(); }
            if (onAdd) { onAdd(std::move(layer)); }
        });
        context->modals().present(dialog);
    }

} // namespace

void showAddGrassLayerDialog(Context *context, std::function<void(Panda::GrassLayer)> onAdd) {
    Panda::GrassLayer tuft;
    tuft.name = "Grass";

    Panda::GrassLayer grassMesh;
    grassMesh.name = "Grass Mesh";
    grassMesh.kind = Panda::GrassKind::Mesh;
    grassMesh.spacing = 0.3f; // tufts are sown densely
    grassMesh.scaleMin = 0.9f; // for meshes scale is a size multiplier, not a height in meters
    grassMesh.scaleMax = 1.1f;

    Panda::GrassLayer bush;
    bush.name = "Bush";
    bush.kind = Panda::GrassKind::Mesh;
    bush.spacing = 4.f;
    bush.scaleMin = 0.8f;
    bush.scaleMax = 1.2f;
    bush.noiseCoverage = 0.5f; // bushes grow in patches
    bush.castShadows = true;

    showAddLayerDialog<Panda::GrassLayer>(
        context, "Add Grass Layer",
        {
            {Ic::FloraTuft, "Grass Tuft", "Built-in blades. Wind, terrain grass palette.", tuft},
            {Ic::FloraGrassMesh, "Grass Mesh", "Authored tufts and flowers, sown densely.", grassMesh},
            {Ic::FloraBush, "Bush", "Mesh sown sparsely in patches. Casts shadows.", bush},
        },
        std::move(onAdd)
    );
}

void showAddTreeLayerDialog(Context *context, std::function<void(Panda::TreeLayer)> onAdd) {
    Panda::TreeLayer tree;
    tree.name = "Tree";

    Panda::TreeLayer rock;
    rock.name = "Rock";
    rock.canopyDensity = 0.f; // a boulder does not block the sky
    rock.imposter = false;
    rock.scaleMin = 0.6f;
    rock.scaleMax = 1.6f;

    showAddLayerDialog<Panda::TreeLayer>(
        context, "Add Tree Layer",
        {
            {Ic::FloraTree, "Tree", "Placed point by point. Billboard far away, canopy shading.", tree},
            {Ic::FloraRock, "Rock", "Any placed object: boulders, stumps. No billboard.", rock},
        },
        std::move(onAdd)
    );
}

void showResizeTerrain2DDialog(
    Context *context, uint32_t cellsX, uint32_t cellsZ, float cellSize,
    std::function<void(uint32_t, uint32_t)> onApply
) {
    if (!context) { return; }
    struct State {
        uint32_t x;
        uint32_t z;
    };
    auto state = std::make_shared<State>(State{cellsX, cellsZ});

    auto dialog = std::make_shared<Dialog>("Resize Terrain");
    char message[256];
    std::snprintf(
        message, sizeof(message),
        "Size in cells, cell = %g m. Existing relief is kept where the grids overlap, new area continues "
        "the edge. Trees outside the new bounds are removed. Undo history of the terrain is cleared.",
        cellSize
    );
    dialog->setMessage(message);
    auto form = makeForm();
    addNumberRow(*form, "Size X", 16.f, 4096.f, state->x, 0);
    addNumberRow(*form, "Size Z", 16.f, 4096.f, state->z, 0);
    dialog->setBody(form);
    dialog->addAction("Cancel", DialogActionRole::Cancel);
    dialog->addAction("Resize", DialogActionRole::Primary, [state, onApply = std::move(onApply)] {
        if (onApply) { onApply(state->x, state->z); }
    });
    context->modals().present(dialog);
}

void showResizeTerrain3DDialog(
    Context *context, uint32_t cellsX, uint32_t cellsY, uint32_t cellsZ,
    std::function<void(uint32_t, uint32_t, uint32_t)> onApply
) {
    if (!context) { return; }
    struct State {
        uint32_t x;
        uint32_t y;
        uint32_t z;
    };
    auto state = std::make_shared<State>(State{cellsX, cellsY, cellsZ});

    auto dialog = std::make_shared<Dialog>("Resize Terrain");
    dialog->setMessage(
        "Size of the voxel grid. Existing voxels are kept where the grids overlap. "
        "Undo history of the terrain is cleared."
    );
    auto form = makeForm();
    addNumberRow(*form, "Size X", 16.f, 1024.f, state->x, 0);
    addNumberRow(*form, "Size Y", 16.f, 1024.f, state->y, 0);
    addNumberRow(*form, "Size Z", 16.f, 1024.f, state->z, 0);
    dialog->setBody(form);
    dialog->addAction("Cancel", DialogActionRole::Cancel);
    dialog->addAction("Resize", DialogActionRole::Primary, [state, onApply = std::move(onApply)] {
        if (onApply) { onApply(state->x, state->y, state->z); }
    });
    context->modals().present(dialog);
}

void showImportHeightmapDialog(
    Context *context, std::function<void(const std::string &, float)> onImport
) {
    if (!context) { return; }
    struct State {
        std::string path;
        float heightScale = 30.f;
    };
    auto state = std::make_shared<State>();

    auto dialog = std::make_shared<Dialog>("Import Heightmap");
    dialog->setMessage(
        "A grayscale image is resampled onto the terrain grid (terrain size is kept): black = 0 m, "
        "white = Height Scale. The relief is replaced, undo history of the terrain is cleared."
    );
    auto form = makeForm();

    auto fileRow = std::make_shared<Panel>();
    fileRow->setBackgroundColor(Color(0x00000000));
    fileRow->layout().setFlexDirection(FlexDirection::Row);
    fileRow->layout().setAlignItems(Align::Center);
    fileRow->layout().setGap(8.f);
    auto fileLabel = std::make_shared<Label>("No file selected");
    fileLabel->setFont(Typo::Caption);
    fileLabel->setTextRole(TextRole::Dim);
    fileLabel->layout().setFlexGrow(1.f);
    fileLabel->layout().setFlexShrink(1.f);
    fileRow->addSubview(fileLabel);
    auto browse = std::make_shared<Button>("Browse…");
    browse->setSize(ButtonSize::Small);
    browse->layout().setFlexShrink(0.f);
    fileRow->addSubview(browse);
    form->addSubview(std::make_shared<PropertyRow>("Image", fileRow));
    addNumberRow(*form, "Height Scale", 1.f, 2000.f, state->heightScale);
    dialog->setBody(form);

    dialog->addAction("Cancel", DialogActionRole::Cancel);
    auto import = dialog->addAction("Import", DialogActionRole::Primary, [state, onImport = std::move(onImport)] {
        if (onImport) { onImport(state->path, state->heightScale); }
    });
    import->setEnabled(false); // until a file is chosen
    Button *importButton = import.get();
    Label *fileLabelPtr = fileLabel.get();
    browse->setOnClick([state, importButton, fileLabelPtr](Button &) {
        const std::optional<Panda::path_t> path = editorHost().openFileDialog();
        if (!path) { return; }
        state->path = path->string();
        fileLabelPtr->setText(path->filename().string());
        fileLabelPtr->setTextRole(TextRole::Primary);
        importButton->setEnabled(true);
    });
    context->modals().present(dialog);
}

void showScatterTreesDialog(
    Context *context, const std::string &layerName,
    std::function<void(const Panda::TreeScatterRules &, bool)> onScatter
) {
    if (!context) { return; }
    struct State {
        Panda::TreeScatterRules rules;
        bool replaceExisting = true;
    };
    auto state = std::make_shared<State>();

    auto dialog = std::make_shared<Dialog>("Scatter Trees — " + layerName);
    dialog->setMessage(
        "Plants trees of the layer over the whole terrain by rules. The result is ordinary tree "
        "points: refine them with the Trees brush. One undo step."
    );
    auto form = makeForm();
    addNumberRow(*form, "Spacing (m)", 0.5f, 200.f, state->rules.spacing);

    auto soil = std::make_shared<Combobox>();
    soil->setItems({"Any", "Layer 1", "Layer 2", "Layer 3", "Layer 4"});
    soil->setSelectedIndex(static_cast<int>(state->rules.soilLayer));
    soil->setOnSelectionChanged([state](Combobox &c) {
        state->rules.soilLayer = static_cast<uint32_t>(std::max(c.selectedIndex(), 0));
    });
    form->addSubview(std::make_shared<PropertyRow>("Soil Layer", soil));

    addNumberRow(*form, "Min Height", -1000.f, 1000.f, state->rules.minHeight);
    addNumberRow(*form, "Max Height", -1000.f, 1000.f, state->rules.maxHeight);
    addNumberRow(*form, "Max Slope", 0.f, 90.f, state->rules.maxSlopeDeg);
    addNumberRow(*form, "Noise Coverage", 0.f, 1.f, state->rules.noiseCoverage);
    addNumberRow(*form, "Seed", 1.f, 100000.f, state->rules.seed, 0);

    auto replace = std::make_shared<Toggle>();
    replace->setOn(state->replaceExisting);
    replace->setOnValueChanged([state](Toggle &t) { state->replaceExisting = t.isOn(); });
    form->addSubview(std::make_shared<PropertyRow>("Replace Existing", replace, false));
    dialog->setBody(form);

    dialog->addAction("Cancel", DialogActionRole::Cancel);
    dialog->addAction("Scatter", DialogActionRole::Primary, [state, onScatter = std::move(onScatter)] {
        if (onScatter) { onScatter(state->rules, state->replaceExisting); }
    });
    context->modals().present(dialog);
}

} // namespace TerrainEditor
