#pragma once

#include "core/BuildConfig.h"
#include "platform/Window.h"
#include "renderer/RenderScene.h"
#include "renderer/Renderer.h"

namespace engine {

class Application final {
public:
    Application();
    void run();

private:
    Window window_{1280, 720, build::kWindowTitle};
    Renderer renderer_{window_};
    RenderScene scene_;
};

} // namespace engine
