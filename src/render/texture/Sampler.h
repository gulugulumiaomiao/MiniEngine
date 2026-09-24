#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "core/serialization/Transferable.h"
#include "rhi/api/RhiTypes.h" // rhi::RID
#include "rhi/api/Sampler.h"  // rhi::SamplerDesc（toRhi 翻译目标）

namespace engine {

// 层2 采样语义描述（Unity 风格）；构造层3 sampler 时由 toRhi 翻译成 rhi::SamplerDesc。
enum class TextureFilterMode { Point, Bilinear, Trilinear };
enum class TextureAddressMode { Repeat, MirroredRepeat, ClampToEdge };

struct SamplerDesc : public Transferable {
    TextureFilterMode filterMode{TextureFilterMode::Bilinear};
    TextureAddressMode addressModeU{TextureAddressMode::Repeat};
    TextureAddressMode addressModeV{TextureAddressMode::Repeat};
    float maxAnisotropy{1.0F};

    SamplerDesc() = default;
    SamplerDesc(TextureFilterMode filterMode,
                TextureAddressMode addressModeU,
                TextureAddressMode addressModeV,
                float maxAnisotropy)
        : filterMode(filterMode), addressModeU(addressModeU), addressModeV(addressModeV),
          maxAnisotropy(maxAnisotropy) {}

    [[nodiscard]] bool transfer(Transfer& archive) override;
};

/// 层2 → 层3 采样描述翻译（Texture 默认 sampler 与 Sampler::resolve 共用）。
[[nodiscard]] rhi::SamplerDesc toRhi(const SamplerDesc& desc);

/// 层2 运行时采样器：只保留必要语义属性 + 访问器，持有一个层3 sampler RID。
///
/// 由层2 `SamplerDesc`（语义）经 `toRhi` 翻译成 `rhi::SamplerDesc` 后交 `IDevice::createSampler`
/// 创建（设备按 desc 去重）。不拥有层3 sampler：设备持有并随设备释放，故析构不 destroy。
class Sampler final : public RefCounted {
public:
    /// 经当前 active 设备按语义 desc 创建（去重）层3 sampler，包装为层2 Sampler。
    [[nodiscard]] static Ref<Sampler> resolve(const SamplerDesc& desc);

    [[nodiscard]] TextureFilterMode filterMode() const { return filterMode_; }
    [[nodiscard]] TextureAddressMode addressModeU() const { return addressModeU_; }
    [[nodiscard]] TextureAddressMode addressModeV() const { return addressModeV_; }
    [[nodiscard]] float maxAnisotropy() const { return maxAnisotropy_; }

    /// 层3 sampler 句柄（IDevice sampler 池分配）。
    [[nodiscard]] rhi::RID rhiHandle() const { return handle_; }

private:
    Sampler(const SamplerDesc& desc, rhi::RID handle);

    rhi::RID handle_;
    TextureFilterMode filterMode_;
    TextureAddressMode addressModeU_;
    TextureAddressMode addressModeV_;
    float maxAnisotropy_;
};

} // namespace engine
