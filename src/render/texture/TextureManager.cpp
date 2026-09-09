#include "render/texture/TextureManager.h"

#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"

#include <limits>
#include <array>
#include <utility>

namespace engine {

TextureHandle TextureManager::load(const VirtualPath& texturePath) {
    if (!texturePath.valid()) {
        Log::error("TextureManager", "Invalid Texture path: %s", texturePath.string().c_str());
        return {};
    }
    if (const TextureHandle existing = handleFor(texturePath); existing)
        return existing;
    const std::shared_ptr<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(texturePath);
    return asset ? insert(asset->instantiate()) : TextureHandle{};
}

TextureHandle TextureManager::createBuiltin(const VirtualPath& path,
                                            std::uint32_t width,
                                            std::uint32_t height,
                                            std::span<const std::byte> pixels,
                                            TextureColorSpace colorSpace) {
    Texture texture;
    texture.assetPath_ = path;
    texture.desc_ = {TextureType::Texture2D,
                     colorSpace == TextureColorSpace::Srgb ? TextureFormat::Rgba8Srgb
                                                           : TextureFormat::Rgba8Unorm,
                     colorSpace,
                     width,
                     height,
                     1};
    texture.mipData_.push_back({width, height, {pixels.begin(), pixels.end()}});
    return insert(std::move(texture));
}

TextureHandle TextureManager::defaultWhite() {
    if (!find(defaultWhite_)) {
        constexpr std::array pixels{
            std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        defaultWhite_ = createBuiltin(
            VirtualPath{"builtin://textures/white"}, 1, 1, pixels, TextureColorSpace::Srgb);
    }
    return defaultWhite_;
}

TextureHandle TextureManager::defaultBlack() {
    if (!find(defaultBlack_)) {
        constexpr std::array pixels{
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xff}};
        defaultBlack_ = createBuiltin(
            VirtualPath{"builtin://textures/black"}, 1, 1, pixels, TextureColorSpace::Srgb);
    }
    return defaultBlack_;
}

TextureHandle TextureManager::defaultNormal() {
    if (!find(defaultNormal_)) {
        constexpr std::array pixels{
            std::byte{0x80}, std::byte{0x80}, std::byte{0xff}, std::byte{0xff}};
        defaultNormal_ = createBuiltin(
            VirtualPath{"builtin://textures/normal"}, 1, 1, pixels, TextureColorSpace::Linear);
    }
    return defaultNormal_;
}

TextureHandle TextureManager::errorTexture() {
    if (!find(errorTexture_)) {
        constexpr std::array pixels{std::byte{0xff},
                                    std::byte{0x00},
                                    std::byte{0xff},
                                    std::byte{0xff},
                                    std::byte{0x00},
                                    std::byte{0x00},
                                    std::byte{0x00},
                                    std::byte{0xff},
                                    std::byte{0x00},
                                    std::byte{0x00},
                                    std::byte{0x00},
                                    std::byte{0xff},
                                    std::byte{0xff},
                                    std::byte{0x00},
                                    std::byte{0xff},
                                    std::byte{0xff}};
        errorTexture_ = createBuiltin(
            VirtualPath{"builtin://textures/error"}, 2, 2, pixels, TextureColorSpace::Srgb);
    }
    return errorTexture_;
}

bool TextureManager::replace(TextureHandle handle, Texture texture) {
    Texture* current = find(handle);
    if (!current || current->assetPath() != texture.assetPath() || !validate(texture)) {
        Log::error("TextureManager", "Cannot replace an invalid Texture");
        return false;
    }
    if (current->version_ == std::numeric_limits<std::uint64_t>::max()) {
        Log::error("TextureManager", "Texture version overflow");
        return false;
    }
    texture.version_ = current->version_ + 1;
    texture.dirty_ = true;
    *current = std::move(texture);
    return true;
}

bool TextureManager::replace(const VirtualPath& texturePath) {
    const TextureHandle handle = handleFor(texturePath);
    if (!handle)
        return true;
    const std::shared_ptr<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(texturePath);
    return asset && replace(handle, asset->instantiate());
}

bool TextureManager::validate(const Texture& texture) const {
    return texture.assetPath().valid() && validateTexture(texture.desc(), texture.mipData());
}

void TextureManager::clear() {
    defaultWhite_ = {};
    defaultBlack_ = {};
    defaultNormal_ = {};
    errorTexture_ = {};
    InstanceManager::clear();
}

} // namespace engine
