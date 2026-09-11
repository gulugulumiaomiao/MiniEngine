#include "render/pipeline/passes/ShadowCasterPass.h"

#include "render/pipeline/RenderContext.h"
#include "render/pipeline/passes/RenderPassUtils.h"
#include "render/render_graph/RenderGraph.h"
#include "render/scene/RenderScene.h"

#include <algorithm>
#include <vector>

namespace engine {

ShadowCasterPass::ShadowCasterPass(std::uint32_t shadowMapSize, DrawFilter filter)
    : shadowMapSize_(shadowMapSize), filter_(std::move(filter)) {}

void ShadowCasterPass::execute(RenderContext& context,
                               RenderGraph& graph,
                               rhi::BindGroupHandle sceneBindGroup,
                               const DrawList& drawList) {
    std::vector<DrawItem> items;
    items.reserve(drawList.items.size());
    for (const DrawItem& item : drawList.items) {
        if (item.renderPhase != RenderPhase::ShadowCaster) {
            continue;
        }
        const std::uint32_t objectIndex = item.arguments.firstInstance;
        const std::uint32_t layerMask = objectIndex < context.scene().objects().size()
                                            ? context.scene().objects()[objectIndex].layerMask
                                            : 0xFFFFFFFFU;
        if (filter_.accepts(item, layerMask)) {
            items.push_back(item);
        }
    }
    if (items.empty()) {
        return;
    }

    const RgTextureHandle shadowMap = graph.createTexture({
        .dimension = rhi::TextureDimension::Texture2D,
        .format = rhi::TextureFormat::Depth32Float,
        .width = shadowMapSize_,
        .height = shadowMapSize_,
        .depth = 1,
        .mipCount = 1,
        .usage = rhi::TextureUsage::DepthStencilAttachment,
        .aspect = rhi::TextureAspect::Depth,
        .debugName = "ShadowMap",
    });

    RgRenderingInfo rendering;
    rendering.renderArea = {0, 0, shadowMapSize_, shadowMapSize_};
    rendering.depthAttachments.push_back(
        {shadowMap, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.0F});

    std::vector<RgResourceUsage> resources;
    resources.reserve(1);
    resources.push_back({shadowMap, rhi::TextureAspect::Depth, rhi::ResourceState::DepthAttachment});

    graph.addGraphicsPass("ShadowCaster",
                          std::move(rendering),
                          std::move(resources),
                          [sceneBindGroup, items = std::move(items), size = shadowMapSize_](
                              rhi::IGraphicsCommandEncoder& encoder) mutable {
                              encoder.setViewport(
                                  {0.0F, 0.0F, static_cast<float>(size), static_cast<float>(size), 0.0F, 1.0F});
                              encoder.setScissor({0, 0, size, size});
                              drawFilteredItems(items, sceneBindGroup, encoder);
                          });
}

} // namespace engine
