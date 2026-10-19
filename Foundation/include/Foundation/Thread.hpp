#pragma once

namespace Foundation {

// Name of the current thread for the debugger/profiler (the only thing std::thread cannot do
// cross-platform). Threads are created with plain std::thread; call this as the first line of the
// thread body.
void setCurrentThreadName(const char *name);

} // namespace Foundation
