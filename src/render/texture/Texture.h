#pragma once

#include "asset/base/Asset.h"
#include "asset/base/AssetId.h"
#include "render/base/RenderHandle.h"
#include "rhi/api/Sampler.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace engine {

class TextureAsset;
class TextureManager;

enum class TextureType { Texture2D };
enum class TextureFormat { Rgba8Unorm, Rgba8Srgb };
enum class TextureColorSpace { Linear, Srgb };

// Unity-style texture sampling settings. These live next to TextureDesc because
// they are part of the texture asset, not the shader or material.
enum class TextureFilterMode { Point, Bilinear, Trilinear };
enum class TextureAddressMode { Repeat, MirroredRepeat, ClampToEdge };

struct TextureSamplerSettings : public Transferable {
    TextureFilterMode filterMode{TextureFilterMode::Bilinear};
    TextureAddressMode addressModeU{TextureAddressMode::Repeat};
    TextureAddressMode addressModeV{TextureAddressMode::Repeat};
    float maxAnisotropy{1.0F};

    TextureSamplerSettings() = default;
    TextureSamplerSettings(TextureFilterMode filterMode,
                           TextureAddressMode addressModeU,
                           TextureAddressMode addressModeV,
                           float maxAnisotropy)
        : filterMode(filterMode),
          addressModeU(addressModeU),
          addressModeV(addressModeV),
          maxAnisotropy(maxAnisotropy) {}

    [[nodiscard]] bool transfer(Transfer& archive) override;
    [[nodiscard]] rhi::SamplerDesc toRhi() const;
};

struct TextureMipData : public Transferable {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::byte> bytes;

    TextureMipData() = default;
    TextureMipData(std::uint32_t width, std::uint32_t height, std::vector<std::byte> bytes)
        : width(width), height(height), bytes(std::move(bytes)) {}

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

struct TextureDesc : public Transferable {
    TextureType type{TextureType::Texture2D};
    TextureFormat format{TextureFormat::Rgba8Srgb};
    TextureColorSpace colorSpace{TextureColorSpace::Srgb};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mipCount{1};
    TextureSamplerSettings sampler;

    TextureDesc() = default;
    TextureDesc(TextureType type,
                TextureFormat format,
                TextureColorSpace colorSpace,
                std::uint32_t width,
                std::uint32_t height,
                std::uint32_t mipCount = 1)
        : type(type), format(format), colorSpace(colorSpace), width(width), height(height),
          mipCount(mipCount) {}

    TextureDesc(TextureType type,
                TextureFormat format,
                TextureColorSpace colorSpace,
                std::uint32_t width,
                std::uint32_t height,
                std::uint32_t mipCount,
                TextureSamplerSettings sampler)
        : type(type),
          format(format),
          colorSpace(colorSpace),
          width(width),
          height(height),
          mipCount(mipCount),
          sampler(std::move(sampler)) {}

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

class Texture final {
public:
    [[nodiscard]] const VirtualPath& assetPath() const { return assetPath_; }
    [[nodiscard]] const TextureDesc& desc() const { return desc_; }
    [[nodiscard]] std::span<const TextureMipData> mipData() const { return mipData_; }
    [[nodiscard]] std::uint64_t version() const { return version_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    void markClean() { dirty_ = false; }

    [[nodiscard]] AssetId assetId() const { return assetId_; }
    [[nodiscard]] bool isAssetBacked() const { return assetId_.valid(); }
    [[nodiscard]] Texture clone() const;
    void rebuildFromAsset(const TextureAsset& asset);

private:
    friend class TextureAsset;
    friend class TextureManager;

    VirtualPath assetPath_;
    AssetId assetId_;
    TextureDesc desc_;
    std::vector<TextureMipData> mipData_;
    std::uint64_t version_{1};
    bool dirty_{true};
};

class TextureAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Texture; }

    TextureDesc desc;
    std::vector<TextureMipData> mipData;

    [[nodiscard]] Texture instantiate() const;
    [[nodiscard]] bool transfer(Transfer& archive) override;
};

[[nodiscard]] bool validateTexture(const TextureDesc& desc,
                                   std::span<const TextureMipData> mipData);

} // namespace engine
