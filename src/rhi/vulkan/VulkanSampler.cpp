#include "rhi/vulkan/VulkanSampler.h"
#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanConversions.h"

#include <algorithm>
#include <utility>

namespace engine::rhi::vulkan {

VulkanSampler::VulkanSampler(VkDevice device, const SamplerDesc& desc, float maxAnisotropyLimit)
    : device_(device) {
    VkSamplerCreateInfo createInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    createInfo.magFilter = toVulkan(desc.magFilter);
    createInfo.minFilter = toVulkan(desc.minFilter);
    createInfo.mipmapMode = desc.mipmapFilter == SamplerMipmapFilter::Linear
                                ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    createInfo.addressModeU = toVulkan(desc.addressU);
    createInfo.addressModeV = toVulkan(desc.addressV);
    createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.anisotropyEnable = desc.maxAnisotropy > 1.0F ? VK_TRUE : VK_FALSE;
    createInfo.maxAnisotropy = std::max(1.0F, std::min(desc.maxAnisotropy, maxAnisotropyLimit));
    createInfo.minLod = desc.minLod;
    createInfo.maxLod = desc.maxLod;
    createInfo.borderColor = toVulkan(desc.borderColor);
    createInfo.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
    createInfo.compareOp = toVulkan(desc.compareOp);
    if (vkCreateSampler(device_, &createInfo, nullptr, &sampler_) != VK_SUCCESS) {
        Log::fatal("VulkanSampler", "vkCreateSampler failed");
    }
}

VulkanSampler::~VulkanSampler() {
    if (sampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_, sampler_, nullptr);
}

} // namespace engine::rhi::vulkan
