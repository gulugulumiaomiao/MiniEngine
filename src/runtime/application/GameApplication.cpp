#include "runtime/application/GameApplication.h"

#include "core/base/BuildConfig.h"
#include "core/logging/Log.h"
#include "runtime/engine/Engine.h"

namespace engine {

AppConfig GameApplication::getConfig() const {
    return {
        .name = std::string{build::kWindowTitle},
        .width = 1280,
        .height = 720,
        .vsync = true,
    };
}

void GameApplication::onStart() {
    if (!ENGINE.loadScene(
            VirtualPath{"asset://scenes/blinn_phong_showcase.scene.json"})) {
        Log::error("GameApplication", "Cannot load the showcase Scene");
        ENGINE.requestQuit();
    }
}

void GameApplication::onUpdate(float deltaTime) { (void)deltaTime; }

void GameApplication::onStop() {
    ENGINE.scene().clear();
    ENGINE.renderScene().clear();
}

} // namespace engine
