#pragma once

#include "core/base/KeyedHandleRegistry.h"
#include "core/base/Singleton.h"
#include "render/texture/Texture.h"

namespace engine {

class TextureManager final : public Singleton<TextureManager>,
                             public KeyedHandleRegistry<Texture, TextureHandle, AssetId> {
public:
    [[nodiscard]] TextureHandle load(const AssetId& assetId);
    [[nodiscard]] TextureHandle load(const VirtualPath& texturePath);
    [[nodiscard]] TextureHandle clone(TextureHandle source);
    void refreshAsset(const AssetId& assetId);
    void refreshAsset(const VirtualPath& texturePath);

    [[nodiscard]] TextureHandle defaultWhite();
    [[nodiscard]] TextureHandle defaultBlack();
    [[nodiscard]] TextureHandle defaultNormal();
    [[nodiscard]] TextureHandle errorTexture();
    [[nodiscard]] bool replace(TextureHandle handle, Texture texture);
    [[nodiscard]] bool replace(const VirtualPath& texturePath);
    void clear() override;

private:
    friend class Singleton<TextureManager>;
    TextureManager() = default;

    [[nodiscard]] AssetId keyOf(const Texture& texture) const override {
        return texture.assetId();
    }
    [[nodiscard]] bool validate(const Texture& texture) const override;
    [[nodiscard]] TextureHandle loadFromPath(const VirtualPath& path, const AssetId& assetId);
    [[nodiscard]] TextureHandle createBuiltin(const VirtualPath& path,
                                              std::uint32_t width,
                                              std::uint32_t height,
                                              std::span<const std::byte> pixels,
                                              TextureColorSpace colorSpace);

    TextureHandle defaultWhite_;
    TextureHandle defaultBlack_;
    TextureHandle defaultNormal_;
    TextureHandle errorTexture_;
};

} // namespace engine

#define TEXTURE_MANAGER (::engine::TextureManager::instance())
