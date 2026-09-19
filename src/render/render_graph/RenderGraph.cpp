#include "render/render_graph/RenderGraph.h"

#include "core/logging/Log.h"

#include <algorithm>
#include <queue>
#include <ranges>
#include <sstream>
#include <unordered_map>
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

namespace {

bool isWriteState(rhi::ResourceState state) {
    return state == rhi::ResourceState::ColorAttachment ||
           state == rhi::ResourceState::DepthAttachment;
}

std::unordered_map<std::string, std::vector<std::size_t>>& planCache() {
    static std::unordered_map<std::string, std::vector<std::size_t>> cache;
    return cache;
}

} // namespace

bool RenderGraph::compile(RgTexturePool& pool) {
    if (compiled_) {
        Log::fatal("RenderGraph", "A RenderGraph can only be compiled once");
    }
    lastError_.clear();
    executionOrder_.clear();
    planCacheHit_ = false;
    pool_ = &pool;

    const std::string signature = planSignature();
    std::vector<std::size_t> order;
    if (const auto cached = planCache().find(signature); cached != planCache().end()) {
        order = cached->second;
        planCacheHit_ = true;
    } else {
        if (!buildExecutionPlan(order)) {
            pool_ = nullptr;
            return false;
        }
        if (planCache().size() >= 256) {
            planCache().clear();
        }
        planCache().emplace(signature, order);
    }

    for (TextureNode& node : textures_) {
        if (node.isTransient) {
            node.poolEntry = pool.acquire(node.desc);
            node.resolvedTexture = node.poolEntry->texture;
            node.resolvedView = node.poolEntry->view;
        }
    }

    compiledPasses_.reserve(passes_.size());
    executionOrder_.reserve(order.size());
    for (const std::size_t passIndex : order) {
        const GraphicsPass& pass = passes_[passIndex];
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
        executionOrder_.push_back(pass.name);
    }

    compiled_ = true;
    return true;
}

bool RenderGraph::buildExecutionPlan(std::vector<std::size_t>& order) {
    const std::size_t passCount = passes_.size();
    std::vector<std::vector<std::size_t>> edges(passCount);
    std::vector<std::size_t> indegree(passCount);
    const auto addEdge = [&](std::size_t from, std::size_t to) {
        if (from == to || std::ranges::find(edges[from], to) != edges[from].end()) {
            return;
        }
        edges[from].push_back(to);
        ++indegree[to];
    };

    for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex) {
        const GraphicsPass& pass = passes_[passIndex];
        for (const RgResourceUsage& usage : pass.resources) {
            if (!usage.texture.valid() || usage.texture.index >= textures_.size()) {
                return failCompile("Pass '" + pass.name + "' references an invalid texture");
            }
        }
        for (const RgColorAttachment& attachment : pass.rendering.colorAttachments) {
            const bool declared = std::ranges::any_of(pass.resources, [&](const auto& usage) {
                return usage.texture == attachment.texture &&
                       usage.state == rhi::ResourceState::ColorAttachment;
            });
            if (!declared) {
                return failCompile("Pass '" + pass.name +
                                   "' has an undeclared color attachment write");
            }
        }
        for (const RgDepthAttachment& attachment : pass.rendering.depthAttachments) {
            const bool declared = std::ranges::any_of(pass.resources, [&](const auto& usage) {
                return usage.texture == attachment.texture &&
                       usage.state == rhi::ResourceState::DepthAttachment;
            });
            if (!declared) {
                return failCompile("Pass '" + pass.name +
                                   "' has an undeclared depth attachment write");
            }
        }
    }

    for (std::size_t textureIndex = 0; textureIndex < textures_.size(); ++textureIndex) {
        std::vector<std::size_t> writers;
        std::vector<std::size_t> readers;
        for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex) {
            for (const RgResourceUsage& usage : passes_[passIndex].resources) {
                if (usage.texture.index != textureIndex) {
                    continue;
                }
                (isWriteState(usage.state) ? writers : readers).push_back(passIndex);
            }
        }
        std::ranges::sort(writers);
        writers.erase(std::unique(writers.begin(), writers.end()), writers.end());
        std::ranges::sort(readers);
        readers.erase(std::unique(readers.begin(), readers.end()), readers.end());

        if (textures_[textureIndex].isTransient && writers.empty() && !readers.empty()) {
            return failCompile("Transient texture " + std::to_string(textureIndex) +
                               " is read without a producer");
        }
        for (std::size_t i = 1; i < writers.size(); ++i) {
            addEdge(writers[i - 1], writers[i]);
        }
        for (const std::size_t reader : readers) {
            const auto nextWriter = std::ranges::upper_bound(writers, reader);
            if (nextWriter != writers.begin()) {
                const std::size_t producer = *std::prev(nextWriter);
                addEdge(producer, reader);
                if (nextWriter != writers.end()) {
                    addEdge(reader, *nextWriter);
                }
            } else if (writers.size() == 1) {
                addEdge(writers.front(), reader);
            } else if (writers.size() > 1) {
                return failCompile("Texture " + std::to_string(textureIndex) +
                                   " has an ambiguous producer");
            }
        }
    }

    std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
    for (std::size_t i = 0; i < passCount; ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }
    while (!ready.empty()) {
        const std::size_t current = ready.top();
        ready.pop();
        order.push_back(current);
        for (const std::size_t dependent : edges[current]) {
            if (--indegree[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }
    if (order.size() != passCount) {
        return failCompile("Render pass dependencies contain a cycle");
    }
    return true;
}

std::string RenderGraph::planSignature() const {
    std::ostringstream signature;
    signature << textures_.size() << ':' << passes_.size();
    for (const TextureNode& texture : textures_) {
        signature << '|'
                  << texture.isTransient << ',' << static_cast<int>(texture.aspect) << ','
                  << static_cast<int>(texture.desc.format);
    }
    for (const GraphicsPass& pass : passes_) {
        signature << '#' << pass.name;
        for (const RgResourceUsage& usage : pass.resources) {
            signature << ';' << usage.texture.index << ',' << static_cast<int>(usage.aspect) << ','
                      << static_cast<int>(usage.state);
        }
        signature << 'c';
        for (const RgColorAttachment& attachment : pass.rendering.colorAttachments) {
            signature << attachment.texture.index << ',';
        }
        signature << 'd';
        for (const RgDepthAttachment& attachment : pass.rendering.depthAttachments) {
            signature << attachment.texture.index << ',';
        }
    }
    return signature.str();
}

bool RenderGraph::failCompile(std::string message) {
    lastError_ = std::move(message);
    Log::error("RenderGraph", "%s", lastError_.c_str());
    return false;
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
        bool transient{};
    };
    std::vector<State> states;
    states.reserve(textures_.size());
    for (const TextureNode& node : textures_) {
        states.push_back({RgTextureHandle{static_cast<std::uint32_t>(&node - textures_.data())},
                          node.resolvedTexture,
                          node.aspect,
                          node.initialState,
                          node.finalState,
                          node.trackedState,
                          node.isTransient});
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
        // Transient textures return to the pool in their last-used state; a barrier to the
        // default finalState (Undefined) is invalid in Vulkan, and the next frame's first
        // declared usage performs the transition anyway.
        if (state.transient) {
            continue;
        }
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
    planCacheHit_ = false;
    lastError_.clear();
    executionOrder_.clear();
}

rhi::TextureViewHandle RenderGraph::resolvedTextureView(RgTextureHandle handle) const {
    return resolveNode(handle).resolvedView;
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
