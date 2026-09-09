#pragma once

#include "render/renderer/RenderResources.h"
#include "rhi/api/ResourceDesc.h"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace engine {

namespace rhi {
class IDevice;
}

class Texture;

class TextureGpuCache final {
public:
    explicit TextureGpuCache(rhi::IDevice& device);
    ~TextureGpuCache();

    [[nodiscard]] std::optional<rhi::TextureBinding> prepare(TextureHandle handle,
                                                             Texture& texture);
    void invalidate(TextureHandle handle);
    void clear();
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        rhi::TextureHandle texture;
        rhi::TextureViewHandle view;
        std::uint64_t textureVersion{};
    };

    [[nodiscard]] static std::uint64_t key(TextureHandle handle);
    void destroy(Entry& entry);

    rhi::IDevice& device_;
    rhi::SamplerHandle sampler_;
    std::unordered_map<std::uint64_t, Entry> entries_;
};

} // namespace engine
