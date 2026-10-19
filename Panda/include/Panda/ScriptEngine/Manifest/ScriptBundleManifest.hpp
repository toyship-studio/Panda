#pragma once

#include "Panda/ScriptEngine/ABI/ScriptManifest.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Panda {

// The engine's own copy of the bundle manifest (ScriptEngine::getManifest): the flat ABI arrays
// turned into owned strings and vectors, so it outlives the bundle it came from.
struct ScriptFieldManifest final {
    FieldHandle handle = 0;
    std::string name;
    ScriptFieldType type = ScriptFieldType::UNKNOWN;

    operator bool() const {
        return !name.empty();
    }
};

struct ScriptClassManifest final {
    std::string name;
    int order = 0;
    std::vector<ScriptFieldManifest> fields;

    ScriptFieldManifest getField(const char *fieldName) const {
        for (const ScriptFieldManifest &field : fields) {
            if (field.name == fieldName) { return field; }
        }
        return {};
    }

    operator bool() const {
        return !name.empty();
    }
};

struct ScriptBundleManifest final {
    std::vector<ScriptClassManifest> classes;

    ScriptClassManifest getClass(const char *className) const {
        for (const ScriptClassManifest &clazz : classes) {
            if (clazz.name == className) { return clazz; }
        }
        return {};
    }

    static ScriptBundleManifest fromInfo(const Scripting::ScriptBundleInfo &info) {
        ScriptBundleManifest manifest;
        if (info.classes == nullptr) { return manifest; }
        manifest.classes.reserve(info.classCount);
        for (uint32_t c = 0; c < info.classCount; c++) {
            const Scripting::ScriptClassInfo &classInfo = info.classes[c];
            ScriptClassManifest &clazz = manifest.classes.emplace_back();
            clazz.name = classInfo.name != nullptr ? classInfo.name : "";
            clazz.order = classInfo.order;
            clazz.fields.reserve(classInfo.fieldCount);
            for (uint32_t f = 0; classInfo.fields != nullptr && f < classInfo.fieldCount; f++) {
                const Scripting::ScriptFieldInfo &fieldInfo = classInfo.fields[f];
                clazz.fields.push_back({fieldInfo.handle, fieldInfo.name != nullptr ? fieldInfo.name : "", fieldInfo.type});
            }
        }
        return manifest;
    }
};

} // namespace Panda
