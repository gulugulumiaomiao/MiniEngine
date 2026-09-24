#include "rhi/vulkan/VulkanTexture.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanDevice.h"

namespace engine::rhi::vulkan {
namespace {

[[nodiscard]] VkSampleCountFlagBits toVkSamples(SampleCount samples) {
    switch (samples) {
    case SampleCount::Two: return VK_SAMPLE_COUNT_2_BIT;
    case SampleCount::Four: return VK_SAMPLE_COUNT_4_BIT;
    case SampleCount::Eight: return VK_SAMPLE_COUNT_8_BIT;
    case SampleCount::One: return VK_SAMPLE_COUNT_1_BIT;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

} // namespace

VulkanTexture::VulkanTexture(VulkanDevice& device,
                             VmaAllocator allocator,
                             const TextureDesc& desc,
                             VkFormat nativeFormat,
                             VkImageUsageFlags nativeUsage)
    : device_(&device), allocator_(allocator), nativeFormat_(nativeFormat), format_(desc.format),
      type_(desc.dimension), width_(desc.width), height_(desc.height), depth_(desc.depth),
      arrayLayers_(desc.arrayLayers), mipLevels_(desc.mipCount), usage_(nativeUsage) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {desc.width, desc.height, desc.depth};
    imageInfo.mipLevels = desc.mipCount;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = nativeFormat;
    imageInfo.tiling =
        desc.tiling == TextureTiling::Linear ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = nativeUsage;
    imageInfo.samples = toVkSamples(desc.samples);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator_, &imageInfo, &allocationInfo, &image_, &allocation_, nullptr) !=
        VK_SUCCESS) {
        Log::fatal("VulkanTexture", "vmaCreateImage failed");
    }
}

VulkanTexture::VulkanTexture(VulkanDevice& device,
                             VkImage externalImage,
                             const TextureDesc& desc,
                             VkFormat nativeFormat)
    : device_(&device), image_(externalImage), nativeFormat_(nativeFormat), format_(desc.format),
      type_(desc.dimension), width_(desc.width), height_(desc.height), depth_(desc.depth),
      arrayLayers_(desc.arrayLayers), mipLevels_(desc.mipCount), uploaded_(true) {
    if (image_ == VK_NULL_HANDLE)
        Log::fatal("VulkanTexture", "Cannot wrap a null external texture");
}

VulkanTexture::~VulkanTexture() {
    if (allocation_ != VK_NULL_HANDLE)
        vmaDestroyImage(allocator_, image_, allocation_);
}

} // namespace engine::rhi::vulkan
