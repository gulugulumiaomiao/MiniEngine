#include "render/backend/MaterialGpuCache.h"

#include "core/logging/Log.h"
#include "render/material/Material.h"

#include <algorithm>
#include <span>
#include <string>

namespace engine {

MaterialGpuCache::MaterialGpuCache(rhi::IDevice& device, rhi::BindGroupLayoutHandle layout,
                                   std::uint32_t frameCount)
    : device_(device), layout_(layout), frames_(frameCount) {
}

MaterialGpuCache::~MaterialGpuCache() {
    clear();
}

std::uint64_t MaterialGpuCache::key(MaterialHandle handle) {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) | handle.index;
}

void MaterialGpuCache::beginFrame(std::uint32_t frameIndex) {
    if (frameIndex >= frames_.size()) {
        Log::fatal("MaterialGpuCache", "Invalid frame index");
    }
    currentFrame_ = frameIndex;
    Frame& frame = frames_[frameIndex];
    frame.used = 0;
    frame.materialEntries.clear();
}

rhi::BindGroupHandle MaterialGpuCache::prepare(MaterialHandle handle, const Material& material) {
    Frame& frame = frames_[currentFrame_];
    const std::uint64_t materialKey = key(handle);
    if (const auto found = frame.materialEntries.find(materialKey);
        found != frame.materialEntries.end()) {
        return frame.entries[found->second].bindGroup;
    }

    const std::uint32_t index = frame.used++;
    if (index == frame.entries.size())
        frame.entries.emplace_back();
    Entry& entry = frame.entries[index];
    if (entry.bindGroup) {
        device_.destroyBindGroup(entry.bindGroup);
        entry.bindGroup = {};
    }

    const std::uint64_t byteSize = std::max<std::size_t>(16, material.uniformBytes().size());
    if (!entry.uniformBuffer || entry.uniformCapacity < byteSize) {
        if (entry.uniformBuffer)
            device_.destroyBuffer(entry.uniformBuffer);
        entry.uniformBuffer = device_.createBuffer({
            .size = byteSize,
            .usage = rhi::BufferUsage::Uniform,
            .memoryUsage = rhi::MemoryUsage::Upload,
            .debugName = "Material uniforms",
        });
        entry.uniformCapacity = byteSize;
    }
    if (!material.uniformBytes().empty()) {
        device_.uploadBuffer(entry.uniformBuffer, material.uniformBytes());
    }

    std::vector<rhi::BindGroupEntry> bindings{{
        .binding = 0,
        .type = rhi::BindingType::UniformBuffer,
        .buffer = entry.uniformBuffer,
        .size = byteSize,
    }};
    std::uint32_t textureBinding = 1;
    for (const ShaderPropertyDesc& property : material.shader().properties()) {
        if (property.type != ShaderPropertyType::Texture2D)
            continue;
        const auto found = material.textures.find(property.name);
        const std::string path = found == material.textures.end() ? std::string{} : found->second;
        if (!textureResolver_) {
            Log::warn("MaterialGpuCache", "Texture resolver is not installed; skipping %s (%s)",
                      property.name.c_str(), path.c_str());
            ++textureBinding;
            continue;
        }
        const std::optional<rhi::TextureBinding> texture = textureResolver_(path);
        if (!texture) {
            Log::warn("MaterialGpuCache", "Texture is not ready: %s", path.c_str());
            ++textureBinding;
            continue;
        }
        bindings.push_back({
            .binding = textureBinding++,
            .type = rhi::BindingType::SampledTexture,
            .textureView = texture->view,
            .sampler = texture->sampler,
        });
    }
    entry.bindGroup = device_.createBindGroup({layout_, bindings, "Material bind group"});
    frame.materialEntries.emplace(materialKey, index);
    return entry.bindGroup;
}

void MaterialGpuCache::release(Entry& entry) {
    if (entry.bindGroup)
        device_.destroyBindGroup(entry.bindGroup);
    if (entry.uniformBuffer)
        device_.destroyBuffer(entry.uniformBuffer);
    entry = {};
}

void MaterialGpuCache::clear() {
    for (Frame& frame : frames_) {
        for (Entry& entry : frame.entries)
            release(entry);
        frame.entries.clear();
        frame.materialEntries.clear();
        frame.used = 0;
    }
}

} // namespace engine
