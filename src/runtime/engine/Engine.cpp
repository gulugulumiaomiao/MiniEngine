#include "runtime/engine/Engine.h"

#include "asset/importer/AssetImportPipeline.h"
#include "asset/manager/AssetManager.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "render/gpu/frame/FrameGpuManager.h"
#include "render/pipeline/MiniForwardPipeline.h"
#include "render/pipeline/RenderPipeline.h"
#include "render/gpu/material/MaterialGpuManager.h"
#include "render/gpu/mesh/MeshGpuManager.h"
#include "render/gpu/pipeline/GraphicsPipelineManager.h"
#include "render/gpu/shader/ShaderGpuManager.h"
#include "render/gpu/texture/TextureGpuManager.h"
#include "render/material/Material.h"
#include "render/material/MaterialManager.h"
#include "render/mesh/Mesh.h"
#include "render/mesh/MeshManager.h"
#include "render/renderer/Renderer.h"
#include "render/shader/Shader.h"
#include "render/shader/ShaderManager.h"
#include "render/texture/TextureManager.h"
#include "rhi/RhiFactory.h"
#include "runtime/window/Window.h"
#include "scene/scene/SceneAsset.h"

#if defined(MINI_EDITOR)
#include "tools/editor/ProjectTemplate.h"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace engine {

Engine::Engine() = default;

#if defined(MINI_EDITOR)
void Engine::loadEditorConfig(const std::filesystem::path& path) {
    editorConfigPath_ = path;
    const auto loaded = EditorConfig::load(path);
    if (!loaded) {
        Log::error(
            "Engine", "Cannot load or create editor configuration: %s", path.string().c_str());
        return;
    }
    editorConfig_ = *loaded;
    Log::info("Engine", "Loaded editor configuration from %s", path.string().c_str());
}

void Engine::saveEditorConfig() {
    if (!editorConfig_.save(editorConfigPath_))
        Log::warn("Engine", "Cannot save editor config");
}
#endif

WindowConfig Engine::effectiveWindowConfig() const {
    WindowConfig effective = config_.window;
#if defined(MINI_EDITOR)
    if (editorConfig_.window.has_value()) {
        effective.width = editorConfig_.window->width;
        effective.height = editorConfig_.window->height;
        effective.vsync = editorConfig_.window->vsync;
    }
#endif
    if (projectConfig_.window.has_value()) {
        effective.width = projectConfig_.window->width;
        effective.height = projectConfig_.window->height;
        effective.vsync = projectConfig_.window->vsync;
    }
    return effective;
}
Engine::~Engine() = default;

int Engine::run(Application& application,
                const rhi::IContextFactory& contextFactory,
                const std::filesystem::path& configPath) {
    if (running_) {
        Log::error("Engine", "Engine is already running an Application");
        return 1;
    }

    contextFactory_ = &contextFactory;

    if (!initialize(configPath, contextFactory))
        return 1;

    Log::info("Engine", "Starting application");
    application.onStart();
    loop(application);
    application.onStop();
    Log::info("Engine", "Stopping application");
    shutdown();
    return 0;
}

bool Engine::initialize(const std::filesystem::path& configPath,
                        const rhi::IContextFactory& contextFactory) {
    const std::filesystem::path normalizedConfigPath =
        std::filesystem::absolute(configPath).lexically_normal();
    std::string configError;
    const std::optional<EngineConfig> loadedConfig =
        EngineConfig::load(normalizedConfigPath, configError);
    if (!loadedConfig) {
        Log::fatal("Engine",
                   "Cannot load engine configuration %s: %s",
                   normalizedConfigPath.string().c_str(),
                   configError.c_str());
    }
    Log::info(
        "Engine", "Loaded engine configuration from %s", normalizedConfigPath.string().c_str());
    config_ = *loadedConfig;
    shouldQuit_ = false;
    deltaTime_ = 0.0F;

    std::filesystem::path workingDirectory{config_.workingDirectory};
    if (workingDirectory.is_relative()) {
        workingDirectory = normalizedConfigPath.parent_path() / workingDirectory;
    }
    workingDirectory = std::filesystem::absolute(workingDirectory).lexically_normal();
    std::error_code workingDirectoryError;
    if (!std::filesystem::is_directory(workingDirectory, workingDirectoryError) ||
        workingDirectoryError) {
        Log::fatal("Engine",
                   "Engine working directory does not exist: %s",
                   workingDirectory.string().c_str());
    }
    std::filesystem::current_path(workingDirectory, workingDirectoryError);
    if (workingDirectoryError) {
        Log::fatal(
            "Engine", "Cannot set engine working directory: %s", workingDirectory.string().c_str());
    }
    Log::info("Engine", "Working directory: %s", workingDirectory.string().c_str());

#if defined(MINI_EDITOR)
    // Editor starts without project mounts; openProject() supplies them.
    Log::info("Engine", "Editor mode: waiting for project selection");
#else
    // Game runtime: project mounts are supplied via working directory.
    const std::vector<std::pair<std::string_view, std::filesystem::path>> gameMounts = {
        {"assets", workingDirectory / "assets"},
        {"library", workingDirectory / "library"},
        {"shader-cache", workingDirectory / "generated-shaders/runtime"},
        {"shader-bin", workingDirectory / "generated-shaders/compiled"},
    };
    for (const auto& [scheme, directory] : gameMounts) {
        const bool readOnly = (scheme == "shader-bin");
        if (!FILE_SYSTEM.mountDirectory(scheme, directory, readOnly)) {
            Log::fatal(
                "Engine", "Cannot mount %s:// at %s", scheme.data(), directory.string().c_str());
        }
        Log::info("Engine",
                  "Mounted %s:// at %s%s",
                  scheme.data(),
                  directory.string().c_str(),
                  readOnly ? " (read-only)" : "");
    }
    if (!initializeProjectSubsystems()) {
        shutdown();
        return false;
    }
#endif

    applyWindowConfig(effectiveWindowConfig());

    // 编辑器进入项目前没有 shader-cache:// 挂载，不使用管线缓存。
#if defined(MINI_EDITOR)
    constexpr bool enablePipelineCache = false;
#else
    constexpr bool enablePipelineCache = true;
#endif
    if (!initializeGpuManagers(contextFactory, enablePipelineCache)) {
        shutdown();
        return false;
    }

    running_ = true;
    return true;
}

void Engine::applyWindowConfig(const WindowConfig& effective) {
    // Destroy old window and renderer first
    if (renderer_) {
        renderer_->waitIdle();
        renderer_.reset();
    }
    window_.reset();

    // Create new window
    window_ = std::make_unique<Window>(effective.width, effective.height, "Mini Engine");
    Log::info("Engine",
              "Window: %ux%u vsync=%s",
              effective.width,
              effective.height,
              effective.vsync ? "on" : "off");
}

bool Engine::initializeProjectSubsystems() {
    if (!ASSET_MANAGER.initialize()) {
        Log::error("Engine", "Cannot initialize asset system");
        return false;
    }
    installSceneChangeListener();
    return true;
}

bool Engine::initializeGpuManagers(const rhi::IContextFactory& contextFactory,
                                   bool enablePipelineCache) {
    const auto [width, height] = window_->framebufferSize();
    rhi::Context context = contextFactory.createContext({
        .surface = {.windowSystem = rhi::WindowSystem::Win32,
                    .nativeDisplay = window_->nativeInstance(),
                    .nativeWindow = window_->nativeHandle()},
        .swapchain = {.width = width, .height = height, .vsync = config_.window.vsync},
        .enablePipelineCache = enablePipelineCache,
    });
    renderer_ = std::make_unique<Renderer>(*window_, std::move(context));

    RenderPipelineRegistry pipelineRegistry;
    pipelineRegistry.registerPipeline("MiniForward",
                                      []() { return std::make_unique<MiniForwardPipeline>(); });
    renderer_->setPipeline(pipelineRegistry.create(config_.render.pipeline));

    if (!FRAME_GPU_MANAGER.initialize(renderer_->device()) ||
        !MESH_GPU_MANAGER.initialize(renderer_->device()) ||
        !TEXTURE_GPU_MANAGER.initialize(renderer_->device()) ||
        !MATERIAL_GPU_MANAGER.initialize(renderer_->device(),
                                         FRAME_GPU_MANAGER.materialLayout(),
                                         FrameGpuManager::kFramesInFlight) ||
        !SHADER_GPU_MANAGER.initialize(renderer_->device()) ||
        !GRAPHICS_PIPELINE_MANAGER.initialize(renderer_->device(),
                                              FRAME_GPU_MANAGER.sceneLayout(),
                                              FRAME_GPU_MANAGER.materialLayout())) {
        Log::error("Engine", "Cannot initialize GPU resource managers");
        return false;
    }
    return true;
}

void Engine::teardownProjectSubsystems() {
    scene_->clear();
    renderScene_.clear();
    if (renderer_)
        renderer_->waitIdle();
    MATERIAL_GPU_MANAGER.shutdown();
    GRAPHICS_PIPELINE_MANAGER.shutdown();
    SHADER_GPU_MANAGER.shutdown();
    TEXTURE_GPU_MANAGER.shutdown();
    MESH_GPU_MANAGER.shutdown();
    FRAME_GPU_MANAGER.shutdown();
    renderer_.reset();
    MESH_MANAGER.clear();
    MATERIAL_MANAGER.clear();
    TEXTURE_MANAGER.clear();
    SHADER_MANAGER.clear();
    ASSET_MANAGER.shutdown();
    activeScenePath_ = {};
    sceneReloadPending_ = false;
}

void Engine::installSceneChangeListener() {
    ASSET_MANAGER.setChangeListener([this](const VirtualPath& path, AssetType type, bool removed) {
        if (type != AssetType::Scene || path != activeScenePath_)
            return;
        if (removed) {
            Log::warn("Engine", "Active scene source was removed: %s", path.string().c_str());
            return;
        }
        sceneReloadPending_ = true;
    });
}

void Engine::loop(Application& application) {
    using Clock = std::chrono::steady_clock;
    auto previousTime = Clock::now();
    while (!shouldQuit_ && !window_->shouldClose()) {
        window_->pollEvents();
        ASSET_IMPORT_PIPELINE.processFileEvents();

        if (sceneReloadPending_) {
            sceneReloadPending_ = false;
            if (!reloadScene()) {
                Log::error("Engine", "Keeping the previous active Scene");
            }
        }

        const auto currentTime = Clock::now();
        deltaTime_ =
            std::min(std::chrono::duration<float>(currentTime - previousTime).count(), 0.25F);
        previousTime = currentTime;

        application.onUpdate(deltaTime_);
        scene_->update(deltaTime_);
        const auto [width, height] = window_->framebufferSize();
        const float aspectRatio =
            height != 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0F;
        scene_->buildRenderScene(renderScene_, aspectRatio);
        renderer_->renderFrame(renderScene_);
    }
    renderer_->waitIdle();
}

void Engine::shutdown() {
    teardownProjectSubsystems();
    window_.reset();
    scene_ = std::make_unique<Scene>("Main Scene");
    activeScenePath_ = {};
    sceneReloadPending_ = false;
    running_ = false;
    shouldQuit_ = false;
    deltaTime_ = 0.0F;
    projectOpen_ = false;
    projectConfig_ = {};
    contextFactory_ = nullptr;
}

bool Engine::loadScene(const VirtualPath& scenePath) {
    if (!scenePath.valid() || scenePath.scheme() != "assets") {
        Log::error("Engine", "Invalid scene path: %s", scenePath.string().c_str());
        return false;
    }
    const std::shared_ptr<SceneAsset> asset = ASSET_MANAGER.loadAsset<SceneAsset>(scenePath);
    if (!asset)
        return false;

    const SceneInstantiationContext context{
        .loadMesh = [](const VirtualPath& path) { return MESH_MANAGER.load(path); },
        .loadMaterial = [](const VirtualPath& path) { return MATERIAL_MANAGER.load(path); },
    };
    std::unique_ptr<Scene> loaded = asset->instantiate(context);
    if (!loaded) {
        Log::error("Engine", "Cannot instantiate scene: %s", scenePath.string().c_str());
        return false;
    }
    scene_ = std::move(loaded);
    activeScenePath_ = scenePath;
    renderScene_.clear();
    Log::info("Engine", "Loaded scene: %s", scenePath.string().c_str());
    return true;
}

bool Engine::reloadScene() {
    if (!activeScenePath_.valid()) {
        Log::warn("Engine", "No active asset scene to reload");
        return false;
    }
    return loadScene(activeScenePath_);
}

void Engine::resetScene(std::string name) {
    scene_ = std::make_unique<Scene>(std::move(name));
    activeScenePath_ = {};
    renderScene_.clear();
    sceneReloadPending_ = false;
    Log::info("Engine", "Scene was reset to an empty scene");
}

Window& Engine::window() {
    if (!window_)
        Log::fatal("Engine", "Window is unavailable");
    return *window_;
}

Renderer& Engine::renderer() {
    if (!renderer_)
        Log::fatal("Engine", "Renderer is unavailable");
    return *renderer_;
}

bool Engine::openProject(const std::filesystem::path& projectRoot) {
    if (!contextFactory_ || !window_) {
        Log::error("Engine", "Engine must be initialized before opening a project");
        return false;
    }
    // Release any existing GPU context before mounting the project. A previous
    // project is released by closeProject() below. The editor boot, however, already
    // owns a context for the UI bound to the current native window -- without
    // releasing it first, initializeGpuManagers stacks a second live swapchain on
    // the same window and vkCreateSwapchainKHR fails. teardownProjectSubsystems() is
    // idempotent, so both paths are safe here.
    if (projectOpen_)
        closeProject();
    else
        teardownProjectSubsystems();
    const std::filesystem::path normalized =
        std::filesystem::absolute(projectRoot).lexically_normal();
    if (!isProjectDirectory(normalized)) {
        Log::error("Engine", "Not a valid project directory: %s", normalized.string().c_str());
        return false;
    }

#if defined(MINI_EDITOR)
    // Repair the engine contract assets (Error Material, fallback Shader, shared GLSL
    // includes) on every open: they are hardcoded in engine code, so a project that lost
    // or corrupted them would fail to render rather than fail to load. Sample content is
    // deliberately NOT refreshed here -- it was seeded once at creation and belongs to the
    // project, so the user's edits to the demo materials, shaders and scenes survive.
    std::string syncError;
    if (!editor::syncEngineContractIntoProject(normalized, syncError)) {
        Log::error("Engine", "Cannot copy the built-in content into the project: %s",
                   syncError.c_str());
        return false;
    }
#endif

    std::string error;
    const std::optional<ProjectConfig> loaded =
        ProjectConfig::load(projectConfigPath(normalized), error);
    if (!loaded) {
        Log::error("Engine", "Cannot load project configuration: %s", error.c_str());
        return false;
    }
    projectConfig_ = *loaded;

    // Mount project schemes (hardcoded).
    for (const ProjectMount& mount : projectMounts(normalized)) {
        if (!FILE_SYSTEM.mountDirectory(mount.scheme, mount.directory, mount.readOnly)) {
            Log::error("Engine",
                       "Cannot mount %s:// at %s",
                       mount.scheme.c_str(),
                       mount.directory.string().c_str());
            closeProject();
            return false;
        }
        Log::info("Engine",
                  "Mounted %s:// at %s%s",
                  mount.scheme.c_str(),
                  mount.directory.string().c_str(),
                  mount.readOnly ? " (read-only)" : "");
    }

    if (!initializeProjectSubsystems()) {
        closeProject();
        return false;
    }

    if (!ensureProjectMainScene(error)) {
        Log::error("Engine", "Cannot set up the project scene: %s", error.c_str());
        closeProject();
        return false;
    }

    // Apply window config: engine -> editor -> project
    const WindowConfig effectiveWindow = effectiveWindowConfig();

    // Recreate window if size changed
    const auto [currentWidth, currentHeight] = window_->framebufferSize();
    if (effectiveWindow.width != currentWidth || effectiveWindow.height != currentHeight) {
        Log::info("Engine",
                  "Recreating window: %ux%u -> %ux%u",
                  currentWidth,
                  currentHeight,
                  effectiveWindow.width,
                  effectiveWindow.height);
        applyWindowConfig(effectiveWindow);
    }

    if (!initializeGpuManagers(*contextFactory_)) {
        Log::error("Engine", "Cannot initialize GPU resource managers for the new project");
        closeProject();
        return false;
    }

    const VirtualPath scenePath = selectProjectScene({});
    if (scenePath.valid()) {
        if (!loadScene(scenePath))
            Log::warn("Engine", "Cannot load the selected scene: %s", scenePath.string().c_str());
    } else {
        Log::info("Engine", "No scene available in the project yet");
    }

    projectOpen_ = true;
    Log::info("Engine", "Opened project: %s", projectConfig_.name.c_str());
    return true;
}

void Engine::closeProject() {
    if (!projectOpen_)
        return;
    teardownProjectSubsystems();
    // Unmount project schemes (hardcoded list).
    for (const char* scheme : {"assets", "library", "shader-cache", "shader-bin"}) {
        (void)FILE_SYSTEM.unmount(scheme);
    }
    projectOpen_ = false;
    projectConfig_ = {};
    scene_ = std::make_unique<Scene>("Main Scene");
    Log::info("Engine", "Closed the active project");
}

} // namespace engine
