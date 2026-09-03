#include "rhi/vulkan/GpuAllocator.h"
#include "core/Log.h"


namespace engine {

GpuAllocator::GpuAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
    VmaAllocatorCreateInfo createInfo{};
    createInfo.instance = instance;
    createInfo.physicalDevice = physicalDevice;
    createInfo.device = device;
    createInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    if (vmaCreateAllocator(&createInfo, &allocator_) != VK_SUCCESS) {
        Log::fatal("GpuAllocator", "vmaCreateAllocator failed");
    }
}

GpuAllocator::~GpuAllocator() {
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
    }
}

} // namespace engine
