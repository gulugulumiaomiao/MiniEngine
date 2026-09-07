#pragma once

#include "rhi/api/Sampler.h"

#include <vulkan/vulkan.h>

namespace engine::rhi::vulkan {

class VulkanSampler final : public ISampler {
public:
    explicit VulkanSampler(VkDevice device, SamplerFilter filter = SamplerFilter::Linear);
    ~VulkanSampler() override;

    VulkanSampler(const VulkanSampler&) = delete;
    VulkanSampler& operator=(const VulkanSampler&) = delete;
    VulkanSampler(VulkanSampler&&) = delete;
    VulkanSampler& operator=(VulkanSampler&&) = delete;

    [[nodiscard]] VkSampler handle() const { return sampler_; }
    [[nodiscard]] SamplerFilter filter() const override { return filter_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkSampler sampler_{VK_NULL_HANDLE};
    SamplerFilter filter_{SamplerFilter::Linear};
};

} // namespace engine::rhi::vulkan
