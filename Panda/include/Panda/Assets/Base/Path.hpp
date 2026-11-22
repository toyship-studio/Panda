#pragma once

#include <filesystem>
#include <string>

namespace Panda {

using path_t = std::filesystem::path;

path_t replaceLastChars(const path_t &path, const std::string &replacement);

// A path as text to keep in a file or to pass on, and back: UTF-8 on every system, with forward
// slashes (path::string() on Windows is in the code page of the system, and loses a name that is
// not in it).
std::string utf8Path(const path_t &path);
path_t pathFromUtf8(const std::string &text);

} // namespace Panda