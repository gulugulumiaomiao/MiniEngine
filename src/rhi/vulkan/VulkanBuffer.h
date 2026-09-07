#pragma once

#include "rhi/api/Buffer.h"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <span>

namespace engine::rhi::vulkan {

class VulkanBuffer final : public IBuffer {
public:
    VulkanBuffer(VmaAllocator allocator,
                 VkDeviceSize size,
                 VkBufferUsageFlags usage,
                 VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO,
                 VmaAllocationCreateFlags allocationFlags = 0);
    ~VulkanBuffer() override;

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) = delete;
    VulkanBuffer& operator=(VulkanBuffer&&) = delete;

    void upload(std::span<const std::byte> data, VkDeviceSize offset = 0);

    [[nodiscard]] VkBuffer handle() const { return buffer_; }
    [[nodiscard]] std::uint64_t size() const override { return size_; }

private:
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkBuffer buffer_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkDeviceSize size_{};
};

} // namespace engine::rhi::vulkan
