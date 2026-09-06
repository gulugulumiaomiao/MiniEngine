#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine {

enum class LogLevel { Info, Debug, Warn, Error, Fatal };

class Log final {
public:
    static void info(std::string_view source, std::string_view message);
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    static void info(std::string_view source, const char* format, Args&&... args) {
        write(LogLevel::Info, source,
              formatMessage(format, std::forward<Args>(args)...));
    }
    static void debug(std::string_view source, std::string_view message);
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    static void debug(std::string_view source, const char* format, Args&&... args) {
        write(LogLevel::Debug, source,
              formatMessage(format, std::forward<Args>(args)...));
    }
    static void warn(std::string_view source, std::string_view message);
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    static void warn(std::string_view source, const char* format, Args&&... args) {
        write(LogLevel::Warn, source,
              formatMessage(format, std::forward<Args>(args)...));
    }
    static void error(std::string_view source, std::string_view message);
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    static void error(std::string_view source, const char* format, Args&&... args) {
        write(LogLevel::Error, source,
              formatMessage(format, std::forward<Args>(args)...));
    }
    [[noreturn]] static void fatal(std::string_view source,
                                   std::string_view message);
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] static void fatal(std::string_view source, const char* format,
                                   Args&&... args) {
        write(LogLevel::Fatal, source,
              formatMessage(format, std::forward<Args>(args)...));
        std::exit(EXIT_FAILURE);
    }

private:
    template <typename... Args>
    static std::string formatMessage(const char* format, Args&&... args) {
        const int length =
            std::snprintf(nullptr, 0, format, std::forward<Args>(args)...);
        if (length < 0) {
            return "Log message formatting failed";
        }
        std::vector<char> buffer(static_cast<std::size_t>(length) + 1U);
        std::snprintf(buffer.data(), buffer.size(), format,
                      std::forward<Args>(args)...);
        return {buffer.data(), static_cast<std::size_t>(length)};
    }

    static void write(LogLevel level, std::string_view source,
                      std::string_view message);
};

} // namespace engine
