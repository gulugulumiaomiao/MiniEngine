#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

// Concrete Vulkan sampler. Retains only the native VkSampler (built from the SamplerDesc's
// VkSamplerCreateInfo), not the SamplerDesc itself; dedup lives at the device level. Stored
// directly (by value) in the device's sampler handle pool, so it is non-copyable/non-movable.
class VulkanSampler final {
public:
    VulkanSampler(VkDevice device, const SamplerDesc& desc, float maxAnisotropyLimit);
    ~VulkanSampler();

    VulkanSampler(const VulkanSampler&) = delete;
    VulkanSampler& operator=(const VulkanSampler&) = delete;
    VulkanSampler(VulkanSampler&&) = delete;
    VulkanSampler& operator=(VulkanSampler&&) = delete;

    [[nodiscard]] VkSampler handle() const { return sampler_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkSampler sampler_{VK_NULL_HANDLE};
};

} // namespace engine::rhi::vulkan
