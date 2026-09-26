#include "rhi/vulkan/VulkanTexture.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanConversions.h"

namespace engine::rhi::vulkan {

VulkanTexture::VulkanTexture(VmaAllocator allocator, const TextureDesc& desc)
    : allocator_(allocator), format_(desc.format), type_(desc.dimension), width_(desc.width),
      height_(desc.height), arrayLayers_(desc.arrayLayers), mipLevels_(desc.mipCount) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {desc.width, desc.height, desc.depth};
    imageInfo.mipLevels = desc.mipCount;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = toVulkan(desc.format);
    imageInfo.tiling =
        desc.tiling == TextureTiling::Linear ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = toVulkan(desc.usage);
    imageInfo.samples = toVulkan(desc.samples);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator_, &imageInfo, &allocationInfo, &image_, &allocation_, nullptr) !=
        VK_SUCCESS) {
        Log::fatal("VulkanTexture", "vmaCreateImage failed");
    }
}

VulkanTexture::VulkanTexture(VkImage externalImage, const TextureDesc& desc)
    : image_(externalImage), format_(desc.format), type_(desc.dimension), width_(desc.width),
      height_(desc.height), arrayLayers_(desc.arrayLayers), mipLevels_(desc.mipCount),
      uploaded_(true) {
    if (image_ == VK_NULL_HANDLE)
        Log::fatal("VulkanTexture", "Cannot wrap a null external texture");
}

VulkanTexture::~VulkanTexture() {
    if (allocation_ != VK_NULL_HANDLE)
        vmaDestroyImage(allocator_, image_, allocation_);
}

} // namespace engine::rhi::vulkan
