#pragma once

#include "runtime/application/Application.h"
#include "render/renderer/RenderResources.h"
#include "scene/node/SceneHandles.h"

namespace engine {

class GameApplication final : public Application {
public:
    [[nodiscard]] AppConfig getConfig() const override;

protected:
    void onStart() override;
    void onUpdate(float deltaTime) override;
    void onStop() override;

private:
    MeshHandle triangle_;
    MaterialHandle warmMaterial_;
    MaterialHandle coolMaterial_;
    NodeHandle warmNode_;
    NodeHandle coolNode_;
    NodeHandle cameraNode_;
    NodeHandle lightNode_;
};

} // namespace engine
