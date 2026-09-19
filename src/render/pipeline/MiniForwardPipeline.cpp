#include "render/pipeline/MiniForwardPipeline.h"

#include "core/logging/Log.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/pipeline/RenderPreparation.h"
#include "render/pipeline/passes/DepthOnlyPass.h"
#include "render/pipeline/passes/ForwardPass.h"
#include "render/pipeline/passes/ShadowCasterPass.h"
#include "render/render_graph/RenderGraph.h"
#include "render/renderer/RenderFrameStats.h"
#include "render/scene/RenderScene.h"

#include <utility>

namespace engine {

MiniForwardPipeline::MiniForwardPipeline() {
    passes_.push_back(std::make_unique<ShadowCasterPass>(&shadowOutput_));
    passes_.push_back(std::make_unique<DepthOnlyPass>());
    passes_.push_back(std::make_unique<ForwardPass>(&shadowOutput_));
}

bool MiniForwardPipeline::render(RenderContext& context) {
    DrawList drawList = preparePipelineDrawList(context, staticBatcher_);
    RenderFrameStats& frameStats = context.frameStats();

    RenderGraph graph;
    shadowOutput_ = {};
    for (const std::unique_ptr<IRenderPass>& pass : passes_) {
        pass->execute(context, graph, drawList);
    }
    if (!graph.compile(context.rgTexturePool())) {
        Log::error("MiniForwardPipeline", "RenderGraph setup failed: %s", graph.lastError().c_str());
        return false;
    }
    frameStats.renderGraphPasses = graph.passCount();
    frameStats.renderGraphPlanCacheHit = graph.planCacheHit();
    frameStats.transientRenderTargets = context.rgTexturePool().inUseCount();
    if (shadowOutput_.castShadows && shadowOutput_.shadowMap.valid()) {
        FRAME_GPU_MANAGER.bindShadowMap(context.frameIndex(),
                                        graph.resolvedTextureView(shadowOutput_.shadowMap));
    }
    graph.execute(context.encoder());
    graph.reset();
    return true;
}

void MiniForwardPipeline::addPass(std::unique_ptr<IRenderPass> pass) {
    passes_.push_back(std::move(pass));
}

void MiniForwardPipeline::setPasses(std::vector<std::unique_ptr<IRenderPass>> passes) {
    passes_ = std::move(passes);
}

void MiniForwardPipeline::onSwapchainChanged() {
    // Renderer waits for the device before this callback, so cached combined buffers are safe
    // to release here.
    staticBatcher_.clear();
}

} // namespace engine
