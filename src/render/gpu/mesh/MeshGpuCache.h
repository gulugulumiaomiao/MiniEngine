#pragma once

#include "render/base/RenderHandle.h"
#include "render/gpu/common/GpuCacheBase.h"
#include "render/gpu/common/GpuResourceKey.h"
#include "render/gpu/mesh/MeshGpuResource.h"

#include <cstdint>

namespace engine {

using MeshGpuCacheKey = SourceVersionKey;

class MeshGpuCache final
    : public GpuCacheBase<MeshGpuCacheKey, MeshGpuResource, SourceVersionKeyHash> {
public:
    [[nodiscard]] static std::uint64_t sourceKey(MeshHandle handle) { return handleKey(handle); }
    [[nodiscard]] static MeshGpuCacheKey key(MeshHandle handle, std::uint64_t version) {
        return {handleKey(handle), version};
    }
};

} // namespace engine
