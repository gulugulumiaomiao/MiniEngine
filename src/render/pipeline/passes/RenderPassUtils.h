#pragma once

#include "render/renderer/DrawList.h"
#include "render/renderer/RenderFrameStats.h"
#include "rhi/api/CommandEncoder.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace engine {

struct DrawFilter;

[[nodiscard]] std::vector<DrawItem> collectPassItems(const DrawList& drawList,
                                                     RenderPhase phase,
                                                     const DrawFilter& filter);

// Draws a filtered subset of draw items using the provided scene bind group.
// Consecutive items with identical GPU state are merged into instanced batches
// backed by the per-frame instance table. The items are assumed to have already
// been sorted and to have valid pipelines/bind groups.
[[nodiscard]] DrawSubmissionStats drawFilteredItems(std::uint32_t frameIndex,
                       std::span<const DrawItem> items,
                       rhi::BindGroupHandle sceneBindGroup,
                       rhi::IGraphicsCommandEncoder& encoder,
                       std::string_view passName = {});

} // namespace engine
