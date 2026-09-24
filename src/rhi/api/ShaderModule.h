#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace engine::rhi {

enum class ShaderStage {
    Vertex,
    Fragment,
};

struct ShaderDesc {
    ShaderStage stage{ShaderStage::Vertex};
    std::span<const std::byte> bytecode;
    std::string debugName;
};

struct ShaderModuleInfo {
    ShaderStage stage{ShaderStage::Vertex};
    std::size_t bytecodeSize{};
    std::string debugName;
};

class IShaderModule {
public:
    virtual ~IShaderModule() = default;

    [[nodiscard]] virtual const ShaderModuleInfo& info() const = 0;
    [[nodiscard]] ShaderStage stage() const { return info().stage; }
};

} // namespace engine::rhi
