#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Panda {

class Extensions;

struct LoadedPackage final {
    std::string id;
    std::string version;
};

// Registry of the process's modules (context/plugins.md): a module is linked statically, the host
// calls its entry point through loadStatic before the first world. Contributions live until the
// end of the process: their component types, assets and systems sit in the registries, there is
// no unloading — a different set of project modules requires an editor restart. The set (what is
// enabled) is decided by the host, the registry does not store it. Owner — GameContext
// (getPackageLoader).
class PackageLoader final {
public:
    // Module contributions through entry; registering an id again is an error (false).
    bool loadStatic(const std::string &id, const std::string &version, void (*entry)(Extensions &));

    const std::vector<LoadedPackage> &packages() const {
        return m_packages;
    }
    const LoadedPackage *find(std::string_view id) const;

private:
    std::vector<LoadedPackage> m_packages;
};

} // namespace Panda
