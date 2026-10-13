#pragma once

#include "render/pipeline/RenderPass.h"
#include "render/pipeline/RenderPipeline.h"
#include "render/render_graph/RenderGraph.h"
#include "render/pipeline/passes/ShadowCasterPass.h"

#include <memory>
#include <vector>

namespace engine {

// Default SRP-style forward pipeline for MiniEngine.
//
// The pipeline builds a DrawList using DrawListBuilder, resolves GPU material bind groups,
// uploads scene/object uniforms and then executes a configurable list of render passes.
// By default it runs ShadowCaster -> DepthOnly -> Forward.
class MiniForwardPipeline final : public IRenderPipeline {
public:
    MiniForwardPipeline();

    void render(RenderContext& context) override;
    void addPass(std::unique_ptr<IRenderPass> pass);
    void setPasses(std::vector<std::unique_ptr<IRenderPass>> passes);
    // Call after changing pass configuration that affects graph structure/contracts.
    void invalidateGraph() { graphDirty_ = true; }
    [[nodiscard]] const std::vector<std::unique_ptr<IRenderPass>>& passes() const { return passes_; }
    [[nodiscard]] std::uint64_t graphCompileCount() const { return graph_.compileCount(); }

private:
    // Declared by ShadowCasterPass during graph building, then updated each frame;
    // consumed by ForwardPass and by render()
    // when binding the resolved shadow map view into the scene uniform set.
    ShadowCasterOutput shadowOutput_;
    std::vector<std::unique_ptr<IRenderPass>> passes_;
    RenderGraph graph_;
    bool graphDirty_{true};
    bool graphOffscreen_{};
};

} // namespace engine
