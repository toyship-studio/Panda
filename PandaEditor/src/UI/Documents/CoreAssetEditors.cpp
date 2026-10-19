#include "UI/Documents/CoreAssetEditors.hpp"

#include "Preview/ThumbnailsManager.hpp"
#include "Project/Loader/AssetHandlerEditor.hpp"
#include "UI/EditorModelHelper.hpp"

#include <PandaEditorKit/EditorExtensions.hpp>
#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/AssetWell.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <Panda/Assets/MaterialAsset.hpp>
#include <Panda/Assets/MaterialSystemValues.hpp>
#include <Panda/Assets/PostprocessingAsset.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/WorldCommands/CommandHistory.hpp>
#include <Panda/WorldCommands/Impl/EditAssetCommands.hpp>
#include <Panda/WorldCommands/Impl/SetMeshSlotMaterialCommand.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    Panda::AssetHandlerEditor *assetHandler() {
        return static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    }

    void invalidateThumbnail(Panda::AssetId id) {
        if (auto *thumbnails = Panda::ThumbnailsManager::shared()) { thumbnails->invalidate(id); }
    }

    /* ------------------------------ Texture ------------------------------ */

    // Import settings: live — they are written to the registry meta and reload the asset (no
    // document, no dirty state).
    class TextureEditor final : public AssetEditor {
    public:
        explicit TextureEditor(const AssetEditorContext &context)
            : m_context(context) {}

        void build(View &container) override {
            const Panda::AssetId id = m_context.asset;
            auto section = std::make_shared<CollapsibleSection>("Texture Import");
            Panda::AssetInfo::TextureSettings textureMeta;
            if (Panda::AssetHandlerEditor *handler = assetHandler()) {
                const auto &registry = handler->getRegistry();
                auto infoIt = registry.find(id);
                if (infoIt != registry.end() && infoIt->second.type == Panda::AssetType::TEXTURE) {
                    textureMeta = std::get<Panda::AssetInfo::TextureSettings>(infoIt->second.settings);
                }
            }
            // The meta changes the GPU texture under the same id — the thumbnail cache drops the
            // old one.
            const auto applyFiltering = [id](int minIndex, int magIndex) {
                Panda::AssetHandlerEditor *handler = assetHandler();
                if (!handler) { return; }
                const Miren::TextureFiltering minModes[] = {Miren::NEAREST, Miren::LINEAR, Miren::LINEAR_MIPMAP_LINEAR};
                const Miren::TextureFiltering magModes[] = {Miren::NEAREST, Miren::LINEAR};
                handler->import().setTextureFiltering(id, minModes[minIndex], magModes[magIndex]);
                invalidateThumbnail(id);
            };
            auto minFilter = std::make_shared<Combobox>();
            minFilter->setItems({"Nearest", "Linear", "Linear Mipmap"});
            minFilter->setSelectedIndex(
                textureMeta.minFiltering == Miren::NEAREST  ? 0
                : textureMeta.minFiltering == Miren::LINEAR ? 1
                                                            : 2
            );
            section->addContent(std::make_shared<PropertyRow>("Min Filter", minFilter));
            auto magFilter = std::make_shared<Combobox>();
            magFilter->setItems({"Nearest", "Linear"});
            magFilter->setSelectedIndex(textureMeta.magFiltering == Miren::NEAREST ? 0 : 1);
            section->addContent(std::make_shared<PropertyRow>("Mag Filter", magFilter));
            Combobox *minFilterPtr = minFilter.get();
            Combobox *magFilterPtr = magFilter.get();
            minFilter->setOnSelectionChanged([applyFiltering, minFilterPtr, magFilterPtr](Combobox &) {
                applyFiltering(minFilterPtr->selectedIndex(), magFilterPtr->selectedIndex());
            });
            magFilter->setOnSelectionChanged([applyFiltering, minFilterPtr, magFilterPtr](Combobox &) {
                applyFiltering(minFilterPtr->selectedIndex(), magFilterPtr->selectedIndex());
            });
            auto mips = std::make_shared<Toggle>();
            mips->setOn(textureMeta.numMips > 0);
            mips->setOnValueChanged([id](Toggle &toggle) {
                // 16 is a full pyramid (backends clamp it to the actual size).
                if (Panda::AssetHandlerEditor *handler = assetHandler()) {
                    handler->import().setTextureMips(id, toggle.isOn() ? 16 : 0);
                }
                invalidateThumbnail(id);
            });
            section->addContent(std::make_shared<PropertyRow>("Generate Mips", mips, false));
            // sRGB: color textures are decoded to linear on sampling, data textures (normals,
            // masks) must stay linear.
            auto srgb = std::make_shared<Toggle>();
            srgb->setOn(textureMeta.srgb);
            srgb->setOnValueChanged([id](Toggle &toggle) {
                if (Panda::AssetHandlerEditor *handler = assetHandler()) {
                    handler->import().setTextureSrgb(id, toggle.isOn());
                }
                invalidateThumbnail(id);
            });
            section->addContent(std::make_shared<PropertyRow>("sRGB (Color Texture)", srgb, false));
            container.addSubview(section);
        }

    private:
        AssetEditorContext m_context;
    };

    /* ------------------------------ Material ------------------------------ */

    // A live editor: the shader is in the registry meta, the fields are on the live asset. Field
    // edits are undo commands of the document (a gesture merges into one step); the .mat is
    // written on Save.
    class MaterialEditor final : public AssetEditor {
    public:
        explicit MaterialEditor(const AssetEditorContext &context)
            : m_context(context) {}

        void build(View &container) override {
            const Panda::AssetId id = m_context.asset;
            Panda::AssetHandlerEditor *handler = assetHandler();
            if (!handler) { return; }
            const auto &registry = handler->getRegistry();
            auto infoIt = registry.find(id);
            if (infoIt == registry.end()) { return; }

            auto section = makeFormSection("MATERIAL");

            // The shader is a combobox: built-in variants (Unlit/Lit/PBR) + the project's registered
            // shaders, without None (meta.shader == 0 renders as Lit — shown honestly). A change
            // updates the registry meta, reloads the material (the fields are resynchronized by
            // reflection) and rebuilds this form at the next tick.
            std::vector<std::pair<Panda::AssetId, std::string>> shaderOptions = builtinShaderOptions();
            for (const auto &entry : registry) {
                if (entry.second.type == Panda::AssetType::SHADER) {
                    shaderOptions.push_back({entry.first, handler->getAssetName(entry.first)});
                }
            }
            Panda::AssetId currentShader = std::get<Panda::AssetInfo::MaterialSettings>(infoIt->second.settings).shader;
            if (currentShader == 0) { currentShader = shaderOptions[1].first; } // legacy 0 → the actual Lit
            int selectedShader = -1;
            for (std::size_t i = 0; i < shaderOptions.size(); ++i) {
                if (shaderOptions[i].first == currentShader) { selectedShader = static_cast<int>(i); }
            }
            if (selectedShader < 0) { // unknown id: show it, but do not overwrite silently
                shaderOptions.push_back({currentShader, "Unknown (" + std::to_string(currentShader) + ")"});
                selectedShader = static_cast<int>(shaderOptions.size()) - 1;
            }
            std::vector<std::string> shaderItems;
            for (const auto &option : shaderOptions) { shaderItems.push_back(option.second); }
            auto shaderBox = std::make_shared<Combobox>();
            shaderBox->setItems(shaderItems);
            shaderBox->setSelectedIndex(selectedShader);
            shaderBox->setOnSelectionChanged([this, id, shaderOptions](Combobox &box) {
                const int index = box.selectedIndex();
                if (index < 0 || index >= static_cast<int>(shaderOptions.size())) { return; }
                setShader(id, shaderOptions[index].first);
            });
            section->addContent(std::make_shared<PropertyRow>("Shader", shaderBox));

            // Material fields come from the live asset (loaded lazily); the set is synchronized
            // with the shader reflection.
            Panda::AssetRef<Panda::MaterialAsset> material{id};
            if (material) {
                for (const Panda::MaterialField &field : material->getInputs().inputs) {
                    if (field.runtime) { continue; } // fed by the system every frame — not shown to the author
                    const std::string fieldName = field.name;
                    auto emitField = [this, id, fieldName](Panda::MaterialFieldValue value) {
                        editField(id, fieldName, std::move(value));
                    };
                    // A same-named system value is set by the engine (scene wind, etc.): the field
                    // is visible with the ACTUAL value but inactive; below it — the explanation and
                    // a link to the world settings section that controls it. A tooltip does not
                    // work: a disabled view is excluded from hit-testing.
                    const bool systemField = Panda::MaterialSystemValues::isSystem(field);
                    const Panda::MaterialFieldValue &shownValue = Panda::MaterialSystemValues::resolve(field);
                    const auto markSystem = [systemField, &section, &fieldName](std::shared_ptr<View> widget) {
                        if (!systemField) { return; }
                        widget->setEnabled(false);
                        widget->surface().setOpacity(0.55f); // visually "non-editable"
                        auto hint = std::make_shared<Label>(std::string(Panda::MaterialSystemValues::hint(fieldName)));
                        hint->setFont(Typo::Meta);
                        hint->setTextRole(TextRole::Muted);
                        const std::string target = Panda::MaterialSystemValues::section(fieldName);
                        auto link = std::make_shared<Button>("Settings");
                        link->setStyle(ButtonStyle::Link);
                        link->setFont(Typo::Meta);
                        link->layout().setPadding(Edge::Horizontal, 4.f);
                        link->layout().setPadding(Edge::Vertical, 2.f);
                        link->setOnClick([target](Button &) {
                            editorHost().openSettings(Panda::SettingsScope::World, target);
                        });
                        auto row = StackView::horizontal(6.f);
                        row->setAlignment(Align::Center);
                        row->addSubview(hint);
                        row->addSubview(link);
                        // An empty title holds the label column — the explanation sits under the widget.
                        section->addContent(std::make_shared<PropertyRow>("", row, false));
                    };
                    switch (field.type) {
                        case Panda::MaterialFieldType::FLOAT: {
                            auto widget = std::make_shared<NumberField>();
                            widget->setFont(Typo::Value);
                            widget->setValue(std::get<float>(shownValue));
                            widget->setOnValueChanged([emitField](NumberField &f) { emitField(f.getValue()); });
                            section->addContent(std::make_shared<PropertyRow>(fieldName, widget));
                            markSystem(widget);
                            break;
                        }
                        case Panda::MaterialFieldType::UINT: {
                            auto widget = std::make_shared<NumberField>();
                            widget->setFont(Typo::Value);
                            widget->setValue(static_cast<float>(std::get<uint32_t>(shownValue)));
                            widget->setOnValueChanged([emitField](NumberField &f) {
                                emitField(static_cast<uint32_t>(std::max(0.f, f.getValue())));
                            });
                            section->addContent(std::make_shared<PropertyRow>(fieldName, widget));
                            markSystem(widget);
                            break;
                        }
                        // COLOR is an sRGB color (converted to linear on upload to the GPU); raw VEC4
                        // values are edited with the same widget — the value goes as is.
                        case Panda::MaterialFieldType::COLOR:
                        case Panda::MaterialFieldType::VEC4: {
                            const Panda::Vec4 v = std::get<Panda::Vec4>(shownValue);
                            auto widget = std::make_shared<ColorWell>(Color(v.x, v.y, v.z, v.w));
                            widget->setOnColorChanged([emitField](ColorWell &w) {
                                const Color c = w.getColor();
                                emitField(Panda::Vec4(c.r, c.g, c.b, c.a));
                            });
                            section->addContent(std::make_shared<PropertyRow>(fieldName, widget, false));
                            markSystem(widget);
                            break;
                        }
                        case Panda::MaterialFieldType::TEXTURE: {
                            const uint32_t textureId =
                                static_cast<uint32_t>(std::get<Panda::AssetRef<Panda::Asset>>(field.value).getId());
                            addAssetRow(*section, fieldName.c_str(), "texture", textureId, [emitField](uint32_t tid) {
                                emitField(assetRefOf(tid));
                            });
                            break;
                        }
                        default: break;
                    }
                }
            }
            container.addSubview(section);
        }

    private:
        void setShader(Panda::AssetId id, Panda::AssetId shaderId) {
            Panda::AssetHandlerEditor *handler = assetHandler();
            if (!handler) { return; }
            if (!handler->documents().setMaterialShader(id, shaderId)) { return; } // already set
            invalidateThumbnail(id);
            editorHost().log(HostLogLevel::Info, "Material shader updated");
            if (m_context.edited) { m_context.edited(); }
            // The field set has changed → rebuild the form (deferred: a widget must not be
            // destroyed from its own callback).
            if (m_context.rebuild) { m_context.rebuild(); }
        }

        void editField(Panda::AssetId id, const std::string &fieldName, Panda::MaterialFieldValue value) {
            Panda::AssetRef<Panda::MaterialAsset> material{id};
            Panda::MaterialAsset *asset = material.get();
            if (!asset || !asset->getInputs().hasField(fieldName)) { return; }
            // An undo command into the document stack; a series of gesture ticks merges into one
            // step (canMerge); execute applies it to the live asset and marks the document.
            Panda::EditMaterialFieldCommand command(
                id, fieldName, asset->getInputs().getField(fieldName).value, std::move(value)
            );
            editorHost().assetUndoStack(id).SAVE(command);
            invalidateThumbnail(id);
            if (m_context.edited) { m_context.edited(); }
        }

        AssetEditorContext m_context;
    };

    /* ------------------------------ Postprocessing ------------------------------ */

    // A postprocessing pass in terms of the form (edits go as a whole list).
    struct PassEdit {
        Panda::AssetId material = 0;
        bool enabled = true;
        uint32_t downsample = 1; // pass target resolution divisor: 1/2/4/8
    };

    // Resolution divisor of a postfx pass → the item index of the Full/1-2/1-4/1-8 combobox.
    int downsampleToIndex(uint32_t downsample) {
        if (downsample >= 8) { return 3; }
        if (downsample >= 4) { return 2; }
        if (downsample >= 2) { return 1; }
        return 0;
    }

    // The pass list: order, composition, enabled, downsample. Every edit is a discrete undo step
    // of the document and rebuilds the form at the next tick.
    class PostprocessingEditor final : public AssetEditor {
    public:
        explicit PostprocessingEditor(const AssetEditorContext &context)
            : m_context(context) {}

        void build(View &container) override {
            const Panda::AssetId id = m_context.asset;
            auto section = makeFormSection("POSTPROCESSING");
            Panda::AssetRef<Panda::PostprocessingAsset> asset{id};
            auto data = std::make_shared<std::vector<PassEdit>>();
            if (asset) {
                for (Panda::PostprocessingPass &pass : asset->getPasses()) {
                    data->push_back({pass.material.getId(), pass.enabled, pass.downsample});
                }
            }
            auto emit = [this, id, data] { commitPasses(id, *data); };

            // A chain with its own tonemap: the built-in fallback Tonemap is not added, passes
            // after the tonemap pass work in display space (glare/lens flare/afterimage). A single
            // toggle of the asset, outside the undo stack of the passes.
            {
                auto ownTonemap = std::make_shared<Toggle>();
                ownTonemap->setOn(asset ? asset->handlesTonemap() : false);
                ownTonemap->setOnValueChanged([this, id](Toggle &t) {
                    Panda::AssetRef<Panda::PostprocessingAsset> target{id};
                    if (Panda::PostprocessingAsset *postprocessing = target.get()) {
                        postprocessing->setHandlesTonemap(t.isOn());
                        postprocessing->setChanged(true);
                        if (m_context.edited) { m_context.edited(); }
                    }
                });
                section->addContent(std::make_shared<PropertyRow>("Handles Tonemap", ownTonemap, false));
            }

            // "+" in the header — add a pass to the end.
            auto add = std::make_shared<IconButton>(Ic::Plus, Ic::HeaderSize, 23.f);
            add->setCornerRadius(5.f);
            add->setTooltip("Add Pass");
            add->setOnClick([data, emit] {
                data->push_back({});
                emit();
            });
            section->setHeaderAccessory(add);

            for (std::size_t i = 0; i < data->size(); ++i) {
                // Toggle and the material well on the first line; the resolution and the buttons are one
                // block that moves under the well when the tab is too narrow for one line.
                auto row = std::make_shared<Panel>();
                row->setBackgroundColor(Color(0x00000000));
                row->layout().setFlexDirection(FlexDirection::Row);
                row->layout().setFlexWrap(Wrap::Wrap);
                row->layout().setAlignItems(Align::Center);
                row->layout().setGap(6.f);

                auto enabled = std::make_shared<Toggle>();
                enabled->setOn((*data)[i].enabled);
                enabled->setOnValueChanged([data, emit, i](Toggle &t) {
                    (*data)[i].enabled = t.isOn();
                    emit();
                });
                row->addSubview(enabled);

                // Passes are regular materials (drop from the Content Browser).
                auto well = std::make_shared<AssetWell>(
                    static_cast<uint32_t>((*data)[i].material), "material",
                    [](uint32_t materialId) { return editorHost().assetTitle(Panda::AssetId(materialId)); },
                    [data, emit, i](uint32_t materialId) {
                        (*data)[i].material = Panda::AssetId(materialId);
                        emit();
                    },
                    [](uint32_t materialId) { editorHost().selectAsset(Panda::AssetId(materialId)); }
                );
                well->layout().setMinWidth(Length::points(140.f)); // the title of the material stays readable
                row->addSubview(well);

                auto controls = std::make_shared<Panel>();
                controls->setBackgroundColor(Color(0x00000000));
                controls->layout().setFlexDirection(FlexDirection::Row);
                controls->layout().setAlignItems(Align::Center);
                controls->layout().setGap(6.f);
                controls->layout().setFlexShrink(0.f);

                // Pass target resolution: the item index = log2(divisor).
                auto resolution = std::make_shared<Combobox>();
                resolution->setItems({"Full", "1/2", "1/4", "1/8"});
                resolution->setSelectedIndex(downsampleToIndex((*data)[i].downsample));
                resolution->setOnSelectionChanged([data, emit, i](Combobox &c) {
                    (*data)[i].downsample = 1u << std::max(0, c.selectedIndex());
                    emit();
                });
                resolution->layout().setWidth(Length::points(62.f));
                controls->addSubview(resolution);

                auto up = std::make_shared<IconButton>(Ic::ChevronUp, Ic::RowSize, 22.f);
                up->setCornerRadius(5.f);
                up->setTooltip("Move Up");
                up->setOnClick([data, emit, i] {
                    if (i == 0) { return; }
                    std::swap((*data)[i - 1], (*data)[i]);
                    emit();
                });
                auto remove = std::make_shared<IconButton>(Ic::Trash, Ic::RowSize, 22.f);
                remove->setCornerRadius(5.f);
                remove->setTooltip("Remove Pass");
                remove->setOnClick([data, emit, i] {
                    data->erase(data->begin() + static_cast<std::ptrdiff_t>(i));
                    emit();
                });
                controls->addSubview(up);
                controls->addSubview(remove);
                row->addSubview(controls);

                section->addContent(std::make_shared<PropertyRow>("Pass " + std::to_string(i + 1), row));
            }
            container.addSubview(section);
        }

    private:
        void commitPasses(Panda::AssetId id, const std::vector<PassEdit> &passes) {
            Panda::AssetRef<Panda::PostprocessingAsset> asset{id};
            Panda::PostprocessingAsset *postprocessing = asset.get();
            if (!postprocessing) { return; }
            std::vector<Panda::PostprocessingPass> after;
            after.reserve(passes.size());
            for (const PassEdit &edit : passes) {
                Panda::PostprocessingPass pass;
                if (edit.material) { pass.material = Panda::AssetRef<Panda::Asset>(edit.material); }
                pass.enabled = edit.enabled;
                pass.downsample = edit.downsample;
                after.push_back(pass);
            }
            // An undo command into the document stack (the edits are discrete — each is its own
            // history step); execute applies it to the live asset.
            Panda::EditPostprocessingCommand command(id, postprocessing->getPasses(), std::move(after));
            editorHost().assetUndoStack(id).SAVE(command);
            if (m_context.edited) { m_context.edited(); }
            // The list changes structurally (add/remove/move) → rebuild the form deferred.
            if (m_context.rebuild) { m_context.rebuild(); }
        }

        AssetEditorContext m_context;
    };

    /* ------------------------------ Mesh ------------------------------ */

    // The mesh document is the slot layout (the data file is untouchable): a slot replacement is
    // a discrete undo step of the document; Save writes the sidecar.
    class MeshEditor final : public AssetEditor {
    public:
        explicit MeshEditor(const AssetEditorContext &context)
            : m_context(context) {}

        void build(View &container) override {
            const Panda::AssetId id = m_context.asset;
            Panda::AssetHandlerEditor *handler = assetHandler();
            if (!handler) { return; }
            const auto &registry = handler->getRegistry();
            auto infoIt = registry.find(id);
            if (infoIt == registry.end()) { return; }
            const auto *meshMeta = std::get_if<Panda::AssetInfo::MeshSettings>(&infoIt->second.settings);
            if (!meshMeta) { return; }

            auto section = makeFormSection("MATERIALS");
            Panda::AssetRef<Panda::ImportedMeshAsset> mesh{id};
            Panda::ImportedMeshAsset *meshAsset = mesh.get();
            for (size_t i = 0; i < meshMeta->materials.size(); i++) {
                const std::string slotName =
                    meshMeta->materials[i].name.empty() ? "Slot " + std::to_string(i) : meshMeta->materials[i].name;
                uint32_t current = 0;
                if (meshAsset && i < meshAsset->getSlotMaterials().size()) {
                    current = static_cast<uint32_t>(meshAsset->getSlotMaterials()[i].getId());
                }
                addAssetRow(*section, slotName.c_str(), "material", current, [this, id, i](uint32_t materialId) {
                    setSlotMaterial(id, i, Panda::AssetId(materialId));
                });
            }
            if (meshMeta->materials.empty()) {
                auto label = std::make_shared<Label>("No material slots");
                label->setFont(Font(15.f));
                label->setTextRole(TextRole::Secondary);
                section->addContent(std::make_shared<PropertyRow>("", label, false));
            }
            container.addSubview(section);
        }

    private:
        void setSlotMaterial(Panda::AssetId meshId, size_t slot, Panda::AssetId materialId) {
            Panda::AssetRef<Panda::ImportedMeshAsset> mesh{meshId};
            Panda::ImportedMeshAsset *asset = mesh.get();
            if (!asset || slot >= asset->getSlotMaterials().size()) { return; }
            const Panda::AssetId before = asset->getSlotMaterials()[slot].getId();
            if (before == materialId) { return; }
            Panda::SetMeshSlotMaterialCommand command(meshId, slot, before, materialId);
            editorHost().assetUndoStack(meshId).SAVE(command); // execute applies it and marks the asset
            if (m_context.edited) { m_context.edited(); }
        }

        AssetEditorContext m_context;
    };

    std::unique_ptr<AssetEditor> createTextureEditor(const AssetEditorContext &context) {
        return std::make_unique<TextureEditor>(context);
    }
    std::unique_ptr<AssetEditor> createMaterialEditor(const AssetEditorContext &context) {
        return std::make_unique<MaterialEditor>(context);
    }
    std::unique_ptr<AssetEditor> createPostprocessingEditor(const AssetEditorContext &context) {
        return std::make_unique<PostprocessingEditor>(context);
    }
    std::unique_ptr<AssetEditor> createMeshEditor(const AssetEditorContext &context) {
        return std::make_unique<MeshEditor>(context);
    }

} // namespace

void registerCoreAssetKinds(EditorExtensions &extensions) {
    // The kind names are the editor's type strings (EditorModelHelper::assetTypeString); the icons
    // and colors are the ones of the Content Browser.
    extensions.assetKinds.add({"texture", Ic::FilePng, Color(0x12B69DFF), createTextureEditor});
    extensions.assetKinds.add({"material", Ic::Material, Color(0x57B894FF), createMaterialEditor});
    extensions.assetKinds.add({"postfx", Ic::Sprite, Color(0xD16FA8FF), createPostprocessingEditor});
    extensions.assetKinds.add({"mesh", Ic::Mesh, Color(0x8B7FD1FF), createMeshEditor});
}

} // namespace Editor
