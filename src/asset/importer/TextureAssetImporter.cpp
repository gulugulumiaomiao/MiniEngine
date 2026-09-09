#include "asset/importer/TextureAssetImporter.h"

#include "asset/derived_data/AssetArtifact.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "core/serialization/BinaryTransfer.h"
#include "render/texture/Texture.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace engine {
namespace {

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

std::uint32_t readU32(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || sizeof(std::uint32_t) > bytes.size() - offset)
        return 0;
    const auto* value = reinterpret_cast<const unsigned char*>(bytes.data() + offset);
    return static_cast<std::uint32_t>(value[0]) | (static_cast<std::uint32_t>(value[1]) << 8U) |
           (static_cast<std::uint32_t>(value[2]) << 16U) |
           (static_cast<std::uint32_t>(value[3]) << 24U);
}

std::uint64_t readU64(std::span<const std::byte> bytes, std::size_t offset) {
    return static_cast<std::uint64_t>(readU32(bytes, offset)) |
           (static_cast<std::uint64_t>(readU32(bytes, offset + 4U)) << 32U);
}

std::vector<TextureMipData> generateMipChain(TextureMipData base) {
    std::vector<TextureMipData> result;
    result.push_back(std::move(base));
    while (result.back().width > 1 || result.back().height > 1) {
        const TextureMipData& source = result.back();
        TextureMipData destination;
        destination.width = std::max(1U, source.width / 2U);
        destination.height = std::max(1U, source.height / 2U);
        destination.bytes.resize(static_cast<std::size_t>(destination.width) * destination.height *
                                 4U);
        for (std::uint32_t y = 0; y < destination.height; ++y) {
            for (std::uint32_t x = 0; x < destination.width; ++x) {
                for (std::uint32_t channel = 0; channel < 4; ++channel) {
                    std::uint32_t total{};
                    std::uint32_t samples{};
                    for (std::uint32_t dy = 0; dy < 2; ++dy) {
                        for (std::uint32_t dx = 0; dx < 2; ++dx) {
                            const std::uint32_t sourceX = x * 2U + dx;
                            const std::uint32_t sourceY = y * 2U + dy;
                            if (sourceX >= source.width || sourceY >= source.height)
                                continue;
                            const std::size_t index =
                                (static_cast<std::size_t>(sourceY) * source.width + sourceX) * 4U +
                                channel;
                            total += std::to_integer<std::uint8_t>(source.bytes[index]);
                            ++samples;
                        }
                    }
                    const std::size_t destinationIndex =
                        (static_cast<std::size_t>(y) * destination.width + x) * 4U + channel;
                    destination.bytes[destinationIndex] =
                        static_cast<std::byte>((total + samples / 2U) / samples);
                }
            }
        }
        result.push_back(std::move(destination));
    }
    return result;
}

std::shared_ptr<TextureAsset> decodeImage(std::span<const std::byte> source) {
    if (source.empty() || source.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return {};
    int width{};
    int height{};
    int sourceChannels{};
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels{
        stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(source.data()),
                              static_cast<int>(source.size()),
                              &width,
                              &height,
                              &sourceChannels,
                              STBI_rgb_alpha),
        stbi_image_free};
    if (!pixels || width <= 0 || height <= 0)
        return {};
    const std::uint64_t byteSize = static_cast<std::uint64_t>(width) * height * 4U;
    if (byteSize > std::numeric_limits<std::size_t>::max())
        return {};
    TextureMipData base{static_cast<std::uint32_t>(width),
                        static_cast<std::uint32_t>(height),
                        std::vector<std::byte>(static_cast<std::size_t>(byteSize))};
    std::memcpy(base.bytes.data(), pixels.get(), base.bytes.size());
    auto asset = std::make_shared<TextureAsset>();
    asset->mipData = generateMipChain(std::move(base));
    asset->desc = {TextureType::Texture2D,
                   TextureFormat::Rgba8Srgb,
                   TextureColorSpace::Srgb,
                   static_cast<std::uint32_t>(width),
                   static_cast<std::uint32_t>(height),
                   static_cast<std::uint32_t>(asset->mipData.size())};
    return asset;
}

std::shared_ptr<TextureAsset> decodeKtx1(std::span<const std::byte> source) {
    constexpr std::array<unsigned char, 12> identifier{
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    if (source.size() < 64 ||
        std::memcmp(source.data(), identifier.data(), identifier.size()) != 0 ||
        readU32(source, 12) != 0x04030201U || readU32(source, 16) != 0x1401U ||
        readU32(source, 20) != 1U || readU32(source, 24) != 0x1908U ||
        (readU32(source, 28) != 0x8058U && readU32(source, 28) != 0x8C43U) ||
        readU32(source, 36) == 0 || readU32(source, 40) == 0 || readU32(source, 44) != 0 ||
        readU32(source, 48) != 0 || readU32(source, 52) != 1U) {
        return {};
    }
    const std::uint32_t width = readU32(source, 36);
    const std::uint32_t height = readU32(source, 40);
    const std::uint32_t levels = std::max(1U, readU32(source, 56));
    std::size_t offset = 64U + readU32(source, 60);
    std::vector<TextureMipData> mips;
    std::uint32_t mipWidth = width;
    std::uint32_t mipHeight = height;
    for (std::uint32_t level = 0; level < levels; ++level) {
        if (offset + 4U > source.size())
            return {};
        const std::uint32_t imageSize = readU32(source, offset);
        offset += 4U;
        std::size_t expected{};
        if (!rgbaByteSize(mipWidth, mipHeight, expected) || imageSize != expected ||
            offset > source.size() || imageSize > source.size() - offset) {
            return {};
        }
        TextureMipData mip{mipWidth, mipHeight, std::vector<std::byte>(imageSize)};
        std::ranges::copy(source.subspan(offset, imageSize), mip.bytes.begin());
        mips.push_back(std::move(mip));
        offset += (imageSize + 3U) & ~std::size_t{3U};
        mipWidth = std::max(1U, mipWidth / 2U);
        mipHeight = std::max(1U, mipHeight / 2U);
    }
    auto asset = std::make_shared<TextureAsset>();
    const bool srgb = readU32(source, 28) == 0x8C43U;
    asset->desc = {TextureType::Texture2D,
                   srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm,
                   srgb ? TextureColorSpace::Srgb : TextureColorSpace::Linear,
                   width,
                   height,
                   levels};
    asset->mipData = std::move(mips);
    return asset;
}

std::shared_ptr<TextureAsset> decodeKtx2(std::span<const std::byte> source) {
    constexpr std::array<unsigned char, 12> identifier{
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    if (source.size() < 104 ||
        std::memcmp(source.data(), identifier.data(), identifier.size()) != 0 ||
        (readU32(source, 12) != 37U && readU32(source, 12) != 43U) || readU32(source, 16) != 1U ||
        readU32(source, 20) == 0 || readU32(source, 24) == 0 || readU32(source, 28) != 0 ||
        readU32(source, 32) != 0 || readU32(source, 36) != 1U || readU32(source, 40) == 0 ||
        readU32(source, 44) != 0) {
        return {};
    }
    const std::uint32_t width = readU32(source, 20);
    const std::uint32_t height = readU32(source, 24);
    const std::uint32_t levels = readU32(source, 40);
    if (levels > (source.size() - 80U) / 24U)
        return {};
    std::vector<TextureMipData> mips;
    std::uint32_t mipWidth = width;
    std::uint32_t mipHeight = height;
    for (std::uint32_t level = 0; level < levels; ++level) {
        const std::size_t index = 80U + static_cast<std::size_t>(level) * 24U;
        const std::uint64_t offset = readU64(source, index);
        const std::uint64_t length = readU64(source, index + 8U);
        std::size_t expected{};
        if (!rgbaByteSize(mipWidth, mipHeight, expected) || length != expected ||
            offset > source.size() || length > source.size() - offset) {
            return {};
        }
        TextureMipData mip{mipWidth, mipHeight, std::vector<std::byte>(expected)};
        std::ranges::copy(source.subspan(static_cast<std::size_t>(offset), expected),
                          mip.bytes.begin());
        mips.push_back(std::move(mip));
        mipWidth = std::max(1U, mipWidth / 2U);
        mipHeight = std::max(1U, mipHeight / 2U);
    }
    auto asset = std::make_shared<TextureAsset>();
    const bool srgb = readU32(source, 12) == 43U;
    asset->desc = {TextureType::Texture2D,
                   srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm,
                   srgb ? TextureColorSpace::Srgb : TextureColorSpace::Linear,
                   width,
                   height,
                   levels};
    asset->mipData = std::move(mips);
    return asset;
}

} // namespace

AssetImportResult TextureAssetImporter::import(const AssetImportContext& context) const {
    const auto fail = [](std::string error) {
        Log::error("TextureAssetImporter", "%s", error.c_str());
        return AssetImportResult::failed(AssetType::Texture, std::move(error));
    };
    if (context.meta.assetType != AssetType::Texture || !context.meta.assetId.valid() ||
        !context.sourcePath.valid() || !context.artifactPath.valid()) {
        return fail("Invalid Texture import context");
    }
    const auto source = FILE_SYSTEM.readBinary(context.sourcePath);
    if (!source)
        return fail("Cannot read Texture: " + context.sourcePath.string());
    std::string path = context.sourcePath.relativePath();
    std::ranges::transform(path, path.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    std::shared_ptr<TextureAsset> texture;
    if (path.ends_with(".ktx"))
        texture = decodeKtx1(*source);
    else if (path.ends_with(".ktx2"))
        texture = decodeKtx2(*source);
    else
        texture = decodeImage(*source);
    if (!texture || !validateTexture(texture->desc, texture->mipData))
        return fail("Unsupported or invalid Texture: " + context.sourcePath.string());
    BinaryWriter writer;
    if (!texture->transfer(writer))
        return fail("Cannot serialize Texture Artifact: " + context.sourcePath.string());
    const AssetArtifact artifact{
        1, context.meta.assetId, AssetType::Texture, context.sourcePath, writer.takeBytes()};
    if (!saveAssetArtifact(context.artifactPath, artifact))
        return fail("Cannot save Texture Artifact: " + context.artifactPath.string());
    return AssetImportResult::succeeded(AssetType::Texture, context.artifactPath);
}

} // namespace engine
