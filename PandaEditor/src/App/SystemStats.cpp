#include "App/SystemStats.hpp"

#include <chrono>

#if defined(__APPLE__)
#    include <mach/mach.h>
#    include <sys/resource.h>
#endif

namespace Editor {

SystemStats sampleSystemStats() {
    SystemStats stats;
#if defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) ==
        KERN_SUCCESS) {
        stats.residentBytes = info.resident_size;
    }
    // CPU%: delta of (user + system) process time over the delta of wall-clock time.
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        const double cpuSeconds = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 1.e-6 +
                                  usage.ru_stime.tv_sec + usage.ru_stime.tv_usec * 1.e-6;
        const double now =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        static double lastCpuSeconds = cpuSeconds;
        static double lastNow = now;
        const double wall = now - lastNow;
        if (wall > 0.01) {
            stats.cpuPercent = (cpuSeconds - lastCpuSeconds) / wall * 100.0;
            lastCpuSeconds = cpuSeconds;
            lastNow = now;
        }
    }
#endif
    return stats;
}

} // namespace Editor
