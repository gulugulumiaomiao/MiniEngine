#include "render/queue/RenderQueue.h"

#include "render/scene/RenderScene.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

void DrawSorter::sort(std::vector<DrawItem>& items, SortingCriteria criteria, const RenderScene& scene) {
    std::ranges::stable_sort(items, [&](const DrawItem& left, const DrawItem& right) {
        return computeKey(left, criteria, scene) < computeKey(right, criteria, scene);
    });
}

std::uint64_t DrawSorter::computeKey(const DrawItem& item,
                                     SortingCriteria criteria,
                                     const RenderScene& scene) const {
    std::uint64_t key = 0;

    if (hasFlag(criteria, SortingCriteria::BackToFront)) {
        // Pack an inverted distance into the high bits so farther objects draw first.
        float distance = 0.0F;
        if (const std::optional<RenderCamera>& camera = scene.camera()) {
            const std::uint32_t objectIndex = item.arguments.firstInstance;
            if (objectIndex < scene.objects().size()) {
                const math::Vec3 position =
                    math::transformPoint(scene.objects()[objectIndex].transform, math::Vec3{0.0F});
                distance = math::length(position - camera->worldPosition);
            }
        }
        const auto quantized = static_cast<std::uint32_t>(
            std::clamp(distance, 0.0F, static_cast<float>(0xFFFF)));
        key |= static_cast<std::uint64_t>(0xFFFF - quantized) << 48;
    }

    if (hasFlag(criteria, SortingCriteria::RenderQueue)) {
        // RenderQueue is signed and centered around 2000; bias to unsigned 16-bit range.
        const auto biased = static_cast<std::uint32_t>(
            std::clamp(item.renderQueue + 1000, 0, static_cast<int>(0xFFFF)));
        key |= static_cast<std::uint64_t>(biased) << 32;
    }

    if (hasFlag(criteria, SortingCriteria::Pipeline)) {
        key |= static_cast<std::uint64_t>(item.pipeline.index) << 24;
    }

    if (hasFlag(criteria, SortingCriteria::Material)) {
        key |= static_cast<std::uint64_t>(item.material.index) << 12;
    }

    if (hasFlag(criteria, SortingCriteria::Mesh)) {
        key |= static_cast<std::uint64_t>(item.mesh.index) & 0xFFFULL;
    }

    return key;
}

} // namespace engine
