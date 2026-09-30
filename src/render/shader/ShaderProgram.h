#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "render/shader/ShaderCompilePipeline.h" // CompiledShader, ShaderProgramId
#include "render/uniform/PipelineLayout.h"
#include "render/uniform/UniformSet.h"
#include "rhi/api/ResourceDesc.h" // rhi::RID

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

// A GPU-ready shader program: the vs/fs shader-module RIDs, one UniformSet per reflected descriptor
// set, and the shared PipelineLayout that bundles those sets' layouts.
//
// It deliberately does NOT own a graphics pipeline: pipeline creation needs draw-context state
// (vertex layout, render-target formats) that is unknown at compile time, so the draw-item stage
// (GraphicsPipelineStorage) builds the VkPipeline from vertexShader()/fragmentShader()/
// pipelineLayout() plus the mesh/RT context.
class ShaderProgram final : public RefCounted {
public:
    // Builds the program from the two compiled stages: creates the vs/fs shader modules, merges
    // their reflected descriptors per set into UniformSetLayouts (deduped) + UboLayouts, constructs
    // one UniformSet per set, and acquires the shared PipelineLayout.
    [[nodiscard]] static Ref<ShaderProgram> create(rhi::IDevice& device,
                                                   const CompiledShader& vertex,
                                                   const CompiledShader& fragment,
                                                   ShaderProgramId id);
    ~ShaderProgram() override;
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    // Uploads every owned uniform set (each set/Ubo self-manages its dirty state).
    void upload();

    [[nodiscard]] ShaderProgramId id() const { return id_; }
    [[nodiscard]] rhi::RID vertexShader() const { return vertex_; }
    [[nodiscard]] rhi::RID fragmentShader() const { return fragment_; }
    [[nodiscard]] const std::string& vertexEntry() const { return vertexEntry_; }
    [[nodiscard]] const std::string& fragmentEntry() const { return fragmentEntry_; }
    [[nodiscard]] const Ref<PipelineLayout>& pipelineLayout() const { return pipelineLayout_; }
    [[nodiscard]] const std::vector<Ref<UniformSet>>& uniformSets() const { return uniformSets_; }
    [[nodiscard]] std::uint32_t setCount() const {
        return static_cast<std::uint32_t>(uniformSets_.size());
    }

private:
    ShaderProgram() = default;

    ShaderProgramId id_{};
    rhi::IDevice* device_{};
    std::uint64_t deviceUid_{};
    rhi::RID vertex_;
    rhi::RID fragment_;
    std::string vertexEntry_;
    std::string fragmentEntry_;
    Ref<PipelineLayout> pipelineLayout_;
    std::vector<Ref<UniformSet>> uniformSets_; // set 0..N
};

} // namespace engine
