#pragma once

#include "render/pipeline/RenderPass.h"
#include "render/renderer/DrawList.h"
#include "render/pipeline/passes/ShadowCasterPass.h"
#include "render/queue/RenderQueue.h"

namespace engine {

class ForwardPass final : public IRenderPass {
public:
    // A connected shadow output declares a stable sampled dependency. Draw data updates
    // separately each frame, including when shadows are disabled.
    explicit ForwardPass(const ShadowCasterOutput* shadowOutput = nullptr,
                         DrawFilter filter = DrawFilter{RenderQueueRange::all()},
                         rhi::LoadOp depthLoadOp = rhi::LoadOp::Clear);

    void buildGraph(RenderContext& context, RenderGraph& graph) override;
    void updateFrame(RenderContext& context, const DrawList& drawList) override;

private:
    const ShadowCasterOutput* shadowOutput_;
    DrawFilter filter_;
    rhi::LoadOp depthLoadOp_;
    std::vector<DrawItem> items_;
    math::Vec4 clearColor_{0.0F};
};

} // namespace engine
