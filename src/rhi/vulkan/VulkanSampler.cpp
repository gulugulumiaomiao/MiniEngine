#include "rhi/vulkan/VulkanSampler.h"
#include "core/logging/Log.h"

namespace engine::rhi::vulkan {
namespace {

VkFilter toVulkan(SamplerFilter filter) {
    return filter == SamplerFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

VkSamplerAddressMode toVulkan(SamplerAddressMode mode) {
    switch (mode) {
    case SamplerAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

} // namespace

VulkanSampler::VulkanSampler(VkDevice device, const SamplerDesc& desc)
    : device_(device), desc_(desc) {
    VkSamplerCreateInfo createInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    createInfo.magFilter = toVulkan(desc.magFilter);
    createInfo.minFilter = toVulkan(desc.minFilter);
    createInfo.mipmapMode = desc.mipmapFilter == SamplerMipmapFilter::Linear
                                ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    createInfo.addressModeU = toVulkan(desc.addressU);
    createInfo.addressModeV = toVulkan(desc.addressV);
    createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.anisotropyEnable = VK_FALSE;
    createInfo.maxAnisotropy = 1.0F;
    createInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(device_, &createInfo, nullptr, &sampler_) != VK_SUCCESS) {
        Log::fatal("VulkanSampler", "vkCreateSampler failed");
    }
}

VulkanSampler::~VulkanSampler() {
    vkDestroySampler(device_, sampler_, nullptr);
}

} // namespace engine::rhi::vulkan
