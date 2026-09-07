#pragma once

#include "rhi/api/Device.h"
#include "rhi/api/Swapchain.h"

#include <memory>

namespace engine::rhi {

struct Context {
    std::unique_ptr<IDevice> device;
    std::unique_ptr<ISwapchain> swapchain;
};

[[nodiscard]] Context
createDefaultContext(void* nativeInstance, void* nativeWindow, const SwapchainDesc& swapchainDesc);

} // namespace engine::rhi
