#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine::rhi::vulkan {

// Number of sets a freshly opened descriptor pool is sized for. A pool's per-type capacity is
// (one set's per-type count) * kDescriptorSetsPerPool, so it holds exactly this many sets of the
// bucket's profile before the device must open another pool.
inline constexpr std::uint32_t kDescriptorSetsPerPool = 32;

// A descriptor set allocated from a bucketed pool, plus the bookkeeping needed to free it back to
// the exact pool it came from. Stored by value in the device's descriptor-set handle pool.
struct VulkanDescriptorSet {
    VkDescriptorSet set{VK_NULL_HANDLE};
    std::uint64_t unorderedHash{}; // bucket key (engine-layer UniformSetLayout unordered hash)
    std::uint32_t poolId{};        // pool within the bucket (monotonic per bucket, never reused)
};

// One VkDescriptorPool inside a bucket. Vulkan cannot query a pool's remaining capacity, so the
// pool records the per-set descriptor profile it was sized for; a new set joins this pool only if
// its profile matches exactly and allocatedSets < maxSets, which together guarantee that per-type
// capacity is never exceeded (capacity == perSetProfile * maxSets).
struct VulkanDescriptorPool {
    VkDescriptorPool pool{VK_NULL_HANDLE};
    std::uint32_t id{};
    std::uint32_t maxSets{};
    std::uint32_t allocatedSets{};
    std::vector<VkDescriptorPoolSize> perSetProfile; // per-type counts one set consumes
};

// All pools that share one unordered hash, plus the monotonic id source for that bucket. When the
// last pool is freed the bucket is erased from the device map.
struct DescriptorPoolBucket {
    std::vector<VulkanDescriptorPool> pools;
    std::uint32_t nextId{1};
};

} // namespace engine::rhi::vulkan
