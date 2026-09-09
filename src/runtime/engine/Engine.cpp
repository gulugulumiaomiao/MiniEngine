#include "runtime/engine/Engine.h"

#include "asset/importer/AssetImportPipeline.h"
#include "asset/manager/AssetManager.h"
#include "core/base/BuildConfig.h"
#include "core/filesystem/FileSystem.h"
#include "core/logging/Log.h"
#include "render/gpu/GpuCacheRegistry.h"
#include "render/gpu/frame/FrameGpuManager.h"
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

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace engine {

Engine::Engine() = default;
Engine::~Engine() = default;

int Engine::run(Application& application, const rhi::IContextFactory& contextFactory) {
    if (running_) {
        Log::error("Engine", "Engine is already running an Application");
        return 1;
    }

    if (!initialize(application.getConfig(), contextFactory))
        return 1;

    Log::info("Engine", "Starting application: %s", config_.name.c_str());
    application.onStart();
    loop(application);
    application.onStop();
    Log::info("Engine", "Stopping application: %s", config_.name.c_str());
    shutdown();
    return 0;
}

bool Engine::initialize(const AppConfig& config, const rhi::IContextFactory& contextFactory) {
    if (config.name.empty() || config.width == 0 || config.height == 0) {
        Log::error("Engine", "Invalid application configuration");
        return false;
    }

    config_ = config;
    shouldQuit_ = false;
    deltaTime_ = 0.0F;

#if defined(MINI_RELEASE)
    constexpr bool assetReadOnly = true;
#else
    constexpr bool assetReadOnly = false;
#endif
    const std::filesystem::path assetRoot{MINI_ASSET_DIR};
    if (!FILE_SYSTEM.mountDirectory("asset", assetRoot, assetReadOnly) ||
        !FILE_SYSTEM.mountDirectory("library", assetRoot.parent_path() / "library", false) ||
        !FILE_SYSTEM.mountDirectory("shader", MINI_GENERATED_SHADER_DIR, false) ||
        !ASSET_MANAGER.initialize()) {
        Log::error("Engine", "Cannot initialize asset system: %s", assetRoot.string().c_str());
        shutdown();
        return false;
    }
    ASSET_MANAGER.setChangeListener([this](const VirtualPath& path, AssetType type, bool removed) {
        if (type != AssetType::Scene || path != activeScenePath_)
            return;
        if (removed) {
            Log::warn("Engine", "Active Scene source was removed: %s", path.string().c_str());
            return;
        }
        sceneReloadPending_ = true;
    });

    window_ = std::make_unique<Window>(config_.width, config_.height, config_.name);
    const auto [width, height] = window_->framebufferSize();
    rhi::Context context = contextFactory.createContext({
        .surface = {.windowSystem = rhi::WindowSystem::Win32,
                    .nativeDisplay = window_->nativeInstance(),
                    .nativeWindow = window_->nativeHandle()},
        .swapchain = {.width = width, .height = height, .vsync = config_.vsync},
    });
    renderer_ = std::make_unique<Renderer>(*window_, std::move(context));
    if (!FRAME_GPU_MANAGER.initialize(renderer_->device()) ||
        !GPU_CACHE.initialize(FrameGpuManager::kFramesInFlight) ||
        !MESH_GPU_MANAGER.initialize(renderer_->device()) ||
        !TEXTURE_GPU_MANAGER.initialize(renderer_->device()) ||
        !MATERIAL_GPU_MANAGER.initialize(renderer_->device(), FRAME_GPU_MANAGER.materialLayout()) ||
        !SHADER_GPU_MANAGER.initialize(renderer_->device()) ||
        !GRAPHICS_PIPELINE_MANAGER.initialize(renderer_->device(),
                                              FRAME_GPU_MANAGER.sceneLayout(),
                                              FRAME_GPU_MANAGER.materialLayout())) {
        Log::error("Engine", "Cannot initialize GPU resource managers");
        shutdown();
        return false;
    }
    running_ = true;
    return true;
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
    scene_->clear();
    renderScene_.clear();
    if (renderer_)
        renderer_->waitIdle();
    MATERIAL_GPU_MANAGER.shutdown();
    GRAPHICS_PIPELINE_MANAGER.shutdown();
    SHADER_GPU_MANAGER.shutdown();
    TEXTURE_GPU_MANAGER.shutdown();
    MESH_GPU_MANAGER.shutdown();
    if (!GPU_CACHE.shutdown())
        Log::error("Engine", "Render cache was not empty during shutdown");
    FRAME_GPU_MANAGER.shutdown();
    renderer_.reset();
    MESH_MANAGER.clear();
    MATERIAL_MANAGER.clear();
    TEXTURE_MANAGER.clear();
    SHADER_MANAGER.clear();
    ASSET_MANAGER.shutdown();
    (void)FILE_SYSTEM.unmount("shader");
    (void)FILE_SYSTEM.unmount("asset");
    (void)FILE_SYSTEM.unmount("library");
    window_.reset();
    scene_ = std::make_unique<Scene>("Main Scene");
    activeScenePath_ = {};
    sceneReloadPending_ = false;
    running_ = false;
    shouldQuit_ = false;
    deltaTime_ = 0.0F;
}

bool Engine::loadScene(const VirtualPath& scenePath) {
    if (!scenePath.valid() || scenePath.scheme() != "asset") {
        Log::error("Engine", "Invalid Scene path: %s", scenePath.string().c_str());
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
        Log::error("Engine", "Cannot instantiate Scene: %s", scenePath.string().c_str());
        return false;
    }
    scene_ = std::move(loaded);
    activeScenePath_ = scenePath;
    renderScene_.clear();
    Log::info("Engine", "Loaded Scene: %s", scenePath.string().c_str());
    return true;
}

bool Engine::reloadScene() {
    if (!activeScenePath_.valid()) {
        Log::warn("Engine", "No active asset Scene to reload");
        return false;
    }
    return loadScene(activeScenePath_);
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

} // namespace engine
