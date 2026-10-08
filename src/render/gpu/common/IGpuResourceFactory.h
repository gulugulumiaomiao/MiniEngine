#pragma once

namespace engine {

template <typename CreateInfo, typename Resource> class IGpuResourceFactory {
public:
    IGpuResourceFactory() = default;
    virtual ~IGpuResourceFactory() = default;

    IGpuResourceFactory(const IGpuResourceFactory&) = delete;
    IGpuResourceFactory& operator=(const IGpuResourceFactory&) = delete;
    IGpuResourceFactory(IGpuResourceFactory&&) = delete;
    IGpuResourceFactory& operator=(IGpuResourceFactory&&) = delete;

    [[nodiscard]] virtual bool create(const CreateInfo& createInfo, Resource& destination) = 0;
    virtual void release(Resource& resource) = 0;
};

} // namespace engine
