#pragma once

#include <Rain/Value.hpp>

#include <string>

namespace Panda {

// A value of a file whose owner is not loaded (a component or a settings section of a module
// that is not in the registries): the key and the value as they were read, written back on save —
// user data survives the absence of the module (context/plugins.md, decision 3).
struct ForeignValue final {
    std::string name;
    Rain::Value value;
};

} // namespace Panda
