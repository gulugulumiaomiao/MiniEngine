#pragma once

#include <vk_mem_alloc.h>

namespace engine {

class Image final {
public:
    Image(VmaAllocator allocator, VkDevice device, VkExtent3D extent, VkFormat format,
          VkImageUsageFlags usage, VkImageAspectFlags aspectMask);
    ~Image();

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&&) = delete;
    Image& operator=(Image&&) = delete;

    [[nodiscard]] VkImage handle() const { return image_; }
    [[nodiscard]] VkImageView view() const { return view_; }
    [[nodiscard]] VkFormat format() const { return format_; }

private:
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkImage image_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkImageView view_{VK_NULL_HANDLE};
    VkFormat format_{VK_FORMAT_UNDEFINED};
};

} // namespace engine
