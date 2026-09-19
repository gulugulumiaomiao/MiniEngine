#include "render/pipeline/passes/ForwardPass.h"

#include "render/gpu/frame/FrameGpuManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/RenderPassUtils.h"
#include "render/queue/RenderQueue.h"
#include "render/render_graph/RenderGraph.h"
#include "render/render_target/RenderTarget.h"
#include "render/scene/RenderScene.h"
#include "rhi/api/Swapchain.h"

#include <algorithm>
#include <vector>

namespace engine {

ForwardPass::ForwardPass(const ShadowCasterOutput* shadowOutput, DrawFilter filter)
    : shadowOutput_(shadowOutput), filter_(std::move(filter)) {}

void ForwardPass::execute(RenderContext& context,
                          RenderGraph& graph,
                          const DrawList& drawList) {
    std::vector<DrawItem> items =
        collectPassItems(drawList, RenderPhase::Forward, filter_);
    if (items.empty() && !context.offscreenScene()) {
        return;
    }

    if (!context.offscreenScene())
        context.markBackBufferWritten();

    // Sort opaque front-to-back by queue/state, then transparent back-to-front by camera distance.
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> transparent;
    opaque.reserve(items.size());
    transparent.reserve(items.size());
    for (DrawItem& item : items) {
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

    items.clear();
    items.insert(items.end(), std::make_move_iterator(opaque.begin()), std::make_move_iterator(opaque.end()));
    items.insert(items.end(),
                 std::make_move_iterator(transparent.begin()),
                 std::make_move_iterator(transparent.end()));
    RenderTarget& forwardTarget = context.currentForwardTarget();
    const RgTextureHandle backBuffer = context.offscreenScene()
        ? forwardTarget.importColor(graph, 0, rhi::ResourceState::ShaderRead)
        : graph.importTexture({
            .texture = context.swapchain().currentTexture(),
            .view = context.swapchain().currentTextureView(),
            .initialState = context.swapchain().currentTextureState(),
            .finalState = rhi::ResourceState::Present,
            .aspect = rhi::TextureAspect::Color,
        });
    const RgTextureHandle depthHandle = forwardTarget.importDepth(graph);

    RgRenderingInfo rendering;
    rendering.renderArea = {0, 0, context.sceneWidth(), context.sceneHeight()};
    rendering.colorAttachments.push_back(
        {backBuffer, rhi::LoadOp::Clear, rhi::StoreOp::Store, drawList.clearColor});
    rendering.depthAttachments.push_back(
        {depthHandle, rhi::LoadOp::Clear, rhi::StoreOp::DontCare, 1.0F});

    std::vector<RgResourceUsage> resources;
    resources.reserve(3);
    resources.push_back({backBuffer, rhi::TextureAspect::Color, rhi::ResourceState::ColorAttachment});
    resources.push_back({depthHandle, rhi::TextureAspect::Depth, rhi::ResourceState::DepthAttachment});
    if (shadowOutput_ && shadowOutput_->shadowMap.valid()) {
        resources.push_back({shadowOutput_->shadowMap,
                             rhi::TextureAspect::Depth,
                             rhi::ResourceState::ShaderRead});
    }

    graph.addGraphicsPass("Forward",
                          std::move(rendering),
                          std::move(resources),
                          [items = std::move(items), &context](
                              rhi::IGraphicsCommandEncoder& encoder) mutable {
                              encoder.setViewport({0.0F,
                                                   0.0F,
                                                   static_cast<float>(context.sceneWidth()),
                                                   static_cast<float>(context.sceneHeight()),
                                                   0.0F,
                                                   1.0F});
                              encoder.setScissor(
                                  {0, 0, context.sceneWidth(), context.sceneHeight()});
                              context.recordSubmission(drawFilteredItems(
                                  context.frameIndex(),
                                  items,
                                  FRAME_GPU_MANAGER.sceneBindGroup(context.frameIndex()),
                                  encoder,
                                  "Forward"));
                          });
}

} // namespace engine
