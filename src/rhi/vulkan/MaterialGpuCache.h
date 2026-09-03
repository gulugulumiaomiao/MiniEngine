#pragma once

#include "renderer/RenderResources.h"
#include "rhi/RhiTypes.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

class Buffer;
class DescriptorAllocator;
class Material;

class MaterialGpuCache final {
public:
    using TextureResolver =
        std::function<std::optional<VkDescriptorImageInfo>(std::string_view)>;

    MaterialGpuCache(VkDevice device, VmaAllocator allocator,
                     VkDescriptorSetLayout layout, std::uint32_t frameCount);
    ~MaterialGpuCache();
    void setTextureResolver(TextureResolver resolver) {
        textureResolver_ = std::move(resolver);
    }

    void beginFrame(std::uint32_t frameIndex, DescriptorAllocator& descriptors,
                    std::uint32_t descriptorGeneration);
    [[nodiscard]] rhi::BindGroupHandle prepare(MaterialHandle handle,
                                                const Material& material);
    [[nodiscard]] VkDescriptorSet resolve(rhi::BindGroupHandle handle,
                                          std::uint32_t frameIndex) const;
    void clear();

private:
    struct Entry {
        std::unique_ptr<Buffer> uniformBuffer;
        VkDescriptorSet descriptor{VK_NULL_HANDLE};
        MaterialHandle material;
        std::uint64_t materialVersion{};
    };
    struct Frame {
        std::vector<Entry> entries;
        std::unordered_map<std::uint64_t, std::uint32_t> materialEntries;
        DescriptorAllocator* descriptors{};
        std::uint32_t generation{};
        std::uint32_t used{};
    };

    [[nodiscard]] static std::uint64_t key(MaterialHandle handle);

    VkDevice device_{VK_NULL_HANDLE};
    VmaAllocator allocator_{VK_NULL_HANDLE};
    VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
    std::vector<Frame> frames_;
    std::uint32_t currentFrame_{};
    TextureResolver textureResolver_;
};

} // namespace engine
