#include "runtime/window/Window.h"

#include "core/logging/Log.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <tuple>

namespace engine {
namespace {

// 配置和项目名称使用 UTF-8，Win32 标题统一使用 UTF-16。
std::wstring wideTitle(std::string_view title) {
    if (title.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, title.data(),
                                         static_cast<int>(title.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, title.data(), static_cast<int>(title.size()),
                       result.data(), size);
    return result;
}

} // namespace

Window::Window(std::uint32_t width, std::uint32_t height, std::string_view title) {
    instance_ = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0) {
        Log::fatal("Window", "Win32 window class registration failed");
    }

    RECT rectangle{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);
    const std::wstring ownedTitle = wideTitle(title);
    handle_ = CreateWindowExW(0,
                              kWindowClass,
                              ownedTitle.c_str(),
                              WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT,
                              CW_USEDEFAULT,
                              rectangle.right - rectangle.left,
                              rectangle.bottom - rectangle.top,
                              nullptr,
                              nullptr,
                              instance_,
                              this);
    if (!handle_) {
        UnregisterClassW(kWindowClass, instance_);
        Log::fatal("Window", "Win32 window creation failed");
    }
    ShowWindow(handle_, SW_SHOW);
}

Window::~Window() {
    if (handle_) {
        DestroyWindow(handle_);
    }
    UnregisterClassW(kWindowClass, instance_);
}

void Window::setTitle(std::string_view title) {
    const std::wstring ownedTitle = wideTitle(title);
    if (!SetWindowTextW(handle_, ownedTitle.c_str()))
        Log::warn("Window", "Cannot update window title");
}

bool Window::consumeResize() {
    const bool result = resized_;
    resized_ = false;
    return result;
}

void Window::pollEvents() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            shouldClose_ = true;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
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
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(create->lpCreateParams);
        SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        if (self->messageHandler_) {
            self->messageHandler_(handle, message, wParam, lParam);
        }
        if (message == WM_SIZE) {
            self->resized_ = true;
            return 0;
        }
        if (message == WM_CLOSE) {
            self->shouldClose_ = true;
            return 0;
        }
    }
    return DefWindowProcW(handle, message, wParam, lParam);
}

} // namespace engine
