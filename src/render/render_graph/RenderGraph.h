#pragma once

#include "render/render_graph/RgTexturePool.h"
#include "render/render_graph/RgTypes.h"
#include "rhi/api/CommandEncoder.h"

#include <functional>
#include <string>
#include <vector>

namespace engine {

// RenderGraph v2: declarative render-graph with transient textures, compilation and pooling.
//
// Recording phase:
//   - importTexture() brings externally-owned textures into the graph.
//   - createTexture() declares transient textures that will be allocated from an RgTexturePool.
//   - addGraphicsPass() declares a pass and its resource usages.
//
// Compile phase:
//   - compile(pool) resolves every RgTextureHandle to actual RHI texture/view handles by
//     acquiring transient textures from the pool.
//
// Execution phase:
//   - execute(encoder) emits barriers, calls beginRendering and runs each pass callback.
//
// After execution the caller should call reset() (or let the destructor do it) so transient
// textures are released back to the pool.
class RenderGraph final {
public:
    using ExecuteCallback = std::function<void(rhi::IGraphicsCommandEncoder&)>;

    struct ImportedTexture {
        rhi::TextureHandle texture;
        rhi::TextureViewHandle view;
        rhi::ResourceState initialState{rhi::ResourceState::Undefined};
        rhi::ResourceState finalState{rhi::ResourceState::Undefined};
        rhi::TextureAspect aspect{rhi::TextureAspect::Color};
        // When present, execute() writes finalState back after recording the final barrier.
        rhi::ResourceState* trackedState{};
    };

    RenderGraph() = default;
    ~RenderGraph();

    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&&) = delete;
    RenderGraph& operator=(RenderGraph&&) = delete;

    [[nodiscard]] RgTextureHandle importTexture(ImportedTexture texture);
    [[nodiscard]] RgTextureHandle createTexture(RgTextureDesc desc);

    void addGraphicsPass(std::string name,
                         RgRenderingInfo rendering,
                         std::vector<RgResourceUsage> resources,
                         ExecuteCallback execute);

    void compile(RgTexturePool& pool);
    void execute(rhi::IGraphicsCommandEncoder& encoder) const;
    void reset();

    // Resolves a texture node to its RHI view; valid after compile() and before reset().
    // Pipelines use this to bind graph-allocated textures (e.g. the shadow map) into bind
    // groups between compile and execute.
    [[nodiscard]] rhi::TextureViewHandle resolvedTextureView(RgTextureHandle handle) const;

    [[nodiscard]] bool compiled() const { return compiled_; }
    [[nodiscard]] std::size_t passCount() const { return passes_.size(); }
    [[nodiscard]] std::size_t textureCount() const { return textures_.size(); }

private:
    struct TextureNode {
        // Imported source.
        rhi::TextureHandle texture;
        rhi::TextureViewHandle view;
        rhi::ResourceState initialState{rhi::ResourceState::Undefined};
        rhi::ResourceState finalState{rhi::ResourceState::Undefined};
        rhi::TextureAspect aspect{rhi::TextureAspect::Color};
        rhi::ResourceState* trackedState{};

        // Transient source.
        RgTextureDesc desc;
        bool isTransient{false};

        // Resolved during compile().
        rhi::TextureHandle resolvedTexture;
        rhi::TextureViewHandle resolvedView;
        RgTexturePool::PooledTexture* poolEntry{};
    };

    struct GraphicsPass {
        std::string name;
        RgRenderingInfo rendering;
        std::vector<RgResourceUsage> resources;
        ExecuteCallback execute;
    };

    struct CompiledPass {
        std::string name;
        rhi::RenderingInfo rendering;
        std::vector<RgResourceUsage> resources;
        ExecuteCallback execute;
    };

    [[nodiscard]] const TextureNode& resolveNode(RgTextureHandle handle) const;
    void releaseTransientTextures();

    std::vector<TextureNode> textures_;
    std::vector<GraphicsPass> passes_;
    std::vector<CompiledPass> compiledPasses_;
    RgTexturePool* pool_{};
    bool compiled_{false};
};

} // namespace engine
