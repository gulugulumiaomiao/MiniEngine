#pragma once

#include <vulkan/vulkan.h>

#include <span>

namespace engine::rhi::vulkan {

// Wrapper around a VkDescriptorSetLayout, owned by the device's set-layout handle pool. It supports
// the two-step lifecycle the reflection-driven UniformSetLayout needs: default-construct to reserve
// a RID before the bindings are known (descriptor_set_layout_allocate_rid), then initialize() to
// build the VkDescriptorSetLayout once they are (descriptor_set_layout_build).
class VulkanDescriptorSetLayout final {
public:
    // Reserved-slot constructor: leaves layout_ null so the device can mint a RID before the
    // bindings are known; initialize() builds it later.
    VulkanDescriptorSetLayout() = default;
    VulkanDescriptorSetLayout(VkDevice device,
                              std::span<const VkDescriptorSetLayoutBinding> bindings);
    ~VulkanDescriptorSetLayout();

    VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&) = delete;
    VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;
    VulkanDescriptorSetLayout(VulkanDescriptorSetLayout&&) = delete;
    VulkanDescriptorSetLayout& operator=(VulkanDescriptorSetLayout&&) = delete;

    // Build the VkDescriptorSetLayout into a reserved (default-constructed) slot.
    void initialize(VkDevice device, std::span<const VkDescriptorSetLayoutBinding> bindings);

    [[nodiscard]] VkDescriptorSetLayout handle() const { return layout_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
};

} // namespace engine::rhi::vulkan
