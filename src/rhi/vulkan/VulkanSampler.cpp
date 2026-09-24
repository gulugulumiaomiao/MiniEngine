#include "rhi/vulkan/VulkanSampler.h"
#include "core/logging/Log.h"

#include <algorithm>
#include <utility>

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

VkBorderColor toVulkan(SamplerBorderColor color) {
    switch (color) {
    case SamplerBorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case SamplerBorderColor::OpaqueBlack: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case SamplerBorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }
    return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
}

VkCompareOp toVulkan(CompareOp op) {
    switch (op) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS_OR_EQUAL;
}

} // namespace

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

VulkanSampler::VulkanSampler(VulkanSampler&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      sampler_(std::exchange(other.sampler_, VK_NULL_HANDLE)) {}

VulkanSampler::~VulkanSampler() {
    if (sampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_, sampler_, nullptr);
}

} // namespace engine::rhi::vulkan
