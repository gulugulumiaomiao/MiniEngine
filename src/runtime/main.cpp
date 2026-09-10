#include "runtime/application/GameApplication.h"
#include "runtime/engine/Engine.h"
#include "rhi/vulkan/VulkanFactory.h"

#include <filesystem>

int main(int argc, char** argv) {
    (void)argc;
    engine::GameApplication application;
    const engine::rhi::vulkan::VulkanFactory contextFactory;
    const std::filesystem::path executable =
        std::filesystem::absolute(std::filesystem::path{argv[0]}).lexically_normal();
    return ENGINE.run(application, contextFactory, executable.parent_path() / "engine.json");
}
