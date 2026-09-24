#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "render/texture/Texture.h" // engine::SamplerDesc / toRhi / rhi::RID

namespace engine {

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
