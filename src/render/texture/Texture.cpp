#include "render/texture/Texture.h"

#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"
#include "render/texture/TextureManager.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace engine {
namespace {

constexpr std::uint32_t kTextureMagic = 0x52584554U;
constexpr std::uint16_t kTextureVersion = 3;

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

} // namespace

bool TextureMipData::transfer(Transfer& archive) {
    return archive.beginObject({}) && archive.transfer("width", width) &&
           archive.transfer("height", height) && archive.transfer("bytes", bytes) &&
           archive.endObject();
}

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

bool validateTexture(const TextureDesc& desc, std::span<const TextureMipData> mipData) {
    // The complete type model is serialized now, but this rollout intentionally creates only
    // single-layer 2D textures until upload and barrier paths support every dimension.
    if (desc.type != TextureType::Texture2D || desc.width == 0 || desc.height == 0 ||
        desc.depth != 1 || desc.arrayLayers != 1 || desc.mipCount == 0 ||
        desc.mipCount != mipData.size() ||
        (desc.format != TextureFormat::Rgba8Unorm && desc.format != TextureFormat::Rgba8Srgb) ||
        (desc.colorSpace != TextureColorSpace::Linear &&
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
    std::uint32_t width = desc.width;
    std::uint32_t height = desc.height;
    for (const TextureMipData& mip : mipData) {
        std::size_t expected{};
        if (!rgbaByteSize(width, height, expected) || mip.width != width || mip.height != height ||
            mip.bytes.size() != expected) {
            return false;
        }
        width = std::max(1U, width / 2U);
        height = std::max(1U, height / 2U);
    }
    return true;
}

Texture::Texture(VirtualPath assetPath,
                 AssetId assetId,
                 TextureDesc desc,
                 std::vector<TextureMipData> mipData,
                 std::uint64_t version)
    : assetPath_(std::move(assetPath)), assetId_(assetId), desc_(std::move(desc)),
      mipData_(std::move(mipData)), version_(version) {}

Texture::~Texture() {
    TEXTURE_RESOURCE_MANAGER.unregister(this);
}

void Texture::rebuild(TextureDesc desc, std::vector<TextureMipData> mipData) {
    if (version_ == std::numeric_limits<std::uint64_t>::max())
        Log::fatal("Texture", "Texture version overflow");
    desc_ = std::move(desc);
    mipData_ = std::move(mipData);
    ++version_;
}

bool TextureAsset::transfer(Transfer& archive) {
    std::uint32_t magic = kTextureMagic;
    std::uint16_t version = kTextureVersion;
    TextureDesc decodedDesc;
    std::vector<TextureMipData> decodedMips;
    TextureDesc& targetDesc = archive.reading() ? decodedDesc : desc;
    std::vector<TextureMipData>& targetMips = archive.reading() ? decodedMips : mipData;
    if ((archive.writing() && !validateTexture(desc, mipData)) || !archive.beginObject({}) ||
        !archive.transfer("magic", magic) || magic != kTextureMagic ||
        !archive.transfer("version", version) || version != kTextureVersion ||
        !archive.transfer("description", targetDesc) || !archive.transfer("mip_data", targetMips) ||
        !archive.endObject() || (archive.reading() && !validateTexture(decodedDesc, decodedMips))) {
        Log::error(
            "TextureAsset", "Invalid TextureAsset payload: %s", assetPath().string().c_str());
        return false;
    }
    if (archive.reading()) {
        desc = decodedDesc;
        mipData = std::move(decodedMips);
    }
    return true;
}

} // namespace engine
