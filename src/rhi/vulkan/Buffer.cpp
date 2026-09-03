#include "rhi/vulkan/Buffer.h"
#include "core/Log.h"

#include <cstring>

namespace engine {

Buffer::Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage,
               VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocationFlags)
    : allocator_(allocator), size_(size) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = allocationFlags;
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocationInfo, &buffer_, &allocation_, nullptr) !=
        VK_SUCCESS) {
        Log::fatal("Buffer", "vmaCreateBuffer failed");
    }
}

Buffer::~Buffer() {
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }
}

void Buffer::upload(std::span<const std::byte> data, VkDeviceSize offset) {
    if (offset > size_ || data.size_bytes() > size_ - offset) {
        Log::fatal("Buffer", "Upload exceeds buffer size");
    }

    void* mapped = nullptr;
    if (vmaMapMemory(allocator_, allocation_, &mapped) != VK_SUCCESS) {
        Log::fatal("Buffer", "vmaMapMemory failed");
    }
    std::memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size_bytes());
    const VkResult flushResult = vmaFlushAllocation(allocator_, allocation_, offset, data.size_bytes());
    vmaUnmapMemory(allocator_, allocation_);
    if (flushResult != VK_SUCCESS) {
        Log::fatal("Buffer", "vmaFlushAllocation failed");
    }
}

} // namespace engine
