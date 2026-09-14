# 编辑器项目管理

编辑器以“项目”为单位组织资产与场景。本文描述项目的定义、配置文件三件套、
创建/打开/关闭/切换的完整流程，以及内建内容的分层策略。入口与总览见
[Editor.md](Editor.md)。

## 项目是什么

项目就是一个目录：`project.json` 加上固定布局 `assets/`、`library/`、
`generated-shaders/runtime`、`generated-shaders/compiled`。挂载表由该布局派生
（`projectMounts`），项目无法声明引擎不管理的 scheme：

| Scheme | 物理路径 | 用途 |
|---|---|---|
| `assets://` | `<root>/assets` | 项目资产源文件（可读写） |
| `library://` | `<root>/library` | AssetDatabase 与导入产物 |
| `shader-cache://` | `<root>/generated-shaders` | 运行时生成的 GLSL 与 SPIR-V 缓存 |
| `shader-bin://` | `<root>/shader-bin` | 预编译 SPIR-V（Publish 配置读取） |

目录布局中 `shader-cache://` 与 `shader-bin://` 对应 `generated-shaders/` 与
`shader-bin/` 两个子目录，`createNewProject` 会一次性创建全部四个子目录。

## 配置文件三件套

| 文件 | 位置 | 归属 |
|---|---|---|
| `engine.json` | `<CWD>/config/engine.json` | 引擎窗口与渲染偏好，游戏与编辑器共用 |
| `editor.json` | `<CWD>/editor/config/editor.json` | 编辑器窗口偏好 + 内嵌项目注册表 |
| `project.json` | `<项目根>/project.json` | 项目名称、渲染管线、管线缓存路径 |

- `ProjectConfig` 位于 `src/runtime/config/`，不受 `MINI_EDITOR` 保护，普通引擎
  和编辑器都能读写；只有打开项目时的模板同步（`syncEngineContractIntoProject`）
  属于编辑器分支。
- 编辑器偏好写在 `editor.json`（物理路径，无虚拟挂载）：

```json
{
  "schema_version": 1,
  "window": {"width": 1600, "height": 900, "vsync": true, "x": -1, "y": -1, "maximized": false},
  "registry": {
    "entries": [{"root_directory": "D:/Projects/Demo", "name": "Demo"}],
    "last_scenes": [{"project": "D:/Projects/Demo", "scene": "assets://scenes/main.scene.json"}]
  }
}
```

- registry 内嵌在同一文件里，只能通过 `EditorConfig::save` 落盘；单独调用
  `ProjectRegistry::save()` 会丢掉 window 与 schema 字段。`ProjectRegistry::load/save`
  那套基于 `VirtualPath` 的独立读写只服务于测试与复用。

## ProjectRegistry

`ProjectRegistry`（`engine::editor` 命名空间）维护：

- 最近项目列表（上限 16 条），每条记录根目录与项目名；
- 每个项目的最后打开场景（`last_scenes`，当前尚未接线到打开流程）；
- 移除条目（`remove`，仅删记录）与删除磁盘目录（`deleteFromDisk`，走 Windows
  回收站或永久删除）。

注册表的变化统一通过 `ProjectPickerPanel` 的 changed 回调触发
`ENGINE.saveEditorConfig()` 落盘。

## 创建项目

入口是 `ProjectPickerPanel` 的 `New Project...` 表单。创建逻辑在
`ProjectTemplate::createNewProject`（`engine::editor` 命名空间）：

```text
校验项目名称（ProjectConfig::validate）
  -> 目标目录已存在则拒绝
  -> 创建 assets/ library/ generated-shaders/ shader-bin/ 四个子目录
  -> syncEngineContractIntoProject()   // 复制 builtin/core/，覆盖式
  -> seedSampleContentIntoProject()    // 复制 builtin/samples/，只填空缺
  -> 写 project.json
  -> 生成空的 pipeline_cache.bin 占位（已存在则不动）
  -> Log: Created new project: <name> at <root>
失败 -> remove_all 回滚半成品目录
```

`pipeline_cache.bin` 占位的意义：引擎打开项目时会读取
`shader-cache://pipeline_cache.bin`，设备找不到文件会记 missing；空文件是合法的
“尚无缓存”占位，第一批管线出现后由设备覆写。设备写过的缓存不会被占位逻辑覆盖。

### 内建内容两层策略

引擎**不**挂载 `builtin://`。源树的 `builtin/` 在编译期以 `MINI_SOURCE_BUILTIN_DIR`
烘进 `MiniEngineEditor`，复制进项目的 `assets/`，因此项目可以脱离引擎位置独立
读取和发布。伴随的 `*.meta` 一律不复制——项目的导入管线会为每个资产生成自己的
Meta，沿用源树的 Meta 会把项目绑到外来的资产身份上。

`builtin/` 按**归属**分两层，两层都扁平合并进同一个 `assets/` 树，所以资产的
`assets://` 路径与它来自哪一层无关：

| 层 | 内容 | 复制时机 | 冲突策略 |
|---|---|---|---|
| `core/` | 引擎硬编码依赖的 6 个文件：`error.material.json`、`builtin_color` 的 shader 三件套、`include/scene.glsl`、`include/objects.glsl` | 创建时 + **每次打开** | 覆盖已存在文件 |
| `samples/` | 19 个演示资产：blinn_phong 与 vertex_color 全套、示例材质、两个场景、`procedural_showcase.mesh.json`、`checker.png` | **仅创建时** | 跳过已存在文件 |

这样分层是因为两类资产的归属不同。`core/` 是引擎契约：`MaterialManager` 与
`ShaderManager` 用硬编码路径找它们（`assets://materials/error.material.json`、
`assets://shaders/builtin_color.shader.json`），少一个就渲染不出东西而不是加载失败，
所以每次打开都无条件覆盖修复，代价是对它们的本地修改不被保留——这是有意的。
`samples/` 播种一次之后就**归项目所有**，用户可以自由修改、重命名或删除，
跳过已存在文件保证重复播种只填空缺、不覆盖既有文件。

两层扁平合并的前提是相对路径不相交。`CopyBuiltin.cmake` 在复制前会做一次冲突校验，
同名文件出现在两层时直接 `FATAL_ERROR`，避免结果依赖复制顺序。

> 注意：冲突策略由 `copyBuiltinLayer` 自己判断目标是否存在并决定删除或跳过，
> 不依赖 `std::filesystem::copy_options`——MinGW libstdc++ 的 `copy_file` 在同卷
> 目标已存在时会一律失败，与传入的 options 无关。

## 打开项目

入口有三处：picker 最近列表、`Browse Folder...` 原生对话框、创建成功后的回调。
它们都只设置 `pendingProjectRoot_`，由下一次 `onUpdate` 在 `beginFrame` 之前消费
（点击发生在活动 ImGui 窗口内，切换会销毁重建上下文，就地执行会访问已释放的
ImGui 状态）。

`EditorApplication::openProject` 的完整时序：

```text
imguiLayer_.detach()                      // 先摘掉绑定旧 renderer/device 的 UI
ENGINE.openProject(root)
  -> releaseProject()                    // 保存旧项目配置，再卸载旧 GPU 上下文和挂载
  -> syncEngineContractIntoProject()      // 只修复引擎契约资源，不碰示例内容
  -> ProjectConfig::load()
  -> 挂载 assets:// library:// shader-cache:// shader-bin://
  -> initializeProjectSubsystems()        // AssetManager + Engine 自己的场景重载监听
  -> ensureProjectMainScene()
  -> 合并 engine -> editor -> project 窗口配置，尺寸变化时重建窗口，否则直接更新标题
  -> initializeGpuManagers(contextFactory) // 打开项目后默认启用管线缓存
  -> selectProjectScene({}) 并 loadScene
installSceneChangeListener()              // 抢回单槽监听，见下方第三处顺序约束
imguiLayer_.attach(新 renderer, window)   // 必须重新挂载，否则新 renderer 没有 overlay
dockLayoutApplied_ = false                // 上下文已重建，布局需重新推导
registry.addProject(root, 项目名)
document_.open(activeScenePath) 或 createEmpty()
ENGINE.saveEditorConfig()
```

打开失败（目录不存在、`project.json` 损坏、契约资源同步失败）时同样恢复启动界面，
旧项目不会保留。

### 三处顺序约束

- **先 detach 再 openProject**：`ImGuiLayer` 绑定旧 renderer 与 device。不先摘掉，
  新 renderer 就没有 overlay，而空 draw list 的帧会把获取到的 backbuffer 留在
  undefined 布局并直接 present。
- **openProject 里必须先 teardown**：编辑器启动时已经为 UI 建了一个绑定当前窗口的
  GPU 上下文，不释放就再 `initializeGpuManagers` 会在同一窗口上叠第二个活
  swapchain，`vkCreateSwapchainKHR` 直接失败。
- **openProject 成功后必须重装场景监听**：`AssetManager` 只有一个 `changeListener_`
  槽位（不是多播）。`teardownProjectSubsystems()` 里的 `ASSET_MANAGER.shutdown()`
  会清空它，紧接着 `initializeProjectSubsystems()` 又装上 Engine 自己的自动
  `reloadScene`。编辑器必须在这之后调 `installSceneChangeListener()` 把槽位抢回来，
  重载策略才归 `SceneDocument`：干净文档静默重载，脏文档弹冲突框而不是静默丢弃
  本地编辑。漏掉这一步的话，Engine 会在编辑器背后重载并重建 `Scene`，使
  `HierarchyPanel` 持有的 `NodeHandle` 失效（Inspector 退回 “Nothing selected”）。

## 关闭与切换

`File > Close Project` 只置 `pendingProjectClose_`，同样延迟到下一帧执行：

```text
imguiLayer_.detach()
ENGINE.closeProject()      // 保存 project.json，卸载项目，重建禁用管线缓存的启动界面
清空文档与选择状态
imguiLayer_.attach()
projectPicker_.show()
```

切换项目**复用打开流程**：`ENGINE.openProject` 内部先 `releaseProject()` 释放旧项目，
再初始化新项目。不存在“先走一遍完整关闭流程再打开”的绕路，打开失败则回到启动界面。

## 窗口标题与配置保存

窗口标题来自 `WindowConfig::name`：引擎默认 `Mini Engine`，编辑器默认 `Mini Editor`。
编辑器打开项目后显示 `Mini Editor: 项目名称`，切换时同步更新（包括中文名称）。
`File > Close Project` 保存 `project.json`（含当前有效窗口尺寸和名称），标题恢复
编辑器名称。退出编辑器时由 `Engine::shutdown()` 保存编辑器配置和仍打开的项目配置；
项目窗口的名称和尺寸不会覆盖编辑器的启动偏好。缺少 `name` 的旧配置沿用默认值。

## 相关测试

| 测试 | 覆盖 |
|---|---|
| `ProjectTemplateTest` | 创建布局、两层复制语义（core 覆盖修复、samples 不覆盖用户编辑）、同卷场景 |
| `ProjectLifecycleTest` | 项目打开/关闭/切换的挂载与子系统顺序、配置持久化 |
| `ProjectRegistryTest` | 最近列表增删、last_scenes 追踪、磁盘删除 |
| `ProjectConfigTest` / `ProjectConfigEditorTest` | project.json 读写与校验，双库变体各验一遍 |
| `ProjectSceneTest` | 项目挂载下的场景加载 |
