#include "render/queue/RenderQueue.h"

#include "render/scene/RenderScene.h"

#include <algorithm>
#include <cmath>

namespace engine {

bool DrawFilter::accepts(const DrawItem& item, std::uint32_t objectLayerMask) const {
    if (!queueRange.contains(item.renderQueue)) {
        return false;
    }
    if ((layerMask & objectLayerMask) == 0) {
        return false;
    }
    return true;
}

void DrawSorter::sort(std::vector<DrawItem>& items,
                      SortingCriteria criteria,
                      const RenderScene& scene) {
    std::ranges::stable_sort(items, [&](const DrawItem& left, const DrawItem& right) {
        return computeKey(left, criteria, scene) < computeKey(right, criteria, scene);
    });
}

DrawSorter::SortKey DrawSorter::computeKey(const DrawItem& item,
                                          SortingCriteria criteria,
                                          const RenderScene& scene) const {
    float depth = 0.0F;

    if (hasFlag(criteria, SortingCriteria::BackToFront)) {
        // Invert distance so farther objects draw first.
        float distance = 0.0F;
        if (const std::optional<RenderCamera>& camera = scene.camera()) {
            const std::uint32_t objectIndex = item.sourceObjectIndex();
            if (objectIndex < scene.objects().size()) {
                const math::Vec3 position =
                    math::transformPoint(scene.objects()[objectIndex].transform, math::Vec3{0.0F});
                distance = math::length(position - camera->worldPosition);
            }
        }
        // Preserve fractional distances and keep malformed transforms from breaking ordering.
        depth = std::isnan(distance) ? 0.0F : -distance;
    }

    // Each component is independent: large IDs cannot overwrite render queue/depth bits.
    return {depth,
            hasFlag(criteria, SortingCriteria::RenderQueue) ? item.renderQueue : 0,
            hasFlag(criteria, SortingCriteria::Pipeline) ? item.pipeline.index() : 0U,
            hasFlag(criteria, SortingCriteria::Material) ? item.materialKey : 0ULL,
            hasFlag(criteria, SortingCriteria::Mesh) ? item.indexBuffer.index() : 0U};
}

} // namespace engine
