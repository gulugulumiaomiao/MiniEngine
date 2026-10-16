#include "render/pipeline/passes/ForwardPass.h"

#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/RenderPassUtils.h"
#include "render/queue/RenderQueue.h"
#include "render/render_graph/RenderGraph.h"
#include "render/render_target/RenderTarget.h"
#include "render/scene/RenderScene.h"

#include <iterator>
#include <utility>

namespace engine {

ForwardPass::ForwardPass(const ShadowCasterOutput* shadowOutput, DrawFilter filter, rhi::LoadOp depthLoadOp)
    : shadowOutput_(shadowOutput), filter_(std::move(filter)), depthLoadOp_(depthLoadOp) {}

void ForwardPass::updateFrame(RenderContext& context, const DrawList& drawList) {
    filterDrawItems(context, drawList, RenderPhase::Forward, filter_, items_);
    // Sort opaque front-to-back by queue/state, then transparent back-to-front by camera distance.
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> transparent;
    opaque.reserve(items_.size());
    transparent.reserve(items_.size());
    for (DrawItem& item : items_) {
        if (RenderQueueRange::opaque().contains(item.renderQueue)) {
            opaque.push_back(std::move(item));
        } else {
            transparent.push_back(std::move(item));
        }
    }

    DrawSorter sorter;
    sorter.sort(opaque,
                SortingCriteria::RenderQueue | SortingCriteria::Pipeline | SortingCriteria::Material |
                    SortingCriteria::Mesh,
                context.scene());
    sorter.sort(transparent, SortingCriteria::BackToFront, context.scene());

    items_.clear();
    items_.insert(items_.end(), std::make_move_iterator(opaque.begin()), std::make_move_iterator(opaque.end()));
    items_.insert(items_.end(),
                 std::make_move_iterator(transparent.begin()),
                 std::make_move_iterator(transparent.end()));
    clearColor_ = drawList.clearColor;
}

void ForwardPass::buildGraph(RenderContext& context, RenderGraph& graph) {
    const RgTextureHandle backBuffer = context.offscreenScene()
        ? graph.importTexture("ForwardColor",
            [](RenderContext& frame) { return frame.currentForwardTarget().colorBinding(0); },
            rhi::ResourceState::ShaderRead)
        : graph.importSwapchainTexture(context.swapchain());
    const RgTextureHandle depthHandle = graph.importTexture("ForwardDepth",
        [](RenderContext& frame) { return frame.currentForwardTarget().depthBinding(); },
        rhi::ResourceState::DepthAttachment);

    RgRasterPass pass;
    pass.name = "Forward";
    pass.renderAreaResolver = [](const RenderContext& frame) {
        return rhi::Rect{0, 0, frame.sceneWidth(), frame.sceneHeight()};
    };
    pass.colorAttachments.push_back(
        {backBuffer, rhi::LoadOp::Clear, rhi::StoreOp::Store, math::Vec4{0.0F}});
    pass.depthAttachments.push_back(
        {depthHandle, depthLoadOp_, rhi::StoreOp::DontCare, 1.0F});

    if (shadowOutput_ && shadowOutput_->shadowMap.valid()) {
        pass.sampledTextures.push_back(shadowOutput_->shadowMap);
    }

    pass.colorAttachments[0].clearColorResolver =
        [this](const RenderContext&) { return clearColor_; };

    pass.execute = [this](const RgExecuteContext& execution) {
        if (!execution.frame.offscreenScene()) execution.frame.markBackBufferWritten();
        drawRasterItems(execution, items_);
    };
    graph.addGraphicsPass(std::move(pass));
}

} // namespace engine
