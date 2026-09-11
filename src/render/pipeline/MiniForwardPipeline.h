#pragma once

#include "render/pipeline/RenderPass.h"
#include "render/pipeline/RenderPipeline.h"

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
    void onSwapchainChanged() override;

    void addPass(std::unique_ptr<IRenderPass> pass);
    void setPasses(std::vector<std::unique_ptr<IRenderPass>> passes);
    [[nodiscard]] const std::vector<std::unique_ptr<IRenderPass>>& passes() const { return passes_; }

private:
    void resolveMaterialBindGroups(DrawList& drawList, std::uint32_t frameIndex);

    std::vector<std::unique_ptr<IRenderPass>> passes_;
};

} // namespace engine
