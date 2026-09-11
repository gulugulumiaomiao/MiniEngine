#pragma once

#include "render/pipeline/RenderPass.h"
#include "render/queue/RenderQueue.h"
#include "render/render_graph/RgTypes.h"

#include <cstdint>

namespace engine {

// Result of the shadow caster pass, filled in during execute(). The pipeline owns the
// output structure, passes a pointer to ShadowCasterPass (writer) and ForwardPass (reader),
// and uses the resolved shadow map handle after RenderGraph::compile.
struct ShadowCasterOutput {
    RgTextureHandle shadowMap;
    bool castShadows{false};
};

// Shadow caster pass. Renders ShadowCaster phase items into a transient depth-only shadow
// map declared through the RenderGraph. The map is created whenever the scene has a
// shadow-casting directional light; materials without a ShadowCaster pass simply contribute
// no casters while the map still resolves to a fully-cleared depth.
class ShadowCasterPass final : public IRenderPass {
public:
    explicit ShadowCasterPass(ShadowCasterOutput* output = nullptr,
                              std::uint32_t shadowMapSize = 1024,
                              DrawFilter filter = DrawFilter{RenderQueueRange::opaque()});

    void execute(RenderContext& context,
                 RenderGraph& graph,
                 const DrawList& drawList) override;

private:
    ShadowCasterOutput* output_;
    std::uint32_t shadowMapSize_;
    DrawFilter filter_;
};

} // namespace engine