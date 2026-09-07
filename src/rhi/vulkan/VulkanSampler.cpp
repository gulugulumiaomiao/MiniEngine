#include "rhi/vulkan/VulkanSampler.h"
#include "core/logging/Log.h"

namespace engine::rhi::vulkan {
namespace {

VkFilter toVulkan(SamplerFilter filter) {
    return filter == SamplerFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

} // namespace

VulkanSampler::VulkanSampler(VkDevice device, SamplerFilter filter)
    : device_(device), filter_(filter) {
    VkSamplerCreateInfo createInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    createInfo.magFilter = toVulkan(filter);
    createInfo.minFilter = toVulkan(filter);
    createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(device_, &createInfo, nullptr, &sampler_) != VK_SUCCESS) {
        Log::fatal("VulkanSampler", "vkCreateSampler failed");
    }
}

VulkanSampler::~VulkanSampler() {
    vkDestroySampler(device_, sampler_, nullptr);
}

} // namespace engine::rhi::vulkan
