#pragma once

#include "render/pipeline/RenderPipeline.h"
#include "render/pipeline/passes/ShadowCasterPass.h"
#include "render/renderer/RenderItems.h"
#include "render/renderer/StaticBatcher.h"
#include "rhi/api/RhiTypes.h"

#include <array>
#include <cstdint>

namespace engine {

struct DrawList;

// Validation deferred pipeline. Geometry is rendered into a transient color GBuffer and
// persistent camera depth, then a fullscreen resolve samples the GBuffer into the camera
// target or swapchain. It exercises graph dependencies, transient RTs and sampled resolves.
class MiniDeferredPipeline final : public IRenderPipeline {
public:
    MiniDeferredPipeline() = default;
    ~MiniDeferredPipeline() override;

    [[nodiscard]] bool render(RenderContext& context) override;
    void onSwapchainChanged() override;

    [[nodiscard]] const SourceDrawGroups& sourceDrawGroups() const { return sourceDrawGroups_; }

private:
    struct ResolveBinding {
        rhi::TextureViewHandle view;
        rhi::BindGroupHandle group;
    };

    void resolveMaterialBindGroups(DrawList& drawList, std::uint32_t frameIndex);
    [[nodiscard]] bool ensureResolveResources(rhi::IDevice& device,
                                              rhi::TextureFormat outputFormat);
    [[nodiscard]] bool updateResolveBinding(std::uint32_t frameIndex,
                                            rhi::TextureViewHandle view);
    void releaseFrameBindings();
    void releaseResources();

    rhi::IDevice* device_{};
    rhi::TextureFormat outputFormat_{rhi::TextureFormat::Undefined};
    rhi::BindGroupLayoutHandle resolveLayout_;
    rhi::SamplerHandle resolveSampler_;
    rhi::ShaderHandle resolveVertexShader_;
    rhi::ShaderHandle resolveFragmentShader_;
    rhi::GraphicsPipelineHandle resolvePipeline_;
    std::array<ResolveBinding, 2> resolveBindings_{};
    SourceDrawGroups sourceDrawGroups_;
    StaticBatcher staticBatcher_;
    ShadowCasterOutput shadowOutput_;
};

} // namespace engine
