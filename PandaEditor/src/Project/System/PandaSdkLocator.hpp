#pragma once

#include <Panda/Assets/Base/Path.hpp>
#include <optional>

namespace Panda {

std::optional<path_t> findPandaSdkConfigDirectoryAtOrAbove(const path_t &startPath);
std::optional<path_t> findPandaSdkConfigDirectory();

} // namespace Panda
