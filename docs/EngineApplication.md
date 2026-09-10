# Engine 与 Application

## 职责边界

`Engine` 是 Runtime 和生命周期的唯一控制者，负责读取引擎配置、挂载文件系统、初始化与关闭各子系统、维护主循环，以及承载主 `Scene` 和当前 `RenderScene` 快照。

`Application` 是项目逻辑入口，只实现 `onStart()`、`onUpdate(deltaTime)` 和 `onStop()`。窗口和文件系统参数不再由 `Application` 硬编码，而是来自启动配置。

## 启动配置

引擎启动时固定读取可执行文件同目录下的 `engine.json`。源配置位于 `config/engine.json`，构建后会自动复制到可执行文件目录。配置文件不存在、JSON 无法解析、字段非法或缺少必要 mount 时，`Engine` 会调用 `Log::fatal` 并立即退出。

```json
{
  "schema_version": 1,
  "working_directory": "../..",
  "application": {
    "name": "Mini Vulkan Engine",
    "width": 1280,
    "height": 720,
    "vsync": true
  },
  "filesystem": {
    "mounts": [
      {"scheme": "asset", "type": "directory", "path": "assets", "read_only": false},
      {"scheme": "library", "type": "directory", "path": "library", "read_only": false},
      {"scheme": "shader-cache", "type": "directory", "path": "generated-shaders/runtime", "read_only": false},
      {"scheme": "shader-bin", "type": "directory", "path": "generated-shaders/compiled", "read_only": true}
    ]
  }
}
```

`working_directory` 相对 `engine.json` 解析，并被设置为进程工作目录；所有相对 mount 路径再以该目录为基准解析。当前支持的 mount 类型是 `directory`，scheme 必须唯一；`asset`、`library`、`shader-cache` 和 `shader-bin` 是运行时必需项。

Debug 和 Release 可执行文件分别位于 `build/clang-debug` 与 `build/clang-release`，所以 `working_directory` 使用 `../..` 指向仓库根目录。`assets`、`library` 和 `generated-shaders` 均位于该根目录下，构建目录不保存这些资源副本。

`shader-cache://` 保存开发运行时可重新生成的预处理源码和 SPIR-V；`shader-bin://` 对应打包产物，运行时只读。`shader-generated://` 仅作为 `FileDependencyGraph` 的逻辑节点，因此不需要 mount。

## 生命周期

```text
main()
  -> 定位 executable/engine.json
  -> 创建 GameApplication
  -> Engine::run(Application&, ContextFactory&, configPath)
       -> 读取并校验 EngineConfig
       -> 按配置表统一挂载 FileSystem
       -> 初始化 AssetManager、Window、Renderer 和 GPU Managers
       -> Application::onStart()
       -> while 未退出
            -> Window::pollEvents()
            -> AssetImportPipeline::processFileEvents()
            -> Application::onUpdate(deltaTime)
            -> Scene::update(deltaTime)
            -> Scene::buildRenderScene(RenderScene, aspectRatio)
            -> Renderer::renderFrame(RenderScene)
       -> Application::onStop()
       -> Engine::shutdown()
       -> 逆序卸载配置中的 FileSystem mounts
```

工具与测试是独立进程，会为自己的隔离环境建立 mount；引擎运行时的 mount 则全部集中在 `Engine::initialize()`。
