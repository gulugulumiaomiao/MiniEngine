#pragma once

#include "render/gpu/common/IGpuCache.h"
#include "render/gpu/pipeline/GraphicsPipelineGpuResource.h"

#include <cstdint>
#include <unordered_map>

namespace engine {

using GraphicsPipelineCacheKey = std::uint64_t;

class GraphicsPipelineCache final
    : public IGpuCache<GraphicsPipelineCacheKey, GraphicsPipelineGpuResource> {
public:
    [[nodiscard]] const GraphicsPipelineGpuResource*
    find(const GraphicsPipelineCacheKey& key) const override;
    [[nodiscard]] std::optional<GraphicsPipelineGpuResource>
    put(GraphicsPipelineCacheKey key, GraphicsPipelineGpuResource resource) override;
    [[nodiscard]] std::optional<GraphicsPipelineGpuResource>
    remove(const GraphicsPipelineCacheKey& key) override;
    [[nodiscard]] std::vector<Entry> extractIf(const Predicate& predicate) override;
    [[nodiscard]] std::vector<Entry> extractAll() override;
    [[nodiscard]] std::size_t size() const override { return entries_.size(); }

private:
    std::unordered_map<GraphicsPipelineCacheKey, GraphicsPipelineGpuResource> entries_;
};

} // namespace engine
