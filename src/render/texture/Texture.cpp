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

Texture::Texture(VirtualPath assetPath,
                 AssetId assetId,
                 TextureDesc desc,
                 std::vector<std::uint8_t> pixels,
                 std::uint64_t version)
    : assetPath_(std::move(assetPath)), assetId_(assetId), desc_(std::move(desc)),
      pixels_(std::move(pixels)), version_(version) {}

Texture::~Texture() {
    TEXTURE_RESOURCE_MANAGER.unregister(this);
}

void Texture::rebuild(TextureDesc desc, std::vector<std::uint8_t> pixels) {
    if (version_ == std::numeric_limits<std::uint64_t>::max())
        Log::fatal("Texture", "Texture version overflow");
    desc_ = std::move(desc);
    pixels_ = std::move(pixels);
    ++version_;
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
    }
    return true;
}

} // namespace engine
