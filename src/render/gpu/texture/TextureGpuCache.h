#pragma once

#include "render/base/RenderHandle.h"
#include "render/gpu/common/GpuCacheBase.h"
#include "render/gpu/common/GpuResourceKey.h"
#include "render/gpu/texture/TextureGpuResource.h"

#include <cstdint>

namespace engine {

using TextureGpuCacheKey = SourceVersionKey;

class TextureGpuCache final
    : public GpuCacheBase<TextureGpuCacheKey, TextureGpuResource, SourceVersionKeyHash> {
public:
    [[nodiscard]] static std::uint64_t sourceKey(TextureHandle handle) { return handleKey(handle); }
    [[nodiscard]] static TextureGpuCacheKey key(TextureHandle handle, std::uint64_t version) {
        return {handleKey(handle), version};
    }
};

} // namespace engine
