#pragma once

#include "Bamboo/Logger.hpp"
#include "Bamboo/Allocator.hpp"
#include "Bamboo/ScriptRegistry/ScriptClass.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Bamboo {

class ScriptRegistry {
private:
    template<typename T>
    static auto addFieldsIfHas(ScriptClass &) -> std::enable_if_t<!hasFields<T>> {}

    template<typename T>
    static auto addFieldsIfHas(ScriptClass &info) -> std::enable_if_t<hasFields<T>> {
        info.fields = StripType<T>::getFields();
    }

    // What a field of the class is to the engine: `Item` is one value — an int, a float or a
    // handle — and the field is one of them or a std::vector of them.
    template<typename T>
    struct FieldShape {
        using Item = T;
        static constexpr bool list = false;
    };
    template<typename T>
    struct FieldShape<std::vector<T>> {
        using Item = T;
        static constexpr bool list = true;
    };

    template<typename T>
    static constexpr ScriptFieldType getType() {
        if constexpr (std::is_same_v<T, int>) { return ScriptFieldType::INTEGER; }
        if constexpr (std::is_same_v<T, float>) { return ScriptFieldType::FLOAT; }
        if constexpr (std::is_same_v<T, Bamboo::EntityHandle>) { return ScriptFieldType::ENTITY; }
        if constexpr (std::is_same_v<T, Bamboo::TextureHandle>) { return ScriptFieldType::TEXTURE; }
        if constexpr (std::is_same_v<T, Bamboo::MaterialHandle>) { return ScriptFieldType::MATERIAL; }
        if constexpr (std::is_same_v<T, Bamboo::WorldHandle>) { return ScriptFieldType::WORLD; }
        if constexpr (std::is_same_v<T, Bamboo::FontHandle>) { return ScriptFieldType::FONT; }
        if constexpr (std::is_same_v<T, Bamboo::MeshHandle>) { return ScriptFieldType::MESH; }
        return ScriptFieldType::UNKNOWN;
    }

    // One value out of the array the engine sent: a number as it is, a handle from its id.
    template<typename Item>
    static Item fieldItem(const void *values, uint32_t index) {
        const void *source = static_cast<const uint8_t *>(values) + index * sizeof(uint32_t);
        if constexpr (std::is_arithmetic_v<Item>) {
            static_assert(sizeof(Item) == sizeof(uint32_t), "a value of a script field is 4 bytes");
            Item item;
            memcpy(&item, source, sizeof(Item));
            return item;
        } else {
            uint32_t id = 0;
            memcpy(&id, source, sizeof(id));
            return Internal::HandleAccess::make<Item>(id);
        }
    }

    template<typename FieldType>
    static void assignField(void *field, const void *values, uint32_t count) {
        using Item = typename FieldShape<FieldType>::Item;
        if constexpr (FieldShape<FieldType>::list) {
            FieldType &list = *static_cast<FieldType *>(field);
            list.clear();
            list.reserve(count);
            for (uint32_t i = 0; i < count; i++) { list.push_back(fieldItem<Item>(values, i)); }
        } else if (count > 0) {
            *static_cast<FieldType *>(field) = fieldItem<Item>(values, 0);
        }
    }

    struct ScriptEntry {
        Handle classHandle;
        Bamboo::Shared<Bamboo::Script> script;
    };

    struct EntityScripts {
        std::unordered_map<ScriptID, ScriptEntry> instances;
    };

    std::vector<ScriptClass> m_scriptClasses;
    std::unordered_map<Handle, EntityScripts> m_entityScripts;

public:
    ~ScriptRegistry() {
        clear();
    }

    const std::vector<ScriptClass> &getClasses() const {
        return m_scriptClasses;
    }

    template<typename FieldType>
    ScriptFieldInfo makeFieldInfo(const char *fieldName, uint32_t offset) {
        using TypeRemovedExtents = std::remove_all_extents_t<FieldType>;
        using TypeRemovedRefs = std::remove_reference_t<TypeRemovedExtents>;
        using TypeRemovedPtrs = RemoveAllPointers<TypeRemovedRefs>;
        constexpr bool isRef = std::is_reference_v<FieldType>;
        constexpr bool isConst = std::is_const_v<TypeRemovedPtrs>;

        static_assert(!isConst, "Const types in script fields not supported");
        static_assert(!isRef, "Reference types in script fields not supported");

        constexpr ScriptFieldType type = getType<typename FieldShape<FieldType>::Item>();
        if constexpr (type == ScriptFieldType::UNKNOWN) {
            return ScriptFieldInfo(type, false, fieldName, offset, nullptr);
        } else {
            return ScriptFieldInfo(type, FieldShape<FieldType>::list, fieldName, offset, &assignField<FieldType>);
        }
    }

    template<typename T>
    void registerScriptClass(const char *name, int order = 0) {
        for (const auto &clazz : m_scriptClasses) {
            if (strCmp(name, clazz.name) == 0) { return; }
        }
        ScriptClass clazz;
        clazz.name = name;
        clazz.order = order;
        addFieldsIfHas<T>(clazz);
        clazz.instantiateFunc = [](Bamboo::EntityHandle entity) {
            Bamboo::Shared<T> script = Bamboo::makeShared<T>();
            script->m_entity = entity;
            return Bamboo::SharedCast<Script>(script);
        };
        m_scriptClasses.emplace_back(clazz);
    }

    EntityScripts getEntityScripts(EntityHandle entityHandle) {
        if (m_entityScripts.find(entityHandle.id) == m_entityScripts.end()) { return {}; }
        return m_entityScripts.at(entityHandle.id);
    }

    ScriptEntry getEntryWithId(EntityHandle entityHandle, ScriptID id) {
        if (m_entityScripts.find(entityHandle.id) == m_entityScripts.end()) { return {}; }
        const auto &instances = m_entityScripts.at(entityHandle.id).instances;
        if (instances.find(id) == instances.end()) { return {}; }
        return instances.at(id);
    }

    void setFieldValue(
        Handle entityHandle, Handle scriptId, FieldHandle fieldId, const void *values, uint32_t count
    ) {
        ScriptEntry scriptEntry = getEntryWithId(entityHandle, scriptId);
        if (!scriptEntry.script) { return; }
        Handle classHandle = scriptEntry.classHandle;
        if (classHandle >= m_scriptClasses.size()) { return; }
        ScriptClass &clazz = m_scriptClasses[classHandle];
        auto fieldIt =
            std::find_if(clazz.fields.begin(), clazz.fields.end(), [fieldId](const ScriptFieldDefinition &field) {
                return field.handle == fieldId;
            });
        if (fieldIt == clazz.fields.end()) { return; }
        const ScriptFieldInfo &info = fieldIt->info;
        if (!info.assign) {
            LOG_ERROR("BAMBOO: the field %s is of a type a script field cannot have", info.name);
            return;
        }
        info.assign(addOffset(scriptEntry.script.get(), info.offset), values, count);
    }

    void entityWillBeDeleted(Handle entityHandle) {
        if (m_entityScripts.find(entityHandle) == m_entityScripts.end()) { return; }
        auto scripts = m_entityScripts.at(entityHandle);
        for (auto [id, entry] : scripts.instances) {
            entry.script->shutdown();
        }
        m_entityScripts.erase(entityHandle);
    }

    void clear() {
        for (auto [handle, scripts] : m_entityScripts) {
            for (auto [id, entry] : scripts.instances) {
                entry.script->shutdown();
            }
        }
        m_entityScripts.clear();
    }

    Handle instantiate(Bamboo::EntityHandle entity, const char *name) {
        for (Handle classId = 0; classId < m_scriptClasses.size(); classId++) {
            ScriptClass &clazz = m_scriptClasses[classId];
            if (Bamboo::strCmp(name, clazz.name) == 0) {
                m_lastHandle++;
                ScriptEntry entry;
                entry.classHandle = classId;
                entry.script = clazz.instantiateFunc(entity);
                m_entityScripts[entity.id].instances[m_lastHandle] = entry;
                return m_lastHandle;
            }
        }
        return 0;
    }

private:
    Handle m_lastHandle = 0;
};

ScriptRegistry *getScriptRegistry();

} // namespace Bamboo
