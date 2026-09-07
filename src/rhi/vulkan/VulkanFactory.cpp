#include "rhi/RhiFactory.h"

#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanSwapchain.h"

namespace engine::rhi {

Context createDefaultContext(void* nativeInstance, void* nativeWindow,
                             const SwapchainDesc& swapchainDesc) {
    auto device = std::make_unique<vulkan::VulkanDevice>(nativeInstance, nativeWindow);
    auto swapchain = std::make_unique<vulkan::VulkanSwapchain>(*device, swapchainDesc);
    return {std::move(device), std::move(swapchain)};
}

} // namespace engine::rhi
