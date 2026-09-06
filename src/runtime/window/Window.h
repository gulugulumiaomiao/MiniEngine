#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace engine {

class Window final {
public:
    Window(std::uint32_t width, std::uint32_t height, std::string_view title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool shouldClose() const { return shouldClose_; }
    [[nodiscard]] HWND nativeHandle() const { return handle_; }
    [[nodiscard]] HINSTANCE nativeInstance() const { return instance_; }
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> framebufferSize() const;
    [[nodiscard]] bool consumeResize();
    void pollEvents();
    void waitForUsableFramebuffer();

private:
    static LRESULT CALLBACK windowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

    static constexpr const char* kWindowClass = "MiniVulkanEngineWindow";
    HINSTANCE instance_{};
    HWND handle_{};
    bool resized_{};
    bool shouldClose_{};
};

} // namespace engine
