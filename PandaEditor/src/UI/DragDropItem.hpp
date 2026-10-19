#pragma once

#include <Foundation/Foundation.hpp>
#include <Rain/Rain.hpp>
#include <Panda/Base/UUID.hpp>

namespace Panda {

constexpr const char *PANDA_DRAGDROP_NAME = "PANDA_DRAGDROP";
constexpr int MAX_DRAGDROP_VALUES = 64;

enum class DragDropItemType { TEXTURE, ENTITY, SHADER, MATERIAL, IMPORTED_MESH, WORLD };

struct DragDropItem {
    DragDropItemType type;
    int count;
    UUID values[MAX_DRAGDROP_VALUES];
};

} // namespace Panda
