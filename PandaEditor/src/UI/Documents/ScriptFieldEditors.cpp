#include "UI/Documents/ScriptFieldEditors.hpp"

#include "Project/Loader/AssetHandlerEditor.hpp"

#include <Panda/Extensions/Extensions.hpp>
#include <Panda/GameLogic/GameContext.hpp>
#include <Panda/GameLogic/World.hpp>
#include <PandaEditorKit/Widgets/EntityWell.hpp>
#include <PandaEditorKit/Widgets/FormWidgets.hpp>
#include <PandaEditorKit/Widgets/Icons.hpp>
#include <PandaEditorKit/Widgets/Typography.hpp>

#include <cmath>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

namespace Editor {

using namespace PandaUI;

namespace {

    template<typename T>
    T itemAs(const ScriptItem &item) {
        const T *value = std::get_if<T>(&item);
        return value ? *value : T(0);
    }

    // Runs the function on the list a value holds; a value that is not a list is left alone.
    template<typename Value, typename Function>
    void withList(Value &value, Function function) {
        std::visit(
            [&function](auto &actual) {
                if constexpr (requires { actual.size(); }) { function(actual); }
            },
            value
        );
    }

    ScriptItem listItem(const Panda::ScriptFieldValue &value, size_t index) {
        ScriptItem item = int32_t(0);
        withList(value, [&item, index](const auto &list) {
            if (index < list.size()) { item = list[index]; }
        });
        return item;
    }

    // The kind of the assets a field of the type refers to (AssetKind::name); nullptr — the type
    // is not a reference to an asset, or it is one of a kind nobody named (`kind` is of an ASSET
    // field: a kind a module brings).
    const char *assetKindOf(Panda::ScriptFieldType type, const std::string &kind) {
        switch (type) {
            case Panda::ScriptFieldType::TEXTURE: return "texture";
            case Panda::ScriptFieldType::MATERIAL: return "material";
            case Panda::ScriptFieldType::WORLD: return "world";
            case Panda::ScriptFieldType::FONT: return "font";
            case Panda::ScriptFieldType::MESH: return "mesh";
            case Panda::ScriptFieldType::ASSET: return kind.empty() ? nullptr : kind.c_str();
            default: return nullptr;
        }
    }

} // namespace

ScriptItem scriptItemOf(const Panda::ScriptFieldValue &value) {
    ScriptItem item = int32_t(0);
    std::visit(
        [&item](const auto &actual) {
            if constexpr (!requires { actual.size(); }) { item = actual; }
        },
        value
    );
    return item;
}

Panda::ScriptFieldValue scriptValueOf(const ScriptItem &item) {
    return std::visit([](auto actual) { return Panda::ScriptFieldValue(actual); }, item);
}

std::shared_ptr<View> makeScriptValueEditor(
    Panda::ScriptFieldType type, const std::string &kind, Panda::World *world, const ScriptItem &value,
    std::function<void(ScriptItem)> set
) {
    switch (type) {
        case Panda::ScriptFieldType::INTEGER: {
            // A field of a script has no range of its own.
            auto field = std::make_shared<NumberField>();
            field->setFont(Typo::Value);
            field->setStep(1.f);
            field->setValue(static_cast<float>(itemAs<int32_t>(value)));
            field->setOnValueChanged([set](NumberField &f) {
                set(ScriptItem(static_cast<int32_t>(std::lround(f.getValue()))));
            });
            return field;
        }
        case Panda::ScriptFieldType::FLOAT: {
            auto field = std::make_shared<NumberField>();
            field->setFont(Typo::Value);
            field->setValue(itemAs<float>(value));
            field->setOnValueChanged([set](NumberField &f) { set(ScriptItem(f.getValue())); });
            return field;
        }
        case Panda::ScriptFieldType::TEXTURE:
        case Panda::ScriptFieldType::MATERIAL:
        case Panda::ScriptFieldType::WORLD:
        case Panda::ScriptFieldType::FONT:
        case Panda::ScriptFieldType::MESH:
        case Panda::ScriptFieldType::ASSET: {
            const char *assetKind = assetKindOf(type, kind);
            if (assetKind == nullptr) { return nullptr; }
            return makeAssetWell(assetKind, static_cast<uint32_t>(itemAs<Panda::UUID>(value)), [set](uint32_t id) {
                set(ScriptItem(Panda::UUID(id)));
            });
        }
        case Panda::ScriptFieldType::ENTITY: {
            if (world == nullptr) { return nullptr; }
            return std::make_shared<EntityWell>(*world, itemAs<Panda::UUID>(value), [set](Panda::UUID id) {
                set(ScriptItem(id));
            });
        }
        default: return nullptr; // UNKNOWN
    }
}

namespace {

    constexpr const char *ITEM_PAYLOAD = "scriptList.item";
    constexpr float ITEM_INDENT = 12.f;
    constexpr float ITEM_INDEX_WIDTH = 28.f;
    constexpr float ITEM_BUTTON_SIZE = 22.f;

    // What the rows of one list share: how the value is reached, and the edits of the list.
    // An edit starts from the value the world holds at that moment, not from the one the rows
    // were built with: the rows of a list edit one value between them.
    class ListModel final : public std::enable_shared_from_this<ListModel> {
    public:
        explicit ListModel(ScriptListField field)
            : m_field(std::move(field)) {}

        const ScriptListField &field() const {
            return m_field;
        }

        // The new value of one item: an edit like that of any field.
        void setItem(size_t index, const ScriptItem &item) {
            Panda::ScriptFieldValue value;
            if (!m_field.read(value)) { return; }
            withList(value, [index, &item](auto &list) {
                using Item = typename std::remove_reference_t<decltype(list)>::value_type;
                if (index < list.size()) { list[index] = itemAs<Item>(item); }
            });
            m_field.commit(std::move(value), false);
        }

        // An item with no value at the end.
        void add(View &from) {
            restructure(from, [](Panda::ScriptFieldValue &value) {
                withList(value, [](auto &list) { list.emplace_back(0); });
            });
        }

        // The references of a drop, each an item at the end, in their order.
        void append(View &from, std::vector<std::uint64_t> ids) {
            restructure(from, [ids = std::move(ids)](Panda::ScriptFieldValue &value) {
                if (auto *list = std::get_if<std::vector<Panda::UUID>>(&value)) {
                    for (std::uint64_t id : ids) { list->emplace_back(static_cast<uint32_t>(id)); }
                }
            });
        }

        // The item takes the place of the row `to`; the rows between them shift by one.
        void move(View &from, size_t index, size_t to) {
            restructure(from, [index, to](Panda::ScriptFieldValue &value) {
                withList(value, [index, to](auto &list) {
                    if (index >= list.size() || to >= list.size() || index == to) { return; }
                    const auto item = list[index];
                    list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
                    list.insert(list.begin() + static_cast<std::ptrdiff_t>(to), item);
                });
            });
        }

        void remove(View &from, size_t index) {
            restructure(from, [index](Panda::ScriptFieldValue &value) {
                withList(value, [index](auto &list) {
                    if (index < list.size()) { list.erase(list.begin() + static_cast<std::ptrdiff_t>(index)); }
                });
            });
        }

        // What of a dragged payload this list takes: the assets of its kind — out of a folder or
        // of a selection of several kinds, those that are — or the entities, for a list of
        // entities.
        std::vector<std::uint64_t> accepted(const DragPayload &payload) const {
            if (m_field.type == Panda::ScriptFieldType::ENTITY) {
                return payload.type == "entity" ? payload.items : std::vector<std::uint64_t>();
            }
            const char *kind = assetKindOf(m_field.type, m_field.kind);
            if (kind == nullptr) { return {}; }
            if (payload.type == std::string("asset.") + kind) { return payload.items; }
            if (payload.type != "asset" && payload.type != "folder") { return {}; }
            std::vector<std::uint64_t> ids;
            Panda::AssetHandlerEditor *handler = Panda::AssetHandlerEditor::current();
            const Panda::AssetKind *registered = Panda::GameContext::getExtensions().assetKinds.find(kind);
            if (handler == nullptr || registered == nullptr) { return ids; }
            for (std::uint64_t id : payload.items) {
                const auto found = handler->getRegistry().find(Panda::AssetId(static_cast<uint32_t>(id)));
                if (found != handler->getRegistry().end() && found->second.type == registered->type) { ids.push_back(id); }
            }
            return ids;
        }

    private:
        // An edit of the list itself builds the form again, and the view that asks for it — a
        // button, a row a drop lands on — is a part of that form: the edit waits for the start
        // of the next tick.
        void restructure(View &from, std::function<void(Panda::ScriptFieldValue &)> change) {
            Context *context = from.getContext();
            if (context == nullptr) { return; }
            context->postDeferred([model = shared_from_this(), change = std::move(change)] {
                Panda::ScriptFieldValue value;
                if (!model->m_field.read(value)) { return; }
                change(value);
                model->m_field.commit(std::move(value), true);
            });
        }

        ScriptListField m_field;
    };

    // A row that shows it would take what is dragged over it.
    void setDropHovered(View &row, bool hovered) {
        row.setBackgroundColor(hovered ? ThemeColor::accentSoft : ThemeColor(Color(0x00000000)));
    }

    // The header of a list: the name of the field, how many items it has, the button that adds
    // one. Assets of the kind of the list, or entities, dropped here become items at its end.
    class ListHeaderRow final : public PropertyRow {
    public:
        ListHeaderRow(std::shared_ptr<ListModel> model, std::string title, std::shared_ptr<View> controls)
            : PropertyRow(std::move(title), std::move(controls), false)
            , m_model(std::move(model)) {
            setPlacement(PropertyRowPlacement::Trailing);
            surface().setCornerRadius(Radius::sm);
        }

        bool canAcceptDrop(const DragInfo &drag) override {
            return !m_model->accepted(drag.payload).empty();
        }
        void dragEntered(const DragInfo &) override {
            setDropHovered(*this, true);
        }
        void dragExited() override {
            setDropHovered(*this, false);
        }
        bool performDrop(const DragInfo &drag) override {
            m_model->append(*this, m_model->accepted(drag.payload));
            return true;
        }

    private:
        std::shared_ptr<ListModel> m_model;
    };

    // An item of a list: its index, the editor of its value and the buttons of its place. The
    // index is the handle of the row: dragged onto another row of the list, the item takes the
    // place of that row.
    class ListItemRow final : public PropertyRow {
    public:
        ListItemRow(std::shared_ptr<ListModel> model, size_t index, std::shared_ptr<View> cell)
            : PropertyRow(std::to_string(index), cell)
            , m_model(std::move(model))
            , m_cell(cell.get())
            , m_index(index) {
            // The items are a block under the header: the index needs little room, the value
            // gets the rest of the row.
            setIndent(ITEM_INDENT);
            setLabelWidth(ITEM_INDENT + ITEM_INDEX_WIDTH);
            setHelp("Drag onto another item to move this one there");
            setCursor(Cursor::ResizeNS); // over the index; the value and the buttons have their own
            m_cell->setCursor(Cursor::Arrow);
            surface().setCornerRadius(Radius::sm);
        }

        // A press on the index is kept by the row, so that a drag can start from it.
        bool pointerDown(PointerEvent &event) override {
            return event.button == PointerButton::Left && overIndex(event.position.x - getWorldFrame().origin.x);
        }
        // Only the index is the handle: a drag that starts in the value — a selection in a
        // number field, a press on a well — is not a drag of the item.
        bool prepareDrag(DragPayload &payload, Point localPosition) override {
            if (!overIndex(localPosition.x)) { return false; }
            payload.type = ITEM_PAYLOAD;
            payload.items = {m_index};
            payload.source = m_model.get();
            payload.ghostText = "Item " + std::to_string(m_index);
            return true;
        }

        bool canAcceptDrop(const DragInfo &drag) override {
            return drag.payload.type == ITEM_PAYLOAD && drag.payload.source == m_model.get() &&
                   !drag.payload.items.empty() && drag.payload.items.front() != m_index;
        }
        void dragEntered(const DragInfo &) override {
            setDropHovered(*this, true);
        }
        void dragExited() override {
            setDropHovered(*this, false);
        }
        bool performDrop(const DragInfo &drag) override {
            m_model->move(*this, static_cast<size_t>(drag.payload.items.front()), m_index);
            return true;
        }

    private:
        bool overIndex(float localX) const {
            return localX < m_cell->getWorldFrame().origin.x - getWorldFrame().origin.x;
        }

        std::shared_ptr<ListModel> m_model;
        View *m_cell; // the editor of the row: the value and the buttons (owned by the row)
        size_t m_index;
    };

    std::shared_ptr<IconButton> makeItemButton(const char *icon, const char *tooltip) {
        auto button = std::make_shared<IconButton>(icon, Ic::RowSize, ITEM_BUTTON_SIZE);
        button->setCornerRadius(Radius::sm);
        button->setTooltip(tooltip);
        button->layout().setFlexShrink(0.f);
        return button;
    }

    std::shared_ptr<Panel> makeRowCell(float gap) {
        auto cell = std::make_shared<Panel>();
        cell->setBackgroundColor(Color(0x00000000));
        cell->layout().setFlexDirection(FlexDirection::Row);
        cell->layout().setAlignItems(Align::Center);
        cell->layout().setGap(gap);
        return cell;
    }

} // namespace

void addScriptListRows(CollapsibleSection &section, ScriptListField field) {
    Panda::ScriptFieldValue value;
    if (!field.read || !field.commit || !field.read(value)) { return; }
    const std::string name = field.name;
    const Panda::ScriptFieldType type = field.type;
    Panda::World *world = field.world;
    const size_t count = Panda::scriptFieldItems(value).count;
    const bool references = type == Panda::ScriptFieldType::ENTITY || assetKindOf(type, field.kind) != nullptr;
    auto model = std::make_shared<ListModel>(std::move(field));

    auto summary = std::make_shared<Label>(count == 0 ? "Empty" : std::to_string(count) + (count == 1 ? " item" : " items"));
    summary->setFont(Typo::Caption);
    summary->setTextRole(TextRole::Dim);
    auto add = makeItemButton(Ic::Plus, "Add Item");
    IconButton *addButton = add.get();
    add->setOnClick([model, addButton] { model->add(*addButton); });
    auto controls = makeRowCell(8.f);
    controls->addSubview(summary);
    controls->addSubview(add);
    // The label is the name of the C++ field made readable; the name itself is the tooltip.
    auto header = std::make_shared<ListHeaderRow>(model, humanizeIdentifier(name), controls);
    header->setHelp(
        !references                                 ? name
        : type == Panda::ScriptFieldType::ENTITY ? name + " — drop an entity here to add it to the list"
                                                 : name + " — drop an asset or a folder of them here to add to the list"
    );
    // What a UI test finds the rows by (PandaUI/Core/Accessibility.hpp): the header by the name
    // of the field in the class, the row of an item by the name and the index — "levels.1". The
    // rows of all the lists of a script lie side by side in its section.
    header->setAccessibilityId(name);
    summary->setAccessibilityId("summary");
    section.addContent(header);

    for (size_t index = 0; index < count; ++index) {
        auto editor = makeScriptValueEditor(type, model->field().kind, world, listItem(value, index), [model, index](ScriptItem item) {
            model->setItem(index, item);
        });
        if (!editor) { continue; }
        editor->layout().setFlexGrow(1.f);
        editor->layout().setFlexShrink(1.f);

        // The value has buttons of its own (an asset well: replace, clear); these two are about
        // the place of the item in the list.
        auto up = makeItemButton(Ic::ChevronUp, "Move Up");
        up->setEnabled(index > 0);
        IconButton *upButton = up.get();
        up->setOnClick([model, upButton, index] {
            if (index > 0) { model->move(*upButton, index, index - 1); }
        });
        auto remove = makeItemButton(Ic::Trash, "Remove Item");
        IconButton *removeButton = remove.get();
        remove->setOnClick([model, removeButton, index] { model->remove(*removeButton, index); });

        auto cell = makeRowCell(4.f);
        cell->addSubview(editor);
        cell->addSubview(up);
        cell->addSubview(remove);
        auto row = std::make_shared<ListItemRow>(model, index, cell);
        row->setAccessibilityId(name + "." + std::to_string(index));
        section.addContent(row);
    }
}

} // namespace Editor
