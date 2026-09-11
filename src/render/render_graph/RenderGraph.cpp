#include "render/render_graph/RenderGraph.h"

#include "core/logging/Log.h"

#include <algorithm>
#include <ranges>
#include <utility>

namespace engine {

RenderGraph::~RenderGraph() {
    reset();
}

RgTextureHandle RenderGraph::importTexture(ImportedTexture texture) {
    if (!texture.texture || !texture.view) {
        Log::fatal("RenderGraph", "Imported texture and view must both be valid");
    }
    const RgTextureHandle handle{static_cast<std::uint32_t>(textures_.size())};
    TextureNode node;
    node.texture = texture.texture;
    node.view = texture.view;
    node.initialState = texture.initialState;
    node.finalState = texture.finalState;
    node.aspect = texture.aspect;
    node.trackedState = texture.trackedState;
    node.resolvedTexture = texture.texture;
    node.resolvedView = texture.view;
    textures_.push_back(std::move(node));
    return handle;
}

RgTextureHandle RenderGraph::createTexture(RgTextureDesc desc) {
    if (desc.format == rhi::TextureFormat::Undefined || desc.width == 0 || desc.height == 0 ||
        desc.mipCount == 0) {
        Log::fatal("RenderGraph", "Invalid transient texture description");
    }
    const RgTextureHandle handle{static_cast<std::uint32_t>(textures_.size())};
    TextureNode node;
    node.desc = std::move(desc);
    node.isTransient = true;
    textures_.push_back(std::move(node));
    return handle;
}

void RenderGraph::addGraphicsPass(std::string name,
                                  RgRenderingInfo rendering,
                                  std::vector<RgResourceUsage> resources,
                                  ExecuteCallback execute) {
    passes_.push_back(
        {std::move(name), std::move(rendering), std::move(resources), std::move(execute)});
}

void RenderGraph::compile(RgTexturePool& pool) {
    if (compiled_) {
        Log::fatal("RenderGraph", "A RenderGraph can only be compiled once");
    }
    pool_ = &pool;

    for (TextureNode& node : textures_) {
        if (node.isTransient) {
            node.poolEntry = pool.acquire(node.desc);
            node.resolvedTexture = node.poolEntry->texture;
            node.resolvedView = node.poolEntry->view;
        }
    }

    compiledPasses_.reserve(passes_.size());
    for (const GraphicsPass& pass : passes_) {
        CompiledPass compiled;
        compiled.name = pass.name;
        compiled.resources = pass.resources;
        compiled.execute = pass.execute;

        compiled.rendering.renderArea = pass.rendering.renderArea;
        compiled.rendering.colorAttachments.reserve(pass.rendering.colorAttachments.size());
        for (const RgColorAttachment& attachment : pass.rendering.colorAttachments) {
            const TextureNode& node = resolveNode(attachment.texture);
            compiled.rendering.colorAttachments.push_back(
                {node.resolvedView, attachment.loadOp, attachment.storeOp, attachment.clearColor});
        }
        compiled.rendering.depthAttachments.reserve(pass.rendering.depthAttachments.size());
        for (const RgDepthAttachment& attachment : pass.rendering.depthAttachments) {
            const TextureNode& node = resolveNode(attachment.texture);
            compiled.rendering.depthAttachments.push_back(
                {node.resolvedView, attachment.loadOp, attachment.storeOp, attachment.clearDepth});
        }

        compiledPasses_.push_back(std::move(compiled));
    }

    compiled_ = true;
}

void RenderGraph::execute(rhi::IGraphicsCommandEncoder& encoder) const {
    if (!compiled_) {
        Log::fatal("RenderGraph", "RenderGraph must be compiled before execute");
    }

    struct State {
        RgTextureHandle handle;
        rhi::TextureHandle texture;
        rhi::TextureAspect aspect;
        rhi::ResourceState current;
        rhi::ResourceState final;
        rhi::ResourceState* tracked;
    };
    std::vector<State> states;
    states.reserve(textures_.size());
    for (const TextureNode& node : textures_) {
        states.push_back({RgTextureHandle{static_cast<std::uint32_t>(&node - textures_.data())},
                          node.resolvedTexture,
                          node.aspect,
                          node.initialState,
                          node.finalState,
                          node.trackedState});
    }

    for (const CompiledPass& pass : compiledPasses_) {
        std::vector<rhi::TextureBarrier> barriers;
        for (const RgResourceUsage& usage : pass.resources) {
            auto state = std::ranges::find_if(states, [&usage](const State& candidate) {
                return candidate.handle == usage.texture;
            });
            if (state == states.end()) {
                Log::fatal("RenderGraph", "Pass uses a texture that was not declared");
            }
            if (state->current != usage.state) {
                barriers.push_back({state->texture, usage.aspect, state->current, usage.state});
                state->current = usage.state;
            }
        }
        encoder.beginDebugLabel(pass.name, {0.25F, 0.55F, 1.0F, 1.0F});
        encoder.resourceBarriers(barriers);
        encoder.beginRendering(pass.rendering);
        pass.execute(encoder);
        encoder.endRendering();
        encoder.endDebugLabel();
    }

    std::vector<rhi::TextureBarrier> finalBarriers;
    for (const State& state : states) {
        if (state.current != state.final) {
            finalBarriers.push_back({state.texture, state.aspect, state.current, state.final});
        }
    }
    encoder.resourceBarriers(finalBarriers);
    for (State& state : states) {
        if (state.tracked) {
            *state.tracked = state.final;
        }
    }
}

void RenderGraph::reset() {
    releaseTransientTextures();
    compiledPasses_.clear();
    pool_ = nullptr;
    compiled_ = false;
}

const RenderGraph::TextureNode& RenderGraph::resolveNode(RgTextureHandle handle) const {
    if (!handle.valid() || handle.index >= textures_.size()) {
        Log::fatal("RenderGraph", "Invalid RgTextureHandle");
    }
    return textures_[handle.index];
}

void RenderGraph::releaseTransientTextures() {
    for (TextureNode& node : textures_) {
        if (node.isTransient && node.poolEntry) {
            pool_->release(node.poolEntry);
            node.poolEntry = nullptr;
        }
    }
}

} // namespace engine
