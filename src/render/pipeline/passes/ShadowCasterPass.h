#pragma once

#include "render/pipeline/RenderPass.h"
#include "render/queue/RenderQueue.h"

#include <cstdint>

namespace engine {

// Shadow caster pass. Draws ShadowCaster phase items into a transient depth shadow map.
// This is a minimal SRP-style shadow pass: it allocates a temporary depth texture from the
// RenderGraph and renders opaque shadow-casting objects into it.
class ShadowCasterPass final : public IRenderPass {
public:
    explicit ShadowCasterPass(std::uint32_t shadowMapSize = 2048,
                              DrawFilter filter = DrawFilter{RenderQueueRange::opaque()});

    void execute(RenderContext& context,
                 RenderGraph& graph,
                 rhi::BindGroupHandle sceneBindGroup,
                 const DrawList& drawList) override;

private:
    std::uint32_t shadowMapSize_;
    DrawFilter filter_;
};

} // namespace engine
