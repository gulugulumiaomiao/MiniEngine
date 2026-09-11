#pragma once

#include "render/renderer/DrawList.h"
#include "rhi/api/CommandEncoder.h"

#include <functional>
#include <span>
#include <vector>

namespace engine {

// Draws a filtered subset of draw items using the provided scene bind group.
// The items are assumed to have already been sorted and to have valid pipelines/bind groups.
void drawFilteredItems(std::span<const DrawItem> items,
                       rhi::BindGroupHandle sceneBindGroup,
                       rhi::IGraphicsCommandEncoder& encoder);

} // namespace engine
