#pragma once

#include "render/renderer/DrawList.h"

#include <array>
#include <string>
#include <string_view>

namespace engine {

class Mesh;
class RenderContext;
class RenderScene;

// Builds a DrawList from a RenderScene for the MiniForward pipeline.
//
// For every visible object and each sub-mesh, the builder tries to resolve a shader pass for the
// three built-in phases (ShadowCaster, DepthOnly, Forward). It applies the error-material fallback
// for Forward passes and leaves materialBindGroup empty so the pipeline can resolve it later.
class DrawListBuilder final {
public:
    explicit DrawListBuilder(std::string_view renderPipeline = "MiniForward");

    [[nodiscard]] DrawList build(const RenderScene& scene, const RenderContext& context);

private:
    struct ResolvedMaterialPass {
        MaterialHandle material;
        const ShaderPass* pass{};
        rhi::GraphicsPipelineHandle pipeline;

        [[nodiscard]] explicit operator bool() const { return static_cast<bool>(pipeline); }
    };

    [[nodiscard]] ResolvedMaterialPass resolveMaterialPass(const RenderContext& context,
                                                           MaterialHandle materialHandle,
                                                           const Mesh& meshInstance,
                                                           RenderPhase phase) const;

    std::string renderPipeline_;
};

} // namespace engine
