#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>
#include <string_view>

namespace engine::rhi::vulkan {

// Concrete Vulkan shader module. Stored directly (by value) in the device's shader handle pool,
// so it is non-copyable/non-movable. Keeps only the native VkShaderModule: a module is
// stage-agnostic in Vulkan (the stage is chosen when the pipeline references it), and the
// debug name stays with the ShaderDesc the caller already holds.
class VulkanShaderModule final {
public:
    VulkanShaderModule(VkDevice device, std::span<const std::byte> bytecode, std::string_view debugName);
    ~VulkanShaderModule();

    VulkanShaderModule(const VulkanShaderModule&) = delete;
    VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;
    VulkanShaderModule(VulkanShaderModule&&) = delete;
    VulkanShaderModule& operator=(VulkanShaderModule&&) = delete;

    [[nodiscard]] VkShaderModule handle() const { return module_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkShaderModule module_{VK_NULL_HANDLE};
};

} // namespace engine::rhi::vulkan
