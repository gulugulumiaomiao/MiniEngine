#include "render/pipeline/passes/DepthOnlyPass.h"

#include "render/gpu/frame/FrameGpuManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/RenderPassUtils.h"
#include "render/render_graph/RenderGraph.h"
#include "render/render_target/RenderTarget.h"
#include "render/scene/RenderScene.h"
#include "rhi/api/Swapchain.h"

#include <algorithm>
#include <vector>

namespace engine {

DepthOnlyPass::DepthOnlyPass(DrawFilter filter) : filter_(std::move(filter)) {}

void DepthOnlyPass::execute(RenderContext& context,
                            RenderGraph& graph,
                            const DrawList& drawList) {
    std::vector<DrawItem> items;
    items.reserve(drawList.items.size());
    for (const DrawItem& item : drawList.items) {
        if (item.renderPhase != RenderPhase::DepthOnly) {
            continue;
        }
        const std::uint32_t objectIndex = item.arguments.firstInstance;
        const std::uint32_t layerMask = objectIndex < context.scene().objects().size()
                                            ? context.scene().objects()[objectIndex].layerMask
                                            : 0xFFFFFFFFU;
        if (filter_.accepts(item, layerMask)) {
            items.push_back(item);
        }
    }
    if (items.empty()) {
        return;
    }

    RenderTarget& forwardTarget = context.currentForwardTarget();
    const RgTextureHandle depthHandle = forwardTarget.importDepth(graph);

    RgRenderingInfo rendering;
    rendering.renderArea = {0, 0, context.swapchain().width(), context.swapchain().height()};
    rendering.depthAttachments.push_back(
        {depthHandle, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.0F});

    std::vector<RgResourceUsage> resources;
    resources.reserve(1);
    resources.push_back({depthHandle, rhi::TextureAspect::Depth, rhi::ResourceState::DepthAttachment});

    graph.addGraphicsPass("DepthOnly",
                          std::move(rendering),
                          std::move(resources),
                          [items = std::move(items), &context](
                              rhi::IGraphicsCommandEncoder& encoder) mutable {
                              encoder.setViewport({0.0F,
                                                   0.0F,
                                                   static_cast<float>(context.swapchain().width()),
                                                   static_cast<float>(context.swapchain().height()),
                                                   0.0F,
                                                   1.0F});
                              encoder.setScissor(
                                  {0, 0, context.swapchain().width(), context.swapchain().height()});
                              drawFilteredItems(context.frameIndex(),
                                                items,
                                                FRAME_GPU_MANAGER.sceneBindGroup(context.frameIndex()),
                                                encoder);
                          });
}

} // namespace engine
