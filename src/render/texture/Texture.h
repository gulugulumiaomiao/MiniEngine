#pragma once

#include "core/base/RID.h"
#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "render/texture/Sampler.h" // engine::SamplerDesc / TextureFilterMode / toRhi
#include "render/uniform/UniformResource.h"
#include "rhi/api/ResourceDesc.h" // rhi::RID / rhi::TextureBinding

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine {
class AssetId;
class VirtualPath;

namespace rhi {
class IDevice;
}

class TextureAsset;

enum class TextureType { Texture2D, Texture2DArray, Texture3D, TextureCube, TextureCubeArray };
enum class TextureFormat {
    Rgba8Unorm = 0, Rgba8Srgb = 1, Depth32Float = 2, Bgra8Unorm = 3, Bgra8Srgb = 4,
    R8Unorm = 5, Rg8Unorm = 6,
    R16Float = 7, Rg16Float = 8, Rgba16Float = 9,
    R32Float = 10, Rg32Float = 11, Rgba32Float = 12,
    Depth16Unorm = 13,
    R32Uint = 14,
};
enum class TextureColorSpace { Linear, Srgb };

[[nodiscard]] rhi::PixelFormat toRhi(TextureFormat format);

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

// Byte offsets and sizes for a tightly packed CPU mip chain, with no GPU staging padding.
using TextureMipLayout = rhi::TextureMipLayout;

struct TextureDesc {
    TextureType type{TextureType::Texture2D};
    TextureFormat format{TextureFormat::Rgba8Srgb};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t depth{1};
    std::uint32_t arrayLayers{1};
    std::uint32_t mipCount{1};
    SamplerDesc sampler;
    // Runtime allocation options; serialized texture assets use these defaults.
    rhi::TextureUsage usage{rhi::TextureUsage::Sampled | rhi::TextureUsage::TransferDestination};
    rhi::SampleCount samples{rhi::SampleCount::One};
    rhi::TextureTiling tiling{rhi::TextureTiling::Optimal};
    std::string debugName{"Texture"};

    TextureDesc() = default;
    TextureDesc(TextureType type,
                TextureFormat format,
                std::uint32_t width,
                std::uint32_t height,
                std::uint32_t mipCount = 1)
        : type(type), format(format), width(width), height(height),
          mipCount(mipCount) {}

    TextureDesc(TextureType type,
                TextureFormat format,
                std::uint32_t width,
                std::uint32_t height,
                std::uint32_t mipCount,
                SamplerDesc sampler)
        : type(type), format(format), width(width), height(height),
          mipCount(mipCount), sampler(std::move(sampler)) {}
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
class Texture final : public UniformResource {
public:
    ~Texture() override;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&&) = delete;

    // UniformResource 接口：Texture 绑定到组合图像采样器槽
    // （VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER），写入取 binding() 的 view+sampler。
    [[nodiscard]] UniformResourceType resourceType() const override {
        return UniformResourceType::Texture;
    }
    [[nodiscard]] rhi::ResourceType bindingType() const override {
        return rhi::ResourceType::CombinedImageSampler;
    }

    // 资产身份委托给所链接的 TextureAsset（定义在 .cpp，因 TextureAsset 此处尚不完整）；
    // 内建/克隆纹理无 asset，返回空路径 / 无效 id。
    [[nodiscard]] const VirtualPath& assetPath() const;
    [[nodiscard]] AssetId assetId() const;
    [[nodiscard]] bool isAssetBacked() const { return asset_ != nullptr; }

    [[nodiscard]] TextureType type() const { return type_; }
    [[nodiscard]] TextureFormat format() const { return format_; }
    [[nodiscard]] TextureColorSpace colorSpace() const {
        return rhi::isSrgbFormat(pixelFormat()) ? TextureColorSpace::Srgb : TextureColorSpace::Linear;
    }
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }
    [[nodiscard]] std::uint32_t depth() const { return depth_; }
    [[nodiscard]] std::uint32_t arrayLayers() const { return arrayLayers_; }
    [[nodiscard]] std::uint32_t mipCount() const { return mipCount_; }
    [[nodiscard]] rhi::TextureUsage usage() const { return usage_; }
    [[nodiscard]] rhi::PixelFormat pixelFormat() const;
    [[nodiscard]] rhi::SampleCount samples() const { return samples_; }
    [[nodiscard]] rhi::TextureTiling tiling() const { return tiling_; }

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

    // General owning creation for supported 2D textures; no pixel upload.
    [[nodiscard]] static Ref<Texture> create(const TextureDesc& desc);

    // Uninitialized single-mip 2D render textures. Clear/render before sampling.
    // Return an empty Ref for invalid parameters or GPU resource creation failure.
    [[nodiscard]] static Ref<Texture> createColor(
        std::uint32_t width, std::uint32_t height,
        TextureFormat format = TextureFormat::Rgba8Unorm);
    [[nodiscard]] static Ref<Texture> createDepth(
        std::uint32_t width, std::uint32_t height,
        TextureFormat format = TextureFormat::Depth32Float);
    [[nodiscard]] static Ref<Texture> createShadowMap(
        std::uint32_t width, std::uint32_t height,
        TextureFormat format = TextureFormat::Depth32Float);

    /// 默认纹理（经 IDevice::active() 自建，非 asset-backed；设备切换后自动重建）。
    [[nodiscard]] static Ref<Texture> defaultWhite();
    [[nodiscard]] static Ref<Texture> defaultBlack();
    [[nodiscard]] static Ref<Texture> defaultNormal();
    [[nodiscard]] static Ref<Texture> errorTexture();

    /// 采纳外部 view+sampler 作为 CombinedImageSampler 绑定资源（非拥有）：用于阴影贴图/RT 等
    /// 渲染图产出、非 asset-backed 的绑定；~Texture 不销毁外部句柄（ownsGpu_ = false）。
    [[nodiscard]] static Ref<Texture> adoptBinding(rhi::RID view, rhi::RID sampler);

private:
    friend class TextureAsset;

    /// 构造：按 desc 分配三个 RID（不上传数据）。
    explicit Texture(const TextureDesc& desc);
    void initializeGpu(const rhi::TextureDesc& desc, const SamplerDesc& sampler);
    [[nodiscard]] static Ref<Texture> createRenderTexture(
        std::uint32_t width, std::uint32_t height, TextureFormat format,
        rhi::TextureUsage usage);
    /// 采纳外部绑定构造（非拥有）：只记录 view+sampler，ownsGpu_ = false。
    Texture(rhi::RID view, rhi::RID sampler);
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
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint32_t depth_{1};
    std::uint32_t arrayLayers_{1};
    std::uint32_t mipCount_{1};
    rhi::TextureUsage usage_{rhi::TextureUsage::None};
    rhi::SampleCount samples_{rhi::SampleCount::One};
    rhi::TextureTiling tiling_{rhi::TextureTiling::Optimal};
    std::uint64_t deviceUid_{};

    rhi::RID texture_;
    rhi::RID view_;
    rhi::RID sampler_;
    bool ownsGpu_{true}; // false = 采纳的外部 view/sampler，~Texture 不销毁

    TextureAsset* asset_{}; // 非拥有活链接（asset 由 AssetManager 常驻）；仅 instantiate 实例设置
};

/// 层1 纹理资产：序列化源（desc + 扁平像素）。可 instantiate 出唯一运行时 Texture，
/// 或 clone 出多个脱离实例。
// Validates serializable allocation/sampler options and tightly packed mip bytes
// for all supported formats. Source image decoding capabilities are independent.
[[nodiscard]] bool validateTextureAsset(const TextureDesc& desc, std::span<const std::uint8_t> pixels);

} // namespace engine
