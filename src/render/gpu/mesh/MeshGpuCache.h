#pragma once

#include "render/base/RenderHandle.h"
#include "render/gpu/common/IGpuCache.h"
#include "render/gpu/mesh/MeshGpuResource.h"

#include <cstdint>
#include <unordered_map>

namespace engine {

struct MeshGpuCacheKey {
    std::uint64_t source{};
    std::uint64_t version{};

    bool operator==(const MeshGpuCacheKey&) const = default;
};

struct MeshGpuCacheKeyHash {
    [[nodiscard]] std::size_t operator()(const MeshGpuCacheKey& key) const;
};

class MeshGpuCache final : public IGpuCache<MeshGpuCacheKey, MeshGpuResource> {
public:
    [[nodiscard]] static std::uint64_t sourceKey(MeshHandle handle);
    [[nodiscard]] static MeshGpuCacheKey key(MeshHandle handle, std::uint64_t version);

    [[nodiscard]] const MeshGpuResource* find(const MeshGpuCacheKey& key) const override;
    [[nodiscard]] std::optional<MeshGpuResource> put(MeshGpuCacheKey key,
                                                     MeshGpuResource resource) override;
    [[nodiscard]] std::optional<MeshGpuResource> remove(const MeshGpuCacheKey& key) override;
    [[nodiscard]] std::vector<Entry> extractIf(const Predicate& predicate) override;
    [[nodiscard]] std::vector<Entry> extractAll() override;
    [[nodiscard]] std::size_t size() const override { return entries_.size(); }

private:
    std::unordered_map<MeshGpuCacheKey, MeshGpuResource, MeshGpuCacheKeyHash> entries_;
};

} // namespace engine
