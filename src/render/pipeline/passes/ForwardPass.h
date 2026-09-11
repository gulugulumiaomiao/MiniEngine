#pragma once

#include "render/pipeline/RenderPass.h"
#include "render/pipeline/passes/ShadowCasterPass.h"
#include "render/queue/RenderQueue.h"

namespace engine {

class ForwardPass final : public IRenderPass {
public:
    // When a ShadowCasterOutput is provided and the shadow pass ran this frame, the forward
    // pass declares the shadow map as an additional ShaderRead resource.
    explicit ForwardPass(const ShadowCasterOutput* shadowOutput = nullptr,
                         DrawFilter filter = DrawFilter{RenderQueueRange::all()});

    void execute(RenderContext& context,
                 RenderGraph& graph,
                 const DrawList& drawList) override;

private:
    const ShadowCasterOutput* shadowOutput_;
    DrawFilter filter_;
};

} // namespace engine