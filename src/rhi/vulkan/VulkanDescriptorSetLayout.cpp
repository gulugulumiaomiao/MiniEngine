#include "rhi/vulkan/VulkanDescriptorSetLayout.h"

#include "core/logging/Log.h"

#include <cstdint>

namespace engine::rhi::vulkan {

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(
    VkDevice device, std::span<const VkDescriptorSetLayoutBinding> bindings) {
    initialize(device, bindings);
}

void VulkanDescriptorSetLayout::initialize(
    VkDevice device, std::span<const VkDescriptorSetLayoutBinding> bindings) {
    device_ = device;
    VkDescriptorSetLayoutCreateInfo createInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    createInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device_, &createInfo, nullptr, &layout_) != VK_SUCCESS) {
        Log::fatal("VulkanDescriptorSetLayout", "vkCreateDescriptorSetLayout failed");
    }
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
    if (layout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_, layout_, nullptr);
}

} // namespace engine::rhi::vulkan
