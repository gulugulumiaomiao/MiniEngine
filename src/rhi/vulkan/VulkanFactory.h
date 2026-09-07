#pragma once

#include "rhi/RhiFactory.h"

namespace engine::rhi::vulkan {

class VulkanFactory final : public IContextFactory {
public:
    [[nodiscard]] Context createContext(const ContextDesc& desc) const override;
};

} // namespace engine::rhi::vulkan
