#include "rhi/vulkan/VulkanBuffer.h"
#include "core/logging/Log.h"
#include "rhi/vulkan/VulkanConversions.h"

#include <cstring>

namespace engine::rhi::vulkan {

VulkanBuffer::VulkanBuffer(VmaAllocator allocator,
                           VkDeviceSize size,
                           BufferUsage usage,
                           MemoryUsage memoryUsage)
    : allocator_(allocator), size_(size), bufferUsage_(usage), memoryUsage_(memoryUsage) {}

VulkanBuffer::~VulkanBuffer() {
    freeMemory();
}

void VulkanBuffer::allocateMemory() {
    if (buffer_ != VK_NULL_HANDLE) {
        return;
    }
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size_;
    bufferInfo.usage = toVulkan(bufferUsage_);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    const auto [vmaUsage, allocationFlags] = toVulkan(memoryUsage_);
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = vmaUsage;
    allocationInfo.flags = allocationFlags;
    if (vmaCreateBuffer(
            allocator_, &bufferInfo, &allocationInfo, &buffer_, &allocation_, nullptr) !=
        VK_SUCCESS) {
        Log::fatal("VulkanBuffer", "vmaCreateBuffer failed");
    }
}

void VulkanBuffer::freeMemory() {
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
}

void VulkanBuffer::upload(std::span<const std::byte> data, VkDeviceSize offset) {
    if (buffer_ == VK_NULL_HANDLE) {
        Log::fatal("VulkanBuffer", "Upload requires allocated buffer memory");
    }
    if (offset > size_ || data.size_bytes() > size_ - offset) {
        Log::fatal("VulkanBuffer", "Upload exceeds buffer size");
    }

    void* mapped = nullptr;
    if (vmaMapMemory(allocator_, allocation_, &mapped) != VK_SUCCESS) {
        Log::fatal("VulkanBuffer", "vmaMapMemory failed");
    }
    std::memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size_bytes());
    const VkResult flushResult =
        vmaFlushAllocation(allocator_, allocation_, offset, data.size_bytes());
    vmaUnmapMemory(allocator_, allocation_);
    if (flushResult != VK_SUCCESS) {
        Log::fatal("VulkanBuffer", "vmaFlushAllocation failed");
    }
}

} // namespace engine::rhi::vulkan
