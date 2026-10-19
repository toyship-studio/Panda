#include "Panda/Extensions/Extensions.hpp"
#include "Panda/Assets/Base/CoreAssetKinds.hpp"
#include "Panda/GameLogic/Components/CoreComponentTypes.hpp"
#include "Panda/GameLogic/CoreSettings.hpp"
#include "Panda/GameLogic/Systems/CoreWorldSystems.hpp"

namespace Panda {

Extensions::Extensions() {
    registerCoreComponentTypes(components);
    registerCoreAssetKinds(assetKinds);
    registerCoreWorldSystems(worldSystems);
    registerCoreSettings(settings);
}

} // namespace Panda
