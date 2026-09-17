#include "tools/editor/StatisticsPanel.h"

#include "imgui.h"
#include "runtime/engine/Engine.h"
#include "render/renderer/Renderer.h"
#include "runtime/window/Window.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/node/Node.h"
#include "scene/scene/Scene.h"
#include "tools/editor/SceneDocument.h"

#include <cmath>
#include <string>

namespace engine::editor {
namespace {

struct SceneStats {
    std::uint32_t cameras{};
    std::uint32_t meshes{};
    std::uint32_t lights{};
};

void collectStats(Node& node, SceneStats& stats) {
    if (node.getComponent<CameraComponent>() != nullptr)
        ++stats.cameras;
    if (node.getComponent<MeshComponent>() != nullptr)
        ++stats.meshes;
    if (node.getComponent<LightComponent>() != nullptr)
        ++stats.lights;
    for (const NodeHandle child : node.children()) {
        if (Node* childNode = node.scene().findNode(child))
            collectStats(*childNode, stats);
    }
}

const CameraComponent* findPrimaryCamera(Node& node) {
    if (const CameraComponent* camera = node.getComponent<CameraComponent>(); camera && camera->primary)
        return camera;
    for (const NodeHandle child : node.children()) {
        if (Node* childNode = node.scene().findNode(child)) {
            if (const CameraComponent* camera = findPrimaryCamera(*childNode))
                return camera;
        }
    }
    return nullptr;
}

} // namespace

void StatisticsPanel::draw() {
    if (!ImGui::Begin("Statistics", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    if (!document_.valid()) {
        ImGui::TextUnformatted("No Scene document is open");
        ImGui::End();
        return;
    }

    Scene& scene = document_.scene();
    SceneStats stats;
    collectStats(scene.root(), stats);
    ImGui::Text("Scene: %s%s", std::string{scene.name()}.c_str(),
                document_.dirty() ? " *" : "");
    ImGui::Text("Nodes: %zu   Cameras: %u   Meshes: %u   Lights: %u",
                scene.nodeCount(), stats.cameras, stats.meshes, stats.lights);

    const auto [width, height] = ENGINE.window().framebufferSize();
    ImGui::Text("Window: %u x %u", width, height);
    ImGui::Text("Viewport: %u x %u", ENGINE.renderer().sceneWidth(), ENGINE.renderer().sceneHeight());
    const float deltaTime = ENGINE.deltaTime();
    ImGui::Text("Frame: %.2f ms (%.0f FPS)", deltaTime * 1000.0F,
                deltaTime > 0.0F ? 1.0F / deltaTime : 0.0F);

    if (const CameraComponent* camera = findPrimaryCamera(scene.root())) {
        ImGui::Separator();
        ImGui::Text("Primary Camera");
        ImGui::Text("  %s, FOV %.1f, Near %.2f, Far %.1f",
                    camera->projection == CameraProjection::Perspective ? "Perspective"
                                                                        : "Orthographic",
                    camera->fieldOfView, camera->nearPlane, camera->farPlane);
    } else {
        ImGui::TextUnformatted("No primary camera (scene renders the clear color)");
    }

    ImGui::Separator();
    ImGui::End();
}

} // namespace engine::editor
