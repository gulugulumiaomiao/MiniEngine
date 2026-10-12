#include "render/uniform/UniformSet.h"

#include "core/logging/Log.h"
#include "render/texture/Sampler.h"
#include "render/texture/Texture.h"
#include "rhi/api/Device.h"

#include <utility>

namespace engine {

UniformSet::UniformSet(Ref<UniformSetLayout> layout, std::vector<Ref<UboLayout>> uboLayouts)
    : layout_(std::move(layout)) {
    if (!layout_)
        Log::fatal("UniformSet", "Cannot build a uniform set without a layout");
    rhi::IDevice* device = RHI_DEVICE;
    if (!device)
        Log::fatal("UniformSet", "No active device for uniform-set creation");
    deviceUid_ = device->uid();
    const std::vector<ResourceBinding>& bindings = layout_->bindings();

    // Allocate the descriptor set from the pool bucketed by the layout's unordered hash.
    std::vector<rhi::ResourceBindingDesc> descs;
    descs.reserve(bindings.size());
    for (const ResourceBinding& binding : bindings)
        descs.push_back({binding.binding, binding.name, binding.type});
    descriptorSet_ = device->descriptor_set_allocate(
        rhi::DescriptorSetAllocateInfo{layout_->rid(), layout_->unorderedHash(), descs, 1});

    // One resource slot per binding; create a Ubo for each uniform/storage-buffer binding.
    resources_.assign(bindings.size(), {});
    std::size_t uboIndex = 0;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const ResourceBinding& binding = bindings[i];
        const std::uint32_t pos = static_cast<std::uint32_t>(i);
        const bool isUniformBuffer = binding.type == rhi::ResourceType::UniformBuffer;
        if (isUniformBuffer) {
            if (uboIndex < uboLayouts.size() && uboLayouts[uboIndex]) {
                Ref<Ubo> ubo = makeRef<Ubo>(uboLayouts[uboIndex]);
                ubo->create();
                // Every member name of this block resolves to this slot.
                for (const UboUniform& member : uboLayouts[uboIndex]->uniforms())
                    resourceMap_.emplace(member.name, pos);
                resources_[i] = Ref<UniformResource>(std::move(ubo));
            }
            ++uboIndex;
        } else {
            // Storage buffer / texture / sampler binding: keyed by its reflected name, filled later
            // via setResource (a raw SSBO has no reflected member layout, so it is not a Ubo).
            resourceMap_.emplace(binding.name, pos);
        }
    }
}

UniformSet::~UniformSet() {
    // Cross-device safety: only free the descriptor set if the creating device is still active.
    if (rhi::IDevice* device = RHI_DEVICE;
        descriptorSet_ && device && device->uid() == deviceUid_) {
        device->descriptor_set_destroy(descriptorSet_);
    }
    descriptorSet_ = {};
}

bool UniformSet::setUniform(std::string_view name, const ShaderValue& value) {
    const auto found = resourceMap_.find(std::string(name));
    if (found == resourceMap_.end())
        return false;
    const Ref<UniformResource>& resource = resources_[found->second];
    if (!resource || resource->resourceType() != UniformResourceType::Ubo)
        return false;
    return static_cast<Ubo*>(resource.get())->setUniform(name, value);
}

bool UniformSet::setResource(std::string_view name, Ref<UniformResource> resource) {
    const auto found = resourceMap_.find(std::string(name));
    if (found == resourceMap_.end())
        return false;
    return setResourceAt(found->second, std::move(resource));
}

bool UniformSet::setResourceAt(std::uint32_t binding, Ref<UniformResource> resource) {
    if (!resource || binding >= resources_.size())
        return false;
    // The accepted kind is dictated by the layout-declared descriptor type for this slot.
    const rhi::ResourceType declared = layout_->bindings()[binding].type;
    UniformResourceType expected;
    switch (declared) {
    case rhi::ResourceType::UniformBuffer: expected = UniformResourceType::Ubo; break;
    case rhi::ResourceType::StorageBuffer: expected = UniformResourceType::StorageBuffer; break;
    case rhi::ResourceType::CombinedImageSampler:
    case rhi::ResourceType::SampledImage:
    case rhi::ResourceType::InputAttachment:
    case rhi::ResourceType::StorageImage: expected = UniformResourceType::Texture; break;
    case rhi::ResourceType::Sampler: expected = UniformResourceType::Sampler; break;
    }
    if (resource->resourceType() != expected)
        return false;
    resources_[binding] = std::move(resource);
    dirty_ = true;
    return true;
}

Ubo* UniformSet::uboAt(std::uint32_t binding) const {
    if (binding >= resources_.size())
        return nullptr;
    const Ref<UniformResource>& resource = resources_[binding];
    if (!resource || resource->resourceType() != UniformResourceType::Ubo)
        return nullptr;
    return static_cast<Ubo*>(resource.get());
}

bool UniformSet::setSamplerOverrideAt(std::uint32_t position, Ref<Sampler> sampler) {
    if (position >= resources_.size())
        return false;
    samplerOverrides_.resize(resources_.size());
    if (samplerOverrides_[position] != sampler) {
        samplerOverrides_[position] = std::move(sampler);
        dirty_ = true;
    }
    return true;
}

void UniformSet::upload() {
    if (dirty_ && descriptorSet_) {
        const std::vector<ResourceBinding>& bindings = layout_->bindings();
        std::vector<rhi::DescriptorWrite> writes;
        writes.reserve(resources_.size());
        for (std::size_t i = 0; i < resources_.size(); ++i) {
            const Ref<UniformResource>& resource = resources_[i];
            if (!resource)
                continue; // unpopulated texture/sampler slot: skip (partial writes are allowed)
            rhi::DescriptorWrite write;
            write.binding = bindings[i].binding;
            write.type = bindings[i].type;
            switch (resource->resourceType()) {
            case UniformResourceType::Ubo:
                write.buffer = static_cast<const Ubo*>(resource.get())->buffer();
                write.offset = 0;
                write.size = 0; // whole buffer
                break;
            case UniformResourceType::StorageBuffer: {
                const auto* ssbo = static_cast<const StorageBufferResource*>(resource.get());
                write.buffer = ssbo->buffer();
                write.offset = 0;
                write.size = ssbo->size();
                break;
            }
            case UniformResourceType::Texture: {
                const rhi::TextureBinding binding =
                    static_cast<const Texture*>(resource.get())
                        ->binding(i < samplerOverrides_.size() ? samplerOverrides_[i]
                                                               : Ref<Sampler>{});
                write.textureView = binding.view;
                write.sampler = binding.sampler;
                break;
            }
            case UniformResourceType::Sampler:
                write.sampler = static_cast<const Sampler*>(resource.get())->rhiHandle();
                break;
            }
            writes.push_back(write);
        }
        if (rhi::IDevice* device = RHI_DEVICE; !writes.empty() && device)
            device->descriptor_set_update(descriptorSet_, writes);
        dirty_ = false;
    }
    // Push UBO contents; each Ubo decides whether it actually needs a buffer upload.
    for (const Ref<UniformResource>& resource : resources_)
        if (resource && resource->resourceType() == UniformResourceType::Ubo)
            static_cast<Ubo*>(resource.get())->upload();
}

} // namespace engine
