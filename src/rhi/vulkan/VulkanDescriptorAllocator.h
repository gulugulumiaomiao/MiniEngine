#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine::rhi::vulkan {

class VulkanDescriptorSetLayout final {
public:
    VulkanDescriptorSetLayout(VkDevice device,
                              std::span<const VkDescriptorSetLayoutBinding> bindings);
    ~VulkanDescriptorSetLayout();

    VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&) = delete;
    VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;
    VulkanDescriptorSetLayout(VulkanDescriptorSetLayout&&) = delete;
    VulkanDescriptorSetLayout& operator=(VulkanDescriptorSetLayout&&) = delete;

    [[nodiscard]] VkDescriptorSetLayout handle() const { return layout_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
};

class VulkanDescriptorAllocator final {
public:
    explicit VulkanDescriptorAllocator(VkDevice device, std::uint32_t maxSets = 32);
    ~VulkanDescriptorAllocator();

    VulkanDescriptorAllocator(const VulkanDescriptorAllocator&) = delete;
    VulkanDescriptorAllocator& operator=(const VulkanDescriptorAllocator&) = delete;
    VulkanDescriptorAllocator(VulkanDescriptorAllocator&&) = delete;
    VulkanDescriptorAllocator& operator=(VulkanDescriptorAllocator&&) = delete;

    [[nodiscard]] VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    void free(VkDescriptorSet descriptor);
    void reset();

private:
    [[nodiscard]] VkDescriptorPool createPool(std::uint32_t maxSets) const;

    VkDevice device_{VK_NULL_HANDLE};
    std::vector<VkDescriptorPool> pools_;
    std::unordered_map<VkDescriptorSet, VkDescriptorPool> owners_;
    std::size_t activePool_{};
    std::uint32_t nextPoolSize_{};
};

} // namespace engine::rhi::vulkan
