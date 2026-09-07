#pragma once

#include "rhi/api/ResourceDesc.h"

namespace engine::rhi {

class IShaderModule {
public:
    virtual ~IShaderModule() = default;

    [[nodiscard]] virtual ShaderStage stage() const = 0;
};

} // namespace engine::rhi
