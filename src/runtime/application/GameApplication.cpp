#include "runtime/application/GameApplication.h"

#include "core/logging/Log.h"
#include "runtime/engine/Engine.h"

namespace engine {

void GameApplication::onStart() {
    if (!ENGINE.loadScene(VirtualPath{"asset://scenes/blinn_phong_showcase.scene.json"})) {
        Log::error("GameApplication", "Cannot load the showcase Scene");
        ENGINE.requestQuit();
    }
}

void GameApplication::onUpdate(float deltaTime) {
    (void)deltaTime;
}

void GameApplication::onStop() {
    ENGINE.scene().clear();
    ENGINE.renderScene().clear();
}

} // namespace engine
