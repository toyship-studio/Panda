#pragma once

#include <string_view>
#include <vector>

#include "ScriptRegistry/ScriptRegistry.hpp"

namespace Bamboo {

template<typename T>
struct AutoRegisterScriptClass {
    AutoRegisterScriptClass(const char *name, int order = 0) {
        getScriptRegistry()->registerScriptClass<T>(name, order);
    }
};

constexpr FieldHandle hashFieldPart(FieldHandle value, char c) {
    return (value ^ static_cast<unsigned char>(c)) * 16777619u;
}

constexpr FieldHandle hashString(const char *value, FieldHandle seed = 2166136261u) {
    while (*value != '\0') {
        seed = hashFieldPart(seed, *value);
        value++;
    }
    return seed;
}

constexpr FieldHandle makeStableFieldHandle(const char *className, const char *fieldName) {
    return hashString(fieldName, hashFieldPart(hashString(className), ':'));
}

} // namespace Bamboo

// --------------------------------
// ---------- MACROS --------------
// --------------------------------

#define _PANDA_CONCAT_IMPL_(a, b) a##b
#define _PANDA_CONCAT_(a, b) _PANDA_CONCAT_IMPL_(a, b)
// _PANDA_EXPAND_ — a workaround for the legacy MSVC preprocessor (without /Zc:preprocessor):
// __VA_ARGS__ goes into the next macro as a single token, and REGISTER_SCRIPT(Name, order) breaks.
#define _PANDA_EXPAND_(x) x
#define _REGISTER_SCRIPT_INTERNAL(name, order, ID)                                                                     \
    inline static Bamboo::AutoRegisterScriptClass<name> _PANDA_CONCAT_(registerScript_, ID)(#name, order);
#define _REGISTER_SCRIPT_DEFAULT(name) _REGISTER_SCRIPT_INTERNAL(name, 0, __COUNTER__)
#define _REGISTER_SCRIPT_WITH_ORDER(name, order) _REGISTER_SCRIPT_INTERNAL(name, order, __COUNTER__)
#define _PANDA_PICK_REGISTER_SCRIPT(_1, _2, NAME, ...) NAME
#define REGISTER_SCRIPT(...)                                                                                           \
    _PANDA_EXPAND_(_PANDA_PICK_REGISTER_SCRIPT(__VA_ARGS__, _REGISTER_SCRIPT_WITH_ORDER, _REGISTER_SCRIPT_DEFAULT)(__VA_ARGS__))

#define PANDA_FIELDS_BEGIN(classType)                                                                                  \
    static std::vector<Bamboo::ScriptFieldDefinition> getFields() {                                                    \
        using ClassType = classType;                                                                                   \
        constexpr const char *className = #classType;                                                                  \
        std::vector<Bamboo::ScriptFieldDefinition> fields;

#define PANDA_FIELD(name)                                                                                              \
    fields.emplace_back(                                                                                               \
        Bamboo::makeStableFieldHandle(className, #name),                                                               \
        Bamboo::getScriptRegistry()->makeFieldInfo<decltype(name)>(#name, Bamboo::offsetOf(&ClassType::name))          \
    );

#define PANDA_FIELDS_END                                                                                               \
    return fields;                                                                                                     \
    }
