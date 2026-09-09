#pragma once

#include "render/gpu/common/IGpuCache.h"
#include "render/gpu/shader/ShaderModuleGpuResource.h"
#include "render/shader/ShaderCompilePipeline.h"

#include <unordered_map>

namespace engine {

class ShaderModuleCache final : public IGpuCache<CompiledShaderId, ShaderModuleGpuResource> {
public:
    [[nodiscard]] const ShaderModuleGpuResource* find(const CompiledShaderId& key) const override;
    [[nodiscard]] std::optional<ShaderModuleGpuResource>
    put(CompiledShaderId key, ShaderModuleGpuResource resource) override;
    [[nodiscard]] std::optional<ShaderModuleGpuResource>
    remove(const CompiledShaderId& key) override;
    [[nodiscard]] std::vector<Entry> extractIf(const Predicate& predicate) override;
    [[nodiscard]] std::vector<Entry> extractAll() override;
    [[nodiscard]] std::size_t size() const override { return entries_.size(); }

private:
    std::unordered_map<CompiledShaderId, ShaderModuleGpuResource> entries_;
};

} // namespace engine
