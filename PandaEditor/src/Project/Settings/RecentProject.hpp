#pragma once

#include <Rain/Rain.hpp>

namespace Panda {

// A project the editor opened (the editor session): its folder, and the name its project.json
// gave it then — what the start screen and File → Recent Projects call a project that is not
// open, without reading its files.
struct RecentProject final : public Rain::Codable {
    std::string path;
    std::string name;

    RAIN_FIELDS_BEGIN(RecentProject)
    RAIN_FIELD(path)
    RAIN_FIELD(name)
    RAIN_FIELDS_END

    bool operator==(const RecentProject &other) const {
        return path == other.path;
    }
};

} // namespace Panda