#include "Foundation/Logger.hpp"
#include "Foundation/PlatformDetection.hpp"

#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>

#ifdef PLATFORM_WINDOWS
#    include <windows.h>
#endif

namespace {

    // One message formatted on the caller's stack: no shared buffer between threads.
    constexpr int MESSAGE_CAPACITY = 1024;

    // The console is one stream: the attributes, the line and the reset go out together.
    std::mutex &consoleMutex() {
        static std::mutex mutex;
        return mutex;
    }

    // "HH:MM:SS" of now; the reentrant localtime — std::localtime shares one tm between threads.
    void timeOfNow(char (&out)[9]) {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef PLATFORM_WINDOWS
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        std::snprintf(out, sizeof(out), "%02d:%02d:%02d", local.tm_hour, local.tm_min, local.tm_sec);
    }

} // namespace

namespace Foundation {

EditorLogger::EditorLoggerCallback EditorLogger::s_callback = nullptr;

namespace {
    std::thread::id s_editorLoggerThread; // the thread that installed the callback (the UI thread)
}

void Logger::log(MessageType type, const char *format, ...) {
    char buffer[MESSAGE_CAPACITY];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(std::string_view(buffer), type);
}

void Logger::log(std::string_view message, Logger::MessageType type) {
    TextInfo textInfo;
    switch (type) {
        case Logger::MessageType::_TRACE:
            break;
        case Logger::MessageType::_INFO:
            textInfo.colorType = ColorType::GREEN;
            break;
        case Logger::MessageType::_WARNING:
            textInfo.colorType = ColorType::YELLOW;
            break;
        case Logger::MessageType::_ERROR:
            textInfo.bold = true;
            textInfo.colorType = ColorType::RED;
            break;
        case Logger::MessageType::_CRITICAL:
            textInfo.bold = true;
            textInfo.colorType = ColorType::DEFAULT;
            textInfo.colorBgType = ColorBgType::RED;
            break;
    }
    std::lock_guard<std::mutex> lock(consoleMutex());
    setTextAttrib(textInfo);
    write(message);
    reset();
}

void Logger::write(std::string_view message) {
    char time[9];
    timeOfNow(time);
    std::printf("[%s] PANDA: %.*s", time, static_cast<int>(message.size()), message.data());
}

void Logger::reset() {
#ifdef PLATFORM_POSIX
    std::printf("\033[0m\n"); // reset
#elif defined(PLATFORM_WINDOWS)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    std::printf("\n");
#endif
}

void Logger::setTextAttrib(TextInfo textInfo) {
    ColorType colorType = textInfo.colorType;
    ColorBgType colorBgType = textInfo.colorBgType;
    bool bold = textInfo.bold;
#ifdef PLATFORM_POSIX
    // color
    const char *red = "\033[31m";
    const char *green = "\033[32m";
    const char *yellow = "\033[33m";
    const char *res = nullptr;
    switch (colorType) {
        case Logger::ColorType::DEFAULT:
            break;
        case Logger::ColorType::GREEN:
            res = green;
            break;
        case Logger::ColorType::YELLOW:
            res = yellow;
            break;
        case Logger::ColorType::RED:
            res = red;
            break;
    }
    if (res != nullptr) { std::printf("%s", res); }

    // bg
    res = nullptr;
    const char *redBg = "\033[41m";
    switch (colorBgType) {
        case Logger::ColorBgType::DEFAULT:
            break;
        case Logger::ColorBgType::RED:
            res = redBg;
            break;
    }
    if (res != nullptr) { std::printf("%s", res); }

    // bold
    if (bold) { std::printf("\033[1m"); }
#elif defined(PLATFORM_WINDOWS)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    auto attrib = FOREGROUND_INTENSITY;

    switch (colorType) {
        case Logger::ColorType::DEFAULT:
            break;
        case Logger::ColorType::GREEN:
            attrib |= FOREGROUND_GREEN;
            break;
        case Logger::ColorType::YELLOW:
            attrib |= FOREGROUND_RED | FOREGROUND_GREEN;
            break;
        case Logger::ColorType::RED:
            attrib |= FOREGROUND_RED;
            break;
    }

    switch (colorBgType) {
        case Logger::ColorBgType::DEFAULT:
            break;
        case Logger::ColorBgType::RED:
            attrib |= BACKGROUND_RED;
            break;
    }

    SetConsoleTextAttribute(hConsole, attrib);
#endif
}

void EditorLogger::init(EditorLoggerCallback callback) {
    s_callback = callback;
    s_editorLoggerThread = std::this_thread::get_id();
}

bool EditorLogger::isInitialized() {
    return s_callback != nullptr;
}

void EditorLogger::log(MessageType type, const char *format, ...) {
    char buffer[MESSAGE_CAPACITY];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    log(std::string_view(buffer), type);
}

void EditorLogger::log(std::string_view message, MessageType type) {
    if (s_callback == nullptr || std::this_thread::get_id() != s_editorLoggerThread) {
        Logger::MessageType loggerType = Logger::MessageType::_INFO;
        switch (type) {
            case Info:
                loggerType = Logger::MessageType::_INFO;
                break;
            case Warning:
                loggerType = Logger::MessageType::_WARNING;
                break;
            case Error:
                loggerType = Logger::MessageType::_ERROR;
                break;
        }
        Logger::log(message, loggerType);
        return;
    }
    s_callback(message, type);
}

} // namespace Foundation
