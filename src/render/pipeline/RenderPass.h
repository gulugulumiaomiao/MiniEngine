#pragma once

#include "render/queue/RenderQueue.h"

namespace engine {

class RenderContext;
class RenderGraph;
struct DrawList;

// Abstract render pass used by programmable pipelines.
//
// A pass receives the full frame context and the prepared DrawList. It is responsible for
// filtering the items it wants to draw, declaring its render-graph resources via the provided
// RenderGraph, and emitting the actual draw commands inside the pass callback.
class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    // Record the pass into the graph. The implementation may add graphics passes, import or
    // create textures, and read from the prepared DrawList.
    virtual void execute(RenderContext& context,
                         RenderGraph& graph,
                         rhi::BindGroupHandle sceneBindGroup,
                         const DrawList& drawList) = 0;
};

} // namespace engine
