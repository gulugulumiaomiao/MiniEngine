#include "core/Log.h"

#include <cstdlib>
#include <iostream>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace engine {
namespace {

constexpr std::string_view color(LogLevel level) {
    switch (level) {
    case LogLevel::Info:
        return "\x1b[32m";
    case LogLevel::Debug:
        return "\x1b[36m";
    case LogLevel::Warn:
        return "\x1b[33m";
    case LogLevel::Error:
        return "\x1b[31m";
    case LogLevel::Fatal:
        return "\x1b[1;35m";
    }
    return "";
}

void enableAnsiColors() {
#if defined(_WIN32)
    for (const DWORD streamId : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        const HANDLE stream = GetStdHandle(streamId);
        DWORD mode{};
        if (stream != INVALID_HANDLE_VALUE && GetConsoleMode(stream, &mode) != 0) {
            SetConsoleMode(stream, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
}

std::mutex& logMutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace

void Log::info(std::string_view source, std::string_view message) {
    write(LogLevel::Info, source, message);
}

void Log::debug(std::string_view source, std::string_view message) {
    write(LogLevel::Debug, source, message);
}

void Log::warn(std::string_view source, std::string_view message) {
    write(LogLevel::Warn, source, message);
}

void Log::error(std::string_view source, std::string_view message) {
    write(LogLevel::Error, source, message);
}

[[noreturn]] void Log::fatal(std::string_view source, std::string_view message) {
    write(LogLevel::Fatal, source, message);
    std::exit(EXIT_FAILURE);
}

void Log::write(LogLevel level, std::string_view source, std::string_view message) {
    static const bool ansiEnabled = [] {
        enableAnsiColors();
        return true;
    }();
    (void)ansiEnabled;
    std::scoped_lock lock{logMutex()};
    std::ostream& output = level == LogLevel::Info || level == LogLevel::Debug
                               ? std::cout
                               : std::cerr;
    output << color(level) << '[' << source << "]: \"" << message
           << "\"\x1b[0m\n";
    output.flush();
}

} // namespace engine
