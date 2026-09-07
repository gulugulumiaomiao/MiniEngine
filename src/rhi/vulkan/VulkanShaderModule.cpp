#include "rhi/vulkan/VulkanShaderModule.h"
#include "core/logging/Log.h"

#include <string>

namespace engine::rhi::vulkan {

VulkanShaderModule::VulkanShaderModule(VkDevice device,
                                       ShaderStage stage,
                                       std::span<const std::byte> bytecode,
                                       std::string_view debugName)
    : device_(device), stage_(stage) {
    const std::size_t byteCount = bytecode.size();
    if (byteCount == 0 || byteCount % sizeof(std::uint32_t) != 0) {
        Log::fatal("VulkanShaderModule",
                   "Invalid SPIR-V byte count: %.*s",
                   static_cast<int>(debugName.size()),
                   debugName.data());
    }

    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = byteCount;
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(bytecode.data());
    const VkResult result = vkCreateShaderModule(device_, &createInfo, nullptr, &module_);
    if (result != VK_SUCCESS) {
        Log::fatal("VulkanShaderModule",
                   "vkCreateShaderModule failed for: %.*s",
                   static_cast<int>(debugName.size()),
                   debugName.data());
    }
}

VulkanShaderModule::~VulkanShaderModule() {
    vkDestroyShaderModule(device_, module_, nullptr);
}

} // namespace engine::rhi::vulkan
