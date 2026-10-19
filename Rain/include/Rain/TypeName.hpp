#pragma once

#include <string_view>

namespace Rain {

template<typename T>
constexpr std::string_view typeName();

template<>
constexpr std::string_view typeName<void>() {
    return "void";
}

namespace TypeNameDetails {
    template<typename T>
    constexpr std::string_view wrappedTypeName() {
#ifdef __clang__
        return __PRETTY_FUNCTION__;
#elif defined(__GNUC__)
        return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        return __FUNCSIG__;
#else
#    error "Unsupported compiler"
#endif
    }

    constexpr std::size_t wrappedTypeNamePrefixLength() {
        return wrappedTypeName<void>().find(typeName<void>());
    }

    constexpr std::size_t wrappedTypeNameSuffixLength() {
        return wrappedTypeName<void>().length() - wrappedTypeNamePrefixLength() -
               typeName<void>().length();
    }

    // MSVC (__FUNCSIG__) prints class types in the elaborated form — "class Foo",
    // "struct Foo" — and the prefix calibration against void does not strip this word. Without
    // stripping, type names on Windows do not match Clang/GCC (this breaks the script ABI,
    // which looks up components by name). Clang/GCC have no such keywords — no-op.
    constexpr std::string_view stripTypeKeyword(std::string_view name, std::string_view keyword) {
        if (name.length() > keyword.length() && name.substr(0, keyword.length()) == keyword) {
            return name.substr(keyword.length());
        }
        return name;
    }
} // namespace TypeNameDetails

template<typename T>
constexpr std::string_view typeName() {
    using namespace TypeNameDetails;
    constexpr auto wrappedName = wrappedTypeName<T>();
    constexpr auto prefixLength = wrappedTypeNamePrefixLength();
    constexpr auto suffixLength = wrappedTypeNameSuffixLength();
    constexpr auto typeNameLength = wrappedName.length() - prefixLength - suffixLength;
    std::string_view name = wrappedName.substr(prefixLength, typeNameLength);
    name = stripTypeKeyword(name, "class ");
    name = stripTypeKeyword(name, "struct ");
    name = stripTypeKeyword(name, "enum ");
    name = stripTypeKeyword(name, "union ");
    return name;
}

} // namespace Rain