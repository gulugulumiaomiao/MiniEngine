#pragma once

#include "render/renderer/DrawList.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace engine {
namespace rhi {
class IDevice;
}

struct StaticBatcherLimits {
    std::uint32_t maxSourceItemsPerBatch{128};
    std::uint32_t maxIndicesPerBatch{1U << 20U};
};

struct StaticBatcherStats {
    std::size_t sourceItems{};
    std::size_t combinedDraws{};
    std::size_t cacheHits{};
    std::size_t cacheMisses{};
};

class StaticBatcher final {
public:
    explicit StaticBatcher(StaticBatcherLimits limits = {});
    ~StaticBatcher();

    StaticBatcher(const StaticBatcher&) = delete;
    StaticBatcher& operator=(const StaticBatcher&) = delete;

    void process(DrawList& drawList, rhi::IDevice& device);
    void clear();
    [[nodiscard]] const StaticBatcherStats& stats() const { return stats_; }

private:
    struct CachedGeometry;
    [[nodiscard]] std::uint64_t key(std::span<const DrawItem> items) const;
    [[nodiscard]] CachedGeometry* resolve(std::span<const DrawItem> items, rhi::IDevice& device);

    StaticBatcherLimits limits_;
    rhi::IDevice* device_{};
    std::unordered_map<std::uint64_t, std::unique_ptr<CachedGeometry>> cache_;
    StaticBatcherStats stats_;
};

} // namespace engine
