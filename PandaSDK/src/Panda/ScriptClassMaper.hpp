#pragma once

#include "Panda/ScriptEngine/ABI/ScriptManifest.hpp"
#include "Bamboo/ScriptRegistry/ScriptClass.hpp"

#include <cstdint>
#include <vector>

namespace Panda {

// The registered script classes as the flat manifest of the ABI. The arrays live in static
// storage of the bundle and are rebuilt on every call; the engine copies them right away.
class ScriptClassMapper {
public:
    static Scripting::ScriptBundleInfo getClassesManifest(const std::vector<Bamboo::ScriptClass> &classes) {
        static std::vector<std::vector<Scripting::ScriptFieldInfo>> s_fields;
        static std::vector<Scripting::ScriptClassInfo> s_classes;
        s_fields.clear();
        s_classes.clear();
        s_fields.reserve(classes.size()); // no reallocation below: the field pointers stay valid
        s_classes.reserve(classes.size());
        for (const Bamboo::ScriptClass &clazz : classes) {
            std::vector<Scripting::ScriptFieldInfo> &fields = s_fields.emplace_back();
            fields.reserve(clazz.fields.size());
            for (const Bamboo::ScriptFieldDefinition &field : clazz.fields) {
                fields.push_back({field.handle, field.info.name, static_cast<Scripting::ScriptFieldType>(field.info.type)});
            }
            s_classes.push_back({clazz.name, clazz.order, fields.data(), static_cast<uint32_t>(fields.size())});
        }
        return {s_classes.data(), static_cast<uint32_t>(s_classes.size())};
    }
};

} // namespace Panda
