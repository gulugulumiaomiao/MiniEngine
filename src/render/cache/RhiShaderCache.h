#pragma once

#include "render/shader/ShaderCompiler.h"
#include "rhi/api/RhiTypes.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

class RhiShaderCache final {
public:
    RhiShaderCache(rhi::IDevice& device, CompiledShaderCache& compiledShaders);
    ~RhiShaderCache();

    [[nodiscard]] rhi::ShaderHandle getOrCreate(CompiledShaderHandle shader);
    void invalidate(std::span<const CompiledShaderId> shaders, std::uint64_t retireSerial);
    void collect(std::uint64_t completedSerial);
    void clear();

private:
    struct Slot {
        rhi::ShaderHandle shader;
        CompiledShaderId compiledId{};
    };
    struct RetiredModule {
        rhi::ShaderHandle shader;
        std::uint64_t serial{};
    };

    rhi::IDevice& device_;
    CompiledShaderCache& compiledShaders_;
    std::unordered_map<CompiledShaderId, std::uint32_t> entries_;
    std::vector<Slot> slots_;
    std::vector<RetiredModule> retired_;
};

} // namespace engine
