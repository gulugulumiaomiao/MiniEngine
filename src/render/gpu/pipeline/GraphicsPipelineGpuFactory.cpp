#include "render/gpu/pipeline/GraphicsPipelineGpuFactory.h"

#include "rhi/api/Device.h"

namespace engine {

bool GraphicsPipelineGpuFactory::create(const rhi::GraphicsPipelineDesc& description,
                                        GraphicsPipelineGpuResource& destination) {
    GraphicsPipelineGpuResource created;
    created.pipeline = device_.createGraphicsPipeline(description);
    if (!created.pipeline)
        return false;
    release(destination);
    destination = created;
    return true;
}

void GraphicsPipelineGpuFactory::release(GraphicsPipelineGpuResource& resource) {
    if (resource.pipeline)
        device_.destroyGraphicsPipeline(resource.pipeline);
    resource = {};
}

} // namespace engine
