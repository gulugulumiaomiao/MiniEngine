#pragma once

#include "render/gpu/common/GpuCacheBase.h"
#include "render/gpu/common/GpuResourceKey.h"
#include "render/gpu/texture/TextureStorageEntry.h"

#include <cstdint>

namespace engine {

using TextureStorageCacheKey = SourceVersionKey;

class TextureStorageCache final
    : public GpuCacheBase<TextureStorageCacheKey, TextureStorageEntry, SourceVersionKeyHash> {
public:
    [[nodiscard]] static std::uint64_t sourceKey(RID handle) { return handleKey(handle); }
    [[nodiscard]] static TextureStorageCacheKey key(RID handle, std::uint64_t version) {
        return {handleKey(handle), version};
    }
};

} // namespace engine
