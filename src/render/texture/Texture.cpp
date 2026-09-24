#include "render/texture/Texture.h"

#include "asset/manager/AssetManager.h"
#include "core/filesystem/VirtualPath.h"
#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"
#include "render/texture/Sampler.h"
#include "rhi/api/Device.h"
#include "rhi/api/Sampler.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace engine {
namespace {

constexpr std::uint32_t kTextureMagic = 0x52584554U;
constexpr std::uint16_t kTextureVersion = 4;

bool rgbaByteSize(std::uint32_t width, std::uint32_t height, std::size_t& result) {
    constexpr std::size_t channels = 4;
    if (width == 0 || height == 0 ||
        static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / height) {
        return false;
    }
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / channels)
        return false;
    result = pixels * channels;
    return true;
}

[[nodiscard]] rhi::TextureType toRhi(TextureType type) {
    switch (type) {
    case TextureType::Texture2D: return rhi::TextureType::Texture2D;
    case TextureType::Texture2DArray: return rhi::TextureType::Texture2DArray;
    case TextureType::Texture3D: return rhi::TextureType::Texture3D;
    case TextureType::TextureCube: return rhi::TextureType::TextureCube;
    case TextureType::TextureCubeArray: return rhi::TextureType::TextureCubeArray;
    }
    Log::fatal("Texture", "Unsupported Texture type");
}

[[nodiscard]] rhi::PixelFormat toRhi(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8Unorm: return rhi::PixelFormat::Rgba8Unorm;
    case TextureFormat::Rgba8Srgb: return rhi::PixelFormat::Rgba8Srgb;
    }
    Log::fatal("Texture", "Unsupported Texture format");
}

[[nodiscard]] rhi::SamplerDesc toRhi(const TextureSamplerSettings& settings) {
    rhi::SamplerDesc desc;
    desc.maxAnisotropy = settings.maxAnisotropy;
    switch (settings.addressModeU) {
    case TextureAddressMode::Repeat: desc.addressU = rhi::SamplerAddressMode::Repeat; break;
    case TextureAddressMode::MirroredRepeat:
        desc.addressU = rhi::SamplerAddressMode::MirroredRepeat;
        break;
    case TextureAddressMode::ClampToEdge:
        desc.addressU = rhi::SamplerAddressMode::ClampToEdge;
        break;
    }
    switch (settings.addressModeV) {
    case TextureAddressMode::Repeat: desc.addressV = rhi::SamplerAddressMode::Repeat; break;
    case TextureAddressMode::MirroredRepeat:
        desc.addressV = rhi::SamplerAddressMode::MirroredRepeat;
        break;
    case TextureAddressMode::ClampToEdge:
        desc.addressV = rhi::SamplerAddressMode::ClampToEdge;
        break;
    }
    switch (settings.filterMode) {
    case TextureFilterMode::Point:
        desc.minFilter = rhi::SamplerFilter::Nearest;
        desc.magFilter = rhi::SamplerFilter::Nearest;
        desc.mipmapFilter = rhi::SamplerMipmapFilter::Nearest;
        break;
    case TextureFilterMode::Bilinear:
        desc.minFilter = rhi::SamplerFilter::Linear;
        desc.magFilter = rhi::SamplerFilter::Linear;
        desc.mipmapFilter = rhi::SamplerMipmapFilter::Nearest;
        break;
    case TextureFilterMode::Trilinear:
        desc.minFilter = rhi::SamplerFilter::Linear;
        desc.magFilter = rhi::SamplerFilter::Linear;
        desc.mipmapFilter = rhi::SamplerMipmapFilter::Linear;
        break;
    }
    return desc;
}

} // namespace

bool TextureSamplerSettings::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("filter_mode", filterMode) &&
           archive.transfer("address_mode_u", addressModeU) &&
           archive.transfer("address_mode_v", addressModeV) &&
           archive.transfer("max_anisotropy", maxAnisotropy) && archive.endObject();
}

bool TextureDesc::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("type", type) &&
           archive.transfer("format", format) && archive.transfer("color_space", colorSpace) &&
           archive.transfer("width", width) && archive.transfer("height", height) &&
           archive.transfer("depth", depth) && archive.transfer("array_layers", arrayLayers) &&
           archive.transfer("mip_count", mipCount) && archive.transfer("sampler", sampler) &&
           archive.endObject();
}

bool computeTextureLayout(const TextureDesc& desc, TextureMipLayout& layout) {
    layout = {};
    // The complete type model is serialized now, but this rollout intentionally supports only
    // single-layer 2D textures until upload and barrier paths handle every dimension.
    if (desc.type != TextureType::Texture2D || desc.depth != 1 || desc.arrayLayers != 1 ||
        desc.mipCount == 0 ||
        (desc.format != TextureFormat::Rgba8Unorm && desc.format != TextureFormat::Rgba8Srgb)) {
        return false;
    }
    layout.offsets.reserve(desc.mipCount);
    std::uint32_t width = desc.width;
    std::uint32_t height = desc.height;
    std::size_t offset{};
    for (std::uint32_t level = 0; level < desc.mipCount; ++level) {
        std::size_t levelSize{};
        if (!rgbaByteSize(width, height, levelSize) ||
            offset > std::numeric_limits<std::size_t>::max() - levelSize) {
            layout = {};
            return false;
        }
        layout.offsets.push_back(offset);
        offset += levelSize;
        width = std::max(1U, width / 2U);
        height = std::max(1U, height / 2U);
    }
    layout.totalSize = offset;
    return true;
}

bool validateTexture(const TextureDesc& desc, std::span<const std::uint8_t> pixels) {
    if ((desc.colorSpace != TextureColorSpace::Linear &&
         desc.colorSpace != TextureColorSpace::Srgb) ||
        (desc.format == TextureFormat::Rgba8Srgb) != (desc.colorSpace == TextureColorSpace::Srgb) ||
        desc.sampler.maxAnisotropy < 1.0F) {
        return false;
    }
    const auto validFilter = [](TextureFilterMode value) {
        return value == TextureFilterMode::Point || value == TextureFilterMode::Bilinear ||
               value == TextureFilterMode::Trilinear;
    };
    const auto validAddress = [](TextureAddressMode value) {
        return value == TextureAddressMode::Repeat || value == TextureAddressMode::MirroredRepeat ||
               value == TextureAddressMode::ClampToEdge;
    };
    if (!validFilter(desc.sampler.filterMode) || !validAddress(desc.sampler.addressModeU) ||
        !validAddress(desc.sampler.addressModeV)) {
        return false;
    }
    TextureMipLayout layout;
    if (!computeTextureLayout(desc, layout))
        return false;
    return pixels.size() == layout.totalSize;
}

// ---- Texture ----

Texture::Texture(const TextureDesc& desc)
    : type_(desc.type), format_(desc.format), colorSpace_(desc.colorSpace), width_(desc.width),
      height_(desc.height), depth_(desc.depth), arrayLayers_(desc.arrayLayers),
      mipCount_(desc.mipCount) {
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device) {
        Log::error("Texture", "No active device for texture creation");
        return;
    }
    device_ = device;
    deviceUid_ = device->uid();
    const rhi::TextureDesc deviceDesc{.dimension = toRhi(desc.type),
                                      .format = toRhi(desc.format),
                                      .width = desc.width,
                                      .height = desc.height,
                                      .depth = desc.depth,
                                      .arrayLayers = desc.arrayLayers,
                                      .mipCount = desc.mipCount,
                                      .usage = rhi::TextureUsage::Sampled |
                                               rhi::TextureUsage::TransferDestination,
                                      .debugName = "Texture"};
    texture_ = device->createTexture(deviceDesc);
    if (!texture_) {
        Log::error("Texture", "Failed to create RHI texture");
        return;
    }
    view_ = device->defaultTextureView(texture_);
    sampler_ = device->createSampler(toRhi(desc.sampler));
    if (!view_ || !sampler_)
        Log::error("Texture", "Failed to create default view/sampler");
}

Texture::~Texture() {
    if (asset_ && asset_->instance_ == this)
        asset_->instance_ = nullptr;
    // 仅当创建本纹理的设备仍是当前 active 设备（按 uid 比对，防地址复用误判）时才销毁 GPU
    // 资源：设备已切换则旧资源随旧设备消失，不能再打到新设备上。默认 view 随层3 texture 释放，
    // 默认 sampler 由设备去重持有。
    if (texture_) {
        if (rhi::IDevice* active = rhi::IDevice::active(); active && active->uid() == deviceUid_)
            active->destroyTexture(texture_);
    }
}

void Texture::bindAsset(TextureAsset* asset) {
    asset_ = asset;
    if (asset_) {
        assetPath_ = asset_->assetPath();
        assetId_ = asset_->assetId();
    }
}

rhi::TextureBinding Texture::binding(const Ref<Sampler>& sampler) const {
    return {view_, sampler ? sampler->rhiHandle() : sampler_};
}

void Texture::upload(std::span<const std::uint8_t> pixels) {
    if (!texture_)
        return;
    rhi::IDevice* device = device_ ? device_ : rhi::IDevice::active();
    if (!device) {
        Log::error("Texture", "No active device for texture upload");
        return;
    }
    TextureDesc desc;
    desc.type = type_;
    desc.format = format_;
    desc.colorSpace = colorSpace_;
    desc.width = width_;
    desc.height = height_;
    desc.depth = depth_;
    desc.arrayLayers = arrayLayers_;
    desc.mipCount = mipCount_;
    if (!validateTexture(desc, pixels)) {
        Log::error("Texture", "Pixel data does not match the texture layout");
        return;
    }
    TextureMipLayout layout;
    if (!computeTextureLayout(desc, layout))
        return;
    std::vector<rhi::TextureUploadRegion> uploads;
    uploads.reserve(layout.offsets.size());
    std::uint32_t mipWidth = width_;
    std::uint32_t mipHeight = height_;
    for (std::uint32_t level = 0; level < layout.offsets.size(); ++level) {
        const std::size_t begin = layout.offsets[level];
        const std::size_t end = level + 1U < layout.offsets.size() ? layout.offsets[level + 1U]
                                                                   : layout.totalSize;
        const std::span<const std::byte> data{
            reinterpret_cast<const std::byte*>(pixels.data() + begin), end - begin};
        uploads.push_back({level, 0, mipWidth, mipHeight, data});
        mipWidth = std::max(1U, mipWidth / 2U);
        mipHeight = std::max(1U, mipHeight / 2U);
    }
    device->uploadTexture(texture_, uploads);
}

Ref<Texture> Texture::clone() const {
    if (!asset_) {
        Log::error("Texture", "clone() requires an asset-backed texture");
        return {};
    }
    return asset_->clone();
}

Ref<Texture> Texture::defaultWhite() {
    static Ref<Texture> instance;
    static std::uint64_t ownerUid = 0;
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device)
        return instance;
    if (!instance || ownerUid != device->uid()) {
        static constexpr std::uint8_t kPixels[4] = {0xffU, 0xffU, 0xffU, 0xffU};
        const TextureDesc desc{
            TextureType::Texture2D, TextureFormat::Rgba8Srgb, TextureColorSpace::Srgb, 1, 1, 1};
        instance = Ref<Texture>(new Texture(desc));
        if (instance->textureHandle())
            instance->upload(kPixels);
        ownerUid = device->uid();
    }
    return instance;
}

Ref<Texture> Texture::defaultBlack() {
    static Ref<Texture> instance;
    static std::uint64_t ownerUid = 0;
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device)
        return instance;
    if (!instance || ownerUid != device->uid()) {
        static constexpr std::uint8_t kPixels[4] = {0x00U, 0x00U, 0x00U, 0xffU};
        const TextureDesc desc{
            TextureType::Texture2D, TextureFormat::Rgba8Srgb, TextureColorSpace::Srgb, 1, 1, 1};
        instance = Ref<Texture>(new Texture(desc));
        if (instance->textureHandle())
            instance->upload(kPixels);
        ownerUid = device->uid();
    }
    return instance;
}

Ref<Texture> Texture::defaultNormal() {
    static Ref<Texture> instance;
    static std::uint64_t ownerUid = 0;
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device)
        return instance;
    if (!instance || ownerUid != device->uid()) {
        static constexpr std::uint8_t kPixels[4] = {0x80U, 0x80U, 0xffU, 0xffU};
        const TextureDesc desc{
            TextureType::Texture2D, TextureFormat::Rgba8Unorm, TextureColorSpace::Linear, 1, 1, 1};
        instance = Ref<Texture>(new Texture(desc));
        if (instance->textureHandle())
            instance->upload(kPixels);
        ownerUid = device->uid();
    }
    return instance;
}

Ref<Texture> Texture::errorTexture() {
    static Ref<Texture> instance;
    static std::uint64_t ownerUid = 0;
    rhi::IDevice* device = rhi::IDevice::active();
    if (!device)
        return instance;
    if (!instance || ownerUid != device->uid()) {
        static constexpr std::uint8_t kPixels[16] = {
            0xffU, 0x00U, 0xffU, 0xffU, 0x00U, 0x00U, 0x00U, 0xffU,
            0x00U, 0x00U, 0x00U, 0xffU, 0xffU, 0x00U, 0xffU, 0xffU,
        };
        const TextureDesc desc{
            TextureType::Texture2D, TextureFormat::Rgba8Srgb, TextureColorSpace::Srgb, 2, 2, 1};
        instance = Ref<Texture>(new Texture(desc));
        if (instance->textureHandle())
            instance->upload(kPixels);
        ownerUid = device->uid();
    }
    return instance;
}

// ---- TextureAsset ----

TextureAsset::~TextureAsset() {
    // 双向观察者：若 asset 先于其唯一实例销毁（如工程关闭时 AssetManager 缓存先清），
    // 必须清除实例的回指指针，否则 ~Texture 会解引用悬垂的 asset_（UAF）。
    if (instance_)
        instance_->asset_ = nullptr;
}

bool TextureAsset::transfer(Transfer& archive) {
    std::uint32_t magic = kTextureMagic;
    std::uint16_t version = kTextureVersion;
    TextureDesc decodedDesc;
    std::vector<std::uint8_t> decodedPixels;
    TextureDesc& targetDesc = archive.reading() ? decodedDesc : desc;
    std::vector<std::uint8_t>& targetPixels = archive.reading() ? decodedPixels : pixels;
    if ((archive.writing() && !validateTexture(desc, pixels)) || !archive.beginObject({}) ||
        !archive.transfer("magic", magic) || magic != kTextureMagic ||
        !archive.transfer("version", version) || version != kTextureVersion ||
        !archive.transfer("description", targetDesc) ||
        !archive.transfer("pixels", targetPixels) || !archive.endObject() ||
        (archive.reading() && !validateTexture(decodedDesc, decodedPixels))) {
        Log::error(
            "TextureAsset", "Invalid TextureAsset payload: %s", assetPath().string().c_str());
        return false;
    }
    if (archive.reading()) {
        desc = decodedDesc;
        pixels = std::move(decodedPixels);
        // 就地重传（热重载）后把新像素推送给唯一实例，保持活链接。
        syncInstance();
    }
    return true;
}

Ref<Texture> TextureAsset::instantiate() {
    if (instance_)
        return Ref<Texture>(instance_); // 复用唯一实例（addRef）
    Ref<Texture> texture(new Texture(desc));
    if (!texture->textureHandle()) {
        Log::error(
            "TextureAsset", "Failed to instantiate texture for %s", assetPath().string().c_str());
        return {};
    }
    texture->bindAsset(this);
    texture->initialize(pixels);
    instance_ = texture.get();
    return texture;
}

Ref<Texture> TextureAsset::clone() {
    Ref<Texture> texture(new Texture(desc));
    if (!texture->textureHandle()) {
        Log::error("TextureAsset", "Failed to clone texture for %s", assetPath().string().c_str());
        return {};
    }
    texture->initialize(pixels); // 脱离实例：不登记、不链接 asset
    return texture;
}

void TextureAsset::syncInstance() {
    if (instance_)
        instance_->upload(pixels);
}

Ref<Texture> resolveTextureReference(std::string_view reference) {
    if (reference.empty())
        return Texture::defaultWhite();
    if (reference == "engine://textures/white")
        return Texture::defaultWhite();
    if (reference == "engine://textures/black")
        return Texture::defaultBlack();
    if (reference == "engine://textures/normal")
        return Texture::defaultNormal();
    if (reference == "engine://textures/error")
        return Texture::errorTexture();

    VirtualPath path{reference};
    if (!path.valid())
        path = VirtualPath{"assets://" + std::string{reference}};
    if (path.valid() && path.scheme() == "assets") {
        if (const Ref<TextureAsset> asset = ASSET_MANAGER.loadAsset<TextureAsset>(path)) {
            if (Ref<Texture> texture = asset->instantiate())
                return texture;
        }
    }
    Log::warn("Texture",
              "Using the error Texture for unresolved reference: %.*s",
              static_cast<int>(reference.size()),
              reference.data());
    return Texture::errorTexture();
}

} // namespace engine
