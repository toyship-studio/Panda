#include "Panda/Assets/MaterialSystemValues.hpp"

#include <vector>

namespace Panda {

namespace {

    struct SystemValue {
        std::string name;
        MaterialFieldValue value;
        std::string hint;    // explanation for the editor of where the value is controlled from
        std::string section; // the world settings section that controls the value
    };

    std::vector<SystemValue> s_values;
    uint32_t s_version = 1;

    bool equals(const MaterialFieldValue &a, const MaterialFieldValue &b) {
        if (a.index() != b.index()) { return false; }
        if (const float *f = std::get_if<float>(&a)) { return *f == std::get<float>(b); }
        if (const uint32_t *u = std::get_if<uint32_t>(&a)) { return *u == std::get<uint32_t>(b); }
        if (const Vec4 *v = std::get_if<Vec4>(&a)) {
            const Vec4 &other = std::get<Vec4>(b);
            return v->x == other.x && v->y == other.y && v->z == other.z && v->w == other.w;
        }
        return false; // an AssetRef is never a system value (UBO types)
    }

    SystemValue *findEntry(const std::string &name) {
        for (SystemValue &entry : s_values) {
            if (entry.name == name) { return &entry; }
        }
        return nullptr;
    }

} // namespace

void MaterialSystemValues::set(const char *name, MaterialFieldValue value, const char *hint, const char *section) {
    if (SystemValue *entry = findEntry(name)) {
        if (hint && entry->hint.empty()) { entry->hint = hint; }
        if (section && entry->section.empty()) { entry->section = section; }
        if (equals(entry->value, value)) { return; } // dedup: may be called every frame
        entry->value = std::move(value);
        s_version++;
        return;
    }
    s_values.push_back({name, std::move(value), hint ? hint : "", section ? section : ""});
    s_version++;
}

const MaterialFieldValue &MaterialSystemValues::resolve(const MaterialField &field) {
    const MaterialFieldValue *systemValue = find(field.name);
    if (systemValue && systemValue->index() == field.value.index()) { return *systemValue; }
    return field.value;
}

bool MaterialSystemValues::isSystem(const MaterialField &field) {
    const MaterialFieldValue *systemValue = find(field.name);
    return systemValue && systemValue->index() == field.value.index();
}

const char *MaterialSystemValues::hint(const std::string &name) {
    const SystemValue *entry = findEntry(name);
    return entry ? entry->hint.c_str() : "";
}

const char *MaterialSystemValues::section(const std::string &name) {
    const SystemValue *entry = findEntry(name);
    return entry ? entry->section.c_str() : "";
}

const MaterialFieldValue *MaterialSystemValues::find(const std::string &name) {
    const SystemValue *entry = findEntry(name);
    return entry ? &entry->value : nullptr;
}

uint32_t MaterialSystemValues::version() {
    return s_version;
}

} // namespace Panda
