#include "rhi/vulkan/ShaderModule.h"
#include "core/Log.h"

#include <string>

namespace engine {

ShaderModule::ShaderModule(VkDevice device, std::span<const std::byte> bytecode,
                           std::string_view debugName)
    : device_(device) {
    const std::size_t byteCount = bytecode.size();
    if (byteCount == 0 || byteCount % sizeof(std::uint32_t) != 0) {
        Log::fatal("ShaderModule", "Invalid SPIR-V byte count: %.*s",
                   static_cast<int>(debugName.size()), debugName.data());
    }

    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = byteCount;
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(bytecode.data());
    const VkResult result = vkCreateShaderModule(device_, &createInfo, nullptr, &module_);
    if (result != VK_SUCCESS) {
        Log::fatal("ShaderModule", "vkCreateShaderModule failed for: %.*s",
                   static_cast<int>(debugName.size()), debugName.data());
    }
}

ShaderModule::~ShaderModule() {
    vkDestroyShaderModule(device_, module_, nullptr);
}

} // namespace engine
