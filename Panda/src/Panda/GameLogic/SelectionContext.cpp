#include "Panda/GameLogic/SelectionContext.hpp"

#ifdef PND_EDITOR

namespace Panda {

std::unordered_set<AssetId> SelectionContext::s_selectedAssets;
std::unordered_set<path_t> SelectionContext::s_selectedFiles;
std::unordered_set<UUID> SelectionContext::s_selectedEntities;
std::unordered_set<UUID> SelectionContext::s_manipulatingEntities;
glm::vec3 SelectionContext::s_medianPosition;
glm::vec3 SelectionContext::s_medianScale;
glm::quat SelectionContext::s_medianRotation;
glm::mat4 SelectionContext::s_medianMatrix;
std::uint64_t SelectionContext::s_entitySelectionVersion = 0;
std::uint64_t SelectionContext::s_transformVersion = 0;

} // namespace Panda

#endif
