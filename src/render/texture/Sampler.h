#pragma once

#include "core/base/Ref.h"
#include "core/base/RefCounted.h"
#include "rhi/api/RhiTypes.h" // rhi::RID
#include "rhi/api/Sampler.h"  // rhi::SamplerDesc / filter / address enums

namespace engine {

/// 层2 运行时采样器：只保留必要属性 + 访问器，持有一个层3 sampler RID。
///
/// 不拥有层3 sampler：`IDevice::createSampler` 按 `SamplerDesc` 去重、由设备持有并随
/// 设备释放，故本类析构不 destroy（避免共享 RID 被单个 Sampler 误毁）。
class Sampler final : public RefCounted {
public:
    /// 经当前 active 设备按 desc 创建（去重）层3 sampler，包装为层2 Sampler。
    [[nodiscard]] static Ref<Sampler> resolve(const rhi::SamplerDesc& desc);

    [[nodiscard]] rhi::SamplerFilter minFilter() const { return minFilter_; }
    [[nodiscard]] rhi::SamplerFilter magFilter() const { return magFilter_; }
    [[nodiscard]] rhi::SamplerMipmapFilter mipmapFilter() const { return mipmapFilter_; }
    [[nodiscard]] rhi::SamplerAddressMode addressU() const { return addressU_; }
    [[nodiscard]] rhi::SamplerAddressMode addressV() const { return addressV_; }
    [[nodiscard]] float maxAnisotropy() const { return maxAnisotropy_; }

    /// 层3 sampler 句柄（IDevice sampler 池分配）。
    [[nodiscard]] rhi::RID rhiHandle() const { return handle_; }

private:
    Sampler(const rhi::SamplerDesc& desc, rhi::RID handle);

    rhi::RID handle_;
    rhi::SamplerFilter minFilter_;
    rhi::SamplerFilter magFilter_;
    rhi::SamplerMipmapFilter mipmapFilter_;
    rhi::SamplerAddressMode addressU_;
    rhi::SamplerAddressMode addressV_;
    float maxAnisotropy_;
};

} // namespace engine
