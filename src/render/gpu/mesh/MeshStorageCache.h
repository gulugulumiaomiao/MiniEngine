#pragma once

#include "render/base/RenderHandle.h"
#include "render/gpu/common/GpuCacheBase.h"
#include "render/gpu/common/GpuResourceKey.h"
#include "render/gpu/mesh/MeshStorageEntry.h"

#include <cstdint>

namespace engine {

using MeshStorageCacheKey = SourceVersionKey;

class MeshStorageCache final
    : public GpuCacheBase<MeshStorageCacheKey, MeshStorageEntry, SourceVersionKeyHash> {
public:
    [[nodiscard]] static std::uint64_t sourceKey(RID handle) { return handleKey(handle); }
    [[nodiscard]] static MeshStorageCacheKey key(RID handle, std::uint64_t version) {
        return {handleKey(handle), version};
    }
};

} // namespace engine
