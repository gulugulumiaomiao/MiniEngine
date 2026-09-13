#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace engine {

class Window final {
public:
    // Observes raw Win32 messages before built-in window handling. The return value is
    // informational only: built-in handling still runs so resize/close tracking stays intact.
    using NativeMessageHandler = std::function<void(HWND, UINT, WPARAM, LPARAM)>;

    Window(std::uint32_t width, std::uint32_t height, std::string_view title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool shouldClose() const { return shouldClose_; }
    [[nodiscard]] HWND nativeHandle() const { return handle_; }
    [[nodiscard]] HINSTANCE nativeInstance() const { return instance_; }
    [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> framebufferSize() const;
    [[nodiscard]] bool consumeResize();
    void setTitle(std::string_view title);
    void pollEvents();
    void waitForUsableFramebuffer();
    void setMessageHandler(NativeMessageHandler handler) { messageHandler_ = std::move(handler); }

private:
    static LRESULT CALLBACK windowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

    static constexpr const wchar_t* kWindowClass = L"MiniVulkanEngineWindow";
    HINSTANCE instance_{};
    HWND handle_{};
    NativeMessageHandler messageHandler_;
    bool resized_{};
    bool shouldClose_{};
};

} // namespace engine
