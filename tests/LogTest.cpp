#include "core/Log.h"

#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "fatal") {
        engine::Log::fatal("LogTest", "Fatal exits the process");
    }

    engine::Log::info("LogTest", "Info message");
    engine::Log::debug("LogTest", "Object count: %u, name: %s", 2U, "Triangle");
    engine::Log::warn("LogTest", "Warn message");
    engine::Log::error("LogTest", "Result: %d, time: %.2f ms", -1, 1.25);
    return 0;
}
