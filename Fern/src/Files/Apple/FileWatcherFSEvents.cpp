// The file watch of macOS: FSEvents. One stream over the watched folders (a stream watches
// folders, with everything under them); its events arrive on a queue of its own and go to the
// sink.
#include "Files/FileWatcherInternal.hpp"

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>


namespace Fern::Files {

namespace {

    // The system gathers the events of this long into one delivery.
    constexpr CFTimeInterval LATENCY = 0.1;

    class FSEventsBackend final : public Backend {
    public:
        explicit FSEventsBackend(std::shared_ptr<ChangeSink> sink)
            : m_sink(std::move(sink)) {}

        bool start() {
            const std::vector<std::filesystem::path> &folders = m_sink->folders();
            if (folders.empty()) { return false; }

            CFMutableArrayRef paths = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
            for (const std::filesystem::path &folder : folders) {
                CFStringRef text = CFStringCreateWithCString(kCFAllocatorDefault, folder.c_str(), kCFStringEncodingUTF8);
                if (text == nullptr) { continue; }
                CFArrayAppendValue(paths, text);
                CFRelease(text);
            }
            FSEventStreamContext context = {0, m_sink.get(), nullptr, nullptr, nullptr};
            m_stream = FSEventStreamCreate(
                kCFAllocatorDefault, &FSEventsBackend::events, &context, paths, kFSEventStreamEventIdSinceNow, LATENCY,
                kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer
            );
            CFRelease(paths);
            if (m_stream == nullptr) { return false; }
            m_queue = dispatch_queue_create("fern.files.watcher", DISPATCH_QUEUE_SERIAL);
            FSEventStreamSetDispatchQueue(m_stream, m_queue);
            if (!FSEventStreamStart(m_stream)) {
                FSEventStreamInvalidate(m_stream);
                FSEventStreamRelease(m_stream);
                dispatch_release(m_queue);
                m_stream = nullptr;
                m_queue = nullptr;
                return false;
            }
            return true;
        }

        ~FSEventsBackend() override {
            if (m_stream == nullptr) { return; }
            FSEventStreamStop(m_stream);
            FSEventStreamInvalidate(m_stream);
            FSEventStreamRelease(m_stream);
            // A delivery that was on its way has ended when the queue is empty.
            dispatch_sync_f(m_queue, nullptr, [](void *) {});
            dispatch_release(m_queue);
        }

    private:
        static void events(
            ConstFSEventStreamRef, void *info, size_t count, void *eventPaths, const FSEventStreamEventFlags flags[],
            const FSEventStreamEventId[]
        ) {
            ChangeSink *sink = static_cast<ChangeSink *>(info);
            char **paths = static_cast<char **>(eventPaths);
            constexpr FSEventStreamEventFlags LOST = kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagUserDropped |
                                                     kFSEventStreamEventFlagKernelDropped;
            for (size_t i = 0; i < count; i++) {
                if ((flags[i] & LOST) != 0) {
                    sink->lostTrack();
                } else if ((flags[i] & kFSEventStreamEventFlagHistoryDone) == 0) {
                    sink->changed(std::filesystem::path(paths[i]));
                }
            }
        }

        std::shared_ptr<ChangeSink> m_sink;
        FSEventStreamRef m_stream = nullptr;
        dispatch_queue_t m_queue = nullptr;
    };

} // namespace

std::unique_ptr<Backend> startSystemBackend(std::shared_ptr<ChangeSink> sink) {
    auto backend = std::make_unique<FSEventsBackend>(std::move(sink));
    if (!backend->start()) { return nullptr; }
    return backend;
}

} // namespace Fern::Files
