#include "render/texture/TextureManager.h"

#include "asset/database/AssetDatabase.h"
#include "asset/manager/AssetManager.h"
#include "core/logging/Log.h"
#include "rhi/api/Device.h"

#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace engine {

bool TextureManager::initialize(rhi::IDevice& device) {
    if (initialized()) {
        Log::error("TextureManager", "Manager is already initialized");
        return false;
    }
    device_ = &device;
    return true;
}

void TextureManager::shutdown() {
    if (!initialized())
        return;
    clear();
    device_ = nullptr;
}

RID TextureManager::load(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("TextureManager", "Invalid Texture AssetId");
        return errorTexture();
    }
    if (const RID existing = findHandle(assetId); existing)
        return existing;
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("TextureManager", "Unknown Texture AssetId: %s", assetId.toString().c_str());
        return errorTexture();
    }
    return loadFromPath(*path, assetId);
}

RID TextureManager::load(const VirtualPath& texturePath) {
    if (!texturePath.valid()) {
        Log::error("TextureManager", "Invalid Texture path: %s", texturePath.string().c_str());
        return errorTexture();
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    if (!assetId) {
        Log::error(
            "TextureManager", "Texture path has no AssetId: %s", texturePath.string().c_str());
        return errorTexture();
    }
    if (const RID existing = findHandle(*assetId); existing)
        return existing;
    return loadFromPath(texturePath, *assetId);
}

RID TextureManager::resolveReference(std::string_view reference) {
    if (reference.empty())
        return defaultWhite();
    VirtualPath path{reference};
    if (!path.valid())
        path = VirtualPath{"assets://" + std::string{reference}};
    RID handle;
    if (path.valid() && path.scheme() == "assets")
        handle = load(path);
    if (!handle) {
        Log::warn("TextureManager",
                  "Using the error Texture for unresolved reference: %.*s",
                  static_cast<int>(reference.size()),
                  reference.data());
        handle = errorTexture();
    }
    return handle;
}

RID TextureManager::loadFromPath(const VirtualPath& texturePath, const AssetId& assetId) {
    Log::info("Texture", "Loading texture: %s", texturePath.string().c_str());
    const std::shared_ptr<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(texturePath);
    if (!asset)
        return errorTexture();
    std::optional<Texture> texture =
        createTexture(texturePath, assetId, asset->desc, asset->mipData);
    return texture ? insert(std::move(*texture)) : errorTexture();
}

std::optional<Texture> TextureManager::createTexture(VirtualPath path,
                                                     AssetId assetId,
                                                     TextureDesc desc,
                                                     std::vector<TextureMipData> mipData,
                                                     std::uint64_t version) {
    if (!initialized() || !path.valid() || !validateTexture(desc, mipData)) {
        Log::error("TextureManager", "Cannot create an invalid or unsupported Texture");
        return std::nullopt;
    }
    const rhi::TextureDesc rhiDesc{.dimension = toRhi(desc.type),
                                   .format = toRhi(desc.format),
                                   .width = desc.width,
                                   .height = desc.height,
                                   .depth = desc.depth,
                                   .arrayLayers = desc.arrayLayers,
                                   .mipCount = desc.mipCount,
                                   .usage = rhi::TextureUsage::Sampled |
                                            rhi::TextureUsage::TransferDestination,
                                   .debugName = path.string()};
    const rhi::RID texture = device_->createTexture(rhiDesc);
    std::vector<rhi::TextureUploadRegion> uploads;
    uploads.reserve(mipData.size());
    std::uint32_t mipLevel{};
    for (const TextureMipData& mip : mipData)
        uploads.push_back({mipLevel++, 0, mip.width, mip.height, mip.bytes});
    device_->uploadTexture(texture, uploads);

    const rhi::TextureViewDesc viewDesc{.type = toRhi(desc.type),
                                        .format = toRhi(desc.format),
                                        .baseMip = 0,
                                        .mipCount = desc.mipCount,
                                        .baseLayer = 0,
                                        .layerCount = desc.arrayLayers};
    rhi::IRHITexture* rhiTexture = device_->resolveTextureResource(texture);
    if (!rhiTexture) {
        device_->destroyTexture(texture);
        return std::nullopt;
    }
    const TextureView defaultView{texture, rhiTexture->defaultView(), viewDesc};
    return Texture{std::move(path),
                   assetId,
                   std::move(desc),
                   std::move(mipData),
                   version,
                   texture,
                   defaultView,
                   *rhiTexture};
}

RID TextureManager::clone(RID source) {
    const Texture* texture = find(source);
    if (!texture) {
        Log::error("TextureManager", "Cannot clone an invalid Texture");
        return {};
    }
    std::optional<Texture> copy =
        createTexture(texture->assetPath(),
                      {},
                      texture->desc(),
                      {texture->mipData().begin(), texture->mipData().end()},
                      texture->version());
    return copy ? insertUnkeyed(std::move(*copy)) : RID{};
}

void TextureManager::refreshAsset(const AssetId& assetId) {
    if (!assetId.valid()) {
        Log::error("TextureManager", "Cannot refresh an invalid AssetId");
        return;
    }
    const std::optional<VirtualPath> path = ASSET_DATABASE.findPath(assetId);
    if (!path) {
        Log::error("TextureManager", "Unknown Texture AssetId: %s", assetId.toString().c_str());
        return;
    }
    (void)replace(*path);
}

void TextureManager::refreshAsset(const VirtualPath& texturePath) {
    if (!texturePath.valid()) {
        Log::error("TextureManager", "Invalid Texture path: %s", texturePath.string().c_str());
        return;
    }
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    if (!assetId) {
        Log::error(
            "TextureManager", "Texture path has no AssetId: %s", texturePath.string().c_str());
        return;
    }
    refreshAsset(*assetId);
}

RID TextureManager::createBuiltin(const VirtualPath& path,
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
    std::optional<Texture> texture = createTexture(path, {}, std::move(desc), std::move(mipData));
    return texture ? insertUnkeyed(std::move(*texture)) : RID{};
}

RID TextureManager::defaultWhite() {
    if (!find(defaultWhite_)) {
        constexpr std::array pixels{
            std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
        defaultWhite_ = createBuiltin(
            VirtualPath{"engine://textures/white"}, 1, 1, pixels, TextureColorSpace::Srgb);
    }
    return defaultWhite_;
}

RID TextureManager::defaultBlack() {
    if (!find(defaultBlack_)) {
        constexpr std::array pixels{
            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0xff}};
        defaultBlack_ = createBuiltin(
            VirtualPath{"engine://textures/black"}, 1, 1, pixels, TextureColorSpace::Srgb);
    }
    return defaultBlack_;
}

RID TextureManager::defaultNormal() {
    if (!find(defaultNormal_)) {
        constexpr std::array pixels{
            std::byte{0x80}, std::byte{0x80}, std::byte{0xff}, std::byte{0xff}};
        defaultNormal_ = createBuiltin(
            VirtualPath{"engine://textures/normal"}, 1, 1, pixels, TextureColorSpace::Linear);
    }
    return defaultNormal_;
}

RID TextureManager::errorTexture() {
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

bool TextureManager::replace(const VirtualPath& texturePath) {
    const std::optional<AssetId> assetId = ASSET_DATABASE.findGuid(texturePath);
    if (!assetId)
        return true;
    const RID handle = findHandle(*assetId);
    if (!handle)
        return true;
    const std::shared_ptr<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(texturePath);
    Texture* current = find(handle);
    if (!asset || !current || current->version() == std::numeric_limits<std::uint64_t>::max())
        return false;
    std::optional<Texture> replacement =
        createTexture(texturePath, *assetId, asset->desc, asset->mipData, current->version() + 1);
    if (!replacement)
        return false;
    device_->waitIdle();
    const rhi::RID retired = current->rhiHandle();
    *current = std::move(*replacement);
    device_->destroyTexture(retired);
    return true;
}

bool TextureManager::validate(const Texture& texture) const {
    return texture.assetPath().valid() && texture.rhiHandle() &&
           validateTexture(texture.desc(), texture.mipData());
}

void TextureManager::clear() {
    defaultWhite_ = {};
    defaultBlack_ = {};
    defaultNormal_ = {};
    errorTexture_ = {};
    if (device_) {
        device_->waitIdle();
        forEach([this](const Texture& texture) {
            if (texture.rhiHandle())
                device_->destroyTexture(texture.rhiHandle());
        });
    }
    KeyedHandleRegistry::clear();
}

} // namespace engine
