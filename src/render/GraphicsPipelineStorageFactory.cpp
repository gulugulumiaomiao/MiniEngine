#include "render/GraphicsPipelineStorageFactory.h"

#include "rhi/api/Device.h"

namespace engine {

bool GraphicsPipelineStorageFactory::create(const rhi::GraphicsPipelineDesc& description,
                                            GraphicsPipelineStorageEntry& destination) {
    rhi::IDevice* device = RHI_DEVICE;
    if (!device)
        return false;
    GraphicsPipelineStorageEntry created;
    created.pipeline = device->pipeline_create(description);
    if (!created.pipeline)
        return false;
    release(destination);
    destination = created;
    return true;
}

void GraphicsPipelineStorageFactory::release(GraphicsPipelineStorageEntry& resource) {
    if (rhi::IDevice* device = RHI_DEVICE; resource.pipeline && device)
        device->pipeline_destroy(resource.pipeline);
    resource = {};
}

} // namespace engine
