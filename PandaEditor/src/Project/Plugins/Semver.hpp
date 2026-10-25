#pragma once
// Semantic versions and the version ranges of plugin manifests (context/plugins.md, `engine` and
// `dependencies`): the npm grammar the portal uses too (`SemverRange` of the backend) — `1.2.3`,
// `^1.2.3`, `~1.2`, `>=1.0.0 <2.0.0`, `1.x`, `*`, alternatives with `||`. The pre-release tag of the
// tested version is ignored: the engine's `0.1.0-preview.142` satisfies `^0.1.0`.
#include <optional>
#include <string>
#include <string_view>

namespace Panda {

struct Semver {
    int major = 0;
    int minor = 0;
    int patch = 0;
    std::string prerelease; // without the dash; empty for a release

    static std::optional<Semver> parse(std::string_view text);
    // major.minor.patch only (the pre-release tag is not compared).
    int compareCore(const Semver &other) const;
    // The full order: a pre-release goes before its release; tags compare like npm does.
    int compare(const Semver &other) const;
    std::string toString() const;
};

// True when the version is in the range. An unparsable range matches nothing, an unparsable version
// neither.
bool semverSatisfies(std::string_view version, std::string_view range);
bool semverSatisfies(const Semver &version, std::string_view range);

// compare() of two version strings; an unparsable one is older than any parsable one, two
// unparsable ones compare as strings.
int semverCompare(std::string_view a, std::string_view b);

} // namespace Panda
