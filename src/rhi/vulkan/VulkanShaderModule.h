#pragma once

#include "rhi/api/ShaderModule.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace engine::rhi::vulkan {

class VulkanShaderModule final : public IShaderModule {
public:
    VulkanShaderModule(VkDevice device,
                       ShaderStage stage,
                       std::span<const std::byte> bytecode,
                       std::string_view debugName);
    ~VulkanShaderModule() override;

    VulkanShaderModule(const VulkanShaderModule&) = delete;
    VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;
    VulkanShaderModule(VulkanShaderModule&&) = delete;
    VulkanShaderModule& operator=(VulkanShaderModule&&) = delete;

    [[nodiscard]] VkShaderModule handle() const { return module_; }
    [[nodiscard]] ShaderStage stage() const override { return stage_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkShaderModule module_{VK_NULL_HANDLE};
    ShaderStage stage_{ShaderStage::Vertex};
};

} // namespace engine::rhi::vulkan
