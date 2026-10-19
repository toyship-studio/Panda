#pragma once

#include <cstdarg>
#include <string_view>

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
