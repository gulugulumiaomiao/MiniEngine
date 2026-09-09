#pragma once

#include "asset/base/Asset.h"
#include "render/base/RenderHandle.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace engine {

class TextureAsset;
class TextureManager;

enum class TextureType { Texture2D };
enum class TextureFormat { Rgba8Unorm, Rgba8Srgb };
enum class TextureColorSpace { Linear, Srgb };

struct TextureMipData {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::byte> bytes;

    [[nodiscard]] bool transfer(Transfer& archive);
};

struct TextureDesc {
    TextureType type{TextureType::Texture2D};
    TextureFormat format{TextureFormat::Rgba8Srgb};
    TextureColorSpace colorSpace{TextureColorSpace::Srgb};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t mipCount{1};

    [[nodiscard]] bool transfer(Transfer& archive);
};

class Texture final {
public:
    [[nodiscard]] const VirtualPath& assetPath() const { return assetPath_; }
    [[nodiscard]] const TextureDesc& desc() const { return desc_; }
    [[nodiscard]] std::span<const TextureMipData> mipData() const { return mipData_; }
    [[nodiscard]] std::uint64_t version() const { return version_; }
    [[nodiscard]] bool dirty() const { return dirty_; }
    void markClean() { dirty_ = false; }

private:
    friend class TextureAsset;
    friend class TextureManager;

    VirtualPath assetPath_;
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
