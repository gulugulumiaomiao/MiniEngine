#pragma once

namespace engine {

namespace rhi {
class IDevice;
}

template <typename CreateInfo, typename Resource> class IGpuResourceFactory {
public:
    explicit IGpuResourceFactory(rhi::IDevice& device) : device_(device) {}
    virtual ~IGpuResourceFactory() = default;

    IGpuResourceFactory(const IGpuResourceFactory&) = delete;
    IGpuResourceFactory& operator=(const IGpuResourceFactory&) = delete;
    IGpuResourceFactory(IGpuResourceFactory&&) = delete;
    IGpuResourceFactory& operator=(IGpuResourceFactory&&) = delete;

    [[nodiscard]] virtual bool create(const CreateInfo& createInfo, Resource& destination) = 0;
    virtual void release(Resource& resource) = 0;

protected:
    rhi::IDevice& device_;
};

} // namespace engine
