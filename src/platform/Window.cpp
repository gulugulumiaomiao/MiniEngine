#include "platform/Window.h"

#include "core/Log.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <tuple>

namespace engine {

Window::Window(std::uint32_t width, std::uint32_t height, std::string_view title) {
    instance_ = GetModuleHandleA(nullptr);
    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExA(&windowClass) == 0) {
        Log::fatal("Window", "Win32 window class registration failed");
    }

    RECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    const std::string ownedTitle(title);
    handle_ = CreateWindowExA(0, kWindowClass, ownedTitle.c_str(), WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, rectangle.right - rectangle.left,
                              rectangle.bottom - rectangle.top, nullptr, nullptr, instance_, this);
    if (!handle_) {
        UnregisterClassA(kWindowClass, instance_);
        Log::fatal("Window", "Win32 window creation failed");
    }
    ShowWindow(handle_, SW_SHOW);
}

Window::~Window() {
    if (handle_) {
        DestroyWindow(handle_);
    }
    UnregisterClassA(kWindowClass, instance_);
}

bool Window::consumeResize() {
    const bool result = resized_;
    resized_ = false;
    return result;
}

void Window::pollEvents() {
    MSG message{};
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            shouldClose_ = true;
        }
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

std::pair<std::uint32_t, std::uint32_t> Window::framebufferSize() const {
    RECT rectangle{};
    GetClientRect(handle_, &rectangle);
    return {static_cast<std::uint32_t>(std::max(0L, rectangle.right - rectangle.left)),
            static_cast<std::uint32_t>(std::max(0L, rectangle.bottom - rectangle.top))};
}

void Window::waitForUsableFramebuffer() {
    auto [width, height] = framebufferSize();
    while ((width == 0 || height == 0) && !shouldClose()) {
        WaitMessage();
        pollEvents();
        std::tie(width, height) = framebufferSize();
    }
}

LRESULT CALLBACK Window::windowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtrA(handle, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<Window*>(create->lpCreateParams);
        SetWindowLongPtrA(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        if (message == WM_SIZE) {
            self->resized_ = true;
            return 0;
        }
        if (message == WM_CLOSE) {
            self->shouldClose_ = true;
            return 0;
        }
    }
    return DefWindowProcA(handle, message, wParam, lParam);
}

} // namespace engine
