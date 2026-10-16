#include "rhi/vulkan/VulkanShaderModule.h"
#include "core/logging/Log.h"

#include <spirv_cross.hpp>
#include <cstring>

namespace engine::rhi::vulkan {

VulkanShaderModule::VulkanShaderModule(VkDevice device,
                                       std::span<const std::byte> bytecode,
                                       std::string_view debugName)
    : device_(device) {
    const std::size_t byteCount = bytecode.size();
    if (byteCount == 0 || byteCount % sizeof(std::uint32_t) != 0) {
        Log::fatal("VulkanShaderModule",
                   "Invalid SPIR-V byte count: %.*s",
                   static_cast<int>(debugName.size()),
                   debugName.data());
    }

    std::vector<std::uint32_t> words(byteCount / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytecode.data(), byteCount);
    try {
        spirv_cross::Compiler compiler(words);
        for (const auto& entry : compiler.get_entry_points_and_stages()) {
            if (entry.execution_model != spv::ExecutionModelFragment) continue;
            compiler.set_entry_point(entry.name, entry.execution_model);
            FragmentEntry reflected;
            reflected.name = entry.name;
            const auto resources = compiler.get_shader_resources(compiler.get_active_interface_variables());
            for (const auto& output : resources.stage_outputs) {
                if (compiler.has_decoration(output.id, spv::DecorationBuiltIn)) continue;
                const auto& type = compiler.get_type(output.type_id);
                const bool directLocation = compiler.has_decoration(output.id, spv::DecorationLocation);
                const auto component = compiler.get_decoration(output.id, spv::DecorationComponent);
                const auto index = compiler.get_decoration(output.id, spv::DecorationIndex);
                FormatNumericType numeric = FormatNumericType::Undefined;
                if (type.basetype == spirv_cross::SPIRType::Float) numeric = FormatNumericType::Float;
                if (type.basetype == spirv_cross::SPIRType::UInt) numeric = FormatNumericType::Uint;
                if (type.basetype == spirv_cross::SPIRType::Int) numeric = FormatNumericType::Sint;
                if (!directLocation || component || index || type.width != 32 || type.columns != 1 ||
                    !type.array.empty() || numeric == FormatNumericType::Undefined) {
                    reflected.supported = false;
                    continue;
                }
                reflected.outputs.push_back({compiler.get_decoration(output.id, spv::DecorationLocation), numeric});
            }
            fragmentEntries_.push_back(std::move(reflected));
        }
    } catch (const spirv_cross::CompilerError& error) {
        fragmentEntries_.clear();
        Log::error("VulkanShaderModule", "Cannot reflect fragment outputs: %s", error.what());
    }

    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = byteCount;
    createInfo.pCode = words.data();
    const VkResult result = vkCreateShaderModule(device_, &createInfo, nullptr, &module_);
    if (result != VK_SUCCESS) {
        Log::fatal("VulkanShaderModule",
                   "vkCreateShaderModule failed for: %.*s",
                   static_cast<int>(debugName.size()),
                   debugName.data());
    }
}

bool VulkanShaderModule::fragmentOutputsMatch(std::string_view entry, std::span<const PixelFormat> formats) const {
    for (const auto& candidate : fragmentEntries_) {
        if (candidate.name != entry) continue;
        if (!candidate.supported) return false;
        for (const auto& output : candidate.outputs) {
            if (output.location >= formats.size()) continue; // Vulkan discards unbound outputs.
            const auto numeric = formatInfo(formats[output.location]).numericType;
            const auto expected = numeric == FormatNumericType::Uint || numeric == FormatNumericType::Sint ?
                numeric : FormatNumericType::Float;
            if (output.type != expected) return false;
        }
        return true;
    }
    return false;
}

VulkanShaderModule::~VulkanShaderModule() {
    if (module_ != VK_NULL_HANDLE)
        vkDestroyShaderModule(device_, module_, nullptr);
}

} // namespace engine::rhi::vulkan
