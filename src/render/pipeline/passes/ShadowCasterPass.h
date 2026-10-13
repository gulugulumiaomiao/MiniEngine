#pragma once

#include "render/pipeline/RenderPass.h"
#include "render/renderer/DrawList.h"
#include "render/queue/RenderQueue.h"
#include "render/render_graph/RgTypes.h"

#include <cstdint>

namespace engine {

// Stable shadow-map handle from buildGraph and per-frame enabled flag from updateFrame.
// The pipeline owns the
// output structure, passes a pointer to ShadowCasterPass (writer) and ForwardPass (reader),
// and uses the physical shadow-map view after RenderGraph::prepareFrame.
struct ShadowCasterOutput {
    RgTextureHandle shadowMap;
    bool castShadows{false};
};

// Shadow caster pass. Renders ShadowCaster phase items into a transient depth-only shadow
// map declared through the RenderGraph. The pass always clears the map, while a scene
// without a shadow-casting directional light contributes no casters.
class ShadowCasterPass final : public IRenderPass {
public:
    explicit ShadowCasterPass(ShadowCasterOutput* output = nullptr,
                              std::uint32_t shadowMapSize = 1024,
                              DrawFilter filter = DrawFilter{RenderQueueRange::opaque()});

    void buildGraph(RenderContext& context, RenderGraph& graph) override;
    void updateFrame(RenderContext& context, const DrawList& drawList) override;

private:
    ShadowCasterOutput* output_;
    std::uint32_t shadowMapSize_;
    DrawFilter filter_;
    std::vector<DrawItem> items_;
};

} // namespace engine
