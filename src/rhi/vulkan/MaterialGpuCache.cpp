#include "rhi/vulkan/MaterialGpuCache.h"

#include "core/Log.h"
#include "renderer/Material.h"
#include "rhi/vulkan/Buffer.h"
#include "rhi/vulkan/DescriptorAllocator.h"

#include <algorithm>

namespace engine {

MaterialGpuCache::MaterialGpuCache(VkDevice device, VmaAllocator allocator,
                                   VkDescriptorSetLayout layout,
                                   std::uint32_t frameCount)
    : device_(device), allocator_(allocator), layout_(layout), frames_(frameCount) {}

MaterialGpuCache::~MaterialGpuCache() = default;

std::uint64_t MaterialGpuCache::key(MaterialHandle handle) {
    return (static_cast<std::uint64_t>(handle.generation) << 32U) | handle.index;
}

void MaterialGpuCache::beginFrame(std::uint32_t frameIndex,
                                  DescriptorAllocator& descriptors,
                                  std::uint32_t descriptorGeneration) {
    if (frameIndex >= frames_.size()) {
        Log::fatal("MaterialGpuCache", "Invalid frame index");
    }
    currentFrame_ = frameIndex;
    Frame& frame = frames_[frameIndex];
    frame.descriptors = &descriptors;
    frame.generation = descriptorGeneration;
    frame.used = 0;
    frame.materialEntries.clear();
}

rhi::BindGroupHandle MaterialGpuCache::prepare(MaterialHandle handle,
                                                const Material& material) {
    Frame& frame = frames_[currentFrame_];
    if (!frame.descriptors) {
        Log::fatal("MaterialGpuCache", "beginFrame must be called first");
    }
    const std::uint64_t materialKey = key(handle);
    if (const auto found = frame.materialEntries.find(materialKey);
        found != frame.materialEntries.end()) {
        return {found->second + 1U, frame.generation};
    }
    const std::uint32_t index = frame.used++;
    if (index == frame.entries.size()) frame.entries.emplace_back();
    Entry& entry = frame.entries[index];
    const VkDeviceSize byteSize = std::max<std::size_t>(16, material.uniformBytes().size());
    if (!entry.uniformBuffer || entry.uniformBuffer->size() < byteSize) {
        entry.uniformBuffer = std::make_unique<Buffer>(
            allocator_, byteSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    }
    if (!material.uniformBytes().empty()) {
        entry.uniformBuffer->upload(material.uniformBytes());
    }
    entry.descriptor = frame.descriptors->allocate(layout_);
    entry.material = handle;
    entry.materialVersion = material.version();
    VkDescriptorBufferInfo bufferInfo{entry.uniformBuffer->handle(), 0, byteSize};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = entry.descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    std::uint32_t textureBinding = 1;
    for (const ShaderPropertyDesc& property : material.shader().properties()) {
        if (property.type != ShaderPropertyType::Texture2D) continue;
        const auto found = material.textures.find(property.name);
        const std::string path = found == material.textures.end()
                                     ? std::string{}
                                     : found->second;
        if (!textureResolver_) {
            Log::warn("MaterialGpuCache",
                      "Texture resolver is not installed; skipping %s (%s)",
                      property.name.c_str(), path.c_str());
            ++textureBinding;
            continue;
        }
        const std::optional<VkDescriptorImageInfo> image = textureResolver_(path);
        if (!image) {
            Log::warn("MaterialGpuCache", "Texture is not ready: %s",
                      path.c_str());
            ++textureBinding;
            continue;
        }
        VkWriteDescriptorSet textureWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        textureWrite.dstSet = entry.descriptor;
        textureWrite.dstBinding = textureBinding++;
        textureWrite.descriptorCount = 1;
        textureWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureWrite.pImageInfo = &*image;
        vkUpdateDescriptorSets(device_, 1, &textureWrite, 0, nullptr);
    }
    frame.materialEntries.emplace(materialKey, index);
    return {index + 1U, frame.generation};
}

VkDescriptorSet MaterialGpuCache::resolve(rhi::BindGroupHandle handle,
                                          std::uint32_t frameIndex) const {
    if (frameIndex >= frames_.size() || handle.index == 0) {
        Log::fatal("MaterialGpuCache", "Invalid material bind group handle");
    }
    const Frame& frame = frames_[frameIndex];
    const std::uint32_t index = handle.index - 1U;
    if (handle.generation != frame.generation || index >= frame.used ||
        frame.entries[index].descriptor == VK_NULL_HANDLE) {
        Log::fatal("MaterialGpuCache", "Stale material bind group handle");
    }
    return frame.entries[index].descriptor;
}

void MaterialGpuCache::clear() {
    for (Frame& frame : frames_) {
        frame.entries.clear();
        frame.materialEntries.clear();
        frame.descriptors = nullptr;
        frame.used = 0;
    }
}

} // namespace engine
