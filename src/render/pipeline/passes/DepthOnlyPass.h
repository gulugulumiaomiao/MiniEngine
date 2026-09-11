#pragma once

#include "render/pipeline/RenderPass.h"
#include "render/queue/RenderQueue.h"

namespace engine {

// Depth-only pre-pass. Draws the DepthOnly phase into the camera depth target.
// When the pass has no matching draw items it does not record anything.
class DepthOnlyPass final : public IRenderPass {
public:
    explicit DepthOnlyPass(DrawFilter filter = DrawFilter{RenderQueueRange::opaque()});

    void execute(RenderContext& context, RenderGraph& graph, const DrawList& drawList) override;

private:
    DrawFilter filter_;
};

} // namespace engine
