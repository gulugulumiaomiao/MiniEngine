#include "render/pipeline/passes/DepthOnlyPass.h"

#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/RenderPassUtils.h"
#include "render/render_graph/RenderGraph.h"
#include "render/render_target/RenderTarget.h"

#include <utility>

namespace engine {

DepthOnlyPass::DepthOnlyPass(DrawFilter filter) : filter_(std::move(filter)) {}

void DepthOnlyPass::updateFrame(RenderContext& context, const DrawList& drawList) {
    filterDrawItems(context, drawList, RenderPhase::DepthOnly, filter_, items_);
}

void DepthOnlyPass::buildGraph(RenderContext& /*context*/, RenderGraph& graph) {
    const RgTextureHandle depthHandle = graph.importTexture("ForwardDepth",
        [](RenderContext& frame) { return frame.currentForwardTarget().depthBinding(); },
        rhi::ResourceState::DepthAttachment, rhi::TextureAspect::Depth);

    RgRasterPass pass;
    pass.name = "DepthOnly";
    pass.renderAreaResolver = [](const RenderContext& frame) {
        return rhi::Rect{0, 0, frame.sceneWidth(), frame.sceneHeight()};
    };
    pass.depthAttachments.push_back(
        {depthHandle, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.0F});

    pass.execute = [this](const RgExecuteContext& execution) {
        drawRasterItems(execution, items_);
    };
    graph.addGraphicsPass(std::move(pass));
}

} // namespace engine
