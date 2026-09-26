#include "render/gpu/pipeline/GraphicsPipelineStorageFactory.h"

#include "rhi/api/Device.h"

namespace engine {

bool GraphicsPipelineStorageFactory::create(const rhi::GraphicsPipelineDesc& description,
                                            GraphicsPipelineStorageEntry& destination) {
    GraphicsPipelineStorageEntry created;
    created.pipeline = device_.pipeline_create(description);
    if (!created.pipeline)
        return false;
    release(destination);
    destination = created;
    return true;
}

void GraphicsPipelineStorageFactory::release(GraphicsPipelineStorageEntry& resource) {
    if (resource.pipeline)
        device_.pipeline_destroy(resource.pipeline);
    resource = {};
}

} // namespace engine
