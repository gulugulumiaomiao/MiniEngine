#include "render/texture/TextureManager.h"

#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"

#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace engine {

Ref<Texture> TextureResourceManager::load(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("TextureResourceManager", "Invalid Texture AssetId");
        return errorTexture();
    }
    if (Ref<Texture> existing = find(assetId))
        return existing;
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("TextureResourceManager",
                   "Unknown Texture AssetId: %s",
                   assetId.toString().c_str());
        return errorTexture();
    }
    return loadFromPath(*path, assetId);
}

Ref<Texture> TextureResourceManager::load(const VirtualPath& texturePath) {
    if (!texturePath.valid()) {
        Log::error("TextureResourceManager",
                   "Invalid Texture path: %s",
                   texturePath.string().c_str());
        return errorTexture();
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    if (!assetId) {
        Log::error("TextureResourceManager",
                   "Texture path has no AssetId: %s",
                   texturePath.string().c_str());
        return errorTexture();
    }
    if (Ref<Texture> existing = find(*assetId))
        return existing;
    return loadFromPath(texturePath, *assetId);
}

Ref<Texture> TextureResourceManager::resolveReference(std::string_view reference) {
    if (reference.empty())
        return defaultWhite();
    VirtualPath path{reference};
    if (!path.valid())
        path = VirtualPath{"assets://" + std::string{reference}};
    Ref<Texture> texture;
    if (path.valid() && path.scheme() == "assets")
        texture = load(path);
    if (!texture) {
        Log::warn("TextureResourceManager",
                  "Using the error Texture for unresolved reference: %.*s",
                  static_cast<int>(reference.size()),
                  reference.data());
        texture = errorTexture();
    }
    return texture;
}

Ref<Texture> TextureResourceManager::loadFromPath(const VirtualPath& texturePath,
                                                  const AssetId& assetId) {
    Log::info("Texture", "Loading texture: %s", texturePath.string().c_str());
    const std::shared_ptr<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(texturePath);
    if (!asset)
        return errorTexture();
    return createTexture(texturePath, assetId, asset->desc, asset->mipData);
}

Ref<Texture> TextureResourceManager::createTexture(VirtualPath path,
                                                   AssetId assetId,
                                                   TextureDesc desc,
                                                   std::vector<TextureMipData> mipData,
                                                   std::uint64_t version) {
    if (!path.valid() || !validateTexture(desc, mipData)) {
        Log::error("TextureResourceManager", "Cannot create an invalid or unsupported Texture");
        return {};
    }

    Ref<Texture> texture{
        new Texture(std::move(path), assetId, std::move(desc), std::move(mipData), version)};
    const RID handle = resources_.insert(texture.get());
    texture->resourceId_ = handle;
    if (assetId.valid())
        assetIndex_.insert_or_assign(assetId, handle);
    return texture;
}

Ref<Texture> TextureResourceManager::clone(const Ref<Texture>& source) {
    if (!source) {
        Log::error("TextureResourceManager", "Cannot clone an invalid Texture");
        return {};
    }
    return createTexture(source->assetPath(),
                         {},
                         source->desc(),
                         {source->mipData().begin(), source->mipData().end()},
                         source->version());
}

void TextureResourceManager::refreshAsset(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("TextureResourceManager", "Cannot refresh an invalid AssetId");
        return;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("TextureResourceManager",
                   "Unknown Texture AssetId: %s",
                   assetId.toString().c_str());
        return;
    }
    (void)replace(*path);
}

void TextureResourceManager::refreshAsset(const VirtualPath& texturePath) {
    if (!texturePath.valid()) {
        Log::error("TextureResourceManager",
                   "Invalid Texture path: %s",
                   texturePath.string().c_str());
        return;
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    if (!assetId) {
        Log::error("TextureResourceManager",
                   "Texture path has no AssetId: %s",
                   texturePath.string().c_str());
        return;
    }
    refreshAsset(*assetId);
}

Ref<Texture> TextureResourceManager::createBuiltin(const VirtualPath& path,
                                                   std::uint32_t width,
                                                   std::uint32_t height,
                                                   std::span<const std::byte> pixels,
                                                   TextureColorSpace colorSpace) {
    TextureDesc desc{TextureType::Texture2D,
                     colorSpace == TextureColorSpace::Srgb ? TextureFormat::Rgba8Srgb
                                                           : TextureFormat::Rgba8Unorm,
                     colorSpace,
                     width,
                     height,
                     1};
    std::vector<TextureMipData> mipData;
    mipData.push_back({width, height, {pixels.begin(), pixels.end()}});
    return createTexture(path, {}, std::move(desc), std::move(mipData));
}

Ref<Texture> TextureResourceManager::defaultWhite() {
    if (!defaultWhite_) {
        constexpr std::array pixels{
            std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        defaultWhite_ = createBuiltin(
            VirtualPath{"engine://textures/white"}, 1, 1, pixels, TextureColorSpace::Srgb);
    }
    return defaultWhite_;
}

Ref<Texture> TextureResourceManager::defaultBlack() {
    if (!defaultBlack_) {
        constexpr std::array pixels{
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xff}};
        defaultBlack_ = createBuiltin(
            VirtualPath{"engine://textures/black"}, 1, 1, pixels, TextureColorSpace::Srgb);
    }
    return defaultBlack_;
}

Ref<Texture> TextureResourceManager::defaultNormal() {
    if (!defaultNormal_) {
        constexpr std::array pixels{
            std::byte{0x80}, std::byte{0x80}, std::byte{0xff}, std::byte{0xff}};
        defaultNormal_ = createBuiltin(
            VirtualPath{"engine://textures/normal"}, 1, 1, pixels, TextureColorSpace::Linear);
    }
    return defaultNormal_;
}

Ref<Texture> TextureResourceManager::errorTexture() {
    if (!errorTexture_) {
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

bool TextureResourceManager::replace(const VirtualPath& texturePath) {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    if (!assetId)
        return true;
    Ref<Texture> current = find(*assetId);
    if (!current)
        return true;
    const std::shared_ptr<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(texturePath);
    if (!asset || current->version() == std::numeric_limits<std::uint64_t>::max())
        return false;
    if (!validateTexture(asset->desc, asset->mipData))
        return false;
    if (destroyObserver_)
        destroyObserver_(current->resourceId());
    current->rebuild(asset->desc, asset->mipData);
    return true;
}

Texture* TextureResourceManager::findRaw(RID handle) const {
    Texture* const* stored = resources_.find(handle);
    return stored ? *stored : nullptr;
}

Ref<Texture> TextureResourceManager::find(RID handle) const {
    return Ref<Texture>{findRaw(handle)};
}

Ref<Texture> TextureResourceManager::find(const AssetId& assetId) const {
    const auto found = assetIndex_.find(assetId);
    return found == assetIndex_.end() ? Ref<Texture>{} : find(found->second);
}

Ref<Texture> TextureResourceManager::find(const VirtualPath& texturePath) const {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    return assetId ? find(*assetId) : Ref<Texture>{};
}

void TextureResourceManager::unregister(Texture* texture) {
    if (!texture || !texture->resourceId_)
        return;
    const RID handle = texture->resourceId_;
    if (texture->assetId_.valid()) {
        const auto found = assetIndex_.find(texture->assetId_);
        if (found != assetIndex_.end() && found->second == handle)
            assetIndex_.erase(found);
    }
    if (destroyObserver_)
        destroyObserver_(handle);
    texture->resourceId_ = {};
    (void)resources_.release(handle);
}

void TextureResourceManager::clear() {
    defaultWhite_.reset();
    defaultBlack_.reset();
    defaultNormal_.reset();
    errorTexture_.reset();

    std::vector<std::pair<RID, Texture*>> remaining;
    remaining.reserve(resources_.size());
    resources_.forEachHandle(
        [&remaining](RID handle, Texture* texture) { remaining.emplace_back(handle, texture); });
    for (const auto& [handle, texture] : remaining) {
        if (!findRaw(handle))
            continue;
        if (destroyObserver_)
            destroyObserver_(handle);
        texture->resourceId_ = {};
        (void)resources_.release(handle);
    }
    assetIndex_.clear();
}

} // namespace engine
