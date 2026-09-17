#include "tools/editor/SceneViewPanel.h"

#include "imgui.h"
#include "render/renderer/Renderer.h"
#include "runtime/engine/Engine.h"
#include "tools/editor/ImGuiRenderer.h"
#include "tools/editor/SceneDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace engine::editor {

void SceneViewPanel::draw() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0F, 0.0F});
    const bool visible = ImGui::Begin("Scene View", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (visible) {
        if (!document_.valid()) {
            ImGui::TextUnformatted("No Scene document is open");
        } else {
            const ImVec2 size = ImGui::GetContentRegionAvail();
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            if (size.x > 0.0F && size.y > 0.0F) {
                const auto width = static_cast<std::uint32_t>(std::max(1.0F, std::floor(size.x * scale.x)));
                const auto height = static_cast<std::uint32_t>(std::max(1.0F, std::floor(size.y * scale.y)));
                ENGINE.renderer().setSceneViewport(width, height);
                ImGui::Image(ImGuiRenderer::kSceneTextureId, size);
            }
        }
    }
    ImGui::End();
}

} // namespace engine::editor