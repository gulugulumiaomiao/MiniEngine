#include "render/pipeline/MiniForwardPipeline.h"

#include "render/gpu/frame/FrameGpuManager.h"
#include "render/gpu/global_uniform/GlobalUniformGpuManager.h"
#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/DepthOnlyPass.h"
#include "render/pipeline/passes/ForwardPass.h"
#include "render/pipeline/passes/ShadowCasterPass.h"
#include "render/renderer/DrawListBuilder.h"
#include "render/scene/RenderScene.h"

#include <utility>

namespace engine {

MiniForwardPipeline::MiniForwardPipeline() {
    passes_.push_back(std::make_unique<ShadowCasterPass>(&shadowOutput_));
    passes_.push_back(std::make_unique<DepthOnlyPass>());
    passes_.push_back(std::make_unique<ForwardPass>(
        &shadowOutput_, DrawFilter{RenderQueueRange::all()}, rhi::LoadOp::Load));
}

void MiniForwardPipeline::render(RenderContext& context) {
    // The renderer has waited for this frame slot. Release its retained bindings before
    // building the new draw list, whose material sets are retained for the next submission.
    FRAME_GPU_MANAGER.beginFrame(context.frameIndex());
    DrawListBuilder builder;
    DrawList drawList = builder.build(context.scene(), context);

    std::erase_if(drawList.items, [](const DrawItem& item) { return !item.pipeline; });

    GLOBAL_UNIFORM_GPU_MANAGER.beginFrame(context.frameIndex());
    FRAME_GPU_MANAGER.upload(context.frameIndex(), drawList);

    if (graphDirty_ || !graph_.compiled() || graphOffscreen_ != context.offscreenScene()) {
        graph_.reset();
        shadowOutput_ = {};
        for (const auto& pass : passes_) pass->buildGraph(context, graph_);
        graph_.compile(context);
        graphDirty_ = false;
        graphOffscreen_ = context.offscreenScene();
    } else if (!graph_.resourcesCompatible(context)) {
        graph_.invalidate();
        graph_.compile(context);
    }
    for (const auto& pass : passes_) pass->updateFrame(context, drawList);
    auto frame = graph_.prepareFrame(context);
    if (shadowOutput_.castShadows && shadowOutput_.shadowMap.valid()) {
        FRAME_GPU_MANAGER.bindShadowMap(context.frameIndex(),
                                        frame.textureView(shadowOutput_.shadowMap));
    }
    graph_.execute(context, frame);
}

void MiniForwardPipeline::addPass(std::unique_ptr<IRenderPass> pass) {
    passes_.push_back(std::move(pass));
    graphDirty_ = true;
}

void MiniForwardPipeline::setPasses(std::vector<std::unique_ptr<IRenderPass>> passes) {
    graph_.reset();
    passes_ = std::move(passes);
    graphDirty_ = true;
}

} // namespace engine
