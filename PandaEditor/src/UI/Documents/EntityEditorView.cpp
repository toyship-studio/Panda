#include "UI/Documents/EntityEditorView.hpp"

#include "UI/EditorDialogs.hpp"
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>
#include "Project/Loader/AssetHandlerEditor.hpp"
#include <PandaEditorKit/Widgets/AssetWell.hpp>

#include <Panda/Assets/Base/AssetRef.hpp>
#include <Panda/GameLogic/Components/TransformComponent.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    // Whether the field values differ among the selected entities → the row is "mixed".
    template <class Getter>
    bool anyDiffers(std::vector<Panda::Entity> &items, Getter get) {
        if (items.size() < 2) { return false; }
        auto first = get(items.front());
        for (std::size_t i = 1; i < items.size(); ++i) {
            if (!(get(items[i]) == first)) { return true; }
        }
        return false;
    }

    // The same for a field of the engine component T among the selected entities that have T.
    template<typename EngineComp, typename Get>
    bool typedFieldMixed(std::vector<Panda::Entity> &entities, Get get) {
        bool found = false;
        decltype(get(std::declval<EngineComp &>())) first{};
        for (Panda::Entity &e : entities) {
            if (e.hasComponent<EngineComp>()) {
                EngineComp &c = e.getComponent<EngineComp>();
                if (!found) { first = get(c); found = true; }
                else if (!(get(c) == first)) { return true; }
            }
        }
        return false;
    }

    void bindVec(
        Vector3Field &field, PropertyRow &row, std::function<glm::vec3(Panda::Entity &)> get,
        std::vector<Panda::Entity> &items, Panda::Entity &primary
    ) {
        const bool mx = anyDiffers(items, [&](Panda::Entity &e) { return get(e).x; });
        const bool my = anyDiffers(items, [&](Panda::Entity &e) { return get(e).y; });
        const bool mz = anyDiffers(items, [&](Panda::Entity &e) { return get(e).z; });
        const glm::vec3 v = get(primary);
        field.setValue(v.x, v.y, v.z); // clears mixed and sets the primary values
        field.setMixed(mx, my, mz);    // restore "—" on the differing axes
        row.setMixed(mx || my || mz);
    }

    std::shared_ptr<Panel> makeThinDivider() {
        auto divider = std::make_shared<Panel>();
        divider->setUserInteractionEnabled(false);
        divider->layout().setHeight(Length::points(1.f));
        divider->setBackgroundColor(ThemeColor::borderFaint);
        return divider;
    }

    bool entityHasComponent(Panda::Entity &e, ComponentType type) {
        switch (type) {
            case ComponentType::Camera: return e.hasComponent<Panda::CameraComponent>();
            case ComponentType::DirectionalLight: return e.hasComponent<Panda::DirectionalLightComponent>();
            case ComponentType::PointLight: return e.hasComponent<Panda::PointLightComponent>();
            case ComponentType::SpriteRenderer: return e.hasComponent<Panda::SpriteRendererComponent>();
            case ComponentType::RuntimeMesh: return e.hasComponent<Panda::RuntimeMeshComponent>();
            case ComponentType::PrimitiveMesh: return e.hasComponent<Panda::PrimitiveMeshComponent>();
            case ComponentType::ImportedSubmesh: return e.hasComponent<Panda::ImportedSubmeshComponent>();
            case ComponentType::Sky: return e.hasComponent<Panda::SkyComponent>();
            case ComponentType::Script: return e.hasComponent<Panda::ScriptListComponent>();
        }
        return false;
    }

} // namespace

EntityEditorView::EntityEditorView(EntityEditorOutput *output)
    : m_output(output) {
    addSubviews();
    setupLayout();
    setupStyle();
}

void EntityEditorView::addSubviews() {
    // PLAY mode banner: edits go to the live runtime world (Stop discards them, Save bakes them
    // into the scene).
    m_runtimeBanner = std::make_shared<Label>("Runtime world — Stop discards changes, Save bakes them");
    m_runtimeBanner->setFont(Typo::Caption);
    m_runtimeBanner->setTextAlignment(TextAlignment::Center);
    m_runtimeBanner->setTextColor(ThemeColor::warning);
    m_runtimeBanner->setBackgroundColor(ThemeColor::warning.withAlpha(0.12f));
    m_runtimeBanner->layout().setPadding(Edge::Horizontal, 8.f);
    m_runtimeBanner->layout().setPadding(Edge::Vertical, 4.f);
    m_runtimeBanner->setHidden(true);
    m_runtimeBanner->layout().setDisplay(Display::None);
    addSubview(m_runtimeBanner);

    /* --- Entity form --- */
    m_body = std::make_shared<ScrollView>();
    m_body->setScrollDirection(ScrollDirection::Vertical);
    m_body->layout().setFlexGrow(1.f);
    m_body->getContentView()->layout().setFlexDirection(FlexDirection::Column);
    m_body->getContentView()->layout().setPadding(Edge::Horizontal, 4.f);
    m_body->getContentView()->layout().setPadding(Edge::Vertical, 0.f);
    m_body->getContentView()->layout().setGap(0.f);
    addSubview(m_body);

    m_name = std::make_shared<TextField>();
    m_name->layout().setFlexGrow(1.f);
    // Static is a tag on the entity (not a component section): a flag next to the name, as in Unity.
    m_staticLabel = std::make_shared<Label>("Static");
    m_static = std::make_shared<Toggle>();
    auto nameRow = std::make_shared<Panel>();
    nameRow->setBackgroundColor(Color(0x00000000));
    nameRow->layout().setFlexDirection(FlexDirection::Row);
    nameRow->layout().setAlignItems(Align::Center);
    nameRow->layout().setPadding(Edge::Horizontal, 8.f);
    nameRow->layout().setPadding(Edge::Vertical, 6.f);
    nameRow->layout().setGap(8.f);
    nameRow->addSubview(m_name);
    nameRow->addSubview(m_staticLabel);
    nameRow->addSubview(m_static);
    m_body->addContentSubview(nameRow);
    m_body->addContentSubview(makeThinDivider());

    auto transform = std::make_shared<CollapsibleSection>("TRANSFORM");
    styleComponentSection(*transform);
    m_position = std::make_shared<Vector3Field>();
    m_positionRow = std::make_shared<PropertyRow>("Position", m_position);
    transform->addContent(m_positionRow);
    m_rotation = std::make_shared<Vector3Field>();
    m_rotationRow = std::make_shared<PropertyRow>("Rotation", m_rotation);
    transform->addContent(m_rotationRow);
    m_scale = std::make_shared<Vector3Field>();
    m_scaleRow = std::make_shared<PropertyRow>("Scale", m_scale);
    transform->addContent(m_scaleRow);
    m_body->addContentSubview(transform);

    m_componentsContainer = std::make_shared<Panel>();
    m_componentsContainer->setBackgroundColor(Color(0x00000000));
    m_componentsContainer->layout().setFlexDirection(FlexDirection::Column);
    m_componentsContainer->layout().setGap(0.f);
    m_body->addContentSubview(m_componentsContainer);

    m_addComponent = std::make_shared<MenuButton>("Add Component");
    m_addComponent->setStyle(ButtonStyle::AccentOutline);
    m_addComponent->layout().setAlignSelf(Align::Center);
    m_addComponent->layout().setMargin(Edge::Top, 8.f);
    m_body->addContentSubview(m_addComponent);

    /* --- Empty state --- */
    m_placeholder = std::make_shared<Panel>();
    m_placeholder->setBackgroundColor(Color(0x00000000));
    m_placeholder->layout().setFlexGrow(1.f);
    m_placeholder->layout().setJustifyContent(Justify::Center);
    m_placeholder->layout().setAlignItems(Align::Center);
    auto placeholderLabel = std::make_shared<Label>("Select an object or asset");
    placeholderLabel->setFont(Typo::Caption);
    placeholderLabel->setTextRole(TextRole::Muted);
    m_placeholder->addSubview(placeholderLabel);
    addSubview(m_placeholder);

    wireEditing();
    showMode(Mode::Empty);
}

void EntityEditorView::wireEditing() {
    m_name->setOnSubmit([this](TextField &f) {
        if (!m_binding) { m_output->entityDidEditName(f.getText()); }
    });
    m_static->setOnValueChanged([this](Toggle &t) {
        if (!m_binding) { m_output->entityDidSetStatic(t.isOn()); }
    });
    m_position->setOnValueChanged([this](Vector3Field &f) {
        if (!m_binding) { m_output->entityDidEditPosition(glm::vec3(f.x(), f.y(), f.z())); }
    });
    m_rotation->setOnValueChanged([this](Vector3Field &f) {
        if (!m_binding) { m_output->entityDidEditRotation(glm::vec3(f.x(), f.y(), f.z())); }
    });
    m_scale->setOnValueChanged([this](Vector3Field &f) {
        if (!m_binding) { m_output->entityDidEditScale(glm::vec3(f.x(), f.y(), f.z())); }
    });
}

void EntityEditorView::setEntities(const std::vector<Panda::Entity> &entities) {
    if (entities.empty()) {
        showMode(Mode::Empty);
        return;
    }
    showMode(Mode::Entity);
    bindEntities(entities);
}

void EntityEditorView::setRuntimeMode(bool runtime) {
    if (!m_runtimeBanner) { return; }
    m_runtimeBanner->setHidden(!runtime);
    m_runtimeBanner->layout().setDisplay(runtime ? Display::Flex : Display::None);
}

void EntityEditorView::refreshTransform() {
    // Scripts in PLAY could have deleted part of the selection between pushes — dead ones are
    // dropped; the full form rebuild comes from syncSelectionFromEngine by the selection version.
    m_entities.erase(
        std::remove_if(
            m_entities.begin(), m_entities.end(), [](Panda::Entity &e) { return !e.isValid(); }
        ),
        m_entities.end()
    );
    if (m_entities.empty()) { return; }
    Panda::Entity primary = m_entities.front();
    if (!primary.isValid()) { return; }
    m_binding = true; // setValue must not emit edits back
    bindVec(*m_position, *m_positionRow, [](Panda::Entity &e) { return e.getTransform().getPosition(); }, m_entities, primary);
    bindVec(*m_rotation, *m_rotationRow, [](Panda::Entity &e) { return e.getTransform().getRotationEuler(); }, m_entities, primary);
    bindVec(*m_scale, *m_scaleRow, [](Panda::Entity &e) { return e.getTransform().getScale(); }, m_entities, primary);
    m_binding = false;
}

void EntityEditorView::setScriptClassProvider(std::function<std::vector<std::string>()> provider) {
    m_scriptClassProvider = std::move(provider);
}

std::string EntityEditorView::assetNameFor(uint32_t id) const {
    if (id == 0) { return {}; }
    auto *handler = static_cast<Panda::AssetHandlerEditor *>(Panda::GameContext::getAssetHandler());
    if (!handler) { return {}; }
    const auto &registry = handler->getRegistry();
    auto it = registry.find(Panda::AssetId(id));
    return it != registry.end() ? handler->getAssetName(it->first) : std::string();
}

void EntityEditorView::bindEntities(const std::vector<Panda::Entity> &entities) {
    m_entities = entities;
    Panda::Entity primary = entities.front();
    m_binding = true;

    const bool nameMixed = anyDiffers(m_entities, [](Panda::Entity &e) { return e.getName(); });
    m_name->setPlaceholder(nameMixed ? "Mixed" : "");
    m_name->setText(nameMixed ? "" : primary.getName());
    const bool staticMixed =
        anyDiffers(m_entities, [](Panda::Entity &e) { return e.hasComponent<Panda::IsStaticComponent>(); });
    m_static->setOn(!staticMixed && primary.hasComponent<Panda::IsStaticComponent>());
    m_staticLabel->setText(staticMixed ? "* Static" : "Static");

    bindVec(*m_position, *m_positionRow, [](Panda::Entity &e) { return e.getTransform().getPosition(); }, m_entities, primary);
    bindVec(*m_rotation, *m_rotationRow, [](Panda::Entity &e) { return e.getTransform().getRotationEuler(); }, m_entities, primary);
    bindVec(*m_scale, *m_scaleRow, [](Panda::Entity &e) { return e.getTransform().getScale(); }, m_entities, primary);

    m_binding = false;
    rebuildComponents(primary);
}

void EntityEditorView::rebuildComponents(Panda::Entity primary) {
    m_componentsContainer->removeAllSubviews();
    auto add = [this](std::shared_ptr<CollapsibleSection> section) {
        m_componentsContainer->addSubview(makeThinDivider());
        m_componentsContainer->addSubview(section);
    };
    if (primary.hasComponent<Panda::CameraComponent>()) { add(buildCameraSection(primary.getComponent<Panda::CameraComponent>())); }
    if (primary.hasComponent<Panda::SpriteRendererComponent>()) { add(buildSpriteRendererSection(primary.getComponent<Panda::SpriteRendererComponent>())); }
    if (primary.hasComponent<Panda::RuntimeMeshComponent>()) { add(buildRuntimeMeshSection(primary.getComponent<Panda::RuntimeMeshComponent>())); }
    if (primary.hasComponent<Panda::PrimitiveMeshComponent>()) { add(buildPrimitiveMeshSection(primary.getComponent<Panda::PrimitiveMeshComponent>())); }
    if (primary.hasComponent<Panda::ImportedSubmeshComponent>()) { add(buildImportedSubmeshSection(primary.getComponent<Panda::ImportedSubmeshComponent>())); }
    if (primary.hasComponent<Panda::SkyComponent>()) { add(buildSkySection(primary.getComponent<Panda::SkyComponent>())); }
    // Module components (the editor registry) — after the built-in ones of the same group.
    const ComponentInspectorRegistry &inspectors = editorExtensions().componentInspectors;
    for (const ComponentInspector &inspector : inspectors.inspectors()) {
        if (inspectors.has(inspector, primary)) { add(buildExtensionSection(inspector, primary)); }
    }
    if (primary.hasComponent<Panda::DirectionalLightComponent>()) { add(buildDirectionalLightSection(primary.getComponent<Panda::DirectionalLightComponent>())); }
    if (primary.hasComponent<Panda::PointLightComponent>()) { add(buildPointLightSection(primary.getComponent<Panda::PointLightComponent>())); }
    if (primary.hasComponent<Panda::ScriptListComponent>()) {
        Panda::ScriptListComponent &slc = primary.getComponent<Panda::ScriptListComponent>();
        for (int i = 0; i < static_cast<int>(slc.scripts.size()); ++i) { add(buildScriptSection(slc.scripts[i], i)); }
    }

    m_addComponent->clearItems();
    for (ComponentType type : addableComponentTypes()) {
        if (type != ComponentType::Script && entityHasComponent(primary, type)) { continue; }
        if (type == ComponentType::Script) {
            m_addComponent->addItem(componentTypeName(type), [this] {
                openScriptPicker(m_addComponent.get(), [this](const std::string &name) { m_output->entityDidAddScript(name); });
            });
        } else {
            m_addComponent->addItem(componentTypeName(type), [this, type] { m_output->entityDidAddComponent(type); });
        }
    }
    for (const ComponentInspector &inspector : inspectors.inspectors()) {
        if (inspectors.has(inspector, primary)) { continue; }
        const std::string component = inspector.component;
        m_addComponent->addItem(inspector.displayName, [this, component] {
            m_output->entityDidAddExtensionComponent(component);
        });
    }
}

std::shared_ptr<CollapsibleSection>
EntityEditorView::buildExtensionSection(const ComponentInspector &inspector, Panda::Entity primary) {
    std::string title = inspector.displayName;
    for (char &c : title) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    auto section = std::make_shared<CollapsibleSection>(title);
    styleComponentSection(*section);
    const std::string component = inspector.component;
    addComponentHeaderMenu(*section, [this, component] { m_output->entityDidRemoveExtensionComponent(component); });
    Panda::World *world = primary.getWorld();
    if (world != nullptr && inspector.build != nullptr) {
        const ComponentInspectorContext context{*world, m_entities};
        inspector.build(*section, context);
    }
    return section;
}

void EntityEditorView::openScriptPicker(View *anchor, std::function<void(const std::string &)> onPick) {
    Context *context = getContext();
    if (!context) { return; }
    Point at(0.f, 0.f);
    if (anchor) {
        const Rect frame = anchor->getWorldFrame();
        at = Point(frame.origin.x, frame.origin.y + frame.size.height);
    }
    std::vector<std::string> classes = m_scriptClassProvider ? m_scriptClassProvider() : std::vector<std::string>{};
    showScriptPicker(context, at, classes, std::move(onPick));
}

void EntityEditorView::addComponentHeaderMenu(CollapsibleSection &section, std::function<void()> onRemove) {
    auto more = std::make_shared<IconButton>(Ic::More, Ic::HeaderSize, 23.f);
    more->setCornerRadius(Radius::sm);
    more->setTooltip("Component Actions");
    IconButton *morePtr = more.get();
    more->setOnClick([this, morePtr, onRemove] {
        auto menu = std::make_shared<Menu>();
        menu->addItem("Remove Component", [onRemove] { if (onRemove) { onRemove(); } });
        const Rect f = morePtr->getWorldFrame();
        auto popup = std::make_shared<Popup>(menu, Rect(f.origin.x, f.origin.y + f.size.height, 0.f, 0.f));
        menu->setOnItemSelected([p = popup.get()] { p->dismiss(); });
        if (Context *ctx = getContext()) { ctx->addOverlay(popup); }
    });
    section.setHeaderAccessory(more);
}

void EntityEditorView::addAssetRow(CollapsibleSection &section, const char *title, const char *kind, uint32_t value,
                                     std::function<void(uint32_t)> set) {
    auto well = std::make_shared<AssetWell>(
        value, kind, [this](uint32_t id) { return assetNameFor(id); }, std::move(set),
        [this](uint32_t id) { m_output->entityDidSelectAsset(Panda::AssetId(id)); }
    );
    section.addContent(std::make_shared<PropertyRow>(title, well));
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildCameraSection(const Panda::CameraComponent &component) {
    using Cam = Panda::CameraComponent;
    auto section = std::make_shared<CollapsibleSection>(componentTypeTitle(ComponentType::Camera));
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this] { m_output->entityDidRemoveComponent(ComponentType::Camera); });
    auto data = std::make_shared<Panda::CameraComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };

    auto primary = std::make_shared<Toggle>();
    primary->setOn(data->isPrimary);
    primary->setOnValueChanged([data, emit](Toggle &t) { data->isPrimary = t.isOn(); emit(); });
    auto primaryRow = std::make_shared<PropertyRow>("Primary", primary, false);
    primaryRow->setMixed(typedFieldMixed<Cam>(m_entities, [](Cam &c) { return c.isPrimary; }));
    section->addContent(primaryRow);

    auto projection = std::make_shared<Combobox>();
    projection->setItems({"Perspective", "Orthographic"});
    projection->setSelectedIndex(static_cast<int>(data->camera.getProjectionType()));
    projection->setOnSelectionChanged([data, emit](Combobox &c) {
        data->camera.setProjectionType(static_cast<Panda::WorldCamera::ProjectionType>(c.selectedIndex()));
        emit();
    });
    auto projectionRow = std::make_shared<PropertyRow>("Projection", projection);
    projectionRow->setMixed(typedFieldMixed<Cam>(m_entities, [](Cam &c) { return static_cast<int>(c.camera.getProjectionType()); }));
    section->addContent(projectionRow);

    auto numberRow = [&](const char *title, float value, float mn, float mx, std::function<void(float)> set,
                         std::function<float(Cam &)> get) {
        auto widget = std::make_shared<NumberField>();
        widget->setFont(Typo::Value);
        widget->setRange(mn, mx);
        widget->setValue(value);
        widget->setMixed(typedFieldMixed<Cam>(m_entities, get));
        widget->setOnValueChanged([set](NumberField &f) { set(f.getValue()); });
        section->addContent(std::make_shared<PropertyRow>(title, widget));
    };
    numberRow(
        "Field of View", data->camera.getFieldOfView(), 1.f, 179.f,
        [data, emit](float v) { data->camera.setFieldOfView(v); emit(); }, [](Cam &c) { return c.camera.getFieldOfView(); }
    );
    numberRow(
        "Ortho Size", data->camera.getOrthoSize(), 0.f, 1000.f,
        [data, emit](float v) { data->camera.setOrthoSize(v); emit(); }, [](Cam &c) { return c.camera.getOrthoSize(); }
    );
    addAssetRow(*section, "Postprocessing", "postfx", static_cast<uint32_t>(data->postprocessing.getId()),
                [data, emit](uint32_t id) { data->postprocessing = assetRefOf(id); emit(); });
    auto postfxEnabled = std::make_shared<Toggle>();
    postfxEnabled->setOn(data->postprocessingEnabled);
    postfxEnabled->setOnValueChanged([data, emit](Toggle &t) { data->postprocessingEnabled = t.isOn(); emit(); });
    auto postfxEnabledR = std::make_shared<PropertyRow>("Postfx Enabled", postfxEnabled, false);
    postfxEnabledR->setMixed(typedFieldMixed<Cam>(m_entities, [](Cam &c) { return c.postprocessingEnabled; }));
    section->addContent(postfxEnabledR);

    // Built-in camera bloom (a pyramid in the engine; the glow source is the emissive of materials).
    auto bloom = std::make_shared<Toggle>();
    bloom->setOn(data->bloomEnabled);
    bloom->setOnValueChanged([data, emit](Toggle &t) { data->bloomEnabled = t.isOn(); emit(); });
    auto bloomR = std::make_shared<PropertyRow>("Bloom", bloom, false);
    bloomR->setMixed(typedFieldMixed<Cam>(m_entities, [](Cam &c) { return c.bloomEnabled; }));
    section->addContent(bloomR);
    numberRow(
        "Bloom Intensity", data->bloomIntensity, 0.f, 100.f,
        [data, emit](float v) { data->bloomIntensity = v; emit(); }, [](Cam &c) { return c.bloomIntensity; }
    );
    numberRow(
        "Bloom Radius", data->bloomRadius, 0.f, 4.f,
        [data, emit](float v) { data->bloomRadius = v; emit(); }, [](Cam &c) { return c.bloomRadius; }
    );
    // Auto exposure (eye adaptation; an engine step before bloom).
    auto autoExposure = std::make_shared<Toggle>();
    autoExposure->setOn(data->autoExposureEnabled);
    autoExposure->setOnValueChanged([data, emit](Toggle &t) { data->autoExposureEnabled = t.isOn(); emit(); });
    auto autoExposureR = std::make_shared<PropertyRow>("Auto Exposure", autoExposure, false);
    autoExposureR->setMixed(typedFieldMixed<Cam>(m_entities, [](Cam &c) { return c.autoExposureEnabled; }));
    section->addContent(autoExposureR);
    numberRow(
        "Exposure Key", data->autoExposureKey, 0.01f, 1.f,
        [data, emit](float v) { data->autoExposureKey = v; emit(); }, [](Cam &c) { return c.autoExposureKey; }
    );
    numberRow(
        "Exposure Speed", data->autoExposureSpeed, 0.1f, 20.f,
        [data, emit](float v) { data->autoExposureSpeed = v; emit(); }, [](Cam &c) { return c.autoExposureSpeed; }
    );
    numberRow(
        "Exposure Min", data->autoExposureMin, 0.01f, 4.f,
        [data, emit](float v) { data->autoExposureMin = v; emit(); }, [](Cam &c) { return c.autoExposureMin; }
    );
    numberRow(
        "Exposure Max", data->autoExposureMax, 0.25f, 16.f,
        [data, emit](float v) { data->autoExposureMax = v; emit(); }, [](Cam &c) { return c.autoExposureMax; }
    );
    return section;
}

std::shared_ptr<CollapsibleSection>
EntityEditorView::buildDirectionalLightSection(const Panda::DirectionalLightComponent &component) {
    using DL = Panda::DirectionalLightComponent;
    auto section = std::make_shared<CollapsibleSection>(componentTypeTitle(ComponentType::DirectionalLight));
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this] { m_output->entityDidRemoveComponent(ComponentType::DirectionalLight); });
    auto data = std::make_shared<Panda::DirectionalLightComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };

    auto color = std::make_shared<ColorWell>(Color(data->color.r, data->color.g, data->color.b, data->color.a));
    color->setOnColorChanged([data, emit](ColorWell &w) {
        const Color c = w.getColor();
        data->color = Panda::Color(c.r, c.g, c.b, c.a);
        emit();
    });
    auto colorR = std::make_shared<PropertyRow>("Color", color, false);
    colorR->setMixed(typedFieldMixed<DL>(m_entities, [](DL &c) { return c.color; }));
    section->addContent(colorR);

    auto intensity = std::make_shared<NumberField>();
    intensity->setFont(Typo::Value);
    intensity->setRange(0.f, 100.f);
    intensity->setValue(data->intensity);
    intensity->setMixed(typedFieldMixed<DL>(m_entities, [](DL &c) { return c.intensity; }));
    intensity->setOnValueChanged([data, emit](NumberField &f) { data->intensity = f.getValue(); emit(); });
    section->addContent(std::make_shared<PropertyRow>("Intensity", intensity));

    auto enabled = std::make_shared<Toggle>();
    enabled->setOn(data->isEnabled);
    enabled->setOnValueChanged([data, emit](Toggle &t) { data->isEnabled = t.isOn(); emit(); });
    auto enabledR = std::make_shared<PropertyRow>("Enabled", enabled, false);
    enabledR->setMixed(typedFieldMixed<DL>(m_entities, [](DL &c) { return c.isEnabled; }));
    section->addContent(enabledR);

    // Cascaded shadows from this sun (shadow settings are in Settings → Shadows).
    auto castShadows = std::make_shared<Toggle>();
    castShadows->setOn(data->castShadows);
    castShadows->setOnValueChanged([data, emit](Toggle &t) {
        data->castShadows = t.isOn();
        emit();
    });
    auto castShadowsR = std::make_shared<PropertyRow>("Cast Shadows", castShadows, false);
    castShadowsR->setMixed(typedFieldMixed<DL>(m_entities, [](DL &c) { return c.castShadows; }));
    section->addContent(castShadowsR);
    return section;
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildPointLightSection(const Panda::PointLightComponent &component) {
    using PL = Panda::PointLightComponent;
    auto section = std::make_shared<CollapsibleSection>(componentTypeTitle(ComponentType::PointLight));
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this] { m_output->entityDidRemoveComponent(ComponentType::PointLight); });
    auto data = std::make_shared<Panda::PointLightComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };

    auto color = std::make_shared<ColorWell>(Color(data->color.r, data->color.g, data->color.b, data->color.a));
    color->setOnColorChanged([data, emit](ColorWell &w) {
        const Color c = w.getColor();
        data->color = Panda::Color(c.r, c.g, c.b, c.a);
        emit();
    });
    auto colorR = std::make_shared<PropertyRow>("Color", color, false);
    colorR->setMixed(typedFieldMixed<PL>(m_entities, [](PL &c) { return c.color; }));
    section->addContent(colorR);

    auto numberRow = [&](const char *title, float value, float mn, float mx, std::function<void(float)> set,
                         std::function<float(PL &)> get) {
        auto widget = std::make_shared<NumberField>();
        widget->setFont(Typo::Value);
        widget->setRange(mn, mx);
        widget->setValue(value);
        widget->setMixed(typedFieldMixed<PL>(m_entities, get));
        widget->setOnValueChanged([set](NumberField &f) { set(f.getValue()); });
        section->addContent(std::make_shared<PropertyRow>(title, widget));
    };
    numberRow("Intensity", data->intensity, 0.f, 100.f, [data, emit](float v) { data->intensity = v; emit(); },
              [](PL &c) { return c.intensity; });
    numberRow("Radius", data->radius, 0.f, 1000.f, [data, emit](float v) { data->radius = v; emit(); },
              [](PL &c) { return c.radius; });

    auto enabled = std::make_shared<Toggle>();
    enabled->setOn(data->isEnabled);
    enabled->setOnValueChanged([data, emit](Toggle &t) { data->isEnabled = t.isOn(); emit(); });
    auto enabledR = std::make_shared<PropertyRow>("Enabled", enabled, false);
    enabledR->setMixed(typedFieldMixed<PL>(m_entities, [](PL &c) { return c.isEnabled; }));
    section->addContent(enabledR);
    return section;
}

std::shared_ptr<CollapsibleSection>
EntityEditorView::buildSpriteRendererSection(const Panda::SpriteRendererComponent &component) {
    using SR = Panda::SpriteRendererComponent;
    auto section = std::make_shared<CollapsibleSection>(componentTypeTitle(ComponentType::SpriteRenderer));
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this] { m_output->entityDidRemoveComponent(ComponentType::SpriteRenderer); });
    auto data = std::make_shared<Panda::SpriteRendererComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };

    auto color = std::make_shared<ColorWell>(Color(data->color.r, data->color.g, data->color.b, data->color.a));
    color->setOnColorChanged([data, emit](ColorWell &w) {
        const Color c = w.getColor();
        data->color = Panda::Color(c.r, c.g, c.b, c.a);
        emit();
    });
    auto colorR = std::make_shared<PropertyRow>("Color", color, false);
    colorR->setMixed(typedFieldMixed<SR>(m_entities, [](SR &c) { return c.color; }));
    section->addContent(colorR);

    auto intRow = [&](const char *title, int value, float mn, float mx, std::function<void(int)> set,
                      std::function<int(SR &)> get) {
        auto widget = std::make_shared<NumberField>();
        widget->setFont(Typo::Value);
        widget->setRange(mn, mx);
        widget->setValue(static_cast<float>(value));
        widget->setMixed(typedFieldMixed<SR>(m_entities, get));
        widget->setOnValueChanged([set](NumberField &f) { set(static_cast<int>(f.getValue())); });
        section->addContent(std::make_shared<PropertyRow>(title, widget));
    };
    intRow("Columns", data->cols, 1.f, 30.f, [data, emit](int v) { data->cols = v; emit(); }, [](SR &c) { return c.cols; });
    intRow("Rows", data->rows, 1.f, 30.f, [data, emit](int v) { data->rows = v; emit(); }, [](SR &c) { return c.rows; });
    intRow("Index", data->index, 0.f, 900.f, [data, emit](int v) { data->index = v; emit(); }, [](SR &c) { return c.index; });

    addAssetRow(*section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    addAssetRow(*section, "Texture", "texture", static_cast<uint32_t>(data->texture.getId()),
                [data, emit](uint32_t id) { data->texture = assetRefOf(id); emit(); });
    return section;
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildRuntimeMeshSection(const Panda::RuntimeMeshComponent &component) {
    auto section = std::make_shared<CollapsibleSection>(componentTypeTitle(ComponentType::RuntimeMesh));
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this] { m_output->entityDidRemoveComponent(ComponentType::RuntimeMesh); });
    auto data = std::make_shared<Panda::RuntimeMeshComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    addAssetRow(*section, "Mesh", "mesh", static_cast<uint32_t>(data->runtimeMesh.getId()),
                [data, emit](uint32_t id) { data->runtimeMesh = assetRefOf(id); emit(); });
    addAssetRow(*section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    return section;
}

std::shared_ptr<CollapsibleSection>
EntityEditorView::buildPrimitiveMeshSection(const Panda::PrimitiveMeshComponent &component) {
    auto section = std::make_shared<CollapsibleSection>(componentTypeTitle(ComponentType::PrimitiveMesh));
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this] { m_output->entityDidRemoveComponent(ComponentType::PrimitiveMesh); });
    auto data = std::make_shared<Panda::PrimitiveMeshComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };

    auto kindCombobox = std::make_shared<Combobox>();
    kindCombobox->setItems({"Cube", "Sphere", "Capsule", "Plane", "Cylinder"});
    kindCombobox->setSelectedIndex(static_cast<int>(data->kind));
    kindCombobox->setOnSelectionChanged([data, emit](Combobox &c) {
        data->kind = static_cast<Panda::PrimitiveMeshKind>(c.selectedIndex());
        emit();
    });
    section->addContent(std::make_shared<PropertyRow>("Kind", kindCombobox));

    // An empty material = the built-in checker (the sky pattern: empty means the engine material).
    addAssetRow(*section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    return section;
}

std::shared_ptr<CollapsibleSection>
EntityEditorView::buildImportedSubmeshSection(const Panda::ImportedSubmeshComponent &component) {
    auto section = std::make_shared<CollapsibleSection>(componentTypeTitle(ComponentType::ImportedSubmesh));
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this] { m_output->entityDidRemoveComponent(ComponentType::ImportedSubmesh); });
    auto data = std::make_shared<Panda::ImportedSubmeshComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    addAssetRow(*section, "Mesh", "mesh", static_cast<uint32_t>(data->importedMesh.getId()),
                [data, emit](uint32_t id) { data->importedMesh = assetRefOf(id); emit(); });
    addAssetRow(*section, "Override", "material", static_cast<uint32_t>(data->material.getId()),
                [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    return section;
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildSkySection(const Panda::SkyComponent &component) {
    auto section = std::make_shared<CollapsibleSection>(componentTypeTitle(ComponentType::Sky));
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this] { m_output->entityDidRemoveComponent(ComponentType::Sky); });
    auto data = std::make_shared<Panda::SkyComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    addAssetRow(*section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    return section;
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildScriptSection(Panda::ExternalScript &script, int scriptIndex) {
    auto section = std::make_shared<CollapsibleSection>(script.getName().empty() ? "SCRIPT" : script.getName());
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this, scriptIndex] { m_output->entityDidRemoveScript(scriptIndex); });
    for (Panda::ScriptField &field : script.getFields()) {
        const std::string name = field.name;
        switch (field.type) {
            case Panda::ScriptFieldType::INTEGER: {
                auto widget = std::make_shared<NumberField>();
                widget->setFont(Typo::Value);
                widget->setValue(static_cast<float>(std::get<int32_t>(field.value)));
                widget->setOnValueChanged([this, scriptIndex, name](NumberField &f) {
                    m_output->entityDidEditScriptField(scriptIndex, name, Panda::ScriptFieldValue(static_cast<int32_t>(f.getValue())));
                });
                section->addContent(std::make_shared<PropertyRow>(name, widget));
                break;
            }
            case Panda::ScriptFieldType::FLOAT: {
                auto widget = std::make_shared<NumberField>();
                widget->setFont(Typo::Value);
                widget->setValue(std::get<float>(field.value));
                widget->setOnValueChanged([this, scriptIndex, name](NumberField &f) {
                    m_output->entityDidEditScriptField(scriptIndex, name, Panda::ScriptFieldValue(f.getValue()));
                });
                section->addContent(std::make_shared<PropertyRow>(name, widget));
                break;
            }
            case Panda::ScriptFieldType::TEXTURE:
            case Panda::ScriptFieldType::MATERIAL:
            case Panda::ScriptFieldType::WORLD:
            case Panda::ScriptFieldType::FONT: {
                const char *kind = field.type == Panda::ScriptFieldType::TEXTURE  ? "texture"
                                 : field.type == Panda::ScriptFieldType::MATERIAL ? "material"
                                 : field.type == Panda::ScriptFieldType::FONT     ? "font"
                                                                                  : "scene";
                addAssetRow(*section, name.c_str(), kind, static_cast<uint32_t>(std::get<Panda::UUID>(field.value)),
                            [this, scriptIndex, name](uint32_t id) {
                                m_output->entityDidEditScriptField(scriptIndex, name, Panda::ScriptFieldValue(Panda::UUID(id)));
                            });
                break;
            }
            case Panda::ScriptFieldType::ENTITY: {
                const uint32_t ref = static_cast<uint32_t>(std::get<Panda::UUID>(field.value));
                auto value = std::make_shared<Label>(ref ? ("Entity #" + std::to_string(ref)) : "None");
                value->setFont(Typo::Value);
                value->setTextRole(TextRole::Muted);
                section->addContent(std::make_shared<PropertyRow>(name, value));
                break;
            }
            default: break; // UNKNOWN
        }
    }
    return section;
}

void EntityEditorView::showMode(Mode mode) {
    auto set = [](const std::shared_ptr<View> &view, bool on) {
        view->setHidden(!on);
        view->layout().setDisplay(on ? Display::Flex : Display::None);
    };
    set(m_body, mode == Mode::Entity);
    set(m_placeholder, mode == Mode::Empty);
    setNeedsLayout();
}

bool EntityEditorView::pointerDown(PointerEvent &) {
    m_output->entityDidClickBackground();
    return true;
}

void EntityEditorView::setupLayout() {
    layout().setFlexDirection(FlexDirection::Column);
}

void EntityEditorView::setupStyle() {
    setBackgroundColor(ThemeColor::windowBackground);
}

} // namespace Editor
