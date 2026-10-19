#pragma once

#include <PandaUI/Elements/Panel.hpp>

#include <functional>
#include <memory>

namespace PandaUI {
class NumberField;
}

namespace Editor {

// A vec3 editor for inspector forms (the core and the editor parts of modules). Three fields X/Y/Z, each with a colored
// label button on the left (X red, Y green, Z blue); a click on the button resets its
// component to 0. Built from generic PandaUI bricks (NumberField + Control).
class Vector3Field final : public PandaUI::Panel {
public:
    using ValueChangedCallback = std::function<void(Vector3Field &)>;

    Vector3Field();

    void setValue(float x, float y, float z);
    float x() const;
    float y() const;
    float z() const;

    // "Mixed" state per axis (multiple selection): the axis shows "—" instead of a number.
    void setMixed(bool x, bool y, bool z);

    void setOnValueChanged(ValueChangedCallback callback);

private:
    std::shared_ptr<PandaUI::NumberField> m_fields[3];
    ValueChangedCallback m_onValueChanged;
};

} // namespace Editor
