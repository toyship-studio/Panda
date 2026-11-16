#include "UI/Documents/EntityEditorView.hpp"

#include "UI/Documents/ScriptFieldEditors.hpp"
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
    m_name->setAccessibilityId("name"); // what a UI test finds the parts of the form by
    m_name->layout().setFlexGrow(1.f);
    // Static is a tag on the entity (not a component section): a flag next to the name, as in Unity.
    m_staticLabel = std::make_shared<Label>("Static");
    m_static = std::make_shared<Toggle>();
    m_static->setAccessibilityId("static");
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
    transform->setAccessibilityId("Transform"); // a section goes by the name of its component, not by its caps
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
    // dropped; the full form rebuild comes from EditorController::syncSelection by the selection version.
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
    // Every component section comes from the registry — the core's and the modules' alike, in
    // the order of registration; the scripts of the entity follow.
    const ComponentInspectorRegistry &inspectors = editorExtensions().componentInspectors;
    for (const ComponentInspector &inspector : inspectors.inspectors()) {
        if (inspectors.has(inspector, primary)) { add(buildComponentSection(inspector, primary)); }
    }
    if (primary.hasComponent<Panda::ScriptListComponent>()) {
        Panda::ScriptListComponent &slc = primary.getComponent<Panda::ScriptListComponent>();
        for (int i = 0; i < static_cast<int>(slc.scripts.size()); ++i) { add(buildScriptSection(slc.scripts[i], i)); }
    }

    // What Add Component offers: the components the primary target does not have yet, and a
    // script — an entity takes any number of them.
    m_addable.clear();
    for (const ComponentInspector &inspector : inspectors.inspectors()) {
        if (inspectors.has(inspector, primary)) { continue; }
        const std::string component = inspector.component;
        m_addable.push_back({{inspector.displayName, inspector.group, inspector.icon.empty() ? Ic::EntityCube : inspector.icon},
                             [this, component] { m_output->entityDidAddComponent(component); }});
    }
    m_addable.push_back({{"Script", {}, Ic::Script}, [this] {
        openScriptPicker(m_addComponent.get(), [this](const std::string &name) { m_output->entityDidAddScript(name); });
    }});
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
EntityEditorView::buildComponentSection(const ComponentInspector &inspector, Panda::Entity primary) {
    std::string title = inspector.displayName;
    for (char &c : title) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    auto section = std::make_shared<CollapsibleSection>(title);
    section->setAccessibilityId(inspector.displayName);
    styleComponentSection(*section);
    const std::string component = inspector.component;
    addComponentHeaderMenu(
        *section, [this, component] { m_output->entityDidRemoveComponent(component); },
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
                    LOG_WARN_EDITOR("Paste Values: the clipboard does not hold the values of this component");
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

// A script section always says something: the fields the class publishes; for a class without
// them — how one gets here; for a class the loaded build does not have — that it will not run;
// and where the class is written, one click from the external editor.
std::shared_ptr<CollapsibleSection> EntityEditorView::buildScriptSection(Panda::ExternalScript &script, int scriptIndex) {
    auto section = std::make_shared<CollapsibleSection>(script.getName().empty() ? "SCRIPT" : script.getName());
    styleComponentSection(*section);
    addComponentHeaderMenu(*section, [this, scriptIndex] { m_output->entityDidRemoveScript(scriptIndex); });
    Panda::World *world = m_entities.empty() ? nullptr : m_entities.front().getWorld();
    const std::vector<std::string> classes = m_scriptClassProvider ? m_scriptClassProvider() : std::vector<std::string>{};
    const bool inBuild = std::find(classes.begin(), classes.end(), script.getName()) != classes.end();
    if (!inBuild) {
        auto note = makeFormHint(
            classes.empty() ? "The scripts of the project are not built: the class does not run. Build them in the CMake panel."
                            : "The script build has no class of this name: it does not run. Build the scripts, or the "
                              "class was renamed or is no longer registered (REGISTER_SCRIPT)."
        );
        note->setTextColor(ThemeColor::warning);
        section->addContent(note);
    } else if (script.getFields().empty()) {
        section->addContent(makeFormHint("No published fields. PANDA_FIELD(member) in the class shows a member here."));
    }
    for (Panda::ScriptField &field : script.getFields()) {
        const std::string name = field.name;
        if (field.isList()) {
            // The rows of a list reach the value through the world: they outlive an edit of
            // another row, and an edit of the list itself is applied after the click is over.
            ScriptListField list;
            list.name = name;
            list.type = field.type;
            list.world = world;
            list.read = [this, weak = weak_from_this(), scriptIndex, name](Panda::ScriptFieldValue &value) {
                Panda::ScriptField *live = weak.expired() ? nullptr : scriptField(scriptIndex, name);
                if (live == nullptr) { return false; }
                value = live->value;
                return true;
            };
            list.commit = [this, weak = weak_from_this(), scriptIndex, name](Panda::ScriptFieldValue value, bool structural) {
                if (!weak.expired()) { m_output->entityDidEditScriptField(scriptIndex, name, std::move(value), structural); }
            };
            addScriptListRows(*section, std::move(list));
            continue;
        }
        auto editor = makeScriptValueEditor(field.type, world, scriptItemOf(field.value), [this, scriptIndex, name](ScriptItem item) {
            m_output->entityDidEditScriptField(scriptIndex, name, scriptValueOf(item), false);
        });
        if (!editor) { continue; }
        // The label is the name of the C++ field made readable; the name itself is the tooltip.
        auto row = std::make_shared<PropertyRow>(humanizeIdentifier(name), editor);
        row->setHelp(name);
        row->setAccessibilityId(name); // a UI test finds the row by the name of the field in the class
        section->addContent(row);
    }
    // Where the class is written: the file that registers it, opened in the external editor.
    const Panda::path_t source = editorHost().scriptSource(script.getName());
    if (!source.empty()) {
        auto open = makeFormButton(source.filename().string(), ButtonStyle::Link, [source] {
            editorHost().openInExternalEditor(source);
        });
        open->setTooltip("Open " + source.string() + " in the external editor");
        open->layout().setAlignSelf(Align::Start);
        section->addContent(open);
    }
    return section;
}

Panda::ScriptField *EntityEditorView::scriptField(int scriptIndex, const std::string &fieldName) {
    if (m_entities.empty() || !m_entities.front().isValid()) { return nullptr; }
    Panda::Entity primary = m_entities.front();
    if (!primary.hasComponent<Panda::ScriptListComponent>()) { return nullptr; }
    std::vector<Panda::ExternalScript> &scripts = primary.getComponent<Panda::ScriptListComponent>().scripts;
    if (scriptIndex < 0 || scriptIndex >= static_cast<int>(scripts.size())) { return nullptr; }
    Panda::ExternalScript &script = scripts[static_cast<size_t>(scriptIndex)];
    return script.hasField(fieldName.c_str()) ? &script.getField(fieldName.c_str()) : nullptr;
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
