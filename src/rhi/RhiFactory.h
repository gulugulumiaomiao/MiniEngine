#pragma once

#include "rhi/api/Device.h"
#include "rhi/api/Swapchain.h"

#include <memory>

namespace engine::rhi {

enum class WindowSystem {
    Win32,
};

struct SurfaceSource {
    WindowSystem windowSystem{WindowSystem::Win32};
    void* nativeDisplay{};
    void* nativeWindow{};
};

struct Context {
    std::unique_ptr<IDevice> device;
    std::unique_ptr<ISwapchain> swapchain;
};

struct ContextDesc {
    SurfaceSource surface;
    SwapchainDesc swapchain;
};

class IContextFactory {
public:
    virtual ~IContextFactory() = default;

    [[nodiscard]] virtual Context createContext(const ContextDesc& desc) const = 0;
};

} // namespace engine::rhi
