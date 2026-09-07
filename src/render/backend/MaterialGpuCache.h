#pragma once

#include "render/renderer/RenderResources.h"
#include "rhi/api/Device.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine {

class Material;

class MaterialGpuCache final {
    public:
    using TextureResolver = std::function<std::optional<rhi::TextureBinding>(std::string_view)>;

    MaterialGpuCache(rhi::IDevice& device, rhi::BindGroupLayoutHandle layout,
                     std::uint32_t frameCount);
    ~MaterialGpuCache();

    void setTextureResolver(TextureResolver resolver) { textureResolver_ = std::move(resolver); }
    void beginFrame(std::uint32_t frameIndex);
    [[nodiscard]] rhi::BindGroupHandle prepare(MaterialHandle handle, const Material& material);
    void clear();

    private:
    struct Entry {
        rhi::BufferHandle uniformBuffer;
        std::uint64_t uniformCapacity{};
        rhi::BindGroupHandle bindGroup;
    };
    struct Frame {
        std::vector<Entry> entries;
        std::unordered_map<std::uint64_t, std::uint32_t> materialEntries;
        std::uint32_t used{};
    };

    [[nodiscard]] static std::uint64_t key(MaterialHandle handle);
    void release(Entry& entry);

    rhi::IDevice& device_;
    rhi::BindGroupLayoutHandle layout_;
    std::vector<Frame> frames_;
    std::uint32_t currentFrame_{};
    TextureResolver textureResolver_;
};

} // namespace engine
