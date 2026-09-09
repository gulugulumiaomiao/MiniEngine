#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/pipeline/GraphicsPipelineGpuResource.h"
#include "rhi/api/PipelineDesc.h"

namespace engine {

class GraphicsPipelineGpuFactory final
    : public IGpuResourceFactory<rhi::GraphicsPipelineDesc, GraphicsPipelineGpuResource> {
public:
    using IGpuResourceFactory::IGpuResourceFactory;

    [[nodiscard]] bool create(const rhi::GraphicsPipelineDesc& description,
                              GraphicsPipelineGpuResource& destination) override;
    void release(GraphicsPipelineGpuResource& resource) override;
};

} // namespace engine
