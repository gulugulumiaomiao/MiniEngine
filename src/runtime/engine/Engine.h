#pragma once

#include "runtime/application/Application.h"
#include "core/base/Singleton.h"
#include "core/filesystem/VirtualPath.h"
#include "render/scene/RenderScene.h"
#include "scene/scene/Scene.h"

#include <memory>

namespace engine {

class Renderer;
class Window;
namespace rhi {
class IContextFactory;
}

class Engine final : public Singleton<Engine> {
public:
    ~Engine();

    [[nodiscard]] int run(Application& application, const rhi::IContextFactory& contextFactory);
    void requestQuit() { shouldQuit_ = true; }

    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] float deltaTime() const { return deltaTime_; }
    [[nodiscard]] const AppConfig& config() const { return config_; }
    [[nodiscard]] Window& window();
    [[nodiscard]] Renderer& renderer();
    [[nodiscard]] Scene& scene() { return *scene_; }
    [[nodiscard]] RenderScene& renderScene() { return renderScene_; }
    [[nodiscard]] bool loadScene(const VirtualPath& scenePath);
    [[nodiscard]] bool reloadScene();
    [[nodiscard]] const VirtualPath& activeScenePath() const { return activeScenePath_; }

private:
    friend class Singleton<Engine>;
    Engine();

    [[nodiscard]] bool initialize(const AppConfig& config,
                                  const rhi::IContextFactory& contextFactory);
    void loop(Application& application);
    void shutdown();

    AppConfig config_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Scene> scene_{std::make_unique<Scene>("Main Scene")};
    RenderScene renderScene_;
    VirtualPath activeScenePath_;
    float deltaTime_{};
    bool sceneReloadPending_{};
    bool shouldQuit_{};
    bool running_{};
};

} // namespace engine

#define ENGINE (::engine::Engine::instance())
