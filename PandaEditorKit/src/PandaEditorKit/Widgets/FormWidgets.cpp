#include "PandaEditorKit/Widgets/FormWidgets.hpp"

#include "PandaEditorKit/EditorHost.hpp"
#include "PandaEditorKit/Widgets/AssetWell.hpp"
#include "PandaEditorKit/Widgets/EntityWell.hpp"
#include "PandaEditorKit/Widgets/Icons.hpp"
#include "PandaEditorKit/Widgets/Typography.hpp"
#include "PandaEditorKit/Widgets/Vector3Field.hpp"

#include <cctype>
#include <cmath>
#include <utility>

namespace Editor {

using namespace PandaUI;

void styleComponentSection(CollapsibleSection &section) {
    section.setTitleFont(Typo::ComponentTitle);
    section.setTitleColor(ThemeColor::textMuted);
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

std::string humanizeIdentifier(const std::string &name) {
    std::string result;
    result.reserve(name.size() + 8);
    bool startOfWord = true;
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c == '_' || c == '-' || c == ' ') {
            startOfWord = true;
            continue;
        }
        const unsigned char previous = i > 0 ? static_cast<unsigned char>(name[i - 1]) : 0;
        const unsigned char next = i + 1 < name.size() ? static_cast<unsigned char>(name[i + 1]) : 0;
        // A word starts at a capital after a small letter ("baseColor"), at the last capital of
        // a run before a small letter ("HDRColor" → "HDR Color") and where digits begin.
        const bool capitalStart =
            std::isupper(c) && (std::islower(previous) || std::isdigit(previous) || (std::isupper(previous) && std::islower(next)));
        const bool digitStart = std::isdigit(c) && std::isalpha(previous);
        if (!result.empty() && (startOfWord || capitalStart || digitStart)) { result += ' '; }
        result += static_cast<char>(startOfWord || result.empty() || result.back() == ' ' ? std::toupper(c) : c);
        startOfWord = false;
    }
    return result;
}

std::shared_ptr<View> makeFormWarning(const std::string &text) {
    auto row = std::make_shared<Panel>();
    row->setBackgroundColor(Color(0x00000000));
    row->layout().setFlexDirection(FlexDirection::Row);
    row->layout().setAlignItems(Align::Start);
    row->layout().setGap(6.f);
    row->layout().setWidth(Length::percent(100.f));
    row->layout().setMargin(Edge::Vertical, 4.f);
    auto icon = makeIcon(Ic::Warning, Ic::RowSize, ThemeColor::warning);
    icon->layout().setFlexShrink(0.f);
    row->addSubview(icon);
    auto label = std::make_shared<Label>(text);
    label->setFont(Typo::Caption);
    label->setTextColor(ThemeColor::warning);
    label->setNumberOfLines(0);
    label->layout().setFlexGrow(1.f);
    label->layout().setFlexShrink(1.f);
    label->layout().setFlexBasis(0.f);
    row->addSubview(label);
    return row;
}

std::shared_ptr<Label> makeFormHint(const std::string &text) {
    auto hint = std::make_shared<Label>(text);
    hint->setFont(Typo::Caption);
    hint->setTextRole(TextRole::Dim);
    hint->setNumberOfLines(0);
    hint->layout().setWidth(Length::percent(100.f));
    hint->layout().setMargin(Edge::Vertical, 4.f);
    return hint;
}

std::shared_ptr<View> makeAssetWell(const char *kind, uint32_t value, std::function<void(uint32_t)> set, bool required) {
    auto well = std::make_shared<AssetWell>(
        value, kind, [](uint32_t id) { return editorHost().assetTitle(Panda::AssetId(id)); }, std::move(set),
        [](uint32_t id) { editorHost().openAsset(Panda::AssetId(id)); }
    );
    if (required) { well->setRequired(true); }
    return well;
}

namespace {

    std::shared_ptr<PropertyRow> named(std::shared_ptr<PropertyRow> row, const char *field) {
        if (field != nullptr) { row->setField(field); }
        return row;
    }

} // namespace

std::shared_ptr<PropertyRow> addAssetRow(
    CollapsibleSection &section, const char *title, const char *kind, uint32_t value,
    std::function<void(uint32_t)> set, const char *field
) {
    auto row = named(std::make_shared<PropertyRow>(title, makeAssetWell(kind, value, std::move(set))), field);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addNumberRow(
    CollapsibleSection &section, const char *title, float min, float max, float value, std::function<void(float)> onChange,
    const char *field
) {
    auto number = std::make_shared<NumberField>();
    number->setFont(Typo::Value);
    number->setRange(min, max);
    number->setValue(value);
    number->setOnValueChanged([onChange = std::move(onChange)](NumberField &f) { onChange(f.getValue()); });
    auto row = named(std::make_shared<PropertyRow>(title, number), field);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addIntegerRow(
    CollapsibleSection &section, const char *title, int min, int max, int value, std::function<void(int)> onChange,
    const char *field
) {
    auto number = std::make_shared<NumberField>();
    number->setFont(Typo::Value);
    number->setStep(1.f);
    number->setRange(static_cast<float>(min), static_cast<float>(max));
    number->setValue(static_cast<float>(value));
    number->setOnValueChanged([onChange = std::move(onChange)](NumberField &f) {
        onChange(static_cast<int>(std::lround(f.getValue())));
    });
    auto row = named(std::make_shared<PropertyRow>(title, number), field);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow>
addToggleRow(CollapsibleSection &section, const char *title, bool value, std::function<void(bool)> onChange, const char *field) {
    auto toggle = std::make_shared<Toggle>();
    toggle->setOn(value);
    toggle->setOnValueChanged([onChange = std::move(onChange)](Toggle &t) { onChange(t.isOn()); });
    auto row = named(std::make_shared<PropertyRow>(title, toggle, false), field);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addColorRow(
    CollapsibleSection &section, const char *title, const Panda::Color &value,
    std::function<void(const Panda::Color &)> onChange, const char *field, bool alpha
) {
    auto well = std::make_shared<ColorWell>(Color(value.r, value.g, value.b, value.a));
    well->setAlphaEditable(alpha);
    well->setOnColorChanged([onChange = std::move(onChange)](ColorWell &w) {
        const Color c = w.getColor();
        onChange(Panda::Color(c.r, c.g, c.b, c.a));
    });
    auto row = named(std::make_shared<PropertyRow>(title, well, false), field);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addComboRow(
    CollapsibleSection &section, const char *title, std::vector<std::string> items, int selected,
    std::function<void(int)> onChange, const char *field
) {
    auto combobox = std::make_shared<Combobox>();
    combobox->setItems(std::move(items));
    combobox->setSelectedIndex(selected);
    combobox->setOnSelectionChanged([onChange = std::move(onChange)](Combobox &c) { onChange(c.selectedIndex()); });
    auto row = named(std::make_shared<PropertyRow>(title, combobox), field);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addVector3Row(
    CollapsibleSection &section, const char *title, glm::vec3 value, std::function<void(glm::vec3)> onChange,
    const char *field
) {
    auto vector = std::make_shared<Vector3Field>();
    vector->setValue(value.x, value.y, value.z);
    vector->setOnValueChanged([onChange = std::move(onChange)](Vector3Field &f) { onChange({f.x(), f.y(), f.z()}); });
    auto row = named(std::make_shared<PropertyRow>(title, vector), field);
    section.addContent(row);
    return row;
}

std::shared_ptr<PropertyRow> addEntityRow(
    CollapsibleSection &section, const char *title, Panda::World &world, Panda::UUID value,
    std::function<void(Panda::UUID)> onChange, const char *field
) {
    auto well = std::make_shared<EntityWell>(world, value, std::move(onChange));
    auto row = named(std::make_shared<PropertyRow>(title, well), field);
    section.addContent(row);
    return row;
}

void showMixed(PropertyRow &row, bool mixed, glm::bvec3 axes) {
    row.setMixed(mixed);
    if (auto *number = dynamic_cast<NumberField *>(row.editor().get())) {
        number->setMixed(mixed);
    } else if (auto *vector = dynamic_cast<Vector3Field *>(row.editor().get())) {
        vector->setMixed(axes.x, axes.y, axes.z);
    }
}

} // namespace Editor
