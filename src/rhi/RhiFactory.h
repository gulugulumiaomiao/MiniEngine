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
    // 控制是否创建和持久化管线缓存，缓存路径由后端固定。
    bool enablePipelineCache{true};
};

class IContextFactory {
public:
    virtual ~IContextFactory() = default;

    [[nodiscard]] virtual Context createContext(const ContextDesc& desc) const = 0;
};

} // namespace engine::rhi
