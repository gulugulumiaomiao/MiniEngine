#include "render/pipeline/MiniForwardPipeline.h"

#include "core/logging/Log.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/gpu/material/MaterialGpuManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/DepthOnlyPass.h"
#include "render/pipeline/passes/ForwardPass.h"
#include "render/pipeline/passes/ShadowCasterPass.h"
#include "render/render_graph/RenderGraph.h"
#include "render/renderer/DrawListBuilder.h"
#include "render/scene/RenderScene.h"

#include <utility>

namespace engine {

MiniForwardPipeline::MiniForwardPipeline() {
    passes_.push_back(std::make_unique<ShadowCasterPass>());
    passes_.push_back(std::make_unique<DepthOnlyPass>());
    passes_.push_back(std::make_unique<ForwardPass>());
}

void MiniForwardPipeline::render(RenderContext& context) {
    DrawListBuilder builder;
    DrawList drawList = builder.build(context.scene(), context);

    resolveMaterialBindGroups(drawList, context.frameIndex());
    std::erase_if(drawList.items,
                  [](const DrawItem& item) { return !item.pipeline || !item.materialBindGroup; });

    FRAME_GPU_MANAGER.beginFrame(context.frameIndex());
    const rhi::BindGroupHandle sceneBindGroup =
        FRAME_GPU_MANAGER.upload(context.frameIndex(), drawList);

    RenderGraph graph;
    for (const std::unique_ptr<IRenderPass>& pass : passes_) {
        pass->execute(context, graph, sceneBindGroup, drawList);
    }
    graph.compile(context.rgTexturePool());
    graph.execute(context.encoder());
    graph.reset();
}

void MiniForwardPipeline::resolveMaterialBindGroups(DrawList& drawList, std::uint32_t frameIndex) {
    MATERIAL_GPU_MANAGER.beginFrame(frameIndex);
    for (DrawItem& item : drawList.items) {
        item.materialBindGroup = MATERIAL_GPU_MANAGER.resolve(item.material);
        if (!item.materialBindGroup && item.fallbackPipeline && item.fallbackMaterial) {
            Log::error("MiniForwardPipeline",
                       "Using Error Material after material GPU preparation failed");
            item.pipeline = item.fallbackPipeline;
            item.material = item.fallbackMaterial;
            item.materialBindGroup = MATERIAL_GPU_MANAGER.resolve(item.fallbackMaterial);
        }
    }
}

void MiniForwardPipeline::addPass(std::unique_ptr<IRenderPass> pass) {
    passes_.push_back(std::move(pass));
}

void MiniForwardPipeline::setPasses(std::vector<std::unique_ptr<IRenderPass>> passes) {
    passes_ = std::move(passes);
}

void MiniForwardPipeline::onSwapchainChanged() {
    // Forward targets remain owned by Renderer in stage A.
}

} // namespace engine
