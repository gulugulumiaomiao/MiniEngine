#include "rhi/vulkan/VulkanFactory.h"

#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanSwapchain.h"

namespace engine::rhi::vulkan {

Context VulkanFactory::createContext(const ContextDesc& desc) const {
    auto device = std::make_unique<VulkanDevice>(desc.surface, desc.pipelineCachePath);
    auto swapchain = std::make_unique<VulkanSwapchain>(*device, desc.swapchain);
    return {std::move(device), std::move(swapchain)};
}

} // namespace engine::rhi::vulkan
