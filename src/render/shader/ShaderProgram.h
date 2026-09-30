#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "render/shader/ShaderCompilePipeline.h" // CompiledShader, ShaderProgramId
#include "render/uniform/PipelineLayout.h"
#include "render/uniform/Ubo.h"                  // UboLayout
#include "render/uniform/UniformSetLayout.h"
#include "rhi/api/ResourceDesc.h" // rhi::RID

#include <cstdint>
#include <string>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

// A GPU-ready shader program: the vs/fs shader-module RIDs, the per-set reflected UniformSetLayouts
// (with their UBO block layouts) and the shared PipelineLayout that bundles those set layouts.
//
// It deliberately owns neither a graphics pipeline nor the descriptor sets: pipeline creation needs
// draw-context state (vertex layout, render-target formats) unknown at compile time, so the
// draw-item stage (GraphicsPipelineStorage) builds the VkPipeline from vertexShader()/
// fragmentShader()/pipelineLayout(); and the actual UniformSets are built by their owners
// (MaterialStorage for the per-material set, FrameGpuManager/GlobalUniformGpuManager for the shared
// scene/global sets) from setLayout()/uboLayouts().
class ShaderProgram final : public RefCounted {
public:
    // Builds the program from the two compiled stages: creates the vs/fs shader modules, merges
    // their reflected descriptors per set into deduped UniformSetLayouts + UboLayouts, and acquires
    // the shared PipelineLayout.
    [[nodiscard]] static Ref<ShaderProgram> create(rhi::IDevice& device,
                                                   const CompiledShader& vertex,
                                                   const CompiledShader& fragment,
                                                   ShaderProgramId id);
    ~ShaderProgram() override;
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;

    [[nodiscard]] ShaderProgramId id() const { return id_; }
    [[nodiscard]] rhi::RID vertexShader() const { return vertex_; }
    [[nodiscard]] rhi::RID fragmentShader() const { return fragment_; }
    [[nodiscard]] const std::string& vertexEntry() const { return vertexEntry_; }
    [[nodiscard]] const std::string& fragmentEntry() const { return fragmentEntry_; }
    [[nodiscard]] const Ref<PipelineLayout>& pipelineLayout() const { return pipelineLayout_; }
    [[nodiscard]] std::uint32_t setCount() const {
        return pipelineLayout_ ? static_cast<std::uint32_t>(pipelineLayout_->setLayouts().size())
                               : 0U;
    }
    // The deduped UniformSetLayout for a set. Consumers build their own UniformSet from it, so the
    // scene/global sets can be shared across programs (identical reflected layout -> one cached
    // instance) while the material set stays per-material.
    [[nodiscard]] const Ref<UniformSetLayout>& setLayout(std::uint32_t set) const {
        return pipelineLayout_->setLayouts()[set];
    }
    // UboLayouts for a set's uniform-buffer bindings, in ascending binding order (empty when the set
    // has no UBO). Consumers pass these to UniformSet to instantiate the block buffers.
    [[nodiscard]] const std::vector<Ref<UboLayout>>& uboLayouts(std::uint32_t set) const {
        return setUboLayouts_[set];
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
    std::vector<std::vector<Ref<UboLayout>>> setUboLayouts_; // per set: UBO block layouts, binding order
};

} // namespace engine
