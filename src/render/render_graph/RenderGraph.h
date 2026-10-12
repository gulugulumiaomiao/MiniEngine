#pragma once

#include "render/render_graph/RgTexturePool.h"
#include "render/render_graph/RgRenderPassCache.h"
#include "render/render_graph/RgTypes.h"
#include "rhi/api/Command.h"

#include <functional>
#include <string>
#include <vector>

namespace engine {

// Declarative render graph with transient textures and explicit render-pass groups.
//
// Recording phase:
//   - importTexture() brings externally-owned textures into the graph.
//   - createTexture() declares transient textures that will be allocated from an RgTexturePool.
//   - addGraphicsPass() declares an independent dynamic-rendering pass.
//   - addRasterGroup() declares a render pass containing one subpass per logical pass.
//
// Compile phase:
//   - compile(pool, cache) resolves every RgTextureHandle to actual RHI texture/view handles by
//     acquiring transient textures from the pool, then builds grouping and synchronization.
//
// Execution phase:
//   - execute(commandBuffer) emits the compiled barriers and rendering scopes/callbacks.
//
// After execution the caller should call reset() (or let the destructor do it) so transient
// textures are released back to the pool.
// The shared RgRenderPassCache outlives frame-local graphs and their GPU submissions.
class RenderGraph final {
public:
    using ExecuteCallback = std::function<void(rhi::RID)>;

    struct ImportedTexture {
        rhi::RID texture;
        rhi::RID view;
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
                         std::vector<RgResourceAccess> resources,
                         ExecuteCallback execute);
    void addRasterGroup(std::string name, std::vector<RgRasterPass> subpasses);

    struct CompiledSubpass {
        std::string name;
        rhi::RenderingInfo rendering;
        std::vector<RgResourceAccess> resources;
        std::function<void(const RgExecuteContext&)> execute;
    };
    struct CompiledGroup {
        std::string name;
        bool traditional{};
        rhi::RenderPassDesc renderPassDesc;
        rhi::RenderPassBeginInfo beginInfo;
        std::vector<RgTextureHandle> attachmentTextures;
        std::vector<CompiledSubpass> subpasses;
        std::vector<rhi::TextureBarrier> preBarriers;
    };

    void compile(RgTexturePool& pool);
    // Grouped rendering requires a persistent cache that outlives submitted GPU work.
    void compile(RgTexturePool& pool, RgRenderPassCache& cache);
    void execute(rhi::RID commandBuffer) const;
    void reset();

    // Resolves a texture node to its RHI view; valid after compile() and before reset().
    // Pipelines use this to bind graph-allocated textures (e.g. the shadow map) into bind
    // groups between compile and execute.
    [[nodiscard]] rhi::RID resolvedTextureView(RgTextureHandle handle) const;

    [[nodiscard]] bool compiled() const { return compiled_; }
    [[nodiscard]] std::size_t passCount() const;
    [[nodiscard]] std::size_t textureCount() const { return textures_.size(); }
    [[nodiscard]] const std::vector<CompiledGroup>& compiledGroups() const { return compiledGroups_; }
    [[nodiscard]] const std::vector<rhi::TextureBarrier>& finalBarriers() const { return finalBarriers_; }

private:
    struct TextureNode {
        // Imported source.
        rhi::RID texture;
        rhi::RID view;
        rhi::ResourceState initialState{rhi::ResourceState::Undefined};
        rhi::ResourceState finalState{rhi::ResourceState::Undefined};
        rhi::TextureAspect aspect{rhi::TextureAspect::Color};
        rhi::ResourceState* trackedState{};

        // Transient source.
        RgTextureDesc desc;
        bool isTransient{false};

        // Resolved during compile().
        rhi::RID resolvedTexture;
        rhi::RID resolvedView;
        RgTexturePool::PooledTexture* poolEntry{};
    };

    struct RecordedGroup {
        std::string name;
        bool traditional{};
        std::vector<RgRasterPass> subpasses;
    };

    [[nodiscard]] const TextureNode& resolveNode(RgTextureHandle handle) const;
    void releaseTransientTextures();
    void ensureRecording() const;
    [[nodiscard]] std::vector<RgResourceAccess> normalizeAccesses(const RgRasterPass& pass) const;
    void compileRasterGroup(std::size_t groupIndex);

    std::vector<TextureNode> textures_;
    std::vector<RecordedGroup> groups_;
    std::vector<CompiledGroup> compiledGroups_;
    std::vector<rhi::TextureBarrier> finalBarriers_;
    std::vector<rhi::ResourceState> finalStates_;
    std::vector<rhi::AccessMode> finalAccesses_;
    RgTexturePool* pool_{};
    RgRenderPassCache* renderPassCache_{};
    bool compiled_{false};
};

} // namespace engine
