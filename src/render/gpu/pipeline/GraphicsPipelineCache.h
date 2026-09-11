#pragma once

#include "render/gpu/common/GpuCacheBase.h"
#include "render/gpu/pipeline/GraphicsPipelineGpuResource.h"

#include <cstdint>

namespace engine {

using GraphicsPipelineCacheKey = std::uint64_t;

class GraphicsPipelineCache final
    : public GpuCacheBase<GraphicsPipelineCacheKey, GraphicsPipelineGpuResource> {};

} // namespace engine
