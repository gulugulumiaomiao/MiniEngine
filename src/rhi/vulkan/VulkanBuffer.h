#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace engine::rhi::vulkan {

// Concrete Vulkan buffer. Stored directly (by value) in the device's buffer handle pool, so it
// is neither copyable nor movable; the pool constructs it in place and destroys it on release.
//
// Two-phase lifecycle mirrors the device's split buffer API: the constructor only records the
// creation parameters (size/usage/memoryUsage) and allocates the pool slot's object, leaving the
// VkBuffer/VmaAllocation null; allocateMemory() performs the actual VMA allocation, freeMemory()
// releases it while keeping the object (and its RID) alive. The destructor frees any memory that
// is still resident, so releasing a RID without an explicit freeMemory() stays safe.
//
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

    // Phase 2: create the VkBuffer + VMA allocation from the recorded parameters. No-op if the
    // memory is already resident.
    void allocateMemory();
    // Destroy the GPU memory but keep the object (and its RID) alive for a later allocateMemory().
    void freeMemory();

    void upload(std::span<const std::byte> data, VkDeviceSize offset = 0);

    [[nodiscard]] VkBuffer handle() const { return buffer_; }
    [[nodiscard]] std::uint64_t size() const { return size_; }
    [[nodiscard]] BufferUsage bufferUsage() const { return bufferUsage_; }
    [[nodiscard]] MemoryUsage memoryUsage() const { return memoryUsage_; }
    [[nodiscard]] bool hasMemory() const { return buffer_ != VK_NULL_HANDLE; }

private:
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkBuffer buffer_{VK_NULL_HANDLE};
    VmaAllocation allocation_{VK_NULL_HANDLE};
    VkDeviceSize size_{};
    BufferUsage bufferUsage_{BufferUsage::None};
    MemoryUsage memoryUsage_{MemoryUsage::DeviceLocal};
};

} // namespace engine::rhi::vulkan
