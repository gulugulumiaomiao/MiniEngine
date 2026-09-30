#pragma once

#include "core/base/RefCounted.h"
#include "rhi/api/ResourceDesc.h" // rhi::ResourceType

namespace engine {

// The kind of resource a UniformResource is, used by UniformSet to validate that a resource
// matches the binding slot it is being assigned to.
enum class UniformResourceType { Ubo, Texture, Sampler };

// Common base for everything that can occupy a descriptor-set binding slot: Ubo (uniform /
// storage buffer), Texture (combined image sampler) and Sampler (plain sampler). Derives from
// RefCounted so a UniformSet can hold heterogeneous bindings as Ref<UniformResource>; concrete
// subtypes are single-inheritance (Ubo/Texture/Sampler : UniformResource), so there is no diamond.
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

} // namespace engine
