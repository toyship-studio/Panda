#pragma once

#include "PandaUI/Core/Base.hpp"

#include <cassert>
#include <cmath>

namespace PandaUI {

enum class DimensionUnit {
    Auto,
    Points,
    Fraction,
};

class Dimension {
public:
    DimensionUnit unit;
    float value;

    static Dimension createDimension(DimensionUnit unit, float value) {
        if (unit == DimensionUnit::Auto) {
            assert(value == 0 && "Dimension auto value must be 0.");
        } else if (unit == DimensionUnit::Points) {
            assert((value >= 0 && std::isfinite(value)) && "Value must be a real positive number.");
        } else if (unit == DimensionUnit::Fraction) {
            assert((0 <= value && value <= 1.0) && "Dimension fraction value must be between 0 and 1.");
        }
        return {unit, value};
    }

    Dimension()
        : unit(DimensionUnit::Points)
        , value(0) {}

    Dimension(DimensionUnit unit, float value)
        : unit(unit)
        , value(value) {}
};

} // namespace PandaUI
