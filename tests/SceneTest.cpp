#include "core/math/Math.h"
#include "render/scene/RenderScene.h"
#include "scene/components/CameraComponent.h"
#include "scene/components/LightComponent.h"
#include "scene/components/MaterialComponent.h"
#include "scene/components/MeshComponent.h"
#include "scene/node/Node.h"
#include "scene/scene/Scene.h"
#include "scene/components/TransformComponent.h"

namespace {

struct LifecycleCounts {
    int attached{};
    int detached{};
    int enabled{};
    int disabled{};
    int updated{};
};

class ProbeComponent final : public engine::Component {
public:
    explicit ProbeComponent(LifecycleCounts& counts) : counts_(counts) {}

private:
    void onAttach() override { ++counts_.attached; }
    void onDetach() override { ++counts_.detached; }
    void onEnable() override { ++counts_.enabled; }
    void onDisable() override { ++counts_.disabled; }
    void onUpdate(float) override { ++counts_.updated; }

    LifecycleCounts& counts_;
};

bool near(const engine::math::Vec3& left, const engine::math::Vec3& right) {
    return engine::math::distance(left, right) < 0.0001F;
}

} // namespace

int main() {
    using namespace engine;

    Scene scene{"Test Scene"};
    const NodeHandle sceneRootHandle = scene.rootHandle();
    const NodeHandle rootHandle = scene.createNode("Root");
    const NodeHandle childHandle = scene.createNode("Child");
    Node* sceneRoot = scene.findNode(sceneRootHandle);
    Node* root = scene.findNode(rootHandle);
    Node* child = scene.findNode(childHandle);
    if (!sceneRoot || !root || !child || scene.nodeCount() != 3 ||
        root->parent() != sceneRootHandle || !root->getComponent<TransformComponent>()) {
        return 1;
    }

    if (!child->setParent(*root) || child->parent() != rootHandle || root->children().size() != 1 ||
        root->setParent(childHandle) || sceneRoot->setParent(rootHandle) ||
        scene.destroyNode(sceneRootHandle)) {
        return 2;
    }
    Scene otherScene{"Other"};
    if (child->setParent(otherScene.root()))
        return 17;

    root->transform().setLocalPosition({2.0F, 0.0F, 0.0F});
    child->transform().setLocalPosition({0.0F, 3.0F, 0.0F});
    if (!near(child->transform().worldPosition(), {2.0F, 3.0F, 0.0F})) {
        return 3;
    }
    root->transform().setLocalPosition({4.0F, 0.0F, 0.0F});
    if (!near(child->transform().worldPosition(), {4.0F, 3.0F, 0.0F})) {
        return 4;
    }

    LifecycleCounts counts;
    ProbeComponent* probe = child->addComponent<ProbeComponent>(counts);
    if (!probe || counts.attached != 1 || counts.enabled != 1 ||
        child->addComponent<ProbeComponent>(counts) != probe) {
        return 5;
    }
    scene.update(1.0F / 60.0F);
    if (counts.updated != 1)
        return 6;

    root->setActive(false);
    if (child->activeInHierarchy() || probe->active() || counts.disabled != 1) {
        return 7;
    }
    scene.update(1.0F / 60.0F);
    if (counts.updated != 1)
        return 8;
    root->setActive(true);
    if (!probe->active() || counts.enabled != 2)
        return 9;

    probe->setEnabled(false);
    if (probe->active() || counts.disabled != 2)
        return 10;
    probe->setEnabled(true);
    if (!probe->active() || counts.enabled != 3)
        return 11;

    MeshComponent* mesh = child->addComponent<MeshComponent>();
    MaterialComponent* material = child->addComponent<MaterialComponent>();
    mesh->mesh = MeshHandle{7, 2};
    material->setMaterial(0, MaterialHandle{3, 1});
    material->setMaterial(2, MaterialHandle{8, 4});
    if (mesh->mesh != MeshHandle{7, 2} || material->material(1) != MaterialHandle{3, 1} ||
        material->material(2) != MaterialHandle{8, 4} ||
        root->removeComponent<TransformComponent>()) {
        return 12;
    }

    const NodeHandle cameraHandle = scene.createNode("Camera");
    Node* cameraNode = scene.findNode(cameraHandle);
    CameraComponent* camera = cameraNode->addComponent<CameraComponent>();
    camera->primary = true;
    camera->priority = 10;
    cameraNode->transform().setLocalPosition({0.0F, 0.0F, 5.0F});

    const NodeHandle lightHandle = scene.createNode("Sun");
    Node* lightNode = scene.findNode(lightHandle);
    LightComponent* light = lightNode->addComponent<LightComponent>();
    light->type = LightType::Directional;
    light->color = {0.8F, 0.7F, 0.6F};
    light->intensity = 2.0F;

    RenderScene renderScene;
    scene.buildRenderScene(renderScene, 16.0F / 9.0F);
    if (renderScene.objects().size() != 1 || !renderScene.camera() ||
        renderScene.lights().size() != 1 ||
        renderScene.objects().front().mesh != MeshHandle{7, 2} ||
        renderScene.objects().front().material(1) != MaterialHandle{3, 1} ||
        !near(renderScene.camera()->worldPosition, {0.0F, 0.0F, 5.0F}) ||
        !near(renderScene.lights().front().direction, {0.0F, 0.0F, -1.0F}) ||
        renderScene.lights().front().intensity != 2.0F) {
        return 18;
    }

    if (!scene.destroyNode(cameraHandle) || !scene.destroyNode(lightHandle)) {
        return 19;
    }

    child = scene.findNode(childHandle);
    if (!child->removeComponent<ProbeComponent>() || counts.disabled != 3 || counts.detached != 1 ||
        child->getComponent<ProbeComponent>()) {
        return 13;
    }

    if (!scene.destroyNode(rootHandle) || scene.findNode(rootHandle) ||
        scene.findNode(childHandle) || scene.nodeCount() != 1) {
        return 14;
    }
    const NodeHandle reused = scene.createNode("Reused");
    if (reused.index != rootHandle.index || reused.generation == rootHandle.generation ||
        scene.findNode(reused)->parent() != sceneRootHandle) {
        return 15;
    }
    scene.clear();
    if (scene.nodeCount() != 1 || !scene.findNode(sceneRootHandle) ||
        !scene.root().children().empty()) {
        return 16;
    }
}
