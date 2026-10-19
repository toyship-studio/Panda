#include "Panda/Extensions/ScriptHostApis.hpp"

#include <Foundation/Foundation.hpp>

namespace Panda {

void ScriptHostApiRegistry::add(ScriptHostApiTable table) {
    bool fresh = true;
    for (const ScriptHostApiTable &registered : m_tables) {
        if (registered.name == table.name) { fresh = false; }
    }
    PND_ASSERT_F(fresh, "SCRIPT HOST API %s REGISTERED TWICE", table.name.c_str());
    PND_ASSERT(table.table != nullptr, "SCRIPT HOST API TABLE REQUIRED");
    m_tables.push_back(std::move(table));
}

const void *ScriptHostApiRegistry::find(std::string_view name, uint32_t version) const {
    for (const ScriptHostApiTable &table : m_tables) {
        if (table.name == name && table.version == version) { return table.table; }
    }
    return nullptr;
}

} // namespace Panda
