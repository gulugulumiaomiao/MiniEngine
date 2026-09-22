#include "render/gpu/global_uniform/GlobalUniformGpuManager.h"

#include "core/logging/Log.h"
#include "render/global_uniform/GlobalUniformManager.h"
#include "render/texture/TextureManager.h"
#include "rhi/api/Device.h"

#include <algorithm>
#include <array>

namespace engine {

GlobalUniformGpuManager::GlobalUniformGpuManager() = default;
GlobalUniformGpuManager::~GlobalUniformGpuManager() = default;

bool GlobalUniformGpuManager::initialize(rhi::IDevice& device, std::uint32_t frameCount) {
    if (initialized()) {
        Log::error("GlobalUniformGpuManager", "Manager is already initialized");
        return false;
    }
    device_ = &device;

    constexpr rhi::ShaderVisibility allGraphics =
        rhi::ShaderVisibility::Vertex | rhi::ShaderVisibility::Fragment;
    std::array<rhi::BindGroupLayoutEntry, kMaxGlobalTextures + 1> entries{};
    entries[0] = {0, rhi::BindingType::UniformBuffer, allGraphics};
    for (std::uint32_t binding = 1; binding < entries.size(); ++binding)
        entries[binding] = {binding, rhi::BindingType::SampledTexture, allGraphics};
    layout_ = device_->createBindGroupLayout({entries, "Global uniform bind group layout"});
    if (!layout_) {
        shutdown();
        return false;
    }

    frames_.resize(frameCount);
    return true;
}

void GlobalUniformGpuManager::beginFrame(std::uint32_t frameIndex) {
    if (!initialized() || frameIndex >= frames_.size())
        return;
    updateFrame(frameIndex);
}

rhi::RID GlobalUniformGpuManager::resolve(std::uint32_t frameIndex) const {
    if (frameIndex >= frames_.size())
        return {};
    return frames_[frameIndex].bindGroup;
}

void GlobalUniformGpuManager::updateFrame(std::uint32_t frameIndex) {
    FrameResources& frame = frames_[frameIndex];
    const GlobalUniformManager& globals = GLOBAL_UNIFORM_MANAGER;
    const std::uint64_t byteSize = std::max<std::uint64_t>(16, globals.uniformBytes().size());

    const auto resolveTextureBinding = [this](std::string_view reference) {
        const RID handle = TEXTURE_MANAGER.resolveReference(reference);
        const Texture* texture = TEXTURE_MANAGER.find(handle);
        if (!texture)
            return TextureBinding{};
        return TextureBinding{texture->defaultView(),
                              Sampler::resolve(*device_, texture->defaultSamplerDesc())};
    };
    std::vector<TextureBinding> resolvedTextures;
    resolvedTextures.reserve(kMaxGlobalTextures);
    for (const ShaderPropertyDesc& property : globals.textureProperties()) {
        if (resolvedTextures.size() >= kMaxGlobalTextures)
            break;
        const std::string* reference = globals.findTexture(property.name);
        TextureBinding texture = resolveTextureBinding(reference ? *reference : "");
        if (!texture) {
            Log::error("GlobalUniformGpuManager",
                       "Global texture is unavailable: %s",
                       property.name.c_str());
            texture = resolveTextureBinding("");
        }
        resolvedTextures.push_back(texture);
    }
    const TextureBinding defaultTexture = resolveTextureBinding("");
    while (resolvedTextures.size() < kMaxGlobalTextures)
        resolvedTextures.push_back(defaultTexture);

    const bool layoutChanged = lastLayoutVersion_ != globals.version();
    const bool textureBindingsChanged = frame.textureBindings != resolvedTextures;
    const bool bufferTooSmall = frame.uniformSize < byteSize;
    const bool needsRebuild = layoutChanged || textureBindingsChanged || bufferTooSmall ||
                              !frame.uniformBuffer || !frame.bindGroup;

    if (needsRebuild) {
        if (frame.bindGroup) {
            device_->destroyBindGroup(frame.bindGroup);
            frame.bindGroup = {};
        }
        if (bufferTooSmall && frame.uniformBuffer) {
            device_->destroyBuffer(frame.uniformBuffer);
            frame.uniformBuffer = {};
            frame.uniformSize = 0;
        }
        if (!frame.uniformBuffer) {
            frame.uniformBuffer = device_->createBuffer({
                .size = byteSize,
                .usage = rhi::BufferUsage::Uniform,
                .memoryUsage = rhi::MemoryUsage::Upload,
                .debugName = "Global uniforms",
            });
            frame.uniformSize = byteSize;
        }
    }

    if (frame.uniformVersion != globals.version() || needsRebuild) {
        if (!globals.uniformBytes().empty())
            device_->uploadBuffer(frame.uniformBuffer, globals.uniformBytes());
        frame.uniformVersion = globals.version();
    }

    if (needsRebuild) {
        std::vector<rhi::BindGroupEntry> bindings;
        bindings.reserve(kMaxGlobalTextures + 1);
        bindings.push_back({
            .binding = 0,
            .type = rhi::BindingType::UniformBuffer,
            .buffer = frame.uniformBuffer,
            .size = byteSize,
        });

        std::uint32_t binding = 1;
        for (const TextureBinding& texture : resolvedTextures) {
            const rhi::TextureBinding resolved = texture.toRhi();
            bindings.push_back({
                .binding = binding++,
                .type = rhi::BindingType::SampledTexture,
                .textureView = resolved.view,
                .sampler = resolved.sampler,
            });
        }

        frame.bindGroup =
            device_->createBindGroup({layout_, bindings, "Global uniform bind group"});
        frame.textureBindings = std::move(resolvedTextures);
        lastLayoutVersion_ = globals.version();
    }
}

void GlobalUniformGpuManager::shutdown() {
    if (!initialized())
        return;
    for (FrameResources& frame : frames_) {
        if (frame.bindGroup)
            device_->destroyBindGroup(frame.bindGroup);
        if (frame.uniformBuffer)
            device_->destroyBuffer(frame.uniformBuffer);
        frame = {};
    }
    if (layout_)
        device_->destroyBindGroupLayout(layout_);
    layout_ = {};
    device_ = nullptr;
    lastLayoutVersion_ = 0;
}

} // namespace engine
