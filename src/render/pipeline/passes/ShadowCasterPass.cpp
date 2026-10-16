#include "render/pipeline/passes/ShadowCasterPass.h"
#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/RenderPassUtils.h"
#include "render/render_graph/RenderGraph.h"
#include "render/scene/RenderScene.h"

#include <algorithm>
#include <utility>

namespace engine {

ShadowCasterPass::ShadowCasterPass(ShadowCasterOutput* output,
                                   std::uint32_t shadowMapSize,
                                   DrawFilter filter)
    : output_(output), shadowMapSize_(shadowMapSize), filter_(std::move(filter)) {}

void ShadowCasterPass::updateFrame(RenderContext& context, const DrawList& drawList) {
    const auto shadowCaster =
        std::ranges::find_if(context.scene().lights(), [](const RenderLight& light) {
            return light.type == LightType::Directional && light.castShadow;
        });
    const bool enabled = shadowCaster != context.scene().lights().end();
    if (!enabled) {
        items_.clear();
        return;
    }

    filterDrawItems(context, drawList, RenderPhase::ShadowCaster, filter_, items_);
}

void ShadowCasterPass::buildGraph(RenderContext& /*context*/, RenderGraph& graph) {
    TextureDesc desc{TextureType::Texture2D, TextureFormat::Depth32Float,
        shadowMapSize_, shadowMapSize_};
    desc.debugName = "ShadowMap";
    const RgTextureHandle shadowMap = graph.createTexture(std::move(desc));
    if (output_) {
        output_->shadowMap = shadowMap;
    }

    RgRasterPass pass;
    pass.name = "ShadowCaster";
    pass.renderArea = {0, 0, shadowMapSize_, shadowMapSize_};
    pass.depthAttachments.push_back(
        {shadowMap, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.0F});

    pass.execute = [this](const RgExecuteContext& execution) {
        drawRasterItems(execution, items_);
    };
    graph.addGraphicsPass(std::move(pass));
}

} // namespace engine
