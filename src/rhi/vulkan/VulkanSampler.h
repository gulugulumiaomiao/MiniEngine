#pragma once

#include "rhi/api/Sampler.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

class VulkanSampler final : public ISampler {
public:
    VulkanSampler(VkDevice device, const SamplerDesc& desc);
    ~VulkanSampler() override;

    VulkanSampler(const VulkanSampler&) = delete;
    VulkanSampler& operator=(const VulkanSampler&) = delete;
    VulkanSampler(VulkanSampler&&) = delete;
    VulkanSampler& operator=(VulkanSampler&&) = delete;

    [[nodiscard]] VkSampler handle() const { return sampler_; }
    [[nodiscard]] SamplerFilter filter() const override { return desc_.minFilter; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkSampler sampler_{VK_NULL_HANDLE};
    SamplerDesc desc_;
};

} // namespace engine::rhi::vulkan
