#include "tools/editor/ImGuiLayer.h"

#include "core/logging/Log.h"
#include "render/pipeline/RenderContext.h"
#include "rhi/api/CommandEncoder.h"
#include "rhi/api/Swapchain.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"

#include <span>
#include <utility>

// imgui_impl_win32.h keeps this declaration inside '#if 0' so includers do not
// pull <windows.h>; the backend asks users to copy it into their own .cpp file.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

namespace engine::editor {

ImGuiLayer::~ImGuiLayer() {
    detach();
}

void ImGuiLayer::attach(Renderer& renderer, Window& window) {
    if (initialized_)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    // ImGui stores the pointer, not the string; iniFilename_ owns the storage.
    io.IniFilename = iniFilename_.empty() ? nullptr : iniFilename_.c_str();

    if (!ImGui_ImplWin32_Init(window.nativeHandle())) {
        Log::error("ImGuiLayer", "Cannot initialize the ImGui Win32 backend");
        ImGui::DestroyContext();
        return;
    }

    if (!renderer_.initialize(renderer.device(), renderer.swapchain().format())) {
        Log::error("ImGuiLayer", "Cannot initialize the ImGui RHI renderer");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return;
    }

    window.setMessageHandler(
        [this](HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
            handleNativeMessage(handle, message, wParam, lParam);
        });
    renderer.setOverlay(this);
    engineRenderer_ = &renderer;
    window_ = &window;
    initialized_ = true;
    Log::info("ImGuiLayer", "ImGui editor layer attached");
}

void ImGuiLayer::detach() {
    if (!initialized_)
        return;
    // Persist the layout (including the docking layout) while the context is alive;
    // DestroyContext would make the data unreachable.
    if (ImGui::GetCurrentContext()) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.IniFilename != nullptr)
            ImGui::SaveIniSettingsToDisk(io.IniFilename);
    }
    if (window_) {
        // The hook captures this, so it must go before the layer can be destroyed.
        window_->setMessageHandler({});
        window_ = nullptr;
    }
    if (engineRenderer_) {
        engineRenderer_->waitIdle();
        engineRenderer_->setOverlay(nullptr);
        engineRenderer_ = nullptr;
    }
    renderer_.shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
    Log::info("ImGuiLayer", "ImGui editor layer detached");
}

void ImGuiLayer::handleNativeMessage(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
    if (initialized_)
        ImGui_ImplWin32_WndProcHandler(handle, message, wParam, lParam);
}

void ImGuiLayer::setIniPath(std::string path) {
    iniFilename_ = std::move(path);
}

const std::string& ImGuiLayer::iniPath() const {
    return iniFilename_;
}

void ImGuiLayer::beginFrame() {
    if (!initialized_)
        return;
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::endFrame() {
    if (!initialized_ || !ImGui::GetCurrentContext())
        return;
    // Must run every frame once NewFrame ran, even when the swapchain later skips the
    // frame, otherwise the next NewFrame trips ImGui's forgot-to-render check.
    ImGui::Render();
}

void ImGuiLayer::recordOverlay(RenderContext& context) {
    if (!initialized_)
        return;
    if (!ImGui::GetCurrentContext())
        return;

    // Draw data is built by endFrame() after the panels ran. The overlay is the last
    // writer before the swapchain presents, so it must always leave the acquired
    // backbuffer in PRESENT_SRC. That is only guaranteed by touching it: when the
    // forward pass skipped the backbuffer this frame (empty draw list) the acquired
    // image may still be in the undefined layout, and presenting it unmodified is a
    // validation error. Scene contents are the forward pass's business, so a cleared
    // backbuffer never loses information here.
    const ImDrawData* drawData = ImGui::GetDrawData();
    const bool hasUi = drawData != nullptr && drawData->Valid && drawData->TotalIdxCount > 0;
    rhi::ISwapchain& swapchain = context.swapchain();
    const bool backBufferWritten = context.backBufferWritten();
    if (!hasUi && backBufferWritten)
        return;

    rhi::IGraphicsCommandEncoder& encoder = context.encoder();
    const bool needsClear = !backBufferWritten;
    const rhi::TextureBarrier toAttachment{
        .texture = swapchain.currentTexture(),
        .before = needsClear ? rhi::ResourceState::Undefined : rhi::ResourceState::Present,
        .after = rhi::ResourceState::ColorAttachment,
    };
    encoder.resourceBarriers(std::span{&toAttachment, 1});

    encoder.beginRendering({
        .renderArea = {.width = swapchain.width(), .height = swapchain.height()},
        .colorAttachments = {{
            .view = swapchain.currentTextureView(),
            .loadOp = needsClear ? rhi::LoadOp::Clear : rhi::LoadOp::Load,
        }},
    });
    if (hasUi)
        renderer_.render(encoder, *drawData, context.frameIndex());
    encoder.endRendering();

    const rhi::TextureBarrier toPresent{
        .texture = swapchain.currentTexture(),
        .before = rhi::ResourceState::ColorAttachment,
        .after = rhi::ResourceState::Present,
    };
    encoder.resourceBarriers(std::span{&toPresent, 1});
}

void ImGuiLayer::onSwapchainRecreated(Renderer& renderer) {
    if (!initialized_)
        return;
    // Geometry buffers and the font atlas are resolution independent, and the pipeline
    // uses a dynamic viewport and scissor, so only a changed color format matters.
    renderer_.onColorFormatChanged(renderer.swapchain().format());
}

} // namespace engine::editor
