#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace Panda {

// Values by type, one instance of a type at most, created on first access. The home of the
// per-world state of systems and of the settings of every scope (context/plugins.md): the owner
// of the store knows no types, a contribution keeps what it needs under its own type and removes
// it itself.
class TypeContext final {
public:
    TypeContext() = default;
    TypeContext(const TypeContext &) = delete;
    TypeContext &operator=(const TypeContext &) = delete;

    // The value of the type; created with the arguments on first access (later the arguments are
    // ignored).
    template<typename T, typename... Args>
    T &get(Args &&...args) {
        const std::type_index index(typeid(T));
        auto it = m_values.find(index);
        if (it == m_values.end()) {
            std::shared_ptr<void> value(new T(std::forward<Args>(args)...));
            it = m_values.emplace(index, std::move(value)).first;
        }
        return *static_cast<T *>(it->second.get());
    }
    template<typename T>
    T *find() {
        auto it = m_values.find(std::type_index(typeid(T)));
        return it == m_values.end() ? nullptr : static_cast<T *>(it->second.get());
    }
    template<typename T>
    const T *find() const {
        auto it = m_values.find(std::type_index(typeid(T)));
        return it == m_values.end() ? nullptr : static_cast<const T *>(it->second.get());
    }
    template<typename T>
    void erase() {
        m_values.erase(std::type_index(typeid(T)));
    }
    void clear() {
        m_values.clear();
    }

private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> m_values;
};

} // namespace Panda
