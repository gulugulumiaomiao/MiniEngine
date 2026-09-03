#pragma once

#include <vk_mem_alloc.h>

namespace engine {

class GpuAllocator final {
public:
    GpuAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
    ~GpuAllocator();

    GpuAllocator(const GpuAllocator&) = delete;
    GpuAllocator& operator=(const GpuAllocator&) = delete;
    GpuAllocator(GpuAllocator&&) = delete;
    GpuAllocator& operator=(GpuAllocator&&) = delete;

    [[nodiscard]] VmaAllocator handle() const { return allocator_; }

private:
    VmaAllocator allocator_{VK_NULL_HANDLE};
};

} // namespace engine
