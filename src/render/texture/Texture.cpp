#include "render/texture/Texture.h"

#include "core/logging/Log.h"
#include "core/serialization/Transfer.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace engine {
namespace {

constexpr std::uint32_t kTextureMagic = 0x52584554U;
constexpr std::uint16_t kTextureVersion = 1;

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
    return archive.transfer("width", width) && archive.transfer("height", height) &&
           archive.transfer("bytes", bytes);
}

bool TextureDesc::transfer(Transfer& archive) {
    return archive.transfer("type", type) && archive.transfer("format", format) &&
           archive.transfer("color_space", colorSpace) && archive.transfer("width", width) &&
           archive.transfer("height", height) && archive.transfer("mip_count", mipCount);
}

bool validateTexture(const TextureDesc& desc, std::span<const TextureMipData> mipData) {
    if (desc.type != TextureType::Texture2D || desc.width == 0 || desc.height == 0 ||
        desc.mipCount == 0 || desc.mipCount != mipData.size() ||
        (desc.format != TextureFormat::Rgba8Unorm && desc.format != TextureFormat::Rgba8Srgb) ||
        (desc.colorSpace != TextureColorSpace::Linear &&
         desc.colorSpace != TextureColorSpace::Srgb) ||
        (desc.format == TextureFormat::Rgba8Srgb) != (desc.colorSpace == TextureColorSpace::Srgb)) {
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

Texture TextureAsset::instantiate() const {
    Texture result;
    if (!validateTexture(desc, mipData))
        return result;
    result.assetPath_ = assetPath();
    result.desc_ = desc;
    result.mipData_ = mipData;
    return result;
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
