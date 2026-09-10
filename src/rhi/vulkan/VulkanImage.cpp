#include "rhi/vulkan/VulkanImage.h"
#include "core/logging/Log.h"

namespace engine::rhi::vulkan {
namespace {

TextureFormat fromVulkan(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM: return TextureFormat::Rgba8Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return TextureFormat::Rgba8Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM: return TextureFormat::Bgra8Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB: return TextureFormat::Bgra8Srgb;
    case VK_FORMAT_D32_SFLOAT: return TextureFormat::Depth32Float;
    default: return TextureFormat::Undefined;
    }
}

} // namespace

VulkanImage::VulkanImage(VmaAllocator allocator,
                         VkExtent3D extent,
                         VkFormat format,
                         VkImageUsageFlags usage,
                         std::uint32_t mipCount)
    : allocator_(allocator), nativeFormat_(format), format_(fromVulkan(format)), extent_(extent),
      mipCount_(mipCount) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = extent;
    imageInfo.mipLevels = mipCount;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator_, &imageInfo, &allocationInfo, &image_, &allocation_, nullptr) !=
        VK_SUCCESS) {
        Log::fatal("VulkanImage", "vmaCreateImage failed");
    }
}

VulkanImage::~VulkanImage() {
    if (image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, image_, allocation_);
    }
}

} // namespace engine::rhi::vulkan
