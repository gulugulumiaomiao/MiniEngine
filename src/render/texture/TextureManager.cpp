#include "render/texture/TextureManager.h"

#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"

#include <limits>
#include <array>
#include <utility>

namespace engine {

TextureHandle TextureManager::load(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("TextureManager", "Invalid Texture AssetId");
        return errorTexture();
    }
    if (const TextureHandle existing = findHandle(assetId); existing) {
        return existing;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("TextureManager",
                   "Unknown Texture AssetId: %s",
                   assetId.toString().c_str());
        return errorTexture();
    }
    return loadFromPath(*path, assetId);
}

TextureHandle TextureManager::load(const VirtualPath& texturePath) {
    if (!texturePath.valid()) {
        Log::error("TextureManager",
                   "Invalid Texture path: %s",
                   texturePath.string().c_str());
        return errorTexture();
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    if (!assetId) {
        Log::error("TextureManager",
                   "Texture path has no AssetId: %s",
                   texturePath.string().c_str());
        return errorTexture();
    }
    if (const TextureHandle existing = findHandle(*assetId); existing) {
        return existing;
    }
    return loadFromPath(texturePath, *assetId);
}

TextureHandle TextureManager::loadFromPath(const VirtualPath& texturePath,
                                           const AssetId& assetId) {
    Log::info("Texture", "Loading texture: %s", texturePath.string().c_str());
    const std::shared_ptr<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(texturePath);
    if (!asset) {
        return errorTexture();
    }
    Texture texture = asset->instantiate();
    texture.assetId_ = assetId;
    return insert(std::move(texture));
}

TextureHandle TextureManager::clone(TextureHandle source) {
    Texture* texture = find(source);
    if (!texture) {
        Log::error("TextureManager", "Cannot clone an invalid Texture");
        return {};
    }
    return insertUnkeyed(texture->clone());
}

void TextureManager::refreshAsset(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("TextureManager", "Cannot refresh an invalid AssetId");
        return;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("TextureManager",
                   "Unknown Texture AssetId: %s",
                   assetId.toString().c_str());
        return;
    }
    const std::shared_ptr<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(*path);
    if (!asset) {
        Log::error(
            "TextureManager", "Failed to reload texture asset: %s", path->string().c_str());
        return;
    }
    forEach([&assetId, &asset](Texture& texture) {
        if (texture.assetId() == assetId) {
            texture.rebuildFromAsset(*asset);
        }
    });
}

void TextureManager::refreshAsset(const VirtualPath& texturePath) {
    if (!texturePath.valid()) {
        Log::error("TextureManager",
                   "Invalid Texture path: %s",
                   texturePath.string().c_str());
        return;
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    if (!assetId) {
        Log::error("TextureManager",
                   "Texture path has no AssetId: %s",
                   texturePath.string().c_str());
        return;
    }
    refreshAsset(*assetId);
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
    return insertUnkeyed(std::move(texture));
}

TextureHandle TextureManager::defaultWhite() {
    if (!find(defaultWhite_)) {
        constexpr std::array pixels{
            std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        defaultWhite_ = createBuiltin(
            VirtualPath{"engine://textures/white"}, 1, 1, pixels, TextureColorSpace::Srgb);
    }
    return defaultWhite_;
}

TextureHandle TextureManager::defaultBlack() {
    if (!find(defaultBlack_)) {
        constexpr std::array pixels{
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xff}};
        defaultBlack_ = createBuiltin(
            VirtualPath{"engine://textures/black"}, 1, 1, pixels, TextureColorSpace::Srgb);
    }
    return defaultBlack_;
}

TextureHandle TextureManager::defaultNormal() {
    if (!find(defaultNormal_)) {
        constexpr std::array pixels{
            std::byte{0x80}, std::byte{0x80}, std::byte{0xff}, std::byte{0xff}};
        defaultNormal_ = createBuiltin(
            VirtualPath{"engine://textures/normal"}, 1, 1, pixels, TextureColorSpace::Linear);
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
            VirtualPath{"engine://textures/error"}, 2, 2, pixels, TextureColorSpace::Srgb);
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
    const AssetId assetId = current->assetId_;
    texture.version_ = current->version_ + 1;
    texture.dirty_ = true;
    *current = std::move(texture);
    current->assetId_ = assetId;
    return true;
}

bool TextureManager::replace(const VirtualPath& texturePath) {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    if (!assetId) {
        return true;
    }
    const TextureHandle handle = findHandle(*assetId);
    if (!handle) {
        return true;
    }
    const std::shared_ptr<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(texturePath);
    if (!asset) {
        return false;
    }
    Texture* current = find(handle);
    if (!current) {
        return false;
    }
    current->rebuildFromAsset(*asset);
    return true;
}

bool TextureManager::validate(const Texture& texture) const {
    return texture.assetPath().valid() && validateTexture(texture.desc(), texture.mipData());
}

void TextureManager::clear() {
    defaultWhite_ = {};
    defaultBlack_ = {};
    defaultNormal_ = {};
    errorTexture_ = {};
    KeyedHandleRegistry::clear();
}

} // namespace engine
