#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace engine::rhi::vulkan {

// Concrete Vulkan buffer. Stored directly (by value) in the device's buffer handle pool, so it
// is neither copyable nor movable; the pool constructs it in place and destroys it on release.
// Owns the full RHI -> VMA translation: callers hand it BufferUsage/MemoryUsage and it derives
// the VkBufferUsageFlags plus the VmaAllocationCreateInfo usage/flags pair internally.
class VulkanBuffer final {
public:
    VulkanBuffer(VmaAllocator allocator,
                 VkDeviceSize size,
                 BufferUsage usage,
                 MemoryUsage memoryUsage);
    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) = delete;
    VulkanBuffer& operator=(VulkanBuffer&&) = delete;

    void upload(std::span<const std::byte> data, VkDeviceSize offset = 0);

    [[nodiscard]] VkBuffer handle() const { return buffer_; }
    [[nodiscard]] std::uint64_t size() const { return size_; }
    [[nodiscard]] MemoryUsage memoryUsage() const { return memoryUsage_; }

private:
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkBuffer buffer_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkDeviceSize size_{};
    MemoryUsage memoryUsage_{MemoryUsage::DeviceLocal};
};

} // namespace engine::rhi::vulkan
