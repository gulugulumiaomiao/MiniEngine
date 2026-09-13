# Engine 与 Application

## 职责边界

`Engine` 是 Runtime 和生命周期的唯一控制者，负责读取引擎配置、挂载文件系统、初始化与关闭各子系统、维护主循环，以及承载主 `Scene` 和当前 `RenderScene` 快照。

`Application` 是项目逻辑入口，只实现 `onStart()`、`onUpdate(deltaTime)` 和 `onStop()`。窗口和文件系统参数不再由 `Application` 硬编码，而是来自启动配置。

## 启动配置

引擎启动时固定读取可执行文件同目录下的 `engine.json`。源配置位于 `config/engine.json`，构建后会自动复制到可执行文件目录。配置文件不存在、JSON 无法解析或字段非法时，`Engine` 会调用 `Log::fatal` 并立即退出；`EngineConfig::validate()` 只检查 `schema_version` 必须为 1、`working_directory` 非空、窗口宽高非零。

```json
{
  "schema_version": 1,
  "working_directory": "../..",
  "window": {
    "name": "Mini Engine",
    "width": 1280,
    "height": 720,
    "vsync": true
  },
  "render": {
    "pipeline": "MiniForward"
  }
}
```

`working_directory` 相对 `engine.json` 解析，并被设置为进程工作目录。

管线缓存固定使用 `shader-cache://pipeline_cache.bin`，不再提供路径配置。`rhi::ContextDesc::enablePipelineCache` 默认为 `true`；编辑器进入项目前设为 `false`，不创建、读取或保存缓存，打开项目后启用。

挂载表**不在配置里**。游戏运行时在 `Engine::initialize()` 中按固定表挂载，四个目录一律相对 `working_directory` 解析；编辑器启动时不挂载任何项目 scheme，改由 `openProject()` 通过 `projectMounts(projectRoot)` 从项目布局派生。

| scheme | 目录 | 只读 |
| --- | --- | --- |
| `assets` | `assets` | 否 |
| `library` | `library` | 否 |
| `shader-cache` | `generated-shaders/runtime` | 否 |
| `shader-bin` | `generated-shaders/compiled` | 是 |

这四个 scheme 由 `isProjectMountScheme()` 认定；挂载失败时 `Engine::initialize()` 调用 `Log::fatal`。

Debug、Release 和 Publish 可执行文件分别位于 `build/clang-debug`、`build/clang-release` 与 `build/clang-publish`，所以 `working_directory` 使用 `../..` 指向仓库根目录。`assets`、`library` 和 `generated-shaders` 均位于该根目录下，构建目录不保存这些资源副本。

`shader-cache://` 保存开发运行时可重新生成的预处理源码和 SPIR-V；`shader-bin://` 对应打包产物，运行时只读。`shader-generated://` 仅作为 `FileDependencyGraph` 的逻辑节点，因此不需要 mount。

## 生命周期

```text
main()
  -> 定位 executable/engine.json
  -> 创建 GameApplication
  -> Engine::run(Application&, ContextFactory&, configPath)
       -> 读取并校验 EngineConfig
       -> 按固定挂载表挂载 FileSystem（编辑器变体跳过，等 openProject）
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
```

`Engine::shutdown()` 在释放窗口之前保存当前项目配置；编辑器变体还保存编辑器配置。
项目切换、关闭与 shutdown 共用 `releaseProject()`，在 GPU 缓存保存后卸载已打开项目的四个 scheme。
`closeProject()` 随后恢复无项目窗口和 Renderer，编辑器需重新 attach ImGui；shutdown 不重建它们。
`windowConfig()` 返回当前应用的窗口配置（包括名称和 vsync），不是未合并的引擎默认值。

工具与测试是独立进程，会为自己的隔离环境建立 mount；引擎运行时的 mount 则集中在 `Engine::initialize()`（游戏）或 `Engine::openProject()`（编辑器）。
