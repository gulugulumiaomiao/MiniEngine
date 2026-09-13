#pragma once

#include "runtime/application/Application.h"
#include "core/base/Singleton.h"
#include "core/filesystem/VirtualPath.h"
#include "render/scene/RenderScene.h"
#include "runtime/config/EngineConfig.h"
#include "runtime/config/ProjectConfig.h"
#include "scene/scene/Scene.h"

#include <filesystem>
#include <memory>
#include <string>

#if defined(MINI_EDITOR)
#include "tools/editor/EditorConfig.h"
#endif

namespace engine {

class Renderer;
class Window;
namespace rhi {
class IContextFactory;
}

class Engine final : public Singleton<Engine> {
public:
    ~Engine();

    [[nodiscard]] int run(Application& application,
                          const rhi::IContextFactory& contextFactory,
                          const std::filesystem::path& configPath);
    void requestQuit() { shouldQuit_ = true; }

    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] float deltaTime() const { return deltaTime_; }
    [[nodiscard]] const WindowConfig& windowConfig() const { return config_.window; }
    [[nodiscard]] Window& window();
    [[nodiscard]] Renderer& renderer();
    [[nodiscard]] Scene& scene() { return *scene_; }
    [[nodiscard]] RenderScene& renderScene() { return renderScene_; }
    [[nodiscard]] bool loadScene(const VirtualPath& scenePath);
    [[nodiscard]] bool reloadScene();
    // Replaces the active Scene with an empty one and detaches it from any source path.
    void resetScene(std::string name = "Scene");
    [[nodiscard]] const VirtualPath& activeScenePath() const { return activeScenePath_; }

#if defined(MINI_EDITOR)
    // Loads the editor config from the given path. Called by the editor during
    // construction; Engine::initialize does not reload it. Missing files are
    // auto-created from defaults; on load failure the boot-default config is kept and
    // an error is logged (the editor continues without fatal).
    void loadEditorConfig(const std::filesystem::path& path);
    [[nodiscard]] const EditorConfig& editorConfig() const { return editorConfig_; }
    // Mutable access for the editor GUI (recent-project registry).
    [[nodiscard]] EditorConfig& editorConfig() { return editorConfig_; }
    // Persists the editor config to the path stored by loadEditorConfig(). Logs a
    // warning when saving fails. No-op effect when the config was never loaded.
    void saveEditorConfig();
#endif

    // Mounts the project at projectRoot (assets://, library://, shader-cache://,
    // shader-bin://), reinitializes the asset system and GPU resource managers, and
    // loads the selected scene. When another project is already open it is closed
    // first. Returns false and leaves the engine without an open project on failure.
    [[nodiscard]] bool openProject(const std::filesystem::path& projectRoot);
    // Tears down the asset system, the GPU resource managers and the renderer, then
    // unmounts the project schemes. Only the window stays alive, so the editor must
    // re-attach its overlay to the renderer built by the next openProject(). No-op
    // when no project is open.
    void closeProject();
    [[nodiscard]] bool isProjectOpen() const { return projectOpen_; }
    [[nodiscard]] const ProjectConfig& projectConfig() const { return projectConfig_; }

private:
    friend class Singleton<Engine>;
    Engine();

    [[nodiscard]] bool initialize(const std::filesystem::path& configPath,
                                  const rhi::IContextFactory& contextFactory);
    void loop(Application& application);
    void shutdown();
    // Requests a reload of the active Scene when its source asset changes on disk.
    // Reinstalled whenever the asset system is brought up. AssetManager has a single
    // listener slot rather than a multicast list, so whoever installs last owns the
    // reload policy; the editor reclaims the slot after openProject() to route scene
    // changes through its own document instead.
    void installSceneChangeListener();
    // Shuts down every subsystem that depends on the active project, the renderer
    // included; only the window survives. Called by both closeProject() and shutdown().
    void teardownProjectSubsystems();
    // Brings up the asset system after project mounts are in place.
    [[nodiscard]] bool initializeProjectSubsystems();
    // Creates the renderer and GPU managers. Called by initialize() and openProject().
    // 编辑器进入项目前禁用管线缓存，其他情况默认启用。
    [[nodiscard]] bool initializeGpuManagers(const rhi::IContextFactory& contextFactory,
                                             bool enablePipelineCache = true);
    // Applies the effective window config (engine default -> editor preference ->
    // project override) and creates or recreates the window.
    void applyWindowConfig(const WindowConfig& effective);
    // 合并引擎默认值、编辑器偏好（仅 MINI_EDITOR）与项目覆盖值。
    [[nodiscard]] WindowConfig effectiveWindowConfig() const;

    EngineConfig config_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Scene> scene_{std::make_unique<Scene>("Main Scene")};
    RenderScene renderScene_;
    VirtualPath activeScenePath_;
    float deltaTime_{};
    bool sceneReloadPending_{};
    bool shouldQuit_{};
    bool running_{};
#if defined(MINI_EDITOR)
    std::filesystem::path editorConfigPath_;
    EditorConfig editorConfig_;
#endif
    ProjectConfig projectConfig_;
    bool projectOpen_{};
    const rhi::IContextFactory* contextFactory_{};
};

} // namespace engine

#define ENGINE (::engine::Engine::instance())
