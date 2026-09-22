#pragma once

#include "core/base/KeyedHandleRegistry.h"
#include "core/base/Singleton.h"
#include "render/texture/Texture.h"

#include <optional>
#include <string_view>

namespace engine {

namespace rhi {
class IDevice;
}

class TextureManager final : public Singleton<TextureManager>,
                             public KeyedHandleRegistry<Texture, RID, AssetId> {
public:
    [[nodiscard]] bool initialize(rhi::IDevice& device);
    void shutdown();
    [[nodiscard]] bool initialized() const { return device_ != nullptr; }

    [[nodiscard]] RID load(const AssetId& assetId);
    [[nodiscard]] RID load(const VirtualPath& texturePath);
    [[nodiscard]] RID resolveReference(std::string_view reference);
    [[nodiscard]] RID clone(RID source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& texturePath);

    [[nodiscard]] RID defaultWhite();
    [[nodiscard]] RID defaultBlack();
    [[nodiscard]] RID defaultNormal();
    [[nodiscard]] RID errorTexture();
    [[nodiscard]] bool replace(const VirtualPath& texturePath);
    void clear() override;

private:
    friend class Singleton<TextureManager>;
    TextureManager() = default;

    [[nodiscard]] AssetId keyOf(const Texture& texture) const override { return texture.assetId(); }
    [[nodiscard]] bool validate(const Texture& texture) const override;
    [[nodiscard]] RID loadFromPath(const VirtualPath& path, const AssetId& assetId);
    [[nodiscard]] std::optional<Texture> createTexture(VirtualPath path,
                                                       AssetId assetId,
                                                       TextureDesc desc,
                                                       std::vector<TextureMipData> mipData,
                                                       std::uint64_t version = 1);
    [[nodiscard]] RID createBuiltin(const VirtualPath& path,
                                              std::uint32_t width,
                                              std::uint32_t height,
                                              std::span<const std::byte> pixels,
                                              TextureColorSpace colorSpace);

    rhi::IDevice* device_{};
    RID defaultWhite_;
    RID defaultBlack_;
    RID defaultNormal_;
    RID errorTexture_;
};

} // namespace engine

#define TEXTURE_MANAGER (::engine::TextureManager::instance())
