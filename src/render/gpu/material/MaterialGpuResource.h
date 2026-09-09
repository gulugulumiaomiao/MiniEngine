#pragma once

#include "rhi/api/ResourceDesc.h"

#include <cstdint>

namespace engine {

struct MaterialGpuResource {
    rhi::BufferHandle uniformBuffer;
    std::uint64_t uniformCapacity{};
    rhi::BindGroupHandle bindGroup;
};

} // namespace engine
