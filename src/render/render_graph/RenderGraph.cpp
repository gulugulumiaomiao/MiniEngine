#include "render/render_graph/RenderGraph.h"

#include "core/logging/Log.h"
#include "render/pipeline/RenderContext.h"
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
bool finalReadsContents(ResourceState state) {
    return state == ResourceState::ShaderRead || state == ResourceState::InputAttachment ||
           state == ResourceState::CopySource || state == ResourceState::Present;
}
rhi::TextureUsage usageFor(ResourceState state) {
    switch (state) {
    case ResourceState::ColorAttachment: return rhi::TextureUsage::ColorAttachment;
    case ResourceState::DepthAttachment: return rhi::TextureUsage::DepthStencilAttachment;
    case ResourceState::InputAttachment: return rhi::TextureUsage::InputAttachment;
    case ResourceState::ShaderRead: return rhi::TextureUsage::Sampled;
    case ResourceState::CopySource: return rhi::TextureUsage::TransferSource;
    case ResourceState::CopyDestination: return rhi::TextureUsage::TransferDestination;
    case ResourceState::Undefined:
    case ResourceState::Present: return rhi::TextureUsage::None;
    }
    Log::fatal("RenderGraph", "Invalid texture resource state");
}
bool validArea(const rhi::Rect& a) { return a.x >= 0 && a.y >= 0 && a.width && a.height; }
bool sameArea(const rhi::Rect& a, const rhi::Rect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
bool needsContents(const RgRasterPass& pass, const RgResourceAccess& usage) {
    for (const auto& a : pass.colorAttachments)
        if (a.texture == usage.texture) return a.loadOp == rhi::LoadOp::Load;
    for (const auto& a : pass.depthAttachments)
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

RgFrameResources::~RgFrameResources() { release(); }
RgFrameResources::RgFrameResources(RgFrameResources&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), executed_(other.executed_),
      textures_(std::move(other.textures_)), groups_(std::move(other.groups_)),
      finalBarriers_(std::move(other.finalBarriers_)), finalStates_(std::move(other.finalStates_)),
      finalAccesses_(std::move(other.finalAccesses_)) {}
RgFrameResources& RgFrameResources::operator=(RgFrameResources&& other) noexcept {
    if (this == &other) return *this;
    release();
    owner_ = std::exchange(other.owner_, nullptr);
    executed_ = other.executed_;
    textures_ = std::move(other.textures_);
    groups_ = std::move(other.groups_);
    finalBarriers_ = std::move(other.finalBarriers_);
    finalStates_ = std::move(other.finalStates_);
    finalAccesses_ = std::move(other.finalAccesses_);
    return *this;
}
void RgFrameResources::release() {
    if (!owner_) return;
    for (auto& texture : textures_) RgTexturePool::release(texture.poolEntry);
    --owner_->activeFrames_;
    owner_ = nullptr;
}
void RgFrameResources::reset() {
    release();
    textures_.clear(); groups_.clear(); finalBarriers_.clear();
    finalStates_.clear(); finalAccesses_.clear();
}

rhi::RID RgFrameResources::textureView(RgTextureHandle handle) const {
    if (!owner_ || !handle.valid() || handle.index >= textures_.size())
        Log::fatal("RenderGraph", "Invalid frame texture handle");
    return textures_[handle.index].binding.view;
}
rhi::RID RgFrameResources::texture(RgTextureHandle handle) const {
    if (!owner_ || !handle.valid() || handle.index >= textures_.size())
        Log::fatal("RenderGraph", "Invalid frame texture handle");
    return textures_[handle.index].binding.texture;
}

RenderGraph::~RenderGraph() { ensureNoFrames(); }
void RenderGraph::ensureNoFrames() const {
    if (activeFrames_) Log::fatal("RenderGraph", "Release prepared frame resources before changing the graph");
}
void RenderGraph::ensureRecording() const {
    ensureNoFrames();
    if (compiled_) Log::fatal("RenderGraph", "Cannot modify a compiled graph; call reset first");
}
RgTextureHandle RenderGraph::importSwapchainTexture(const rhi::ISwapchain& swapchain) {
    return importTexture("Swapchain." + std::to_string(reinterpret_cast<std::uintptr_t>(&swapchain)),
        [&swapchain](RenderContext&) {
            return RgTextureBinding{swapchain.currentTexture(), swapchain.currentTextureView(),
                                    swapchain.currentTextureState()};
        }, ResourceState::Present);
}
RgTextureHandle RenderGraph::importTexture(std::string name, TextureResolver resolver,
                                         ResourceState finalState) {
    ensureRecording();
    if (name.empty() || !resolver) Log::fatal("RenderGraph", "External texture source is empty");
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        const auto& node = textures_[i];
        if (node.sourceName != name) continue;
        if (node.finalState != finalState)
            Log::fatal("RenderGraph", "Conflicting named texture sources");
        return {static_cast<std::uint32_t>(i)};
    }
    const RgTextureHandle handle{static_cast<std::uint32_t>(textures_.size())};
    TextureNode node;
    node.sourceName = std::move(name);
    node.resolver = std::move(resolver);
    node.finalState = finalState;
    textures_.push_back(std::move(node));
    return handle;
}
RgTextureBinding RenderGraph::resolveBinding(const TextureNode& node, RenderContext& context) const {
    auto binding = node.resolver(context);
    if (!binding.texture || !binding.view)
        Log::fatal("RenderGraph", "Imported texture and view must both be valid");
    if (binding.trackedState) binding.initialState = context.rgTexturePool().trackedState(binding.trackedState);
    if (binding.initialAccess == AccessMode::Auto) binding.initialAccess =
        binding.initialState == ResourceState::InputAttachment ? AccessMode::ReadWrite : accessFor(binding.initialState);
    return binding;
}
RgTextureHandle RenderGraph::createTexture(TextureDesc desc, TextureExtentResolver extentResolver, ResourceState finalState) {
    ensureRecording();
    if (desc.type != TextureType::Texture2D ||
        (!extentResolver && (!desc.width || !desc.height)) ||
        desc.mipCount != 1 || desc.arrayLayers != 1 || desc.depth != 1)
        Log::fatal("RenderGraph", "Graph transient textures currently require one mip and layer");
    const RgTextureHandle handle{static_cast<std::uint32_t>(textures_.size())};
    TextureNode node;
    node.desc = std::move(desc);
    node.extentResolver = std::move(extentResolver);
    if (finalState == ResourceState::Present)
        Log::fatal("RenderGraph", "Transient textures cannot be presented");
    node.finalState = finalState;
    node.isTransient = true;
    textures_.push_back(std::move(node));
    return handle;
}
void RenderGraph::addGraphicsPass(RgRasterPass pass) {
    ensureRecording();
    if (!pass.execute) Log::fatal("RenderGraph", "Pass callback is empty");
    RecordedGroup group{pass.name, false, {}};
    group.subpasses.push_back(std::move(pass));
    groups_.push_back(std::move(group));
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
    const auto& area = pass.renderArea;
    if ((!pass.renderAreaResolver && !validArea(area)) ||
        pass.depthAttachments.size() > 1)
        Log::fatal("RenderGraph", "Invalid render area or multiple depth attachments");
    std::vector<RgResourceAccess> result;
    const auto add = [&](RgResourceAccess usage) {
        const auto& node = resolveNode(usage.texture);
        if (node.aspect != usage.aspect)
            Log::fatal("RenderGraph", "Invalid pass resource usage");
        const auto found = std::ranges::find(result, usage.texture, &RgResourceAccess::texture);
        if (found == result.end()) result.push_back(usage);
        else {
            if (found->state != usage.state)
                Log::fatal("RenderGraph", "Attachment feedback or conflicting usage in one pass");
        }
    };
    for (const auto handle : pass.sampledTextures)
        add({handle, resolveNode(handle).aspect, ResourceState::ShaderRead, AccessMode::Read});
    std::vector<RgTextureHandle> bindings;
    const auto unique = [&](RgTextureHandle handle) {
        if (std::ranges::find(bindings, handle) != bindings.end())
            Log::fatal("RenderGraph", "Duplicate attachment binding in one pass");
        bindings.push_back(handle);
    };
    for (const auto& a : pass.colorAttachments) {
        unique(a.texture);
        add({a.texture, rhi::TextureAspect::Color, ResourceState::ColorAttachment, AccessMode::ReadWrite});
    }
    for (const auto& a : pass.depthAttachments) {
        unique(a.texture);
        if (a.access != AccessMode::Read && a.access != AccessMode::ReadWrite)
            Log::fatal("RenderGraph", "Depth attachment access must be Read or ReadWrite");
        if (a.access == AccessMode::Read && a.loadOp != rhi::LoadOp::Load)
            Log::fatal("RenderGraph", "Read-only depth attachment must load its contents");
        add({a.texture, rhi::TextureAspect::Depth, ResourceState::DepthAttachment, a.access});
    }
    for (const auto handle : pass.inputAttachments) {
        unique(handle);
        add({handle, resolveNode(handle).aspect, ResourceState::InputAttachment, AccessMode::Read});
    }
    return result;
}

void RenderGraph::validateView(const TextureNode& node, const rhi::TextureViewInfo& info,
                              ResourceState state, const rhi::Rect* area, bool sampledAccess) const {
    rhi::TextureUsage required{rhi::TextureUsage::None};
    switch (state) {
    case ResourceState::ColorAttachment: required = rhi::TextureUsage::ColorAttachment; break;
    case ResourceState::DepthAttachment: required = rhi::TextureUsage::DepthStencilAttachment; break;
    case ResourceState::InputAttachment: required = rhi::TextureUsage::InputAttachment; break;
    case ResourceState::CopySource: required = rhi::TextureUsage::TransferSource; break;
    case ResourceState::CopyDestination: required = rhi::TextureUsage::TransferDestination; break;
    case ResourceState::ShaderRead:
        // Shader-read layout also supports input attachments; sampled bindings require Sampled.
        required = sampledAccess ? rhi::TextureUsage::Sampled :
            rhi::TextureUsage::Sampled | rhi::TextureUsage::InputAttachment;
        break;
    case ResourceState::Undefined:
    case ResourceState::Present: break;
    }
    if (info.format == rhi::PixelFormat::Undefined || info.baseMip || info.mipCount != 1 ||
        info.baseLayer || info.layerCount != 1 ||
        (required != rhi::TextureUsage::None && !rhi::hasFlag(info.usage, required)) ||
        (node.aspect == rhi::TextureAspect::Depth) != rhi::isDepthFormat(info.format) ||
        ((state == ResourceState::ColorAttachment || state == ResourceState::Present) &&
            node.aspect != rhi::TextureAspect::Color) ||
        (state == ResourceState::DepthAttachment && node.aspect != rhi::TextureAspect::Depth) ||
        (area && (static_cast<std::uint64_t>(area->x) + area->width > info.width ||
                  static_cast<std::uint64_t>(area->y) + area->height > info.height)))
        Log::fatal("RenderGraph", "Invalid raster group attachment view/usage/extent");
}
bool RenderGraph::resourcesCompatible(RenderContext& context) const {
    if (!compiled_) return false;
    auto* device = RHI_DEVICE;
    if (!device) return false;
    for (const auto& node : textures_) {
        if (node.isTransient) continue;
        const auto binding = resolveBinding(node, context);
        const auto info = device->texture_view_info(binding.view);
        if (info.format != node.metadata.format || info.samples != node.metadata.samples) return false;
    }
    return true;
}
void RenderGraph::compile(RenderContext& context) {
    if (compiled_) return;
    ensureNoFrames();
    auto* device = RHI_DEVICE;
    if (!device && !textures_.empty()) Log::fatal("RenderGraph", "No device for graph compilation");
    for (auto& node : textures_) {
        if (node.isTransient) {
            const auto samples = toRhiSampleCount(node.desc.samples);
            if (!samples) Log::fatal("RenderGraph", "Unsupported texture sample count; expected 1, 2, 4 or 8");
            node.metadata = {{}, toRhi(node.desc.format), *samples, usageFor(node.finalState),
                node.extentResolver ? 0U : node.desc.width, node.extentResolver ? 0U : node.desc.height,
                0, 1, 0, 1};
        } else {
            const auto binding = resolveBinding(node, context);
            node.metadata = device->texture_view_info(binding.view);
            if (node.metadata.texture != binding.texture)
                Log::fatal("RenderGraph", "Imported view does not reference its texture");
        }
        node.aspect = rhi::isDepthFormat(node.metadata.format) ?
            rhi::TextureAspect::Depth : rhi::TextureAspect::Color;
    }
    compiledGroups_.reserve(groups_.size());
    for (const auto& recorded : groups_) {
        CompiledGroup group;
        group.name = recorded.name;
        group.traditional = recorded.traditional;
        for (const auto& pass : recorded.subpasses) {
            CompiledSubpass subpass{normalizeAccesses(pass)};
            rhi::SampleCount samples{};
            bool hasAttachment{};
            for (const auto& usage : subpass.resources) {
                (void)resolveNode(usage.texture);
                auto& node = textures_[usage.texture.index];
                if (node.isTransient) node.metadata.usage = node.metadata.usage | usageFor(usage.state);
                validateView(node, node.metadata, usage.state, nullptr, usage.state == ResourceState::ShaderRead);
                if (!attachmentState(usage.state)) continue;
                if (hasAttachment && samples != node.metadata.samples)
                    Log::fatal("RenderGraph", "Raster pass requires matching sample counts");
                samples = node.metadata.samples;
                hasAttachment = true;
            }
            if (!recorded.traditional && !pass.inputAttachments.empty())
                Log::fatal("RenderGraph", "Input attachments require an explicit raster group");
            group.subpasses.push_back(std::move(subpass));
        }
        compiledGroups_.push_back(std::move(group));
    }
    for (const auto& node : textures_) {
        if (node.isTransient && node.metadata.usage == rhi::TextureUsage::None)
            Log::fatal("RenderGraph", "Transient texture has no declared use");
        validateView(node, node.metadata, node.finalState);
    }
    for (std::size_t i = 0; i < groups_.size(); ++i)
        if (groups_[i].traditional) compileRasterGroup(i);

    // First-use states are supplied by the physical frame; later states are graph invariants.
    enum class Contents { FrameInitial, Valid, Invalid };
    struct State {
        ResourceState state{ResourceState::Undefined};
        AccessMode access{AccessMode::Read};
        bool frameInitial{true};
        Contents contents{Contents::FrameInitial};
    };
    std::vector<State> states(textures_.size());
    for (std::size_t i = 0; i < textures_.size(); ++i)
        if (textures_[i].isTransient) states[i].contents = Contents::Invalid;
    for (std::size_t g = 0; g < groups_.size(); ++g) {
        auto& group = compiledGroups_[g];
        std::vector<bool> seen(textures_.size());
        for (std::size_t p = 0; p < group.subpasses.size(); ++p) {
            const auto& recorded = groups_[g].subpasses[p];
            for (const auto& usage : group.subpasses[p].resources) {
                auto& state = states[usage.texture.index];
                const bool read = needsContents(recorded, usage);
                if (read && state.contents == Contents::Invalid)
                    Log::fatal("RenderGraph", "Pass reads undefined texture contents");
                const bool checkInitial = read && state.contents == Contents::FrameInitial;
                if (!seen[usage.texture.index] &&
                    (state.frameInitial || state.state != usage.state || writes(state.access) || writes(usage.access)))
                    group.preBarriers.push_back({usage.texture, state.state, usage.state, state.access,
                        group.traditional && attachmentState(usage.state) ? AccessMode::ReadWrite : usage.access,
                        state.frameInitial, checkInitial});
                seen[usage.texture.index] = true;
                state.state = usage.state;
                state.access = usage.access;
                state.frameInitial = false;
                if (read || writes(usage.access)) state.contents = Contents::Valid;
            }
        }
        const auto stored = [&](RgTextureHandle handle, rhi::StoreOp store) {
            auto& state = states[handle.index];
            state.access = AccessMode::ReadWrite;
            if (store == rhi::StoreOp::DontCare) state.contents = Contents::Invalid;
        };
        if (group.traditional) {
            for (std::size_t a = 0; a < group.attachmentTextures.size(); ++a)
                stored(group.attachmentTextures[a], group.renderPassDesc.attachments[a].storeOp);
        } else {
            for (const auto& a : groups_[g].subpasses[0].colorAttachments) stored(a.texture, a.storeOp);
            for (const auto& a : groups_[g].subpasses[0].depthAttachments) stored(a.texture, a.storeOp);
        }
    }
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        const auto& node = textures_[i];
        const auto& state = states[i];
        const auto final = node.finalState != ResourceState::Undefined ? node.finalState : state.state;
        const bool finalRead = node.finalState != ResourceState::Undefined && finalReadsContents(final);
        if (finalRead && state.contents == Contents::Invalid)
            Log::fatal("RenderGraph", "Final state reads undefined texture contents");
        if ((!state.frameInitial && final != state.state) ||
            (state.frameInitial && node.finalState != ResourceState::Undefined))
            finalBarriers_.push_back({{static_cast<std::uint32_t>(i)}, state.state, final,
                                     state.access, accessFor(final), state.frameInitial,
                                     finalRead && state.contents == Contents::FrameInitial});
        finalStates_.push_back(final);
        finalAccesses_.push_back(final == state.state ? state.access : accessFor(final));
        finalFromFrame_.push_back(state.frameInitial);
    }
    compiled_ = true;
    ++compileCount_;
}

void RenderGraph::compileRasterGroup(std::size_t groupIndex) {
    auto& group = compiledGroups_[groupIndex];
    const auto& recorded = groups_[groupIndex];
    auto& handles = group.attachmentTextures;
    std::vector<std::uint32_t> first, last;
    const auto area = recorded.subpasses.front().renderArea;
    for (std::size_t p = 0; p < recorded.subpasses.size(); ++p) {
        const auto& pass = recorded.subpasses[p];
        if (!recorded.subpasses.front().renderAreaResolver && !pass.renderAreaResolver && !sameArea(area, pass.renderArea))
            Log::fatal("RenderGraph", "Raster group render areas must match");
        rhi::SubpassDesc subpass;
        const auto addAttachment = [&](RgTextureHandle handle, ResourceState state,
                                       rhi::LoadOp load, rhi::StoreOp store,
                                       ClearSource clear) {
            const auto& node = resolveNode(handle);
            const auto& info = node.metadata;
            validateView(node, info, state,
                         !pass.renderAreaResolver && info.width && info.height ? &pass.renderArea : nullptr);
            const auto found = std::ranges::find(handles, handle);
            std::uint32_t index{};
            if (found == handles.end()) {
                index = static_cast<std::uint32_t>(handles.size());
                handles.push_back(handle);
                first.push_back(static_cast<std::uint32_t>(p));
                last.push_back(static_cast<std::uint32_t>(p));
                group.renderPassDesc.attachments.push_back({info.format, info.samples, load, store, state, state});
                group.clearSources.push_back(clear);
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
        for (std::uint32_t slot = 0; slot < pass.colorAttachments.size(); ++slot) {
            const auto& a = pass.colorAttachments[slot];
            subpass.colors.push_back(addAttachment(a.texture, ResourceState::ColorAttachment,
                a.loadOp, a.storeOp, {static_cast<std::uint32_t>(p), slot, false, false}));
        }
        for (std::uint32_t slot = 0; slot < pass.depthAttachments.size(); ++slot) {
            const auto& a = pass.depthAttachments[slot];
            subpass.depth = addAttachment(a.texture, ResourceState::DepthAttachment,
                a.loadOp, a.storeOp, {static_cast<std::uint32_t>(p), slot, true, false});
        }
        for (const auto handle : pass.inputAttachments)
            subpass.inputs.push_back(addAttachment(handle, ResourceState::InputAttachment,
                rhi::LoadOp::Load, rhi::StoreOp::Store, {static_cast<std::uint32_t>(p), 0, false, true}));
        group.renderPassDesc.subpasses.push_back(std::move(subpass));
    }
    if (handles.empty()) Log::fatal("RenderGraph", "Raster group has no attachments");
    for (const auto& a : group.renderPassDesc.attachments)
        if (a.samples != group.renderPassDesc.attachments.front().samples)
            Log::fatal("RenderGraph", "Raster group currently requires matching sample counts");
    for (std::size_t p = 0; p < group.subpasses.size(); ++p) {
        for (const auto& usage : group.subpasses[p].resources) {
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
        if (!foundLater && finalReadsContents(node.finalState)) needed = true;
        if (needed) group.renderPassDesc.attachments[a].storeOp = rhi::StoreOp::Store;
    }
}

RgFrameResources RenderGraph::prepareFrame(RenderContext& context) {
    if (!compiled_) Log::fatal("RenderGraph", "RenderGraph must be compiled before preparing a frame");
    ensureNoFrames();
    RgFrameResources frame;
    frame.owner_ = this;
    ++activeFrames_;
    auto* device = RHI_DEVICE;
    if (!device && !textures_.empty()) Log::fatal("RenderGraph", "No device for frame preparation");
    frame.textures_.reserve(textures_.size());
    for (const auto& node : textures_) {
        RgFrameResources::Texture texture;
        if (node.isTransient) {
            auto desc = node.desc;
            if (node.extentResolver) {
                const auto extent = node.extentResolver(context);
                desc.width = extent.width;
                desc.height = extent.height;
            }
            texture.poolEntry = context.rgTexturePool().acquire(desc, node.metadata.usage);
            texture.binding = {texture.poolEntry->texture->textureHandle(),
                               texture.poolEntry->texture->defaultView(),
                               texture.poolEntry->lastState, texture.poolEntry->lastAccess};
        } else texture.binding = resolveBinding(node, context);
        texture.info = device->texture_view_info(texture.binding.view);
        if (texture.info.texture != texture.binding.texture)
            Log::fatal("RenderGraph", "Imported view does not reference its texture");
        if (texture.info.format != node.metadata.format || texture.info.samples != node.metadata.samples)
            Log::fatal("RenderGraph", "Texture format/sample count changed; invalidate and recompile the graph");
        validateView(node, texture.info, texture.binding.initialState);
        validateView(node, texture.info, node.finalState);
        for (const auto& existing : frame.textures_)
            if (existing.binding.texture == texture.binding.texture)
                Log::fatal("RenderGraph", "Multiple logical textures resolve to the same physical image");
        frame.textures_.push_back(std::move(texture));
    }
    frame.groups_.reserve(groups_.size());
    for (std::size_t g = 0; g < groups_.size(); ++g) {
        const auto& plan = compiledGroups_[g];
        RgFrameResources::Group group;
        for (std::size_t p = 0; p < groups_[g].subpasses.size(); ++p) {
            const auto& pass = groups_[g].subpasses[p];
            rhi::RenderingInfo rendering;
            rendering.renderArea = pass.renderAreaResolver ? pass.renderAreaResolver(context) : pass.renderArea;
            if (!validArea(rendering.renderArea)) Log::fatal("RenderGraph", "Invalid render area");
            for (const auto& usage : plan.subpasses[p].resources) {
                const auto& texture = frame.textures_[usage.texture.index];
                validateView(resolveNode(usage.texture), texture.info, usage.state,
                             attachmentState(usage.state) ? &rendering.renderArea : nullptr,
                             usage.state == ResourceState::ShaderRead);
            }
            for (const auto& a : pass.colorAttachments)
                rendering.colorAttachments.push_back({frame.textureView(a.texture), a.loadOp, a.storeOp,
                    a.clearColorResolver ? a.clearColorResolver(context) : a.clearColor});
            for (const auto& a : pass.depthAttachments)
                rendering.depthAttachments.push_back({frame.textureView(a.texture), a.loadOp, a.storeOp,
                    a.clearDepthResolver ? a.clearDepthResolver(context) : a.clearDepth});
            group.rendering.push_back(std::move(rendering));
        }
        if (plan.traditional) {
            const auto area = group.rendering.front().renderArea;
            for (const auto& rendering : group.rendering)
                if (!sameArea(area, rendering.renderArea))
                    Log::fatal("RenderGraph", "Raster group render areas must match");
            auto& begin = group.beginInfo;
            begin.renderArea = area;
            for (const auto& source : plan.clearSources) {
                const auto& rendering = group.rendering[source.subpass];
                rhi::AttachmentClearValue clear{};
                if (!source.input) {
                    if (source.depth) clear.depth = rendering.depthAttachments[source.slot].clearDepth;
                    else clear.color = rendering.colorAttachments[source.slot].clearColor;
                }
                begin.clearValues.push_back(clear);
            }
            std::vector<rhi::RID> views;
            for (const auto handle : plan.attachmentTextures) views.push_back(frame.textureView(handle));
            begin.renderPass = context.rgRenderPassCache().getOrCreateRenderPass(plan.renderPassDesc);
            begin.framebuffer = context.rgRenderPassCache().getOrCreateFramebuffer({begin.renderPass,
                std::move(views), static_cast<std::uint32_t>(area.x) + area.width,
                static_cast<std::uint32_t>(area.y) + area.height, 1});
        }
        frame.groups_.push_back(std::move(group));
    }
    materializeFrameBarriers(frame, false);
    return frame;
}
void RenderGraph::materializeFrameBarriers(RgFrameResources& frame, bool executing) const {
    const auto materialize = [&](const BarrierPlan& plan, std::vector<rhi::TextureBarrier>& barriers, bool final) {
        const auto& binding = frame.textures_[plan.texture.index].binding;
        // Tracked imports can be initialized by another graph after this frame is prepared.
        if (plan.needsContents && binding.initialState == ResourceState::Undefined &&
            (executing || !binding.trackedState))
            Log::fatal("RenderGraph", "Pass reads undefined texture contents");
        const auto before = plan.frameInitial ? binding.initialState : plan.before;
        const auto source = plan.frameInitial ? binding.initialAccess : plan.sourceAccess;
        if (before != plan.after || (!final && (writes(source) || writes(plan.destinationAccess))))
            barriers.push_back(barrier(binding.texture, resolveNode(plan.texture).aspect,
                                       before, plan.after, source, plan.destinationAccess));
    };
    for (std::size_t g = 0; g < compiledGroups_.size(); ++g) {
        auto& barriers = frame.groups_[g].preBarriers;
        barriers.clear();
        barriers.reserve(compiledGroups_[g].preBarriers.size());
        for (const auto& plan : compiledGroups_[g].preBarriers) materialize(plan, barriers, false);
    }
    frame.finalBarriers_.clear();
    frame.finalBarriers_.reserve(finalBarriers_.size());
    for (const auto& plan : finalBarriers_) materialize(plan, frame.finalBarriers_, true);
    frame.finalStates_ = finalStates_;
    frame.finalAccesses_ = finalAccesses_;
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        if (!finalFromFrame_[i]) continue;
        const auto& binding = frame.textures_[i].binding;
        const auto requested = textures_[i].finalState;
        frame.finalStates_[i] = requested == ResourceState::Undefined ? binding.initialState : requested;
        frame.finalAccesses_[i] = frame.finalStates_[i] == binding.initialState ?
            binding.initialAccess : accessFor(frame.finalStates_[i]);
    }
}
void RenderGraph::execute(RenderContext& context, RgFrameResources& frame, rhi::RID commandBuffer) {
    if (!compiled_ || frame.owner_ != this || frame.executed_)
        Log::fatal("RenderGraph", "Invalid or already executed graph frame");
    if (!commandBuffer) commandBuffer = context.commandBuffer();
    auto& pool = context.rgTexturePool();
    pool.bindCommandBuffer(commandBuffer);
    for (auto& texture : frame.textures_) {
        auto& binding = texture.binding;
        if (!binding.trackedState) continue;
        binding.initialState = pool.trackedState(binding.trackedState);
        // The state slot does not retain access mode. Conservatively include attachment
        // stores, including stores performed after a final input-attachment read.
        binding.initialAccess = binding.initialState == ResourceState::InputAttachment ?
            AccessMode::ReadWrite : accessFor(binding.initialState);
    }
    materializeFrameBarriers(frame, true);
    // Register before callbacks can throw, so cancelled recording retains all leases safely.
    for (std::size_t i = 0; i < textures_.size(); ++i)
        if (frame.textures_[i].poolEntry)
            pool.recordUsage(frame.textures_[i].poolEntry, frame.finalStates_[i], frame.finalAccesses_[i]);
    for (std::size_t g = 0; g < compiledGroups_.size(); ++g) {
        const auto& plan = compiledGroups_[g];
        const auto& physical = frame.groups_[g];
        rhi::beginDebugLabel(commandBuffer, plan.name, {0.25F, 0.55F, 1.0F, 1.0F});
        rhi::resourceBarriers(commandBuffer, physical.preBarriers);
        if (plan.traditional) rhi::beginRenderPass(commandBuffer, physical.beginInfo);
        else rhi::beginRendering(commandBuffer, physical.rendering[0]);
        for (std::uint32_t p = 0; p < plan.subpasses.size(); ++p) {
            if (p) rhi::nextSubpass(commandBuffer);
            const auto& pass = groups_[g].subpasses[p];
            rhi::beginDebugLabel(commandBuffer, pass.name, {0.4F, 0.7F, 0.3F, 1.0F});
            pass.execute({commandBuffer, physical.beginInfo.renderPass, p, context, frame,
                          physical.rendering[p].renderArea});
            rhi::endDebugLabel(commandBuffer);
        }
        if (plan.traditional) rhi::endRenderPass(commandBuffer); else rhi::endRendering(commandBuffer);
        rhi::endDebugLabel(commandBuffer);
    }
    rhi::resourceBarriers(commandBuffer, frame.finalBarriers_);
    for (std::size_t i = 0; i < textures_.size(); ++i) {
        const auto state = frame.finalStates_[i];
        const auto& texture = frame.textures_[i];
        if (texture.binding.trackedState) pool.recordState(texture.binding.trackedState, state);
    }
    frame.executed_ = true;
}
void RenderGraph::invalidate() {
    ensureNoFrames();
    compiledGroups_.clear();
    finalBarriers_.clear();
    finalStates_.clear();
    finalAccesses_.clear();
    finalFromFrame_.clear();
    compiled_ = false;
}
void RenderGraph::reset() {
    invalidate();
    groups_.clear();
    textures_.clear();
}
const RenderGraph::TextureNode& RenderGraph::resolveNode(RgTextureHandle handle) const {
    if (!handle.valid() || handle.index >= textures_.size()) Log::fatal("RenderGraph", "Invalid RgTextureHandle");
    return textures_[handle.index];
}

} // namespace engine
