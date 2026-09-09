#pragma once

#include "render/base/RenderHandle.h"
#include "render/gpu/common/IGpuCache.h"
#include "render/gpu/texture/TextureGpuResource.h"

#include <cstdint>
#include <unordered_map>

namespace engine {

struct TextureGpuCacheKey {
    std::uint64_t source{};
    std::uint64_t version{};

    bool operator==(const TextureGpuCacheKey&) const = default;
};

struct TextureGpuCacheKeyHash {
    [[nodiscard]] std::size_t operator()(const TextureGpuCacheKey& key) const;
};

class TextureGpuCache final : public IGpuCache<TextureGpuCacheKey, TextureGpuResource> {
public:
    [[nodiscard]] static std::uint64_t sourceKey(TextureHandle handle);
    [[nodiscard]] static TextureGpuCacheKey key(TextureHandle handle, std::uint64_t version);

    [[nodiscard]] const TextureGpuResource* find(const TextureGpuCacheKey& key) const override;
    [[nodiscard]] std::optional<TextureGpuResource> put(TextureGpuCacheKey key,
                                                        TextureGpuResource resource) override;
    [[nodiscard]] std::optional<TextureGpuResource> remove(const TextureGpuCacheKey& key) override;
    [[nodiscard]] std::vector<Entry> extractIf(const Predicate& predicate) override;
    [[nodiscard]] std::vector<Entry> extractAll() override;
    [[nodiscard]] std::size_t size() const override { return entries_.size(); }

private:
    std::unordered_map<TextureGpuCacheKey, TextureGpuResource, TextureGpuCacheKeyHash> entries_;
};

} // namespace engine
