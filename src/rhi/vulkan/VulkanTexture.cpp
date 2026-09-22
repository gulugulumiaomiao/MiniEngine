#include "rhi/vulkan/VulkanTexture.h"

#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanDevice.h"

#include <algorithm>

namespace engine::rhi::vulkan {

VulkanTexture::VulkanTexture(VulkanDevice& device,
                             VmaAllocator allocator,
                             const TextureDesc& desc,
                             VkFormat nativeFormat,
                             VkImageUsageFlags nativeUsage)
    : device_(&device), allocator_(allocator), nativeFormat_(nativeFormat), desc_(desc) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {desc.width, desc.height, desc.depth};
    imageInfo.mipLevels = desc.mipCount;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = nativeFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = nativeUsage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
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
    : device_(&device), image_(externalImage), nativeFormat_(nativeFormat), desc_(desc),
      uploaded_(true) {
    if (image_ == VK_NULL_HANDLE)
        Log::fatal("VulkanTexture", "Cannot wrap a null external texture");
}

VulkanTexture::~VulkanTexture() {
    if (allocation_ != VK_NULL_HANDLE)
        vmaDestroyImage(allocator_, image_, allocation_);
}

RID VulkanTexture::createView(const TextureViewDesc& desc) {
    return device_->acquireTextureView(*this, desc);
}

RID VulkanTexture::findView(const TextureViewDesc& desc) const {
    const auto found = views_.find(desc);
    return found == views_.end() ? RID{} : found->second;
}

void VulkanTexture::cacheView(const TextureViewDesc& desc, RID view) {
    views_.insert_or_assign(desc, view);
}

void VulkanTexture::removeView(RID view) {
    std::erase_if(views_, [view](const auto& entry) { return entry.second == view; });
    if (defaultView_ == view)
        defaultView_ = {};
}

std::vector<RID> VulkanTexture::viewHandles() const {
    std::vector<RID> result;
    result.reserve(views_.size());
    for (const auto& [desc, handle] : views_) {
        (void)desc;
        result.push_back(handle);
    }
    return result;
}

} // namespace engine::rhi::vulkan
