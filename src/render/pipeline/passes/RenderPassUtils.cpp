#include "render/pipeline/passes/RenderPassUtils.h"

namespace engine {

void drawFilteredItems(std::span<const DrawItem> items,
                       rhi::BindGroupHandle sceneBindGroup,
                       rhi::IGraphicsCommandEncoder& encoder) {
    rhi::GraphicsPipelineHandle boundPipeline;
    rhi::BindGroupHandle boundMaterial;
    for (const DrawItem& item : items) {
        if (item.pipeline != boundPipeline) {
            encoder.bindPipeline(item.pipeline);
            encoder.bindGroup(0, sceneBindGroup);
            boundPipeline = item.pipeline;
        }
        if (item.materialBindGroup != boundMaterial) {
            encoder.bindGroup(1, item.materialBindGroup);
            boundMaterial = item.materialBindGroup;
        }
        for (const DrawItem::VertexBuffer& vertex : item.vertexBuffers) {
            encoder.bindVertexBuffer(vertex.binding, vertex.buffer);
        }
        encoder.bindIndexBuffer(item.indexBuffer, 0, item.indexFormat);
        encoder.drawIndexed(item.arguments);
    }
}

} // namespace engine
