#pragma once

#include "render/gpu/common/IGpuResourceFactory.h"
#include "render/gpu/pipeline/GraphicsPipelineStorageEntry.h"
#include "rhi/api/ResourceDesc.h"

namespace engine {

class GraphicsPipelineStorageFactory final
    : public IGpuResourceFactory<rhi::GraphicsPipelineDesc, GraphicsPipelineStorageEntry> {
public:
    using IGpuResourceFactory::IGpuResourceFactory;

    [[nodiscard]] bool create(const rhi::GraphicsPipelineDesc& description,
                              GraphicsPipelineStorageEntry& destination) override;
    void release(GraphicsPipelineStorageEntry& resource) override;
};

} // namespace engine
