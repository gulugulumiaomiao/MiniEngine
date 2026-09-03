#include "rhi/vulkan/Sampler.h"
#include "core/Log.h"


namespace engine {

Sampler::Sampler(VkDevice device, VkFilter filter) : device_(device) {
    VkSamplerCreateInfo createInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    createInfo.magFilter = filter;
    createInfo.minFilter = filter;
    createInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    createInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    createInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(device_, &createInfo, nullptr, &sampler_) != VK_SUCCESS) {
        Log::fatal("Sampler", "vkCreateSampler failed");
    }
}

Sampler::~Sampler() {
    vkDestroySampler(device_, sampler_, nullptr);
}

} // namespace engine
