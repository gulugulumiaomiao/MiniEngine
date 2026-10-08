#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "core/math/hash.h"
#include "render/uniform/UniformSetLayout.h"
#include "rhi/api/ResourceDesc.h" // rhi::RID, PipelineLayoutDesc

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

// The engine-layer view of a VkPipelineLayout: an ordered array of per-set UniformSetLayout (set
// 0..N) plus the pipeline-layout RID that graphics pipelines are created against. Its identity is
// the ordered combination of each set layout's orderedHash, so two programs whose sets match share
// one PipelineLayout (and one VkPipelineLayout) through the process-wide cache.
class PipelineLayout final : public RefCounted {
public:
    // Dedup entry point: returns the shared instance for this set-layout sequence, creating on miss.
    [[nodiscard]] static Ref<PipelineLayout>
    acquire(std::vector<Ref<UniformSetLayout>> setLayouts);
    static void clearCache();

    explicit PipelineLayout(std::vector<Ref<UniformSetLayout>> setLayouts);
    ~PipelineLayout() override;
    PipelineLayout(const PipelineLayout&) = delete;
    PipelineLayout& operator=(const PipelineLayout&) = delete;

    // Builds the VkPipelineLayout from the set layouts' RIDs (idempotent).
    void create();

    [[nodiscard]] rhi::RID rid() const { return rid_; }
    [[nodiscard]] Hash64 hash() const { return hash_; }
    [[nodiscard]] const std::vector<Ref<UniformSetLayout>>& setLayouts() const { return setLayouts_; }

private:
    std::uint64_t deviceUid_{};
    std::vector<Ref<UniformSetLayout>> setLayouts_;
    rhi::RID rid_;
    Hash64 hash_{};

    static std::unordered_map<Hash64, Ref<PipelineLayout>> cache_;
    static std::uint64_t cacheDeviceUid_;
};

} // namespace engine
