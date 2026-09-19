#pragma once

#include "render/renderer/DrawList.h"

#include <cstddef>

namespace engine {

class RenderContext;
class StaticBatcher;

[[nodiscard]] std::size_t drawItemCount(const DrawList& drawList);
[[nodiscard]] DrawList preparePipelineDrawList(RenderContext& context,
                                               StaticBatcher& staticBatcher);

} // namespace engine
