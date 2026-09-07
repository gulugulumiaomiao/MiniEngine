#include "runtime/application/GameApplication.h"
#include "runtime/engine/Engine.h"
#include "rhi/vulkan/VulkanFactory.h"

int main() {
    engine::GameApplication application;
    const engine::rhi::vulkan::VulkanFactory contextFactory;
    return ENGINE.run(application, contextFactory);
}
