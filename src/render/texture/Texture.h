#pragma once

#include "asset/base/Asset.h"
#include "asset/base/AssetId.h"
#include "core/base/RID.h"
#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "render/texture/Sampler.h" // engine::SamplerDesc / TextureFilterMode / toRhi
#include "rhi/api/TextureView.h"    // rhi::RID / rhi::TextureBinding

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace engine {

namespace rhi {
class IDevice;
}

class TextureAsset;

enum class TextureType { Texture2D, Texture2DArray, Texture3D, TextureCube, TextureCubeArray };
enum class TextureFormat { Rgba8Unorm, Rgba8Srgb };
enum class TextureColorSpace { Linear, Srgb };

// 层2 视图语义描述：自定义 Swizzle + 高层 format；构造层3 view 时由 toRhi 翻译成
// rhi::TextureViewDesc（MatchTexture 解析为源纹理格式）。
enum class SwizzleChannel { Identity, Zero, One, R, G, B, A };
struct Swizzle {
    SwizzleChannel r{SwizzleChannel::Identity};
    SwizzleChannel g{SwizzleChannel::Identity};
    SwizzleChannel b{SwizzleChannel::Identity};
    SwizzleChannel a{SwizzleChannel::Identity};
    [[nodiscard]] bool operator==(const Swizzle&) const = default;
};

enum class TextureViewFormat { MatchTexture, Rgba8Unorm, Rgba8Srgb };

struct TextureViewDesc {
    TextureType type{TextureType::Texture2D};
    TextureViewFormat format{TextureViewFormat::MatchTexture};
    std::uint32_t baseMip{};
    std::uint32_t mipCount{1};
    std::uint32_t baseLayer{};
    std::uint32_t layerCount{1};
    Swizzle swizzle;
    [[nodiscard]] bool operator==(const TextureViewDesc&) const = default;
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
    SamplerDesc sampler;

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
                SamplerDesc sampler)
        : type(type), format(format), colorSpace(colorSpace), width(width), height(height),
          mipCount(mipCount), sampler(std::move(sampler)) {}

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

// Derives the mip layout implied by desc. Returns false for unsupported dimensions/formats
// or on byte-size overflow.
[[nodiscard]] bool computeTextureLayout(const TextureDesc& desc, TextureMipLayout& layout);

/// 层2 运行时纹理：只保留必要属性 + 访问器，持有 3 个层3 RID（texture / view / sampler），
/// view + sampler 作为默认采样绑定。不持有 TextureDesc 与像素数据。
///
/// 两步初始化：构造（接 `TextureDesc`）经 `rhi::IDevice::active()` 分配三个 RID；
/// `initialize`/`upload` 把像素上传到 GPU。默认 view 由层3 texture 持有、默认 sampler 由
/// 设备按 desc 去重持有，故 `~Texture` 只销毁自己创建的层3 texture（view 随之释放）。
/// 记录创建时的 active device：设备切换后旧实例析构不会误伤新设备。
///
/// 由 `TextureAsset::instantiate()`/`clone()` 或静态默认纹理方法创建。
class Texture final : public RefCounted {
public:
    ~Texture() override;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&&) = delete;

    // 资产身份委托给所链接的 TextureAsset（定义在 .cpp，因 TextureAsset 此处尚不完整）；
    // 内建/克隆纹理无 asset，返回空路径 / 无效 id。
    [[nodiscard]] const VirtualPath& assetPath() const;
    [[nodiscard]] AssetId assetId() const;
    [[nodiscard]] bool isAssetBacked() const { return asset_ != nullptr; }

    [[nodiscard]] TextureType type() const { return type_; }
    [[nodiscard]] TextureFormat format() const { return format_; }
    [[nodiscard]] TextureColorSpace colorSpace() const { return colorSpace_; }
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }
    [[nodiscard]] std::uint32_t depth() const { return depth_; }
    [[nodiscard]] std::uint32_t arrayLayers() const { return arrayLayers_; }
    [[nodiscard]] std::uint32_t mipCount() const { return mipCount_; }

    /// 三个层3 RID（均来自 IDevice）。
    [[nodiscard]] rhi::RID textureHandle() const { return texture_; }
    [[nodiscard]] rhi::RID defaultView() const { return view_; }
    [[nodiscard]] rhi::RID defaultSampler() const { return sampler_; }

    /// 默认采样绑定：默认 view + 默认 sampler。
    [[nodiscard]] rhi::TextureBinding binding() const { return {view_, sampler_}; }
    /// 指定 sampler 的采样绑定：默认 view + 覆盖 sampler。
    [[nodiscard]] rhi::TextureBinding binding(const Ref<Sampler>& sampler) const;

    /// 把扁平像素（全 mip 链）上传到 GPU；按 computeTextureLayout 逐 mip 切片。
    void upload(std::span<const std::uint8_t> pixels);

    /// 克隆一个脱离 asset 的独立纹理（与 `TextureAsset::clone` 同义，需 asset-backed）。
    [[nodiscard]] Ref<Texture> clone() const;

    /// 默认纹理（经 IDevice::active() 自建，非 asset-backed；设备切换后自动重建）。
    [[nodiscard]] static Ref<Texture> defaultWhite();
    [[nodiscard]] static Ref<Texture> defaultBlack();
    [[nodiscard]] static Ref<Texture> defaultNormal();
    [[nodiscard]] static Ref<Texture> errorTexture();

private:
    friend class TextureAsset;

    /// 构造：按 desc 分配三个 RID（不上传数据）。
    explicit Texture(const TextureDesc& desc);
    /// 建立与 asset 的活链接（仅 instantiate 实例调用）。asset 为非拥有裸指针：
    /// TextureAsset 由 AssetManager 强缓存常驻，实例不延长其生命周期。
    void bindAsset(TextureAsset* asset);
    /// 第二步初始化：上传像素（等价 upload）。
    void initialize(std::span<const std::uint8_t> pixels) { upload(pixels); }
    /// 弃置 GPU 句柄（所属设备已销毁）：置空三个 RID，使 ~Texture 不再对已亡设备发起销毁。
    /// 仅静态默认纹理在检测到 active 设备切换、重建前对旧实例调用。
    void detachFromDeadDevice() {
        texture_ = {};
        view_ = {};
        sampler_ = {};
    }

    TextureType type_{TextureType::Texture2D};
    TextureFormat format_{TextureFormat::Rgba8Srgb};
    TextureColorSpace colorSpace_{TextureColorSpace::Srgb};
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint32_t depth_{1};
    std::uint32_t arrayLayers_{1};
    std::uint32_t mipCount_{1};

    rhi::RID texture_;
    rhi::RID view_;
    rhi::RID sampler_;

    TextureAsset* asset_{}; // 非拥有活链接（asset 由 AssetManager 常驻）；仅 instantiate 实例设置
};

/// 层1 纹理资产：序列化源（desc + 扁平像素）。可 instantiate 出唯一运行时 Texture，
/// 或 clone 出多个脱离实例。
class TextureAsset final : public Asset {
public:
    ~TextureAsset() override;
    [[nodiscard]] AssetType type() const override { return AssetType::Texture; }

    TextureDesc desc;
    std::vector<std::uint8_t> pixels;

    [[nodiscard]] bool transfer(Transfer& archive) override;

    /// 唯一运行时实例：首次创建并登记，之后复用同一实例（asset 就地重传会推送到它）。
    [[nodiscard]] Ref<Texture> instantiate();
    /// 克隆一个脱离 asset 的独立运行时纹理（每次新建，不随 asset 变化）。
    [[nodiscard]] Ref<Texture> clone();

private:
    friend class Texture;

    /// 把当前 pixels 重新上传给唯一实例（存在时）。就地重传（热重载）后调用。
    void syncInstance();

    Texture* instance_{}; // 裸观察者指针；~Texture 反注册置空
};

/// 解析纹理引用为运行时 Texture：空→白；`engine://textures/{white,black,normal,error}`→内置；
/// `assets://` 或裸路径→经 AssetManager 加载 `TextureAsset` 并 `instantiate`（按 asset 天然去重）；
/// 失败→error 纹理。取代已删除的 `TextureResourceManager::resolveReference`。
[[nodiscard]] Ref<Texture> resolveTextureReference(std::string_view reference);

[[nodiscard]] bool validateTexture(const TextureDesc& desc, std::span<const std::uint8_t> pixels);

} // namespace engine
