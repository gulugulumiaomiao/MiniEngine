#pragma once

#include "render/gpu/common/GpuCacheBase.h"
#include "render/GraphicsPipelineStorageEntry.h"

#include <cstdint>

namespace engine {

using GraphicsPipelineStorageCacheKey = std::uint64_t;

class GraphicsPipelineStorageCache final
    : public GpuCacheBase<GraphicsPipelineStorageCacheKey, GraphicsPipelineStorageEntry> {};

} // namespace engine
