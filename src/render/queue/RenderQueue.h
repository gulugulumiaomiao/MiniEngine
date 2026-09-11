#pragma once

#include "render/renderer/DrawList.h"

#include <cstdint>
#include <vector>

namespace engine {

class RenderScene;

// Render queue constants matching Unity's built-in render queues.
inline constexpr int kRenderQueueBackground = 1000;
inline constexpr int kRenderQueueGeometry = 2000;
inline constexpr int kRenderQueueOpaque = kRenderQueueGeometry;
inline constexpr int kRenderQueueAlphaTest = 2450;
inline constexpr int kRenderQueueGeometryLast = 2500;
inline constexpr int kRenderQueueTransparent = 3000;
inline constexpr int kRenderQueueOverlay = 4000;

// Inclusive range of render queues used for filtering.
struct RenderQueueRange {
    int first{};
    int last{};

    [[nodiscard]] bool contains(int queue) const { return queue >= first && queue <= last; }

    [[nodiscard]] static RenderQueueRange all() { return {0, 5000}; }
    [[nodiscard]] static RenderQueueRange opaque() { return {0, kRenderQueueGeometryLast}; }
    [[nodiscard]] static RenderQueueRange transparent() {
        return {kRenderQueueGeometryLast + 1, 5000};
    }
};

// Filtering settings for a draw call collection, equivalent to Unity FilteringSettings.
struct DrawFilter {
    RenderQueueRange queueRange{RenderQueueRange::all()};
    std::uint32_t layerMask{0xFFFFFFFFU};

    DrawFilter() = default;
    explicit DrawFilter(RenderQueueRange queueRange, std::uint32_t layerMask = 0xFFFFFFFFU)
        : queueRange(queueRange), layerMask(layerMask) {}

    [[nodiscard]] bool accepts(const DrawItem& item, std::uint32_t objectLayerMask) const;
};

// Sorting criteria flags, equivalent to Unity SortingCriteria.
enum class SortingCriteria : std::uint32_t {
    None = 0,
    RenderQueue = 1U << 0U,
    Pipeline = 1U << 1U,
    Material = 1U << 2U,
    Mesh = 1U << 3U,
    BackToFront = 1U << 4U,
};

constexpr SortingCriteria operator|(SortingCriteria left, SortingCriteria right) {
    return static_cast<SortingCriteria>(static_cast<std::uint32_t>(left) |
                                        static_cast<std::uint32_t>(right));
}

constexpr bool hasFlag(SortingCriteria value, SortingCriteria flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

// Sorts draw items using a 64-bit key derived from the requested criteria.
// Distance is only used when BackToFront is set.
class DrawSorter final {
public:
    void sort(std::vector<DrawItem>& items, SortingCriteria criteria, const RenderScene& scene);

private:
    [[nodiscard]] std::uint64_t computeKey(const DrawItem& item,
                                           SortingCriteria criteria,
                                           const RenderScene& scene) const;
};

} // namespace engine
