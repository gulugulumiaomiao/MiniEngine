#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "render/shader/Shader.h" // ShaderValue
#include "render/uniform/Ubo.h"
#include "render/texture/Sampler.h"
#include "render/uniform/UniformResource.h"
#include "render/uniform/UniformSetLayout.h"
#include "rhi/api/ResourceDesc.h" // rhi::RID, DescriptorWrite, ResourceType

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

// A concrete descriptor set: one resource slot per binding of its UniformSetLayout, plus the
// descriptor-set RID the render layer binds. Uniform-buffer bindings are populated with a Ubo at
// construction (from the matching UboLayout); texture/sampler bindings start empty and are filled
// via setResource().
//
// Name lookup (resourceMap_) covers both cases: a non-UBO binding is keyed by its reflected binding
// name, and a UBO binding contributes each of its member names, all mapping to the slot position.
// setUniform() therefore reaches a UBO member by name; setResource() replaces a texture/sampler
// slot.
class UniformSet final : public RefCounted {
public:
    // `uboLayouts` supplies the block layout for each uniform-buffer binding, in ascending binding
    // order (non-UBO bindings are skipped, so it is indexed by the Nth UBO binding).
    UniformSet(Ref<UniformSetLayout> layout, std::vector<Ref<UboLayout>> uboLayouts);
    ~UniformSet() override;
    UniformSet(const UniformSet&) = delete;
    UniformSet& operator=(const UniformSet&) = delete;

    // Writes a scalar/vector/matrix value into the named UBO member. Returns false if the name is
    // unknown or does not resolve to a UBO slot (delegates the type check to Ubo::setUniform).
    [[nodiscard]] bool setUniform(std::string_view name, const ShaderValue& value);
    // Assigns a texture/sampler/UBO resource to the named binding. The accepted resource kind is
    // dictated by the UniformSetLayout-declared descriptor type; a mismatch returns false. Marks
    // the set dirty (descriptors are rewritten on the next upload) on success.
    [[nodiscard]] bool setResource(std::string_view name, Ref<UniformResource> resource);
    // Positional variant (binding slot index). For owners that address slots by index rather than
    // by reflected name — robust against layout dedup returning an instance whose binding names
    // came from a different acquirer.
    [[nodiscard]] bool setResourceAt(std::uint32_t binding, Ref<UniformResource> resource);
    // Rewrites descriptors when dirty, then uploads every UBO's buffer (each Ubo tracks its own
    // dirty flag, so UBO value changes need no descriptor rewrite — the buffer RID is stable).
    bool setSamplerOverrideAt(std::uint32_t position, Ref<Sampler> sampler);
    void upload();

    [[nodiscard]] rhi::RID descriptorSet() const { return descriptorSet_; }
    [[nodiscard]] const Ref<UniformSetLayout>& layout() const { return layout_; }
    [[nodiscard]] const std::vector<Ref<UniformResource>>& resources() const { return resources_; }
    // The Ubo occupying a binding slot (by position in layout()->bindings()), or nullptr when the
    // slot is empty or is not a uniform buffer. Lets an owner bulk-write a pre-packed block blob.
    [[nodiscard]] Ubo* uboAt(std::uint32_t binding) const;

private:
    std::uint64_t deviceUid_{};
    Ref<UniformSetLayout> layout_;
    std::vector<Ref<Sampler>> samplerOverrides_;
    std::vector<Ref<UniformResource>> resources_; // parallel to layout_->bindings()
    std::unordered_map<std::string, std::uint32_t> resourceMap_;
    rhi::RID descriptorSet_;
    bool dirty_{true};
};

} // namespace engine
