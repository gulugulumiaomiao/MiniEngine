// Tests that swapchain images are correctly transitioned from UNDEFINED to
// COLOR_ATTACHMENT_OPTIMAL on first use, and that the layout is correctly
// tracked across swapchain recreation (e.g., when opening a project that
// changes window size).
#include "core/base/BuildConfig.h"
#include "core/logging/Log.h"
#include "rhi/RhiFactory.h"
#include "rhi/api/Device.h"
#include "rhi/api/Swapchain.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanFactory.h"
#include "rhi/vulkan/VulkanSwapchain.h"

#include <cstdio>
#include <memory>
#include <windows.h>

namespace {

int fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

HWND createTestWindow() {
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = DefWindowProcA;
    wc.lpszClassName = "SwapchainLayoutTest";
    RegisterClassExA(&wc);
    return CreateWindowExA(0, "SwapchainLayoutTest", "Test", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr,
                           GetModuleHandle(nullptr), nullptr);
}

} // namespace

int main() {
    using namespace engine;
    using namespace engine::rhi;

    HWND hwnd = createTestWindow();
    if (!hwnd) {
        return fail("Cannot create test window");
    }

    // Create Vulkan device
    const vulkan::VulkanFactory factory;
    SwapchainDesc desc{.width = 800, .height = 600, .vsync = false};
    auto context = factory.createContext({
        .surface = {.windowSystem = WindowSystem::Win32,
                    .nativeDisplay = GetModuleHandle(nullptr),
                    .nativeWindow = hwnd},
        .swapchain = desc,
    });

    if (!context.device || !context.swapchain) {
        return fail("Cannot create Vulkan context");
    }

    auto& device = *context.device;
    auto& swapchain = *context.swapchain;

    // First frame: image should transition from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL
    auto status = swapchain.beginFrame();
    if (status != FrameStatus::Ready) {
        return fail("First beginFrame failed");
    }

    // Record a simple clear operation
    {
        auto& encoder = swapchain.encoder();
        RenderingInfo rendering;
        rendering.renderArea = {0, 0, swapchain.width(), swapchain.height()};
        rendering.colorAttachments.push_back({
            swapchain.currentTextureView(),
            LoadOp::Clear,
            StoreOp::Store,
            {0.0F, 0.0F, 0.0F, 1.0F},
        });
        encoder.beginRendering(rendering);
        encoder.endRendering();
    }

    status = swapchain.endFrame();
    if (status != FrameStatus::Ready) {
        return fail("First endFrame failed");
    }

    // Second frame: image should already be in correct layout
    status = swapchain.beginFrame();
    if (status != FrameStatus::Ready) {
        return fail("Second beginFrame failed");
    }

    {
        auto& encoder = swapchain.encoder();
        RenderingInfo rendering;
        rendering.renderArea = {0, 0, swapchain.width(), swapchain.height()};
        rendering.colorAttachments.push_back({
            swapchain.currentTextureView(),
            LoadOp::Clear,
            StoreOp::Store,
            {1.0F, 0.0F, 0.0F, 1.0F},
        });
        encoder.beginRendering(rendering);
        encoder.endRendering();
    }

    status = swapchain.endFrame();
    if (status != FrameStatus::Ready) {
        return fail("Second endFrame failed");
    }

    // Simulate swapchain recreation (e.g., when opening a project)
    device.waitIdle();
    swapchain.resize(1024, 768);

    // First frame after resize: image should transition from UNDEFINED again
    status = swapchain.beginFrame();
    if (status != FrameStatus::Ready) {
        return fail("Post-resize beginFrame failed");
    }

    {
        auto& encoder = swapchain.encoder();
        RenderingInfo rendering;
        rendering.renderArea = {0, 0, swapchain.width(), swapchain.height()};
        rendering.colorAttachments.push_back({
            swapchain.currentTextureView(),
            LoadOp::Clear,
            StoreOp::Store,
            {0.0F, 1.0F, 0.0F, 1.0F},
        });
        encoder.beginRendering(rendering);
        encoder.endRendering();
    }

    status = swapchain.endFrame();
    if (status != FrameStatus::Ready) {
        return fail("Post-resize endFrame failed");
    }

    // Second frame after resize
    status = swapchain.beginFrame();
    if (status != FrameStatus::Ready) {
        return fail("Second post-resize beginFrame failed");
    }

    {
        auto& encoder = swapchain.encoder();
        RenderingInfo rendering;
        rendering.renderArea = {0, 0, swapchain.width(), swapchain.height()};
        rendering.colorAttachments.push_back({
            swapchain.currentTextureView(),
            LoadOp::Clear,
            StoreOp::Store,
            {0.0F, 0.0F, 1.0F, 1.0F},
        });
        encoder.beginRendering(rendering);
        encoder.endRendering();
    }

    status = swapchain.endFrame();
    if (status != FrameStatus::Ready) {
        return fail("Second post-resize endFrame failed");
    }

    device.waitIdle();
    DestroyWindow(hwnd);
    std::printf("Swapchain layout transition test passed\n");
    return 0;
}
