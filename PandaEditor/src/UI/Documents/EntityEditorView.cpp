#include "UI/Documents/EntityEditorView.hpp"

#include "UI/EditorDialogs.hpp"
#include <PandaEditorKit/ComponentCommands.hpp>
#include <PandaEditorKit/EditorHost.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Picker.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>
#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Panda/Assets/Base/AssetRef.hpp>
#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/Components/TransformComponent.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
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

    constexpr float FORM_INDENT = 14.f; // a row that belongs to the toggle above it

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

    // The engine type of a core component (the registry of the engine), by which its text form
    // is written and read; nullptr — the section has none (a script).
    const Panda::ComponentType *engineTypeOf(ComponentType type) {
        const Panda::ComponentTypeRegistry &types = Panda::GameContext::getExtensions().components;
        switch (type) {
            case ComponentType::Camera: return types.find(entt::type_hash<Panda::CameraComponent>::value());
            case ComponentType::DirectionalLight: return types.find(entt::type_hash<Panda::DirectionalLightComponent>::value());
            case ComponentType::PointLight: return types.find(entt::type_hash<Panda::PointLightComponent>::value());
            case ComponentType::SpriteRenderer: return types.find(entt::type_hash<Panda::SpriteRendererComponent>::value());
            case ComponentType::PrimitiveMesh: return types.find(entt::type_hash<Panda::PrimitiveMeshComponent>::value());
            case ComponentType::ImportedSubmesh: return types.find(entt::type_hash<Panda::ImportedSubmeshComponent>::value());
            case ComponentType::Sky: return types.find(entt::type_hash<Panda::SkyComponent>::value());
            case ComponentType::RuntimeMesh:
            case ComponentType::Script: return nullptr;
        }
        return nullptr;
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
    m_scale->setResetValue(1.f);
    m_scaleRow = std::make_shared<PropertyRow>("Scale", m_scale);
    transform->addContent(m_scaleRow);
    m_body->addContentSubview(transform);

    m_componentsContainer = std::make_shared<Panel>();
    m_componentsContainer->setBackgroundColor(Color(0x00000000));
    m_componentsContainer->layout().setFlexDirection(FlexDirection::Column);
    m_componentsContainer->layout().setGap(0.f);
    m_body->addContentSubview(m_componentsContainer);

    m_addComponent = std::make_shared<Button>("Add Component");
    m_addComponent->setStyle(ButtonStyle::AccentOutline);
    m_addComponent->setOnClick([this](Button &) { openAddComponent(); });
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

    // What Add Component offers: the components the primary target does not have yet.
    m_addable.clear();
    for (ComponentType type : addableComponentTypes()) {
        if (type != ComponentType::Script && entityHasComponent(primary, type)) { continue; }
        if (type == ComponentType::Script) {
            m_addable.push_back({{componentTypeName(type), {}, componentTypeIcon(type)}, [this] {
                openScriptPicker(m_addComponent.get(), [this](const std::string &name) { m_output->entityDidAddScript(name); });
            }});
        } else {
            m_addable.push_back({{componentTypeName(type), {}, componentTypeIcon(type)}, [this, type] {
                m_output->entityDidAddComponent(type);
            }});
        }
    }
    for (const ComponentInspector &inspector : inspectors.inspectors()) {
        if (inspectors.has(inspector, primary)) { continue; }
        const std::string component = inspector.component;
        m_addable.push_back({{inspector.displayName, inspector.group, inspector.icon.empty() ? Ic::EntityCube : inspector.icon},
                             [this, component] { m_output->entityDidAddExtensionComponent(component); }});
    }
}

void EntityEditorView::openAddComponent() {
    Context *context = getContext();
    if (context == nullptr) { return; }
    std::vector<PickerItem> items;
    items.reserve(m_addable.size());
    for (const Addable &addable : m_addable) { items.push_back(addable.item); }
    const Rect frame = m_addComponent->getWorldFrame();
    // The picker is as wide as itself: centered under the button.
    showPicker(
        context, Point(frame.origin.x + frame.size.width * 0.5f - 170.f, frame.origin.y + frame.size.height + 4.f),
        "Add component…", std::move(items),
        [this](int index) {
            if (index < 0 || index >= static_cast<int>(m_addable.size())) { return; }
            // A copy: adding rebuilds the form and the list with it.
            const std::function<void()> run = m_addable[static_cast<size_t>(index)].run;
            if (run) { run(); }
        }
    );
}

std::shared_ptr<CollapsibleSection>
EntityEditorView::buildExtensionSection(const ComponentInspector &inspector, Panda::Entity primary) {
    std::string title = inspector.displayName;
    for (char &c : title) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    auto section = std::make_shared<CollapsibleSection>(title);
    styleComponentSection(*section);
    const std::string component = inspector.component;
    addComponentHeaderMenu(
        *section, [this, component] { m_output->entityDidRemoveExtensionComponent(component); },
        Panda::GameContext::getExtensions().components.find(component)
    );
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

void EntityEditorView::addComponentHeaderMenu(
    CollapsibleSection &section, std::function<void()> onRemove, const Panda::ComponentType *type
) {
    auto more = std::make_shared<IconButton>(Ic::More, Ic::HeaderSize, 23.f);
    more->setCornerRadius(Radius::sm);
    more->setTooltip("Component Actions");
    IconButton *morePtr = more.get();
    more->setOnClick([this, morePtr, onRemove, type] {
        auto menu = std::make_shared<Menu>();
        // The values of a component travel as text — the fragment of a world file that holds
        // it: between entities, worlds and projects, and out of a playing world into the one
        // being edited (the copy survives Stop on the clipboard).
        if (type != nullptr && type->encode != nullptr && !m_entities.empty()) {
            menu->addItem("Copy Values", [this, type] {
                if (!m_entities.empty() && m_entities.front().isValid()) {
                    setClipboardText(componentText(m_entities.front(), *type));
                }
            });
            menu->addItem("Paste Values", [this, type] {
                Panda::World *world = m_entities.empty() ? nullptr : m_entities.front().getWorld();
                if (world == nullptr) { return; }
                if (!commitComponentText(*world, m_entities, *type, getClipboardText())) {
                    editorHost().log(HostLogLevel::Warning, "Paste Values: the clipboard does not hold the values of this component");
                    return;
                }
                editorHost().refreshFromWorld();
            });
            menu->addItem("Reset", [this, type] {
                Panda::World *world = m_entities.empty() ? nullptr : m_entities.front().getWorld();
                if (world != nullptr && resetComponent(*world, m_entities, *type)) { editorHost().refreshFromWorld(); }
            });
            menu->addSeparator();
        }
        menu->addItem("Remove Component", [onRemove] { if (onRemove) { onRemove(); } });
        const Rect f = morePtr->getWorldFrame();
        auto popup = std::make_shared<Popup>(menu, Rect(f.origin.x, f.origin.y + f.size.height, 0.f, 0.f));
        menu->setOnItemSelected([p = popup.get()] { p->dismiss(); });
        if (Context *ctx = getContext()) { ctx->addOverlay(popup); }
    });
    section.setHeaderAccessory(more);
}

// The sections of the core components. Each edits a copy of its component and sends it whole;
// the rows are the kit's (FormWidgets): a number is typed or scrubbed by its label, a row that
// does not apply now is out of the form, the tooltip of a label says what the field is.

std::shared_ptr<CollapsibleSection> EntityEditorView::makeComponentSection(ComponentType type) {
    auto section = std::make_shared<CollapsibleSection>(componentTypeTitle(type));
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this, type] { m_output->entityDidRemoveComponent(type); }, engineTypeOf(type));
    return section;
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildCameraSection(const Panda::CameraComponent &component) {
    using Cam = Panda::CameraComponent;
    using Projection = Panda::WorldCamera::ProjectionType;
    auto section = makeComponentSection(ComponentType::Camera);
    auto data = std::make_shared<Cam>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    auto mixed = [this](auto get) { return componentFieldMixed<Cam>(m_entities, get); };
    // The rows that depend on another field are shown by this, set once they all exist. The
    // rows are held by the section, the callbacks by the rows: raw pointers, no cycle.
    auto refresh = std::make_shared<std::function<void()>>();
    auto changed = [emit, refresh] {
        emit();
        if (*refresh) { (*refresh)(); }
    };

    addToggleRow(*section, "Primary", data->isPrimary, [data, emit](bool on) { data->isPrimary = on; emit(); },
                 mixed([](Cam &c) { return c.isPrimary; }))
        ->setHelp("The camera the game looks through");
    addComboRow(
        *section, "Projection", {"Perspective", "Orthographic"}, static_cast<int>(data->camera.getProjectionType()),
        [data, changed](int index) {
            data->camera.setProjectionType(static_cast<Projection>(index));
            changed();
        },
        mixed([](Cam &c) { return static_cast<int>(c.camera.getProjectionType()); })
    );
    PropertyRow *fieldOfView = addNumberRow(
        *section, "Field of View", 1.f, 179.f, data->camera.getFieldOfView(),
        [data, emit](float v) { data->camera.setFieldOfView(v); emit(); },
        mixed([](Cam &c) { return c.camera.getFieldOfView(); })
    ).get();
    fieldOfView->setHelp("The vertical angle of the view, degrees");
    PropertyRow *orthoSize = addNumberRow(
        *section, "Size", 0.f, 1000.f, data->camera.getOrthoSize(),
        [data, emit](float v) { data->camera.setOrthoSize(v); emit(); },
        mixed([](Cam &c) { return c.camera.getOrthoSize(); })
    ).get();
    orthoSize->setHelp("The height of the view, world units");

    // How the frame reaches the display: the exposure, the curve, the glow, the smoothing.
    addNumberRow(*section, "Exposure", -8.f, 8.f, data->exposure, [data, emit](float v) { data->exposure = v; emit(); },
                 mixed([](Cam &c) { return c.exposure; }))
        ->setHelp("Exposure compensation in stops: +1 is twice as bright. With auto exposure — a correction on top of it");
    addToggleRow(*section, "Auto Exposure", data->autoExposureEnabled,
                 [data, changed](bool on) { data->autoExposureEnabled = on; changed(); },
                 mixed([](Cam &c) { return c.autoExposureEnabled; }))
        ->setHelp("The exposure follows the brightness of the frame, as an eye adapts");
    PropertyRow *exposureKey = addNumberRow(
        *section, "Key", 0.01f, 1.f, data->autoExposureKey, [data, emit](float v) { data->autoExposureKey = v; emit(); },
        mixed([](Cam &c) { return c.autoExposureKey; })
    ).get();
    exposureKey->setHelp("The average brightness the frame is brought to (0.18 — a gray card)");
    PropertyRow *exposureSpeed = addNumberRow(
        *section, "Speed", 0.1f, 20.f, data->autoExposureSpeed, [data, emit](float v) { data->autoExposureSpeed = v; emit(); },
        mixed([](Cam &c) { return c.autoExposureSpeed; })
    ).get();
    exposureSpeed->setHelp("How fast the exposure adapts, 1/s");
    PropertyRow *exposureMin = addNumberRow(
        *section, "Min", 0.01f, 4.f, data->autoExposureMin, [data, emit](float v) { data->autoExposureMin = v; emit(); },
        mixed([](Cam &c) { return c.autoExposureMin; })
    ).get();
    exposureMin->setHelp("The lowest exposure the adaptation may reach");
    PropertyRow *exposureMax = addNumberRow(
        *section, "Max", 0.25f, 16.f, data->autoExposureMax, [data, emit](float v) { data->autoExposureMax = v; emit(); },
        mixed([](Cam &c) { return c.autoExposureMax; })
    ).get();
    exposureMax->setHelp("The highest exposure the adaptation may reach");
    addComboRow(
        *section, "Tonemap", {"AgX", "ACES", "Neutral", "Reinhard", "None"}, static_cast<int>(data->tonemap),
        [data, emit](int index) {
            data->tonemap = static_cast<Panda::Tonemap>(index);
            emit();
        },
        mixed([](Cam &c) { return static_cast<int>(c.tonemap); })
    )->setHelp("The curve that brings the light of the scene into the range of a display");
    // Built-in camera bloom (a pyramid in the engine; the glow source is the emissive of materials).
    addToggleRow(*section, "Bloom", data->bloomEnabled, [data, changed](bool on) { data->bloomEnabled = on; changed(); },
                 mixed([](Cam &c) { return c.bloomEnabled; }))
        ->setHelp("The glow around what emits light (the emissive of materials)");
    PropertyRow *bloomIntensity = addNumberRow(
        *section, "Intensity", 0.f, 100.f, data->bloomIntensity, [data, emit](float v) { data->bloomIntensity = v; emit(); },
        mixed([](Cam &c) { return c.bloomIntensity; })
    ).get();
    PropertyRow *bloomRadius = addNumberRow(
        *section, "Radius", 0.f, 4.f, data->bloomRadius, [data, emit](float v) { data->bloomRadius = v; emit(); },
        mixed([](Cam &c) { return c.bloomRadius; })
    ).get();
    bloomRadius->setHelp("How far the glow spreads (0.5..2)");
    addToggleRow(*section, "Antialiasing", data->antialiasing, [data, emit](bool on) { data->antialiasing = on; emit(); },
                 mixed([](Cam &c) { return c.antialiasing; }))
        ->setHelp("Smooths the edges of the finished frame (FXAA)");

    addAssetRow(*section, "Postprocessing", "postfx", static_cast<uint32_t>(data->postprocessing.getId()),
                [data, changed](uint32_t id) { data->postprocessing = assetRefOf(id); changed(); });
    PropertyRow *postfxEnabled = addToggleRow(
        *section, "Postprocessing Enabled", data->postprocessingEnabled,
        [data, emit](bool on) { data->postprocessingEnabled = on; emit(); },
        mixed([](Cam &c) { return c.postprocessingEnabled; })
    ).get();
    postfxEnabled->setHelp("The passes of the postprocessing asset run; a script turns them on and off");

    for (PropertyRow *row : {exposureKey, exposureSpeed, exposureMin, exposureMax, bloomIntensity, bloomRadius}) {
        row->setIndent(FORM_INDENT);
    }
    *refresh = [=] {
        const bool perspective = data->camera.getProjectionType() == Projection::PERSPECTIVE;
        fieldOfView->setVisible(perspective);
        orthoSize->setVisible(!perspective);
        for (PropertyRow *row : {exposureKey, exposureSpeed, exposureMin, exposureMax}) {
            row->setVisible(data->autoExposureEnabled);
        }
        bloomIntensity->setVisible(data->bloomEnabled);
        bloomRadius->setVisible(data->bloomEnabled);
        postfxEnabled->setVisible(static_cast<bool>(data->postprocessing.getId()));
    };
    (*refresh)();
    return section;
}

std::shared_ptr<CollapsibleSection>
EntityEditorView::buildDirectionalLightSection(const Panda::DirectionalLightComponent &component) {
    using DL = Panda::DirectionalLightComponent;
    auto section = makeComponentSection(ComponentType::DirectionalLight);
    auto data = std::make_shared<DL>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    auto mixed = [this](auto get) { return componentFieldMixed<DL>(m_entities, get); };

    addColorRow(*section, "Color", data->color, [data, emit](const Panda::Color &color) { data->color = color; emit(); },
                mixed([](DL &c) { return c.color; }));
    addNumberRow(*section, "Intensity", 0.f, 100.f, data->intensity, [data, emit](float v) { data->intensity = v; emit(); },
                 mixed([](DL &c) { return c.intensity; }))
        ->setHelp("What a white matte surface facing the sun sends back");
    // Cascaded shadows from this sun (shadow settings are in Settings → Shadows).
    addToggleRow(*section, "Cast Shadows", data->castShadows, [data, emit](bool on) { data->castShadows = on; emit(); },
                 mixed([](DL &c) { return c.castShadows; }))
        ->setHelp("The first enabled sun of a world casts the cascaded shadows (World Settings → Shadows)");
    addToggleRow(*section, "Enabled", data->isEnabled, [data, emit](bool on) { data->isEnabled = on; emit(); },
                 mixed([](DL &c) { return c.isEnabled; }));
    return section;
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildPointLightSection(const Panda::PointLightComponent &component) {
    using PL = Panda::PointLightComponent;
    auto section = makeComponentSection(ComponentType::PointLight);
    auto data = std::make_shared<PL>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    auto mixed = [this](auto get) { return componentFieldMixed<PL>(m_entities, get); };
    auto refresh = std::make_shared<std::function<void()>>();

    addColorRow(*section, "Color", data->color, [data, emit](const Panda::Color &color) { data->color = color; emit(); },
                mixed([](PL &c) { return c.color; }));
    addNumberRow(*section, "Intensity", 0.f, 100.f, data->intensity, [data, emit](float v) { data->intensity = v; emit(); },
                 mixed([](PL &c) { return c.intensity; }))
        ->setHelp("What a white matte surface 1 m away, facing the lamp, sends back");
    addNumberRow(*section, "Radius", 0.f, 1000.f, data->radius, [data, emit](float v) { data->radius = v; emit(); },
                 mixed([](PL &c) { return c.radius; }))
        ->setHelp("Where the light ends, m: it falls with the square of the distance and reaches zero here");
    // A cone makes the lamp a spot that shines along the -Z of the entity; 0 — all around.
    addNumberRow(*section, "Cone Angle", 0.f, 179.f, data->coneAngle,
                 [data, emit, refresh](float v) {
                     data->coneAngle = v;
                     emit();
                     if (*refresh) { (*refresh)(); }
                 },
                 mixed([](PL &c) { return c.coneAngle; }))
        ->setHelp("0 — the lamp shines all around. Above — a spot along the -Z of the entity; the full angle of its cone, degrees");
    PropertyRow *softness = addNumberRow(
        *section, "Softness", 0.f, 1.f, data->coneSoftness, [data, emit](float v) { data->coneSoftness = v; emit(); },
        mixed([](PL &c) { return c.coneSoftness; })
    ).get();
    softness->setHelp("The share of the cone over which the light fades to its edge");
    softness->setIndent(FORM_INDENT);
    addToggleRow(*section, "Cast Shadows", data->castShadows, [data, emit](bool on) { data->castShadows = on; emit(); },
                 mixed([](PL &c) { return c.castShadows; }))
        ->setHelp("A spot costs one more pass over the world every frame, a lamp that shines all around — six");
    addToggleRow(*section, "Enabled", data->isEnabled, [data, emit](bool on) { data->isEnabled = on; emit(); },
                 mixed([](PL &c) { return c.isEnabled; }));

    *refresh = [data, softness] { softness->setVisible(data->coneAngle > 0.f); };
    (*refresh)();
    return section;
}

std::shared_ptr<CollapsibleSection>
EntityEditorView::buildSpriteRendererSection(const Panda::SpriteRendererComponent &component) {
    using SR = Panda::SpriteRendererComponent;
    auto section = makeComponentSection(ComponentType::SpriteRenderer);
    auto data = std::make_shared<SR>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    auto mixed = [this](auto get) { return componentFieldMixed<SR>(m_entities, get); };

    addColorRow(*section, "Color", data->color, [data, emit](const Panda::Color &color) { data->color = color; emit(); },
                mixed([](SR &c) { return c.color; }), /*alpha*/ true);
    addAssetRow(*section, "Texture", "texture", static_cast<uint32_t>(data->texture.getId()),
                [data, emit](uint32_t id) { data->texture = assetRefOf(id); emit(); });
    // A sprite sheet: the texture is a grid of frames and the sprite shows one of them.
    addIntegerRow(*section, "Columns", 1, 30, data->cols, [data, emit](int v) { data->cols = v; emit(); },
                  mixed([](SR &c) { return c.cols; }))
        ->setHelp("The frames of the sprite sheet in a row of the texture");
    addIntegerRow(*section, "Rows", 1, 30, data->rows, [data, emit](int v) { data->rows = v; emit(); },
                  mixed([](SR &c) { return c.rows; }))
        ->setHelp("The rows of frames in the texture");
    addIntegerRow(*section, "Index", 0, 900, data->index, [data, emit](int v) { data->index = v; emit(); },
                  mixed([](SR &c) { return c.index; }))
        ->setHelp("The frame shown, counted from 0 along the rows");
    addAssetRow(*section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    return section;
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildRuntimeMeshSection(const Panda::RuntimeMeshComponent &component) {
    auto section = makeComponentSection(ComponentType::RuntimeMesh);
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
    using PM = Panda::PrimitiveMeshComponent;
    auto section = makeComponentSection(ComponentType::PrimitiveMesh);
    auto data = std::make_shared<PM>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    addComboRow(
        *section, "Shape", {"Cube", "Sphere", "Capsule", "Plane", "Cylinder"}, static_cast<int>(data->kind),
        [data, emit](int index) {
            data->kind = static_cast<Panda::PrimitiveMeshKind>(index);
            emit();
        },
        componentFieldMixed<PM>(m_entities, [](PM &c) { return static_cast<int>(c.kind); })
    );
    // An empty material = the built-in checker (the sky pattern: empty means the engine material).
    addAssetRow(*section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    return section;
}

std::shared_ptr<CollapsibleSection>
EntityEditorView::buildImportedSubmeshSection(const Panda::ImportedSubmeshComponent &component) {
    auto section = makeComponentSection(ComponentType::ImportedSubmesh);
    auto data = std::make_shared<Panda::ImportedSubmeshComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    addAssetRow(*section, "Mesh", "mesh", static_cast<uint32_t>(data->importedMesh.getId()),
                [data, emit](uint32_t id) { data->importedMesh = assetRefOf(id); emit(); });
    addAssetRow(*section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    section->addContent(makeFormHint("Empty — the material the model was imported with."));
    return section;
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildSkySection(const Panda::SkyComponent &component) {
    auto section = makeComponentSection(ComponentType::Sky);
    auto data = std::make_shared<Panda::SkyComponent>(component);
    auto emit = [this, data] { m_output->entityDidEditComponent(ComponentEdit{*data}); };
    addAssetRow(*section, "Material", "material", static_cast<uint32_t>(data->material.getId()),
                [data, emit](uint32_t id) { data->material = assetRefOf(id); emit(); });
    section->addContent(makeFormHint("Empty — the default sky: the environment map of the world, or the built-in gradient."));
    return section;
}

std::shared_ptr<CollapsibleSection> EntityEditorView::buildScriptSection(Panda::ExternalScript &script, int scriptIndex) {
    auto section = std::make_shared<CollapsibleSection>(script.getName().empty() ? "SCRIPT" : script.getName());
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this, scriptIndex] { m_output->entityDidRemoveScript(scriptIndex); });
    Panda::World *world = m_entities.empty() ? nullptr : m_entities.front().getWorld();
    for (Panda::ScriptField &field : script.getFields()) {
        const std::string name = field.name;
        // The label is the name of the C++ field made readable; the name itself is the tooltip.
        const std::string title = humanizeIdentifier(name);
        auto set = [this, scriptIndex, name](Panda::ScriptFieldValue value) {
            m_output->entityDidEditScriptField(scriptIndex, name, std::move(value));
        };
        std::shared_ptr<PropertyRow> row;
        switch (field.type) {
            case Panda::ScriptFieldType::INTEGER: {
                // A field of a script has no range of its own.
                auto widget = std::make_shared<NumberField>();
                widget->setFont(Typo::Value);
                widget->setStep(1.f);
                widget->setValue(static_cast<float>(std::get<int32_t>(field.value)));
                widget->setOnValueChanged([set](NumberField &f) {
                    set(Panda::ScriptFieldValue(static_cast<int32_t>(std::lround(f.getValue()))));
                });
                row = std::make_shared<PropertyRow>(title, widget);
                section->addContent(row);
                break;
            }
            case Panda::ScriptFieldType::FLOAT: {
                auto widget = std::make_shared<NumberField>();
                widget->setFont(Typo::Value);
                widget->setValue(std::get<float>(field.value));
                widget->setOnValueChanged([set](NumberField &f) { set(Panda::ScriptFieldValue(f.getValue())); });
                row = std::make_shared<PropertyRow>(title, widget);
                section->addContent(row);
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
                row = addAssetRow(*section, title.c_str(), kind, static_cast<uint32_t>(std::get<Panda::UUID>(field.value)),
                                  [set](uint32_t id) { set(Panda::ScriptFieldValue(Panda::UUID(id))); });
                break;
            }
            case Panda::ScriptFieldType::ENTITY: {
                if (world == nullptr) { break; }
                row = addEntityRow(*section, title.c_str(), *world, std::get<Panda::UUID>(field.value),
                                   [set](Panda::UUID id) { set(Panda::ScriptFieldValue(id)); });
                break;
            }
            default: break; // UNKNOWN
        }
        if (row) { row->setHelp(name); }
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
