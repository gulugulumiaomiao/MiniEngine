#pragma once

#include "asset/base/Asset.h"
#include "asset/base/AssetId.h"
#include "core/base/RID.h"
#include "core/base/RefCounted.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace engine {

class TextureResourceManager;
class TextureStorageFactory;

enum class TextureType { Texture2D, Texture2DArray, Texture3D, TextureCube, TextureCubeArray };
enum class TextureFormat { Rgba8Unorm, Rgba8Srgb };
enum class TextureColorSpace { Linear, Srgb };

// Unity-style texture sampling settings. These remain an asset-level default suggestion;
// the Texture and TextureView never own a Sampler.
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
        : filterMode(filterMode), addressModeU(addressModeU), addressModeV(addressModeV),
          maxAnisotropy(maxAnisotropy) {}

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

// Byte offset of every mip level inside a tightly packed pixel blob, plus the total blob
// size. Levels run 0..mipCount-1 with no padding; only RGBA8 2D textures are supported.
struct TextureMipLayout {
    std::vector<std::size_t> offsets;
    std::size_t totalSize{};
};

struct TextureDesc : public Transferable {
    TextureType type{TextureType::Texture2D};
    TextureFormat format{TextureFormat::Rgba8Srgb};
    TextureColorSpace colorSpace{TextureColorSpace::Srgb};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
    std::uint32_t arrayLayers{1};
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
        : type(type), format(format), colorSpace(colorSpace), width(width), height(height),
          mipCount(mipCount), sampler(std::move(sampler)) {}

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

// Derives the mip layout implied by desc. Returns false for unsupported dimensions/formats
// or on byte-size overflow.
[[nodiscard]] bool computeTextureLayout(const TextureDesc& desc, TextureMipLayout& layout);

class Texture final : public RefCounted {
public:
    ~Texture() override;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&&) = delete;

    [[nodiscard]] const VirtualPath& assetPath() const { return assetPath_; }
    [[nodiscard]] AssetId assetId() const { return assetId_; }
    [[nodiscard]] bool isAssetBacked() const { return assetId_.valid(); }
    [[nodiscard]] const TextureDesc& desc() const { return desc_; }
    // Read-only view of the transient CPU pixel blob; empty once handed to the GPU.
    [[nodiscard]] std::span<const std::uint8_t> pixels() const { return pixels_; }
    [[nodiscard]] std::uint64_t version() const { return version_; }
    [[nodiscard]] RID resourceId() const { return resourceId_; }

private:
    friend class TextureResourceManager;
    friend class TextureStorageFactory;

    Texture(VirtualPath assetPath,
            AssetId assetId,
            TextureDesc desc,
            std::vector<std::uint8_t> pixels,
            std::uint64_t version);

    void rebuild(TextureDesc desc, std::vector<std::uint8_t> pixels);

    // Hands the pixel blob to the uploader. Transient textures release their CPU copy so the
    // GPU becomes the only owner of the texel data. Builtin textures (retainPixels_) keep it:
    // they are held by the manager across device resets, so the GPU cache can be cleared and
    // rebuilt while the layer-1 Texture survives, which requires re-uploadable pixels.
    [[nodiscard]] std::vector<std::uint8_t> takePixelData() {
        if (retainPixels_)
            return pixels_;
        std::vector<std::uint8_t> taken = std::move(pixels_);
        pixels_.clear();
        return taken;
    }

    VirtualPath assetPath_;
    AssetId assetId_;
    TextureDesc desc_;
    std::vector<std::uint8_t> pixels_;
    bool retainPixels_{false};
    std::uint64_t version_{1};
    RID resourceId_;
};

class TextureAsset final : public Asset {
public:
    [[nodiscard]] AssetType type() const override { return AssetType::Texture; }

    TextureDesc desc;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

[[nodiscard]] bool validateTexture(const TextureDesc& desc, std::span<const std::uint8_t> pixels);

} // namespace engine
