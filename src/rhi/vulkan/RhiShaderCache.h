#pragma once

#include "renderer/ShaderCompiler.h"
#include "rhi/RhiTypes.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine {

class ShaderModule;

class RhiShaderCache final {
public:
    RhiShaderCache(VkDevice device, CompiledShaderCache& compiledShaders);
    ~RhiShaderCache();

    [[nodiscard]] rhi::ShaderHandle getOrCreate(CompiledShaderHandle shader);
    [[nodiscard]] VkShaderModule resolve(rhi::ShaderHandle handle) const;
    void invalidate(std::span<const CompiledShaderId> shaders,
                    std::uint64_t retireSerial);
    void collect(std::uint64_t completedSerial);
    void clear();

private:
    struct Slot {
        std::unique_ptr<ShaderModule> module;
        CompiledShaderId compiledId{};
        std::uint32_t generation{1};
    };
    struct RetiredModule {
        std::unique_ptr<ShaderModule> module;
        std::uint64_t serial{};
    };

    VkDevice device_{VK_NULL_HANDLE};
    CompiledShaderCache& compiledShaders_;
    std::unordered_map<CompiledShaderId, std::uint32_t> entries_;
    std::vector<Slot> slots_;
    std::vector<RetiredModule> retired_;
};

} // namespace engine
