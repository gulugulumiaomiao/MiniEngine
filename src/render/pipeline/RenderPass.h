#pragma once

#include "render/queue/RenderQueue.h"

namespace engine {

class RenderContext;
class RenderGraph;
struct DrawList;

// Abstract render pass used by programmable pipelines.
//
// buildGraph declares persistent structure. updateFrame filters the current DrawList into
// pass-owned data. Stable callbacks read that data and RgExecuteContext::frame at execution.
//
// Pass callbacks run during RenderGraph::execute, after compilation. They query the frame's
// scene bind group from FRAME_GPU_MANAGER at execution time, so pipelines may rebuild the
// bind group between compile and execute (e.g. to bind the resolved shadow map view).
class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    // Record the pass into the graph. The implementation may add graphics passes, import or
    // create textures. Callbacks must not capture this call's local frame context.
    virtual void buildGraph(RenderContext& context, RenderGraph& graph) = 0;
    // Update draw data without changing the graph topology, including when it is empty.
    virtual void updateFrame(RenderContext& context, const DrawList& drawList) = 0;
};

} // namespace engine
