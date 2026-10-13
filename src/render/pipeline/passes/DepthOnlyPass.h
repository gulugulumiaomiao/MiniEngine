#pragma once

#include "render/pipeline/RenderPass.h"
#include "render/renderer/DrawList.h"
#include "render/queue/RenderQueue.h"

namespace engine {

// Depth-only pre-pass. Draws the DepthOnly phase into the camera depth target.
// The clear remains active even when no matching draw items exist.
class DepthOnlyPass final : public IRenderPass {
public:
    explicit DepthOnlyPass(DrawFilter filter = DrawFilter{RenderQueueRange::opaque()});

    void buildGraph(RenderContext& context, RenderGraph& graph) override;
    void updateFrame(RenderContext& context, const DrawList& drawList) override;

private:
    DrawFilter filter_;
    std::vector<DrawItem> items_;
};

} // namespace engine
