#include "Panda/Assets/Base/Path.hpp"

namespace Panda {

path_t replaceLastChars(const path_t &path, const std::string &replacement) {
    std::string str = path.string();
    if (str.size() < replacement.size()) { return path; }
    for (int i = 0; i < replacement.size(); i++) {
        str[str.size() - i - 1] = replacement[replacement.size() - i - 1];
    }
    return path_t(str);
}

std::string utf8Path(const path_t &path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

path_t pathFromUtf8(const std::string &text) {
    return path_t(std::u8string(text.begin(), text.end()));
}

} // namespace Panda