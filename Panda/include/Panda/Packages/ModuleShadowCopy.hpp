#pragma once

#include "Panda/Assets/Base/Path.hpp"

namespace Panda {

// Shadow copy of a dynamic module before loading (script bundle, package modules): the copy is
// loaded, the original stays free for a rebuild while the engine is running. The copy's name is
// unique per load within the process: the OS caches the image by path, and dlclose of a module
// with statics/thread_local may not actually unload it — overwriting a loaded file resulted in
// a dlopen failure or a stale image. Previous copies with the same prefix are cleaned up (busy
// files are silently skipped — they go away on the next load or in a new session). The symbol
// file <stem>.pdb next to the module is copied under the copy's name (the debugger looks for it
// next to the loaded image). false — the module does not exist or is locked.
bool copyModuleToShadowCache(const path_t &moduleFile, const path_t &cacheDirectory, path_t &outShadowFile);

} // namespace Panda
