#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "core/math/hash.h"
#include "rhi/api/ResourceDesc.h" // rhi::RID, rhi::ResourceType

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

// One binding slot of a uniform set, as reflected from SPIR-V: its binding index, reflected name
// and descriptor type.
struct ResourceBinding {
    std::uint32_t binding{};
    std::string name;
    rhi::ResourceType type{rhi::ResourceType::UniformBuffer};
};

// The engine-layer view of a VkDescriptorSetLayout: an ordered array of ResourceBinding plus the
// layout RID that the render layer binds pipelines against. Built by reflection through the
// two-step recording flow (constructor reserves the RID, begin/addBinding record slots, end sorts,
// hashes and builds the GPU layout).
//
// Two hashes characterize a layout:
//  - orderedHash: binding index + descriptor type in ascending binding order. It is the identity
//    used by the shared cache pool (below) and feeds PipelineLayout's hash.
//  - unorderedHash: only the per-descriptor-type counts, independent of binding order/index. It is
//    handed to the device to bucket descriptor pools (sets with equal type counts share a pool).
//
// A process-wide cache (orderedHash -> instance, keyed by device uid) deduplicates layouts so every
// shader reflecting the same set shares one UniformSetLayout (and thus one VkDescriptorSetLayout),
// which is what makes cross-pipeline descriptor-set sharing valid.
class UniformSetLayout final : public RefCounted {
public:
    // Dedup entry point: returns the shared instance for this binding set, creating it on a miss.
    [[nodiscard]] static Ref<UniformSetLayout> acquire(rhi::IDevice& device,
                                                       std::span<const ResourceBinding> bindings);
    static void clearCache();

    explicit UniformSetLayout(rhi::IDevice& device); // step 1: reserve the layout RID only
    ~UniformSetLayout() override;                    // destroy the VkDescriptorSetLayout + recycle RID
    UniformSetLayout(const UniformSetLayout&) = delete;
    UniformSetLayout& operator=(const UniformSetLayout&) = delete;

    void begin();                          // step 2: clear any recorded bindings
    void addBinding(ResourceBinding binding);
    void end();                            // step 3: sort, hash and build the VkDescriptorSetLayout

    [[nodiscard]] rhi::RID rid() const { return rid_; }
    [[nodiscard]] Hash64 orderedHash() const { return orderedHash_; }
    [[nodiscard]] Hash64 unorderedHash() const { return unorderedHash_; }
    [[nodiscard]] const std::vector<ResourceBinding>& bindings() const { return bindings_; }

private:
    rhi::IDevice* device_{};
    std::uint64_t deviceUid_{};
    rhi::RID rid_;
    std::vector<ResourceBinding> bindings_;
    Hash64 orderedHash_{};
    Hash64 unorderedHash_{};

    static std::unordered_map<Hash64, Ref<UniformSetLayout>> cache_;
    static std::uint64_t cacheDeviceUid_;
};

} // namespace engine
