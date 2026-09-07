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
    default: return TextureFormat::Undefined;
    }
}

} // namespace

VulkanImage::VulkanImage(VmaAllocator allocator,
                         VkDevice device,
                         VkExtent3D extent,
                         VkFormat format,
                         VkImageUsageFlags usage,
                         VkImageAspectFlags aspectMask)
    : allocator_(allocator), device_(device), nativeFormat_(format), format_(fromVulkan(format)),
      extent_(extent) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = extent;
    imageInfo.mipLevels = 1;
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

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = nativeFormat_;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &view_) != VK_SUCCESS) {
        vmaDestroyImage(allocator_, image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        Log::fatal("VulkanImage", "vkCreateImageView failed");
    }
}

VulkanImage::~VulkanImage() {
    vkDestroyImageView(device_, view_, nullptr);
    if (image_ != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, image_, allocation_);
    }
}

} // namespace engine::rhi::vulkan
