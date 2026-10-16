#pragma once

#include "render/render_graph/RgTexturePool.h"
#include "render/render_graph/RgRenderPassCache.h"
#include "render/render_graph/RgTypes.h"
#include "render/texture/Texture.h"
#include "rhi/api/Command.h"

#include <functional>
#include <string>
#include <vector>

namespace engine::rhi { class ISwapchain; }

namespace engine {

class RenderContext;
class RenderGraph;

// An external image binding resolved for the current frame. Its owner tracks state.
struct RgTextureBinding {
    rhi::RID texture;
    rhi::RID view;
    rhi::ResourceState initialState{rhi::ResourceState::Undefined};
    rhi::AccessMode initialAccess{rhi::AccessMode::Auto};
    rhi::ResourceState* trackedState{};
};

// Physical bindings and command parameters for one execution of a compiled graph.
// Destruction releases pool leases; native textures/cache objects remain persistent.
// This object must be destroyed before its graph and texture pool.
class RgFrameResources final {
public:
    struct Group {
        rhi::RenderPassBeginInfo beginInfo;
        std::vector<rhi::RenderingInfo> rendering;
        std::vector<rhi::TextureBarrier> preBarriers;
    };

    ~RgFrameResources();
    void reset(); // Release this frame's leases while preserving the compiled graph.
    RgFrameResources(RgFrameResources&& other) noexcept;
    RgFrameResources& operator=(RgFrameResources&& other) noexcept;
    RgFrameResources(const RgFrameResources&) = delete;
    RgFrameResources& operator=(const RgFrameResources&) = delete;

    [[nodiscard]] rhi::RID textureView(RgTextureHandle handle) const;
    [[nodiscard]] rhi::RID texture(RgTextureHandle handle) const;
    [[nodiscard]] const std::vector<Group>& groups() const { return groups_; }
    [[nodiscard]] const std::vector<rhi::TextureBarrier>& finalBarriers() const { return finalBarriers_; }

private:
    friend class RenderGraph;
    RgFrameResources() = default;
    void release();
    struct Texture {
        RgTextureBinding binding;
        rhi::TextureViewInfo info;
        RgTexturePool::Entry* poolEntry{};
    };
    RenderGraph* owner_{};
    bool executed_{};
    std::vector<Texture> textures_;
    std::vector<Group> groups_;
    std::vector<rhi::TextureBarrier> finalBarriers_;
    std::vector<rhi::ResourceState> finalStates_;
    std::vector<rhi::AccessMode> finalAccesses_;
};

// Persistent logical graph. Compile when structure/format changes, then prepare
// physical bindings and record a fresh command buffer every frame. No pool/cache
// or RenderContext pointer is retained by the graph.
class RenderGraph final {
public:
    using TextureResolver = std::function<RgTextureBinding(RenderContext&)>;
    // Graph-specific runtime sizing; TextureDesc remains independent of RenderContext.
    using TextureExtentResolver = std::function<rhi::Rect(const RenderContext&)>;
    RenderGraph() = default;
    ~RenderGraph();
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&&) = delete;
    RenderGraph& operator=(RenderGraph&&) = delete;

    // Registers a stable source; acquisition is resolved again for every frame.
    [[nodiscard]] RgTextureHandle importSwapchainTexture(const rhi::ISwapchain& swapchain);
    // Named sources share one logical attachment; compile derives aspect from the view format.
    [[nodiscard]] RgTextureHandle importTexture(
        std::string name, TextureResolver resolver,
        rhi::ResourceState finalState = rhi::ResourceState::Undefined);
    // Usage is inferred from passes and finalState; CopySource exports a texture for readback.
    [[nodiscard]] RgTextureHandle createTexture(TextureDesc desc, TextureExtentResolver extentResolver = {},
        rhi::ResourceState finalState = rhi::ResourceState::Undefined);
    void addGraphicsPass(RgRasterPass pass);
    void addRasterGroup(std::string name, std::vector<RgRasterPass> subpasses);

    struct BarrierPlan {
        RgTextureHandle texture;
        rhi::ResourceState before;
        rhi::ResourceState after;
        rhi::AccessMode sourceAccess;
        rhi::AccessMode destinationAccess;
        bool frameInitial{};
        bool needsContents{};
    };
    struct CompiledSubpass {
        std::vector<RgResourceAccess> resources;
    };
    struct ClearSource { std::uint32_t subpass{}, slot{}; bool depth{}, input{}; };
    struct CompiledGroup {
        std::string name;
        bool traditional{};
        rhi::RenderPassDesc renderPassDesc;
        std::vector<RgTextureHandle> attachmentTextures;
        std::vector<ClearSource> clearSources;
        std::vector<CompiledSubpass> subpasses;
        std::vector<BarrierPlan> preBarriers;
    };

    void compile(RenderContext& context); // Idempotent until invalidate/reset.
    [[nodiscard]] bool resourcesCompatible(RenderContext& context) const;
    [[nodiscard]] RgFrameResources prepareFrame(RenderContext& context);
    void execute(RenderContext& context, RgFrameResources& frame, rhi::RID commandBuffer = {});
    void invalidate(); // Preserve declarations; rebuild plan on next compile.
    void reset();      // Discard declarations and plan, never per-frame bindings.

    [[nodiscard]] bool compiled() const { return compiled_; }
    [[nodiscard]] std::uint64_t compileCount() const { return compileCount_; }
    [[nodiscard]] std::size_t passCount() const;
    [[nodiscard]] std::size_t textureCount() const { return textures_.size(); }
    [[nodiscard]] const std::vector<CompiledGroup>& compiledGroups() const { return compiledGroups_; }

private:
    friend class RgFrameResources;
    struct TextureNode {
        rhi::ResourceState finalState{rhi::ResourceState::Undefined};
        rhi::TextureAspect aspect{rhi::TextureAspect::Color};
        TextureDesc desc;
        TextureExtentResolver extentResolver;
        bool isTransient{};
        std::string sourceName;
        TextureResolver resolver;
        rhi::TextureViewInfo metadata;
    };
    struct RecordedGroup { std::string name; bool traditional{}; std::vector<RgRasterPass> subpasses; };
    [[nodiscard]] const TextureNode& resolveNode(RgTextureHandle handle) const;
    [[nodiscard]] RgTextureBinding resolveBinding(const TextureNode& node, RenderContext& context) const;
    void ensureRecording() const;
    void ensureNoFrames() const;
    [[nodiscard]] std::vector<RgResourceAccess> normalizeAccesses(const RgRasterPass& pass) const;
    void compileRasterGroup(std::size_t groupIndex);
    void materializeFrameBarriers(RgFrameResources& frame, bool executing) const;
    void validateView(const TextureNode& node, const rhi::TextureViewInfo& info,
                      rhi::ResourceState state, const rhi::Rect* area = nullptr,
                      bool sampledAccess = false) const;
    std::vector<TextureNode> textures_;
    std::vector<RecordedGroup> groups_;
    std::vector<CompiledGroup> compiledGroups_;
    std::vector<BarrierPlan> finalBarriers_;
    std::vector<rhi::ResourceState> finalStates_;
    std::vector<rhi::AccessMode> finalAccesses_;
    std::vector<bool> finalFromFrame_;
    std::uint64_t compileCount_{};
    std::size_t activeFrames_{};
    bool compiled_{};
};

} // namespace engine
