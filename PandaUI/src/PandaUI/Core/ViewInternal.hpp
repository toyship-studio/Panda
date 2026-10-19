#pragma once

#include <yoga/Yoga.h>

namespace PandaUI {

struct ViewInternal {
    YGNodeRef ref = nullptr;
    bool usesIntrinsicContentSize = false;
};

} // namespace PandaUI
