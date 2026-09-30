#pragma once

#include "render/renderer/DrawList.h"
#include "rhi/api/Command.h"

#include <cstdint>
#include <span>
#include <vector>

namespace engine {

// Draws a filtered subset of draw items using the provided scene/global uniform sets.
// Consecutive items with identical GPU state are merged into instanced batches
// backed by the per-frame instance table. The items are assumed to have already
// been sorted and to have valid pipelines/material uniform sets.
void drawFilteredItems(std::uint32_t frameIndex,
                       std::span<const DrawItem> items,
                       const Ref<UniformSet>& scene,
                       const Ref<UniformSet>& global,
                       rhi::RID commandBuffer);

} // namespace engine
