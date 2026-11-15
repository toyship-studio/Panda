#pragma once

#include <PandaUI/Elements/Panel.hpp>

#include <functional>
#include <memory>

namespace PandaUI {
class NumberField;
}

namespace Editor {

// A vec3 editor for inspector forms (the core and the editor parts of modules). Three fields
// X/Y/Z, each with a colored axis handle on the left (X red, Y green, Z blue): a horizontal drag
// of the handle scrubs the component (Shift — coarser, Alt — finer), a double click resets it to
// the reset value (0; 1 for a scale). Built from generic PandaUI bricks (NumberField + Control).
class Vector3Field final : public PandaUI::Panel {
public:
    using ValueChangedCallback = std::function<void(Vector3Field &)>;

    Vector3Field();

    void setValue(float x, float y, float z);
    float x() const;
    float y() const;
    float z() const;

    // What a double click on an axis handle sets the component to: 0 by default.
    void setResetValue(float value);

    // "Mixed" state per axis (multiple selection): the axis shows "—" instead of a number.
    void setMixed(bool x, bool y, bool z);

    void setOnValueChanged(ValueChangedCallback callback);

private:
    std::shared_ptr<PandaUI::NumberField> m_fields[3];
    ValueChangedCallback m_onValueChanged;
    float m_resetValue = 0.f;
};

} // namespace Editor
