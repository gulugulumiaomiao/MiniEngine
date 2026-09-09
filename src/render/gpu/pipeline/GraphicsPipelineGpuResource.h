#pragma once

#include "rhi/api/ResourceDesc.h"

#include <cstdint>

namespace engine {

struct GraphicsPipelineGpuResource {
    rhi::GraphicsPipelineHandle pipeline;
    std::uint64_t program{};
    std::uint64_t vertex{};
    std::uint64_t fragment{};
};

} // namespace engine
