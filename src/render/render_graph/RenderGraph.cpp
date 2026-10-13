#include "render/render_graph/RenderGraph.h"

#include "core/logging/Log.h"
#include "rhi/api/Device.h"
#include <algorithm>
#include <ranges>
#include <utility>

namespace engine {
namespace {
using rhi::AccessMode;
using rhi::ResourceState;

bool writes(AccessMode mode) { return mode == AccessMode::Write || mode == AccessMode::ReadWrite; }
AccessMode accessFor(ResourceState state) {
    switch (state) {
    case ResourceState::ColorAttachment:
    case ResourceState::DepthAttachment: return AccessMode::ReadWrite;
    case ResourceState::CopyDestination: return AccessMode::Write;
    default: return AccessMode::Read;
    }
}
bool attachmentState(ResourceState state) {
    return state == ResourceState::ColorAttachment || state == ResourceState::DepthAttachment ||
           state == ResourceState::InputAttachment;
}
bool sameArea(const rhi::Rect& a, const rhi::Rect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
bool needsContents(const RgRasterPass& pass, const RgResourceAccess& usage) {
    for (const auto& a : pass.rendering.colorAttachments)
        if (a.texture == usage.texture) return a.loadOp == rhi::LoadOp::Load;
    for (const auto& a : pass.rendering.depthAttachments)
        if (a.texture == usage.texture) return a.loadOp == rhi::LoadOp::Load;
    return usage.access != AccessMode::Write;
}
rhi::TextureBarrier barrier(rhi::RID texture, rhi::TextureAspect aspect,
                            ResourceState before, ResourceState after,
                            AccessMode source, AccessMode destination) {
    rhi::TextureBarrier result{texture, aspect, before, after};
    result.sourceAccess = source;
    result.destinationAccess = destination;
    return result;
}
} // namespace

RenderGraph::~RenderGraph() { reset(); }

void RenderGraph::ensureRecording() const {
    if (compiled_) Log::fatal("RenderGraph", "Cannot modify a compiled graph; call reset first");
}

RgTextureHandle RenderGraph::importSwapchainTexture(const rhi::ISwapchain& swapchain) {
    return importTexture(swapchain.currentTexture(), swapchain.currentTextureView(),
                         swapchain.currentTextureState(), ResourceState::Present);
}

RgTextureHandle RenderGraph::importTexture(rhi::RID texture, rhi::RID view,
                                         ResourceState initialState, ResourceState finalState,
                                         rhi::TextureAspect aspect, ResourceState* trackedState) {
    ensureRecording();
    if (!texture || !view)
        Log::fatal("RenderGraph", "Imported texture and view must both be valid");
    // One state record per physical image. Different views need subresource tracking.
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        auto& node = textures_[i];
        if (node.isTransient || node.texture != texture) continue;
        if (node.view != view || node.initialState != initialState ||
            node.finalState != finalState || node.aspect != aspect ||
            (node.trackedState && trackedState && node.trackedState != trackedState))
            Log::fatal("RenderGraph", "Conflicting imports of the same texture");
        if (trackedState) node.trackedState = trackedState;
        return {static_cast<std::uint32_t>(i)};
    }
    const RgTextureHandle handle{static_cast<std::uint32_t>(textures_.size())};
    TextureNode node;
    node.texture = node.resolvedTexture = texture;
    node.view = node.resolvedView = view;
    node.initialState = initialState;
    node.finalState = finalState;
    node.aspect = aspect;
    node.trackedState = trackedState;
    textures_.push_back(std::move(node));
    return handle;
}

RgTextureHandle RenderGraph::createTexture(RgTextureDesc desc) {
    ensureRecording();
    if (desc.dimension != rhi::TextureType::Texture2D || desc.format == rhi::PixelFormat::Undefined ||
        desc.width == 0 || desc.height == 0 ||
        desc.mipCount != 1 || desc.arrayLayers != 1 || desc.depth != 1)
        Log::fatal("RenderGraph", "Graph transient textures currently require one mip and layer");
    const RgTextureHandle handle{static_cast<std::uint32_t>(textures_.size())};
    TextureNode node;
    node.aspect = rhi::isDepthFormat(desc.format) ? rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
    node.desc = std::move(desc);
    node.isTransient = true;
    textures_.push_back(std::move(node));
    return handle;
}

void RenderGraph::addGraphicsPass(std::string name, RgRenderingInfo rendering,
                                  std::vector<RgResourceAccess> resources, ExecuteCallback execute) {
    ensureRecording();
    if (!execute) Log::fatal("RenderGraph", "Pass callback is empty");
    RgRasterPass pass{name, std::move(rendering), std::move(resources),
        [callback = std::move(execute)](const RgExecuteContext& c) { callback(c.commandBuffer); }};
    groups_.push_back({std::move(name), false, {std::move(pass)}});
}

void RenderGraph::addRasterGroup(std::string name, std::vector<RgRasterPass> subpasses) {
    ensureRecording();
    if (subpasses.empty()) Log::fatal("RenderGraph", "Raster group must contain at least one subpass");
    groups_.push_back({std::move(name), true, std::move(subpasses)});
}

std::size_t RenderGraph::passCount() const {
    std::size_t count{};
    for (const auto& group : groups_) count += group.subpasses.size();
    return count;
}

std::vector<RgResourceAccess> RenderGraph::normalizeAccesses(const RgRasterPass& pass) const {
    if (!pass.execute) Log::fatal("RenderGraph", "Pass callback is empty");
    const auto& area = pass.rendering.renderArea;
    if (area.x < 0 || area.y < 0 || area.width == 0 || area.height == 0 ||
        pass.rendering.depthAttachments.size() > 1)
        Log::fatal("RenderGraph", "Invalid render area or multiple depth attachments");
    std::vector<RgResourceAccess> result;
    const auto add = [&](RgResourceAccess usage) {
        const auto& node = resolveNode(usage.texture);
        if (node.aspect != usage.aspect || usage.state == ResourceState::Undefined ||
            usage.state == ResourceState::Present)
            Log::fatal("RenderGraph", "Invalid pass resource usage");
        if (usage.access == AccessMode::Auto) usage.access = accessFor(usage.state);
        if ((usage.state == ResourceState::ShaderRead || usage.state == ResourceState::InputAttachment) &&
            usage.access != AccessMode::Read)
            Log::fatal("RenderGraph", "Shader/input attachment usage must be read-only");
        const auto found = std::ranges::find(result, usage.texture, &RgResourceAccess::texture);
        if (found == result.end()) result.push_back(usage);
        else {
            if (found->state != usage.state)
                Log::fatal("RenderGraph", "Attachment feedback or conflicting usage in one pass");
            if (found->access != usage.access) found->access = AccessMode::ReadWrite;
        }
    };
    for (const auto& usage : pass.resources) add(usage);
    std::vector<RgTextureHandle> bindings;
    const auto unique = [&](RgTextureHandle handle) {
        if (std::ranges::find(bindings, handle) != bindings.end())
            Log::fatal("RenderGraph", "Duplicate attachment binding in one pass");
        bindings.push_back(handle);
    };
    for (const auto& a : pass.rendering.colorAttachments) {
        unique(a.texture);
        add({a.texture, rhi::TextureAspect::Color, ResourceState::ColorAttachment, AccessMode::ReadWrite});
    }
    for (const auto& a : pass.rendering.depthAttachments) {
        unique(a.texture);
        if (a.access != AccessMode::Read && a.access != AccessMode::ReadWrite)
            Log::fatal("RenderGraph", "Depth attachment access must be Read or ReadWrite");
        if (a.access == AccessMode::Read && a.loadOp != rhi::LoadOp::Load)
            Log::fatal("RenderGraph", "Read-only depth attachment must load its contents");
        add({a.texture, rhi::TextureAspect::Depth, ResourceState::DepthAttachment, a.access});
    }
    for (const auto handle : pass.rendering.inputAttachments) {
        unique(handle);
        add({handle, resolveNode(handle).aspect, ResourceState::InputAttachment, AccessMode::Read});
    }
    for (const auto& usage : result)
        if (attachmentState(usage.state) && std::ranges::find(bindings, usage.texture) == bindings.end())
            Log::fatal("RenderGraph", "Attachment usage has no attachment binding");
    return result;
}

void RenderGraph::compile(RgTexturePool& pool) {
    ensureRecording();
    if (!renderPassCache_ && std::ranges::any_of(groups_, &RecordedGroup::traditional))
        Log::fatal("RenderGraph", "Raster groups require a persistent RgRenderPassCache");
    pool_ = &pool;
    for (auto& node : textures_) {
        if (!node.isTransient) continue;
        node.poolEntry = pool.acquire(node.desc);
        node.resolvedTexture = node.poolEntry->texture;
        node.resolvedView = node.poolEntry->view;
        node.initialState = node.poolEntry->lastState;
    }
    compiledGroups_.reserve(groups_.size());
    for (const auto& group : groups_) {
        CompiledGroup compiled;
        compiled.name = group.name;
        compiled.traditional = group.traditional;
        for (const auto& pass : group.subpasses) {
            CompiledSubpass subpass;
            subpass.name = pass.name;
            subpass.execute = pass.execute;
            subpass.resources = normalizeAccesses(pass);
            subpass.rendering.renderArea = pass.rendering.renderArea;
            for (const auto& a : pass.rendering.colorAttachments)
                subpass.rendering.colorAttachments.push_back(
                    {resolveNode(a.texture).resolvedView, a.loadOp, a.storeOp, a.clearColor});
            for (const auto& a : pass.rendering.depthAttachments)
                subpass.rendering.depthAttachments.push_back(
                    {resolveNode(a.texture).resolvedView, a.loadOp, a.storeOp, a.clearDepth});
            if (!group.traditional && !pass.rendering.inputAttachments.empty())
                Log::fatal("RenderGraph", "Input attachments require an explicit raster group");
            compiled.subpasses.push_back(std::move(subpass));
        }
        compiledGroups_.push_back(std::move(compiled));
    }
    for (std::size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i].traditional) compileRasterGroup(i);

    struct State { ResourceState state; AccessMode access; bool initialized; };
    std::vector<State> states;
    for (const auto& node : textures_)
        states.push_back({node.initialState, node.poolEntry ? node.poolEntry->lastAccess :
                          node.initialState == ResourceState::InputAttachment ? AccessMode::ReadWrite :
                          accessFor(node.initialState),
                          !node.isTransient && node.initialState != ResourceState::Undefined});
    for (std::size_t g = 0; g < groups_.size(); ++g) {
        auto& group = compiledGroups_[g];
        std::vector<bool> seen(textures_.size());
        for (std::size_t p = 0; p < group.subpasses.size(); ++p) {
            const auto& recorded = groups_[g].subpasses[p];
            for (const auto& usage : group.subpasses[p].resources) {
                auto& state = states[usage.texture.index];
                const auto& node = resolveNode(usage.texture);
                if (needsContents(recorded, usage) && !state.initialized)
                    Log::fatal("RenderGraph", "Pass reads undefined texture contents");
                if (!seen[usage.texture.index] &&
                    (state.state != usage.state || writes(state.access) || writes(usage.access)))
                    group.preBarriers.push_back(barrier(node.resolvedTexture, node.aspect,
                        state.state, usage.state, state.access,
                        group.traditional && attachmentState(usage.state)
                            ? AccessMode::ReadWrite : usage.access));
                seen[usage.texture.index] = true;
                state.state = usage.state;
                state.access = usage.access;
                if (writes(usage.access)) state.initialized = true;
            }
        }
        // Stores happen at group end even when the last subpass only reads an input.
        if (group.traditional) {
            for (std::size_t a = 0; a < group.attachmentTextures.size(); ++a) {
                auto& state = states[group.attachmentTextures[a].index];
                state.access = AccessMode::ReadWrite; // Store and DontCare can both write.
                if (group.renderPassDesc.attachments[a].storeOp == rhi::StoreOp::DontCare)
                    state.initialized = false;
            }
        } else {
            const auto& recorded = groups_[g].subpasses[0];
            for (const auto& a : recorded.rendering.colorAttachments)
            {
                states[a.texture.index].access = AccessMode::ReadWrite;
                if (a.storeOp == rhi::StoreOp::DontCare) states[a.texture.index].initialized = false;
            }
            for (const auto& a : recorded.rendering.depthAttachments)
            {
                states[a.texture.index].access = AccessMode::ReadWrite;
                if (a.storeOp == rhi::StoreOp::DontCare) states[a.texture.index].initialized = false;
            }
        }
    }
    finalStates_.reserve(textures_.size());
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        const auto& node = textures_[i];
        const auto& state = states[i];
        const auto final = !node.isTransient && node.finalState != ResourceState::Undefined
                             ? node.finalState : state.state;
        if (final != state.state)
            finalBarriers_.push_back(barrier(node.resolvedTexture, node.aspect,
                state.state, final, state.access, accessFor(final)));
        finalStates_.push_back(final);
        finalAccesses_.push_back(final == state.state ? state.access : accessFor(final));
    }
    compiled_ = true;
}

void RenderGraph::compile(RgTexturePool& pool, RgRenderPassCache& cache) {
    ensureRecording();
    renderPassCache_ = &cache;
    compile(pool);
}

void RenderGraph::compileRasterGroup(std::size_t groupIndex) {
    auto& group = compiledGroups_[groupIndex];
    const auto& recorded = groups_[groupIndex];
    auto* device = RHI_DEVICE;
    if (!device) Log::fatal("RenderGraph", "No device for raster group compilation");
    auto& handles = group.attachmentTextures;
    std::vector<std::uint32_t> first, last;
    std::vector<rhi::RID> views;
    const auto area = recorded.subpasses.front().rendering.renderArea;
    for (std::size_t p = 0; p < recorded.subpasses.size(); ++p) {
        const auto& pass = recorded.subpasses[p];
        if (!sameArea(area, pass.rendering.renderArea))
            Log::fatal("RenderGraph", "Raster group render areas must match");
        rhi::SubpassDesc subpass;
        const auto addAttachment = [&](RgTextureHandle handle, ResourceState state,
                                       rhi::LoadOp load, rhi::StoreOp store,
                                       rhi::AttachmentClearValue clear) {
            const auto& node = resolveNode(handle);
            const auto info = device->texture_view_info(node.resolvedView);
            const auto required = state == ResourceState::ColorAttachment ? rhi::TextureUsage::ColorAttachment :
                state == ResourceState::DepthAttachment ? rhi::TextureUsage::DepthStencilAttachment :
                rhi::TextureUsage::InputAttachment;
            if (info.texture != node.resolvedTexture || info.format == rhi::PixelFormat::Undefined ||
                info.baseMip != 0 || info.mipCount != 1 || info.baseLayer != 0 || info.layerCount != 1 ||
                static_cast<std::uint64_t>(area.x) + area.width > info.width ||
                static_cast<std::uint64_t>(area.y) + area.height > info.height ||
                !rhi::hasFlag(info.usage, required) ||
                (node.aspect == rhi::TextureAspect::Depth) != rhi::isDepthFormat(info.format))
                Log::fatal("RenderGraph", "Invalid raster group attachment view/usage/extent");
            const auto found = std::ranges::find(handles, handle);
            std::uint32_t index{};
            if (found == handles.end()) {
                index = static_cast<std::uint32_t>(handles.size());
                handles.push_back(handle);
                views.push_back(node.resolvedView);
                first.push_back(static_cast<std::uint32_t>(p));
                last.push_back(static_cast<std::uint32_t>(p));
                group.renderPassDesc.attachments.push_back({info.format, info.samples, load, store, state, state});
                group.beginInfo.clearValues.push_back(clear);
            } else {
                index = static_cast<std::uint32_t>(found - handles.begin());
                if (state != ResourceState::InputAttachment && load != rhi::LoadOp::Load)
                    Log::fatal("RenderGraph", "Repeated attachment must Load; split group for another clear/discard");
                last[index] = static_cast<std::uint32_t>(p);
                if (state != ResourceState::InputAttachment) group.renderPassDesc.attachments[index].storeOp = store;
                group.renderPassDesc.attachments[index].finalState = state;
            }
            return rhi::AttachmentReference{index, state};
        };
        for (const auto& a : pass.rendering.colorAttachments)
            subpass.colors.push_back(addAttachment(a.texture, ResourceState::ColorAttachment,
                a.loadOp, a.storeOp, {a.clearColor, 1.0F}));
        for (const auto& a : pass.rendering.depthAttachments)
            subpass.depth = addAttachment(a.texture, ResourceState::DepthAttachment,
                a.loadOp, a.storeOp, {math::Vec4{0.0F}, a.clearDepth});
        for (const auto handle : pass.rendering.inputAttachments)
            subpass.inputs.push_back(addAttachment(handle, ResourceState::InputAttachment,
                rhi::LoadOp::Load, rhi::StoreOp::Store, {}));
        group.renderPassDesc.subpasses.push_back(std::move(subpass));
    }
    if (handles.empty()) Log::fatal("RenderGraph", "Raster group has no attachments");
    for (const auto& a : group.renderPassDesc.attachments)
        if (a.samples != group.renderPassDesc.attachments.front().samples)
            Log::fatal("RenderGraph", "Raster group currently requires matching sample counts");
    for (std::size_t p = 0; p < group.subpasses.size(); ++p) {
        for (const auto& usage : group.subpasses[p].resources) {
            if (usage.state != ResourceState::ShaderRead && !attachmentState(usage.state))
                Log::fatal("RenderGraph", "Raster group only supports attachments and sampled reads");
            if (usage.state == ResourceState::ShaderRead && std::ranges::find(handles, usage.texture) != handles.end())
                Log::fatal("RenderGraph", "Sampling a group attachment requires a separate pass; use input attachment for local reads");
            for (std::size_t earlier = 0; earlier < p; ++earlier) {
                for (const auto& source : group.subpasses[earlier].resources) {
                    if (source.texture != usage.texture ||
                        (!writes(source.access) && !writes(usage.access) && source.state == usage.state)) continue;
                    group.renderPassDesc.dependencies.push_back({static_cast<std::uint32_t>(earlier),
                        static_cast<std::uint32_t>(p), source.state, usage.state, source.access, usage.access, true});
                    const auto attachment = std::ranges::find(handles, usage.texture);
                    if (attachment != handles.end() &&
                        last[static_cast<std::size_t>(attachment - handles.begin())] == p) {
                        group.renderPassDesc.dependencies.back().destinationStoreState =
                            resolveNode(usage.texture).aspect == rhi::TextureAspect::Depth
                                ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
                    }
                }
            }
        }
    }
    for (std::size_t a = 0; a < handles.size(); ++a) {
        for (std::uint32_t p = first[a] + 1; p < last[a]; ++p) {
            const auto& usages = group.subpasses[p].resources;
            if (std::ranges::find(usages, handles[a], &RgResourceAccess::texture) == usages.end())
                group.renderPassDesc.subpasses[p].preserves.push_back(static_cast<std::uint32_t>(a));
        }
        // Store is a physical group-boundary decision, upgraded for later consumers.
        bool needed = false, foundLater = false;
        for (std::size_t g = groupIndex + 1; g < groups_.size() && !foundLater; ++g) {
            for (std::size_t p = 0; p < compiledGroups_[g].subpasses.size() && !foundLater; ++p) {
                for (const auto& u : compiledGroups_[g].subpasses[p].resources) {
                    if (u.texture != handles[a]) continue;
                    needed = needsContents(groups_[g].subpasses[p], u);
                    foundLater = true;
                    break;
                }
            }
        }
        const auto& node = resolveNode(handles[a]);
        if (!foundLater && !node.isTransient &&
            (node.finalState == ResourceState::Present || node.finalState == ResourceState::ShaderRead ||
             node.finalState == ResourceState::CopySource)) needed = true;
        if (needed) group.renderPassDesc.attachments[a].storeOp = rhi::StoreOp::Store;
    }
    group.beginInfo.renderArea = area;
    group.beginInfo.renderPass = renderPassCache_->getOrCreateRenderPass(group.renderPassDesc);
    group.beginInfo.framebuffer = renderPassCache_->getOrCreateFramebuffer({group.beginInfo.renderPass,
        std::move(views), static_cast<std::uint32_t>(area.x) + area.width,
        static_cast<std::uint32_t>(area.y) + area.height, 1});
    if (!group.beginInfo.framebuffer) Log::fatal("RenderGraph", "Framebuffer creation failed");
}

void RenderGraph::execute(rhi::RID commandBuffer) const {
    if (!compiled_) Log::fatal("RenderGraph", "RenderGraph must be compiled before execute");
    for (const auto& group : compiledGroups_) {
        rhi::beginDebugLabel(commandBuffer, group.name, {0.25F, 0.55F, 1.0F, 1.0F});
        rhi::resourceBarriers(commandBuffer, group.preBarriers);
        if (group.traditional) rhi::beginRenderPass(commandBuffer, group.beginInfo);
        else rhi::beginRendering(commandBuffer, group.subpasses[0].rendering);
        for (std::uint32_t i = 0; i < group.subpasses.size(); ++i) {
            if (i > 0) rhi::nextSubpass(commandBuffer);
            const auto& pass = group.subpasses[i];
            rhi::beginDebugLabel(commandBuffer, pass.name, {0.4F, 0.7F, 0.3F, 1.0F});
            pass.execute({commandBuffer, group.beginInfo.renderPass, i});
            rhi::endDebugLabel(commandBuffer);
        }
        if (group.traditional) rhi::endRenderPass(commandBuffer);
        else rhi::endRendering(commandBuffer);
        rhi::endDebugLabel(commandBuffer);
    }
    rhi::resourceBarriers(commandBuffer, finalBarriers_);
    for (std::size_t i = 0; i < textures_.size(); ++i)
    {
        if (textures_[i].trackedState) *textures_[i].trackedState = finalStates_[i];
        if (textures_[i].poolEntry) {
            textures_[i].poolEntry->lastState = finalStates_[i];
            textures_[i].poolEntry->lastAccess = finalAccesses_[i];
        }
    }
}

void RenderGraph::reset() {
    releaseTransientTextures();
    groups_.clear();
    textures_.clear();
    compiledGroups_.clear();
    finalBarriers_.clear();
    finalStates_.clear();
    finalAccesses_.clear();
    pool_ = nullptr;
    renderPassCache_ = nullptr;
    compiled_ = false;
}

rhi::RID RenderGraph::resolvedTextureView(RgTextureHandle handle) const {
    if (!compiled_) Log::fatal("RenderGraph", "Texture resolution requires a compiled graph");
    return resolveNode(handle).resolvedView;
}

const RenderGraph::TextureNode& RenderGraph::resolveNode(RgTextureHandle handle) const {
    if (!handle.valid() || handle.index >= textures_.size()) Log::fatal("RenderGraph", "Invalid RgTextureHandle");
    return textures_[handle.index];
}

void RenderGraph::releaseTransientTextures() {
    for (auto& node : textures_)
        if (node.poolEntry) { pool_->release(node.poolEntry); node.poolEntry = nullptr; }
}

} // namespace engine
