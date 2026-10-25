#include "PandaEditorKit/Widgets/FormWidgets.hpp"

#include "PandaEditorKit/EditorHost.hpp"
#include "PandaEditorKit/Widgets/AssetWell.hpp"
#include "PandaEditorKit/Widgets/EntityWell.hpp"
#include "PandaEditorKit/Widgets/Typography.hpp"
#include "PandaEditorKit/Widgets/Vector3Field.hpp"

#include <utility>

namespace Editor {

using namespace PandaUI;

void styleComponentSection(CollapsibleSection &section) {
    section.setTitleFont(Typo::ComponentTitle);
    section.setThemeStyle([](View &view) { static_cast<CollapsibleSection &>(view).setTitleColor(theme().textMuted); });
    section.setTitleLetterSpacing(Typo::ComponentTitleTracking);
    section.setHeaderFill(false);
}

std::shared_ptr<CollapsibleSection> makeFormSection(const std::string &title) {
    auto section = std::make_shared<CollapsibleSection>(title);
    styleComponentSection(*section);
    return section;
}

Panda::AssetRef<Panda::Asset> assetRefOf(uint32_t id) {
    return id ? Panda::AssetRef<Panda::Asset>(Panda::AssetId(id)) : Panda::AssetRef<Panda::Asset>();
}

std::shared_ptr<Button> makeFormButton(const std::string &title, ButtonStyle style, std::function<void()> onClick) {
    auto button = std::make_shared<Button>(title);
    button->setSize(ButtonSize::Small);
    button->setStyle(style);
    button->setOnClick([onClick = std::move(onClick)](Button &) {
        if (onClick) { onClick(); }
    });
    return button;
}

std::shared_ptr<Panel> makeFormButtonRow(std::initializer_list<std::shared_ptr<Button>> buttons) {
    auto row = std::make_shared<Panel>();
    row->setBackgroundColor(Color(0x00000000));
    row->layout().setFlexDirection(FlexDirection::Row);
    row->layout().setGap(8.f);
    row->layout().setMargin(Edge::Vertical, 4.f);
    for (const std::shared_ptr<Button> &button : buttons) {
        button->layout().setFlexGrow(1.f);
        button->layout().setFlexBasis(0.f);
        row->addSubview(button);
    }
    return row;
}

std::shared_ptr<Label> makeFormHint(const std::string &text) {
    auto hint = std::make_shared<Label>(text);
    hint->setFont(Typo::Meta);
    hint->setTextRole(TextRole::Dim);
    hint->setNumberOfLines(0);
    hint->layout().setWidth(Length::percent(100.f));
    hint->layout().setMargin(Edge::Vertical, 4.f);
    return hint;
}

void addAssetRow(
    CollapsibleSection &section, const char *title, const char *kind, uint32_t value,
    std::function<void(uint32_t)> set
) {
    auto well = std::make_shared<AssetWell>(
        value, kind, [](uint32_t id) { return editorHost().assetTitle(Panda::AssetId(id)); }, std::move(set),
        [](uint32_t id) { editorHost().selectAsset(Panda::AssetId(id)); }
    );
    section.addContent(std::make_shared<PropertyRow>(title, well));
}

std::shared_ptr<PropertyRow> addNumberRow(
    CollapsibleSection &section, const char *title, float min, float max, float value, std::function<void(float)> onChange,
    bool mixed
) {
    auto field = std::make_shared<NumberField>();
    field->setFont(Typo::Value);
    field->setRange(min, max);
    field->setValue(value);
    field->setMixed(mixed);
    field->setOnValueChanged([onChange = std::move(onChange)](NumberField &f) { onChange(f.getValue()); });
    auto row = std::make_shared<PropertyRow>(title, field);
    row->setMixed(mixed);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow>
addToggleRow(CollapsibleSection &section, const char *title, bool value, std::function<void(bool)> onChange, bool mixed) {
    auto toggle = std::make_shared<Toggle>();
    toggle->setOn(value);
    toggle->setOnValueChanged([onChange = std::move(onChange)](Toggle &t) { onChange(t.isOn()); });
    auto row = std::make_shared<PropertyRow>(title, toggle, false);
    row->setMixed(mixed);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addColorRow(
    CollapsibleSection &section, const char *title, const Panda::Color &value,
    std::function<void(const Panda::Color &)> onChange, bool mixed
) {
    auto well = std::make_shared<ColorWell>(Color(value.r, value.g, value.b, value.a));
    well->setOnColorChanged([onChange = std::move(onChange)](ColorWell &w) {
        const Color c = w.getColor();
        onChange(Panda::Color(c.r, c.g, c.b, c.a));
    });
    auto row = std::make_shared<PropertyRow>(title, well, false);
    row->setMixed(mixed);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addComboRow(
    CollapsibleSection &section, const char *title, std::vector<std::string> items, int selected,
    std::function<void(int)> onChange, bool mixed
) {
    auto combobox = std::make_shared<Combobox>();
    combobox->setItems(std::move(items));
    combobox->setSelectedIndex(selected);
    combobox->setOnSelectionChanged([onChange = std::move(onChange)](Combobox &c) { onChange(c.selectedIndex()); });
    auto row = std::make_shared<PropertyRow>(title, combobox);
    row->setMixed(mixed);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addVector3Row(
    CollapsibleSection &section, const char *title, glm::vec3 value, std::function<void(glm::vec3)> onChange,
    glm::bvec3 mixed
) {
    auto field = std::make_shared<Vector3Field>();
    field->setValue(value.x, value.y, value.z);
    field->setMixed(mixed.x, mixed.y, mixed.z);
    field->setOnValueChanged([onChange = std::move(onChange)](Vector3Field &f) { onChange({f.x(), f.y(), f.z()}); });
    auto row = std::make_shared<PropertyRow>(title, field);
    row->setMixed(mixed.x || mixed.y || mixed.z);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addEntityRow(
    CollapsibleSection &section, const char *title, Panda::World &world, Panda::UUID value,
    std::function<void(Panda::UUID)> onChange, bool mixed
) {
    auto well = std::make_shared<EntityWell>(world, value, std::move(onChange));
    auto row = std::make_shared<PropertyRow>(title, well);
    row->setMixed(mixed);
    section.addContent(row);
    return row;
}

} // namespace Editor
