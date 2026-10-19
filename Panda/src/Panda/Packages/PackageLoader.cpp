#include "Panda/Packages/PackageLoader.hpp"
#include "Panda/Extensions/Extensions.hpp"
#include "Panda/GameLogic/GameContext.hpp"

#include <Foundation/Foundation.hpp>

namespace Panda {

const LoadedPackage *PackageLoader::find(std::string_view id) const {
    for (const LoadedPackage &package : m_packages) {
        if (package.id == id) { return &package; }
    }
    return nullptr;
}

bool PackageLoader::loadStatic(const std::string &id, const std::string &version, void (*entry)(Extensions &)) {
    if (find(id) != nullptr) {
        LOG_ERROR("PACKAGES: %s registered twice", id.c_str());
        return false;
    }
    entry(GameContext::getExtensions());
    m_packages.push_back({id, version});
    LOG_INFO("PACKAGES: %s %s", id.c_str(), version.c_str());
    return true;
}

} // namespace Panda
