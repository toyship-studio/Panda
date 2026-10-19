#include "PandaSdkLocator.hpp"

#include <Fern/Fern.hpp>
#include <array>

namespace Panda {

std::optional<path_t> findPandaSdkConfigDirectoryAtOrAbove(const path_t &startPath) {
    if (startPath.empty()) { return std::nullopt; }
    path_t current = std::filesystem::absolute(startPath);
    while (true) {
        const std::array<path_t, 2> candidates = {
            current / "PandaSDK" / "lib" / "cmake" / "PandaSDK" / "PandaSDKConfig.cmake",
            current / "bin" / "PandaSDK" / "lib" / "cmake" / "PandaSDK" / "PandaSDKConfig.cmake"
        };

        for (const auto &candidate : candidates) {
            if (std::filesystem::exists(candidate)) { return candidate.parent_path(); }
        }

        path_t parent = current.parent_path();
        if (parent == current) { break; }
        current = parent;
    }
    return std::nullopt;
}

std::optional<path_t> findPandaSdkConfigDirectory() {
    if (auto sdkDir = findPandaSdkConfigDirectoryAtOrAbove(Fern::getStaticResourcesPath())) { return sdkDir; }
    if (auto sdkDir = findPandaSdkConfigDirectoryAtOrAbove(std::filesystem::current_path())) { return sdkDir; }
    return {};
}

} // namespace Panda
