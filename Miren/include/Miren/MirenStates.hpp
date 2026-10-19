#pragma once

namespace Miren {

#define BIT(x) (1 << x)

#define MIREN_STATE_CULL_FACE BIT(1)
#define MIREN_STATE_DEPTH_TEST BIT(2)
#define MIREN_STATE_WIREFRAME BIT(3)
#define MIREN_STATE_BLEND BIT(4)
// Cull FRONT faces (together with CULL_FACE) and do not write color (depth-only shadow map pass).
#define MIREN_STATE_CULL_FRONT BIT(5)
#define MIREN_STATE_NO_COLOR_WRITE BIT(6)

} // namespace Miren
