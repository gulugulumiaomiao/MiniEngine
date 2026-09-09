#pragma once

#include "core/base/Singleton.h"
#include "render/texture/Texture.h"
#include "rhi/api/ResourceDesc.h"

#include <memory>
#include <optional>
#include <string_view>

namespace engine {

class SamplerGpuFactory;
class TextureGpuFactory;

namespace rhi {
class IDevice;
}

class TextureGpuManager final : public Singleton<TextureGpuManager> {
public:
    ~TextureGpuManager();

    [[nodiscard]] bool initialize(rhi::IDevice& device);
    [[nodiscard]] std::optional<rhi::TextureBinding> resolve(TextureHandle handle);
    [[nodiscard]] std::optional<rhi::TextureBinding> resolveReference(std::string_view reference);
    void invalidate(TextureHandle handle);
    void shutdown();
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

private:
    friend class Singleton<TextureGpuManager>;
    TextureGpuManager();

    rhi::IDevice* device_{};
    std::unique_ptr<TextureGpuFactory> textureFactory_;
    std::unique_ptr<SamplerGpuFactory> samplerFactory_;
    rhi::SamplerHandle defaultSampler_;
};

} // namespace engine

#define TEXTURE_GPU_MANAGER (::engine::TextureGpuManager::instance())
