#pragma once

#include "rhi/api/ResourceDesc.h"

#include <cstddef>
#include <string>

namespace engine::rhi {

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
