#pragma once

#include "core/base/RefCounted.h"
#include "rhi/api/ResourceDesc.h" // rhi::ResourceType

namespace engine {

// The kind of resource a UniformResource is, used by UniformSet to validate that a resource
// matches the binding slot it is being assigned to.
enum class UniformResourceType { Ubo, Texture, Sampler, StorageBuffer };

// Common base for everything that can occupy a descriptor-set binding slot: Ubo (uniform
// buffer), Texture (combined image sampler), Sampler (plain sampler) and StorageBufferResource
// (a raw storage buffer). Derives from RefCounted so a UniformSet can hold heterogeneous bindings
// as Ref<UniformResource>; concrete subtypes are single-inheritance, so there is no diamond.
//
// This header intentionally depends only on RefCounted and the backend-agnostic rhi::ResourceType:
// it never includes Texture.h/Sampler.h/Ubo.h, so those layer-2 headers can include it without a
// cycle.
class UniformResource : public RefCounted {
public:
    ~UniformResource() override = default;

    // Which concrete kind this resource is (for UniformSet slot validation).
    [[nodiscard]] virtual UniformResourceType resourceType() const = 0;
    // The descriptor type this resource writes into its binding; UniformSet assembles the
    // DescriptorWrite from it (CombinedImageSampler reads a view+sampler, Sampler reads a sampler,
    // UniformBuffer/StorageBuffer read a buffer range).
    [[nodiscard]] virtual rhi::ResourceType bindingType() const = 0;
};

// A raw storage-buffer binding (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER): wraps a device buffer RID and
// the byte range to bind. Unlike Ubo it has no reflected member layout — the CPU/GPU payload is
// managed by its owner (e.g. the per-frame object-transform SSBO and instance table), which only
// needs the buffer re-bound here. Non-owning: the buffer's lifetime belongs to its producer.
class StorageBufferResource final : public UniformResource {
public:
    StorageBufferResource(rhi::RID buffer, std::uint64_t size) : buffer_(buffer), size_(size) {}

    [[nodiscard]] UniformResourceType resourceType() const override {
        return UniformResourceType::StorageBuffer;
    }
    [[nodiscard]] rhi::ResourceType bindingType() const override {
        return rhi::ResourceType::StorageBuffer;
    }

    void setBuffer(rhi::RID buffer, std::uint64_t size) {
        buffer_ = buffer;
        size_ = size;
    }
    [[nodiscard]] rhi::RID buffer() const { return buffer_; }
    [[nodiscard]] std::uint64_t size() const { return size_; }

private:
    rhi::RID buffer_;
    std::uint64_t size_{};
};

} // namespace engine
