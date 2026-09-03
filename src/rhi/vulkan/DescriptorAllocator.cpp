#include "rhi/vulkan/DescriptorAllocator.h"
#include "core/Log.h"

#include <array>

namespace engine {

DescriptorSetLayout::DescriptorSetLayout(
    VkDevice device, std::span<const VkDescriptorSetLayoutBinding> bindings)
    : device_(device) {
    VkDescriptorSetLayoutCreateInfo createInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    createInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &layout_) != VK_SUCCESS) {
        Log::fatal("DescriptorSetLayout", "vkCreateDescriptorSetLayout failed");
    }
}

DescriptorSetLayout::~DescriptorSetLayout() {
    vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
}

DescriptorAllocator::DescriptorAllocator(VkDevice device, std::uint32_t maxSets)
    : device_(device), nextPoolSize_(maxSets * 2) {
    pools_.push_back(createPool(maxSets));
}

VkDescriptorPool DescriptorAllocator::createPool(std::uint32_t maxSets) const {
    const std::array poolSizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 4},
    };
    VkDescriptorPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    createInfo.maxSets = maxSets;
    createInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    createInfo.pPoolSizes = poolSizes.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device_, &createInfo, nullptr, &pool) != VK_SUCCESS) {
        Log::fatal("DescriptorAllocator", "vkCreateDescriptorPool failed");
    }
    return pool;
}

DescriptorAllocator::~DescriptorAllocator() {
    for (VkDescriptorPool pool : pools_) {
        vkDestroyDescriptorPool(device_, pool, nullptr);
    }
}

VkDescriptorSet DescriptorAllocator::allocate(VkDescriptorSetLayout layout) {
    while (true) {
        VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocateInfo.descriptorPool = pools_[activePool_];
        allocateInfo.descriptorSetCount = 1;
        allocateInfo.pSetLayouts = &layout;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        const VkResult result = vkAllocateDescriptorSets(device_, &allocateInfo, &descriptor);
        if (result == VK_SUCCESS) {
            return descriptor;
        }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            Log::fatal("DescriptorAllocator", "vkAllocateDescriptorSets failed");
        }

        ++activePool_;
        if (activePool_ == pools_.size()) {
            pools_.push_back(createPool(nextPoolSize_));
            nextPoolSize_ *= 2;
        }
    }
}

void DescriptorAllocator::reset() {
    for (VkDescriptorPool pool : pools_) {
        if (vkResetDescriptorPool(device_, pool, 0) != VK_SUCCESS) {
            Log::fatal("DescriptorAllocator", "vkResetDescriptorPool failed");
        }
    }
    activePool_ = 0;
}

} // namespace engine
