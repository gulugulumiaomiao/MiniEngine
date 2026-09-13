#pragma once

#include "render/renderer/Renderer.h"
#include "runtime/window/Window.h"
#include "tools/editor/ImGuiRenderer.h"

namespace engine::editor {

// Bridges Dear ImGui into the engine: Win32 messages are forwarded from Window,
// frames are driven from Engine::loop through Application::onUpdate, and draw data is
// recorded as an IFrameOverlay appended after the render pipeline in the same
// command buffer. Drawing itself goes through ImGuiRenderer, which only uses the RHI.
class ImGuiLayer final : public IFrameOverlay {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer() override;

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // Attaches to the engine window and renderer; renderer must already be initialized.
    void attach(Renderer& renderer, Window& window);
    void detach();

    // Sets the physical path used to persist the ImGui layout (imgui.ini), including
    // the docking layout. Empty disables persistence. ImGui keeps the pointer, so the
    // string must outlive the ImGui context (it is owned by this layer).
    void setIniPath(std::string path);
    [[nodiscard]] const std::string& iniPath() const;

    // Win32 message hook installed via Window::setMessageHandler.
    void handleNativeMessage(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

    // Called by the editor application every frame before panels are drawn.
    void beginFrame();
    // Called by the editor application every frame after panels are drawn, even when
    // the renderer later skips the frame, so the ImGui frame sequence stays valid.
    void endFrame();

    // IFrameOverlay
    void recordOverlay(RenderContext& context) override;
    void onSwapchainRecreated(Renderer& renderer) override;

private:
    ImGuiRenderer renderer_;
    Renderer* engineRenderer_{};
    Window* window_{};
    std::string iniFilename_;
    bool initialized_{};
};

} // namespace engine::editor
