#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Foundation {

// The console log of the engine: one line per call with the time and the level, colored where
// the terminal can. Thread safe: a message is formatted on the caller's stack and the console is
// written under a lock, so the lines of the render thread and the workers do not interleave.
class Logger {
public:
    enum class MessageType { _TRACE, _INFO, _WARNING, _ERROR, _CRITICAL };

    static void log(MessageType type, const char *format, ...);
    static void log(std::string_view message, MessageType type);

private:
    friend class EditorLogger;
    static void print(std::string_view message, MessageType type); // the console only
    enum class ColorType { DEFAULT, GREEN, YELLOW, RED };
    enum class ColorBgType { DEFAULT, RED };
    struct TextInfo {
        ColorType colorType = ColorType::DEFAULT;
        ColorBgType colorBgType = ColorBgType::DEFAULT;
        bool bold = false;
    };
    static void write(std::string_view message);
    static void setTextAttrib(TextInfo textInfo);
    static void reset();
};

// The log the user sees: the Output panel of the editor through a callback the editor installs
// (init); without one the messages go to the console. The callback is the panel's, so it runs
// only on the thread that installed it: a message from another thread goes to the console
// instead of into the UI.
class EditorLogger {
public:
    enum MessageType { Info, Warning, Error };
    using EditorLoggerCallback = void (*)(std::string_view message, MessageType type);

    static void init(EditorLoggerCallback callback);
    static bool isInitialized();
    static void log(std::string_view message, MessageType type);
    static void log(MessageType type, const char *format, ...);

private:
    static EditorLoggerCallback s_callback;
};

// A line of either log, as it was written.
struct LogRecord {
    uint64_t sequence = 0; // grows by one with every line and is never reused
    Logger::MessageType type = Logger::MessageType::_INFO;
    bool editor = false; // written for the user (EditorLogger): the Output panel of the editor shows it
    char time[9] = {};   // "HH:MM:SS"
    std::string text;
};

// The last lines of both logs, kept in memory for a tool attached to the running application:
// the MCP server answers with them (the console of an application the system has started goes
// nowhere, and the Output panel belongs to the UI). Off until enabled — a line then costs what it
// did. Thread safe.
class LogHistory {
public:
    static void enable(size_t capacity = 4096);
    // The sequence the next line gets: a reader that remembers it asks later for what came after.
    static uint64_t nextSequence();
    // The lines from `from` on of `minimum` level and above, the oldest first; of more than
    // `limit` such lines the newest ones. `total` (optional) — how many there were.
    static std::vector<LogRecord> read(uint64_t from, Logger::MessageType minimum, size_t limit, size_t *total = nullptr);
};

} // namespace Foundation

#define LOG_TRACE(...) ::Foundation::Logger::log(Foundation::Logger::MessageType::_TRACE, __VA_ARGS__)
#define LOG_INFO(...) ::Foundation::Logger::log(Foundation::Logger::MessageType::_INFO, __VA_ARGS__)
#define LOG_WARN(...) ::Foundation::Logger::log(Foundation::Logger::MessageType::_WARNING, __VA_ARGS__)
#define LOG_ERROR(...) ::Foundation::Logger::log(Foundation::Logger::MessageType::_ERROR, __VA_ARGS__)
#define LOG_CRITICAL(...) ::Foundation::Logger::log(Foundation::Logger::MessageType::_CRITICAL, __VA_ARGS__)

#define LOG_INFO_EDITOR(...) ::Foundation::EditorLogger::log(Foundation::EditorLogger::MessageType::Info, __VA_ARGS__)
#define LOG_WARN_EDITOR(...)                                                                                           \
    ::Foundation::EditorLogger::log(Foundation::EditorLogger::MessageType::Warning, __VA_ARGS__)
#define LOG_ERROR_EDITOR(...) ::Foundation::EditorLogger::log(Foundation::EditorLogger::MessageType::Error, __VA_ARGS__)
