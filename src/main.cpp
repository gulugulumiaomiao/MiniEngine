#include "core/Application.h"
#include "core/Log.h"

int main() {
    engine::Log::info("Application", "Starting Mini Vulkan Engine");
    engine::Application app;
    app.run();
    engine::Log::info("Application", "Mini Vulkan Engine stopped");
    return 0;
}
