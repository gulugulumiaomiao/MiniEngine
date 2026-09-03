#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace engine {

class ShaderModule final {
public:
    ShaderModule(VkDevice device, std::span<const std::byte> bytecode,
                 std::string_view debugName);
    ~ShaderModule();

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;
    ShaderModule(ShaderModule&&) = delete;
    ShaderModule& operator=(ShaderModule&&) = delete;

    [[nodiscard]] VkShaderModule handle() const { return module_; }

private:
    VkDevice device_{VK_NULL_HANDLE};
    VkShaderModule module_{VK_NULL_HANDLE};
};

} // namespace engine
