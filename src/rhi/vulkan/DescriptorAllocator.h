#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine {

class DescriptorSetLayout final {
public:
    DescriptorSetLayout(VkDevice device,
                        std::span<const VkDescriptorSetLayoutBinding> bindings);
    ~DescriptorSetLayout();

    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout(DescriptorSetLayout&&) = delete;
    DescriptorSetLayout& operator=(DescriptorSetLayout&&) = delete;

    [[nodiscard]] VkDescriptorSetLayout handle() const { return layout_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
};

class DescriptorAllocator final {
public:
    explicit DescriptorAllocator(VkDevice device, std::uint32_t maxSets = 32);
    ~DescriptorAllocator();

    DescriptorAllocator(const DescriptorAllocator&) = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;
    DescriptorAllocator(DescriptorAllocator&&) = delete;
    DescriptorAllocator& operator=(DescriptorAllocator&&) = delete;

    [[nodiscard]] VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    void reset();

private:
    [[nodiscard]] VkDescriptorPool createPool(std::uint32_t maxSets) const;

    VkDevice device_{VK_NULL_HANDLE};
    std::vector<VkDescriptorPool> pools_;
    std::size_t activePool_{};
    std::uint32_t nextPoolSize_{};
};

} // namespace engine
