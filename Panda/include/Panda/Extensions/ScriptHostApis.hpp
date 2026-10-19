#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Panda {

// A named table of a contribution's C functions for scripts (context/plugins.md,
// context/architecture.md): a script module requests it from the host by name and
// version (ScriptHostApi::hostApiTable_Find) — scripting extensions do not touch the layout of
// ScriptHostApi. The table is a flat C struct of the contribution with a layout version,
// lives as long as the contribution is registered.
struct ScriptHostApiTable final {
    std::string name;            // contribution name
    uint32_t version = 0;        // table layout version; a request must match exactly
    const void *table = nullptr;
};

class ScriptHostApiRegistry final {
public:
    void add(ScriptHostApiTable table);
    const std::vector<ScriptHostApiTable> &tables() const {
        return m_tables;
    }
    // nullptr — no such contribution or wrong version.
    const void *find(std::string_view name, uint32_t version) const;

private:
    std::vector<ScriptHostApiTable> m_tables;
};

} // namespace Panda
