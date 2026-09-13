#include "runtime/application/GameApplication.h"
#include "runtime/engine/Engine.h"
#include "rhi/vulkan/VulkanFactory.h"

int main() {
    // engine.json 由 Engine::run 从进程当前工作目录解析，main 不再定位或传入路径。
    engine::GameApplication application;
    const engine::rhi::vulkan::VulkanFactory contextFactory;
    return ENGINE.run(application, contextFactory);
}
