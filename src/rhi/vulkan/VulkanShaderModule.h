#pragma once

#include "rhi/api/ResourceDesc.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <span>
#include <string_view>
#include <string>
#include <vector>

namespace engine::rhi::vulkan {

// Concrete Vulkan shader module. Stored directly (by value) in the device's shader handle pool,
// so it is non-copyable/non-movable. Keeps the native module and fragment output
// metadata per entry point; bytecode is released after creation. The pipeline
// checks the selected entry's numeric output types against its color formats.
class VulkanShaderModule final {
public:
    // Reserved-slot constructor: leaves module_ null so the device can mint a RID before the
    // bytecode is known (shader_allocate_rid). The 3-argument constructor builds immediately.
    VulkanShaderModule() = default;
    VulkanShaderModule(VkDevice device, std::span<const std::byte> bytecode, std::string_view debugName);
    ~VulkanShaderModule();

    VulkanShaderModule(const VulkanShaderModule&) = delete;
    VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;
    VulkanShaderModule(VulkanShaderModule&&) = delete;
    VulkanShaderModule& operator=(VulkanShaderModule&&) = delete;

    [[nodiscard]] VkShaderModule handle() const { return module_; }
    [[nodiscard]] bool fragmentOutputsMatch(std::string_view entry, std::span<const PixelFormat> formats) const;

private:
    struct Output { std::uint32_t location; FormatNumericType type; };
    struct FragmentEntry {
        std::string name;
        std::vector<Output> outputs;
        bool supported{true};
    };
    std::vector<FragmentEntry> fragmentEntries_;
    VkDevice device_{VK_NULL_HANDLE};
    VkShaderModule module_{VK_NULL_HANDLE};
};

} // namespace engine::rhi::vulkan
