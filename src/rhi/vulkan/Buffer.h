#pragma once

#include <vk_mem_alloc.h>

#include <cstddef>
#include <span>

namespace engine {

class Buffer final {
public:
    Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
           VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO,
           VmaAllocationCreateFlags allocationFlags = 0);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    void upload(std::span<const std::byte> data, VkDeviceSize offset = 0);

    [[nodiscard]] VkBuffer handle() const { return buffer_; }
    [[nodiscard]] VkDeviceSize size() const { return size_; }

private:
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkBuffer buffer_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkDeviceSize size_{};
};

} // namespace engine
