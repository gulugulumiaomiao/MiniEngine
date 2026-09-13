# 编辑器（MiniEditor）

`tools/editor/` 是一个 Unity 风格的场景编辑器，构建为独立可执行程序 `MiniEditor`。
它复用引擎的全部运行时（资产、场景、渲染管线），只额外提供 Dear ImGui 面板和
项目管理能力。

## 职责边界

- 编辑器是 `Application` 的一个实现，不修改引擎主循环：所有 UI 工作发生在
  `onStart()`、`onUpdate()` 和 `onStop()` 中。
- UI 绘制只经过 RHI。`ImGuiRenderer` 取代官方 `imgui_impl_vulkan` 后端，因此
  `tools/editor/` 里没有任何直接的 Vulkan 调用；只有 Win32 平台后端
  `imgui_impl_win32` 被 vendored 使用。
- `src/render` 完全不感知 ImGui。唯一接触点是 `Renderer` 定义的 `IFrameOverlay`
  接口，`ImGuiLayer` 是它在编辑器侧的唯一实现。
- `EditorConfig`、`ProjectRegistry` 和 `ProjectTemplate` 位于 `tools/editor/`，
  仅编入 `MiniEngineEditor`。引擎中所有编辑器配置相关代码由 `MINI_EDITOR` 保护。
- `ProjectConfig` 位于 `src/runtime/config/`，与 `Engine::openProject/closeProject`
  及项目状态一起供普通引擎和编辑器共用，不受编辑器宏保护。
  只有打开项目时的模板同步属于编辑器分支；普通引擎不依赖编辑器模板。
  启动方式保持不变：编辑器等待选择项目，游戏默认按工作目录挂载资源。

## 目录结构

```text
tools/editor/
├── main.cpp                入口：构造 EditorApplication 并交给 Engine::run（editor.json/engine.json 各自从当前工作目录解析）
├── EditorApplication.*     Application 实现：菜单栏、Dock 布局、快捷键、项目与场景的打开/保存编排
├── EditorConfig.*          编辑器窗口偏好及内嵌的最近项目注册表
├── ImGuiLayer.*            IFrameOverlay 实现：ImGui 上下文、Win32 后端、帧序与帧末叠加录制
├── ImGuiRenderer.*         用 RHI 绘制 ImDrawData：字体图集、UI 管线、双帧几何缓冲
├── SceneDocument.*         当前场景文档状态：来源路径、dirty、外部变更冲突
├── ProjectRegistry.*       最近项目列表、上次场景持久化与项目移除/删除
├── ProjectTemplate.*       项目创建、内建基础资源同步与示例内容初始化
├── ProjectPickerPanel.*    项目选择/新建/浏览/移除/删除模态框，含原生文件夹对话框
├── ProjectPanel.*          assets:// 资产树浏览，双击场景打开
├── HierarchyPanel.*        场景树：选择、双击重命名、创建与删除节点
├── InspectorPanel.*        选中节点的组件编辑：Transform/Mesh/Material/Camera/Light
├── SceneViewPanel.*        视图状态与场景统计（引擎窗口本身即场景视图）
└── shaders/
    ├── imgui.vert          独立 GLSL，顶点已是 clip space
    └── imgui.frag          独立 GLSL，SRGB_TARGET 变体解码 ImGui 颜色
```

面板之间没有相互依赖：`HierarchyPanel` 持有选择状态，`EditorApplication` 把它
作为参数传给 `InspectorPanel::draw(selection)`；三个场景面板共享同一个
`SceneDocument` 引用。

## 启动流程

```text
main()
  -> EditorApplication 构造
       -> editorConfigPath = <CWD>/editor/config/editor.json   // 内部解析，不再由 main 传入
       -> ENGINE.loadEditorConfig(editorConfigPath)   // 缺失时按默认值创建
       -> imguiLayer_.setIniPath(<CWD>/editor/config/imgui.ini)
       -> 绑定 picker 的 open/create/changed 回调
  -> ENGINE.run(application, VulkanFactory)   // engine.json 由 run 从 <CWD> 解析
       -> Engine::initialize
            // MINI_EDITOR 分支：不挂载任何 scheme，不初始化资产系统
            -> 按 engine.json -> editor.json 合并窗口配置并创建窗口
            -> 创建 Renderer 与 GPU Managers（不启用 pipeline cache）
       -> EditorApplication::onStart
            -> imguiLayer_.attach(renderer, window)
            -> installSceneChangeListener()          // 抢占 AssetManager 的单槽监听
            -> 无项目时 projectPicker_.show()
       -> 每帧 EditorApplication::onUpdate
       -> EditorApplication::onStop -> imguiLayer_.detach()
       -> Engine::shutdown -> 保存编辑器配置及当前项目配置，再释放资源
```

编辑器启动时处于“无项目”状态：`assets://` 等 scheme 尚未挂载，资产系统未初始化，
因此 UI 是唯一可见内容。`onUpdate` 的顺序是固定的：

```text
消费 pendingProjectClose_ / pendingProjectRoot_（若有）
  -> imguiLayer_.beginFrame()
  -> projectPicker_.draw() 为真则 endFrame 并返回
  -> 无项目则提示文本 + endFrame 返回
  -> 无文档则 document_.createEmpty()
  -> handleShortcuts / drawMenuBar / drawDockSpace / 冲突与另存为弹窗
  -> Project / Hierarchy / Inspector / Scene View 面板
  -> imguiLayer_.endFrame()
```

`pendingProjectRoot_` 的存在是硬性要求：picker 的点击发生在活动 ImGui 窗口内，而
切换项目会销毁并重建 ImGui 上下文，就地执行会让之后的每个 ImGui 调用访问已释放的
上下文。因此点击只记录路径，由下一次 `onUpdate` 在 `beginFrame` 之前消费。

## 项目生命周期

项目就是一个目录：`project.json` 加上固定布局 `assets/`、`library/`、
`generated-shaders/runtime`、`generated-shaders/compiled`。挂载表由该布局派生
（`projectMounts`），项目无法声明引擎不管理的 scheme。

创建（`createNewProject`）：校验名称 -> 建四个子目录 -> 复制引擎内建内容的两层到
`assets/` -> 写 `project.json` -> 生成空的 `pipeline_cache.bin` 占位。任一步失败
都会 `remove_all` 回滚半成品目录。

打开（`EditorApplication::openProject`）：

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

窗口标题来自 `WindowConfig::name`：引擎默认 `Mini Engine`，编辑器默认 `Mini Editor`。
编辑器打开项目后显示 `Mini Editor: 项目名称`，切换时同步更新（包括中文名称）。
`File > Close Project` 延迟到下一帧执行：保存 `project.json`（含当前有效窗口尺寸和名称），
卸载项目并重建禁用管线缓存的启动界面，标题恢复编辑器名称。
打开项目失败时也恢复启动界面。退出编辑器时由 `Engine::shutdown()` 保存编辑器配置和
仍打开的项目配置；项目窗口的名称和尺寸不会覆盖编辑器的启动偏好。缺少 `name` 的旧配置沿用默认值。

三处顺序约束值得强调：

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

内建内容策略：引擎**不**挂载 `builtin://`。源树的 `builtin/` 在编译期以
`MINI_SOURCE_BUILTIN_DIR` 烘进 `MiniEngineEditor`，复制进项目的 `assets/`，因此项目
可以脱离引擎位置独立读取和发布。伴随的 `*.meta` 一律不复制——项目的导入管线会为
每个资产生成自己的 Meta，沿用源树的 Meta 会把项目绑到外来的资产身份上。

`builtin/` 按**归属**分两层，两层都扁平合并进同一个 `assets/` 树，所以资产的
`assets://` 路径与它来自哪一层无关：

| 层 | 内容 | 复制时机 | 冲突策略 |
|---|---|---|---|
| `core/` | 引擎硬编码依赖的 6 个文件：`error.material.json`、`builtin_color` 的 shader 三件套、`include/scene.glsl`、`include/objects.glsl` | 创建时 + **每次打开** | `overwrite_existing` |
| `samples/` | 19 个演示资产：blinn_phong 与 vertex_color 全套、示例材质、两个场景、`procedural_showcase.mesh.json`、`checker.png` | **仅创建时** | `skip_existing` |

这样分层是因为两类资产的归属不同。`core/` 是引擎契约：`MaterialManager` 与
`ShaderManager` 用硬编码路径找它们（`assets://materials/error.material.json`、
`assets://shaders/builtin_color.shader.json`），少一个就渲染不出东西而不是加载失败，
所以每次打开都无条件覆盖修复，代价是对它们的本地修改不被保留——这是有意的。
`samples/` 播种一次之后就**归项目所有**，用户可以自由修改、重命名或删除，`skip_existing`
保证重复播种只填空缺、不覆盖既有文件。

两层扁平合并的前提是相对路径不相交。`CopyBuiltin.cmake` 在复制前会做一次冲突校验，
同名文件出现在两层时直接 `FATAL_ERROR`，避免结果依赖复制顺序。

编辑器偏好写在 `editor.json`（物理路径，无虚拟挂载）：

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

registry 内嵌在同一文件里，只能通过 `EditorConfig::save` 落盘；单独调用
`ProjectRegistry::save()` 会丢掉 window 与 schema 字段。`ProjectRegistry::load/save`
那套基于 `VirtualPath` 的独立读写只服务于测试与复用。

## 场景文档

`SceneDocument` 不拥有任何场景对象。运行时 `Scene` 始终由 `Engine` 持有（可能在
加载或重载时被替换），所以访问一律走 `ENGINE.scene()`；文档只记录编辑状态：来源
路径、显示名、`attached_`、`dirty_`、`conflictPending_`、`suppressNextChange_`。

- `open(path)`：校验 scheme 为 `assets` 且后缀为 `.scene.json`，交给
  `ENGINE.loadScene`（失败时保留前一个场景），成功后 `applyLoaded` 清空 dirty。
- `save(error)`：`saveSceneToFile` -> `ASSET_IMPORT_PIPELINE.reimportAsset` ->
  `ASSET_MANAGER.invalidate` -> `dirty_ = false` -> `suppressNextChange_ = true`。
  原子写会被 FileWatcher 报成一次外部变更，这个一次性标记用来吞掉自触发的那一次
  通知。“恰好一次”是有保证的：`writeBinaryAtomic` 先写 `<目标>.tmp-N` 再
  `MoveFileExW(REPLACE_EXISTING)`，而 `FileWatcher::ignored()` 显式排除名字含
  `.tmp-` 的文件，所以临时文件从不进快照；FileWatcher 本身是 50ms 轮询快照、
  按路径去重再加 100–300ms 去抖，目标路径只会产出一条 `Modified`。
- `saveAs(path, error)`：先切换来源路径再走 `save`，成功后更新显示名。
- `handleExternalChange(path, removed)`：只处理当前来源路径。干净文档自动重载；
  脏文档置 `conflictPending_`，由 `EditorApplication::drawConflictModal` 弹出
  “Reload From Disk / Keep Local” 二选一。

保存快捷键为 `Ctrl+S`（`io.WantTextInput` 时不触发），未命名文档会转为另存为弹窗，
默认路径 `assets://scenes/scene.scene.json`。菜单栏显示 `[项目名]`、文档名和 dirty
星号，错误信息以红色文本跟在后面。

## UI 渲染路径

`Renderer::renderFrame` 在渲染管线跑完后调用 `overlay_->recordOverlay(context)`，
叠加绘制写进同一个命令缓冲区；没有 overlay 时 Renderer 自己负责把未触碰的
backbuffer 清成合法状态。

`ImGuiLayer` 的帧序：

- `beginFrame()` = `ImGui_ImplWin32_NewFrame()` + `ImGui::NewFrame()`。
- `endFrame()` = `ImGui::Render()`，**每帧都必须调用**，即使渲染器随后跳过这一帧，
  否则下一次 `NewFrame` 会触发 ImGui 的 forgot-to-render 检查。
- Win32 消息通过 `Window::setMessageHandler` 转发给
  `ImGui_ImplWin32_WndProcHandler`；`detach()` 时先清掉这个钩子（它捕获 `this`）。

`recordOverlay` 的契约是：overlay 是 present 之前的最后写入者，必须把获取到的
backbuffer 留在 `PRESENT_SRC`。实现按 `RenderContext::backBufferWritten()` 分支：

```text
无 UI 且 backbuffer 已写 -> 直接返回
否则 barrier(Undefined 或 Present -> ColorAttachment)
     beginRendering(Load 或 Clear)
     有 UI 则 ImGuiRenderer::render(encoder, drawData, frameIndex)
     endRendering
     barrier(ColorAttachment -> Present)
```

`backBufferWritten()` 由真正画进 backbuffer 的 pass 设置，不能靠“场景里有没有物体”
猜测：场景可以有对象却产不出 draw item。

`ImGuiRenderer` 的关键实现选择：

- 字体图集上传为 `Rgba8Unorm` 纹理 + `ClampToEdge` 采样器（nearest wrap 会让图集
  边缘字形互相渗色），随后 `ClearTexData()` 释放 CPU 侧副本。
- `ImTextureID` 是 64 位整数，直接编码 RHI 的 `BindGroupHandle`
  （`generation << 32 | index`）。活句柄的 generation 恒非零，所以被清零的
  `ImDrawCmd::TextureId` 不会与真实句柄冲突，`ImGui::Image` 无需旁路映射表即可用。
- UI 管线：无深度测试与写入、`CullMode::None`（ImGui 两种绕序都会发）、Alpha
  混合、`colorFormats = {swapchain 格式}`。
- sRGB 附件会选 `-DSRGB_TARGET` 编出的片元变体，在片元阶段把 ImGui 的 sRGB 样式
  颜色解码到线性，避免硬件写入编码把整套主题提亮；逐片元而非逐顶点，渐变才留在
  ImGui 期望的色彩空间。
- 顶点拷贝时把 ImGui 的 display offset/scale 折成 clip space（Vulkan 与 ImGui 同为
  y-down，无需翻转），因此这条管线不需要任何 uniform buffer——RHI 也不暴露 push
  constant。
- 几何按 `FrameGpuManager::kFramesInFlight` 双缓冲，扩容带 4096 顶点/索引余量；
  重建缓冲是安全的，因为 swapchain 已经等过这一帧的 fence。
- 设置 `ImGuiBackendFlags_RendererHasVtxOffset`，超过 64K 顶点的 draw list 也能继续
  用 16 位索引而不被 ImGui 拆分。
- 只支持 `ImDrawCallback_ResetRenderState`，其他 user callback 记 warn 后忽略。

UI shader 绕过 ShaderLab 资产管线：两段 GLSL 用
`glslc --target-env=vulkan1.3 -O -mfmt=num`（sRGB 变体加 `-DSRGB_TARGET`）离线编译，
生成的 SPIR-V 字直接内联在 `ImGuiRenderer.cpp` 的 `constexpr uint32` 数组里（uint32
保证四字节对齐），对应 GLSL 源码作为注释保留在同一处；因此不再需要 shader 文件夹
或构建期编译步骤。

`onSwapchainRecreated` 只在颜色格式变化时重建管线：几何缓冲和字体图集与分辨率无关，
管线用的是动态 viewport 与 scissor。

## 面板与 Dock 布局

Dock 宿主是一个铺满工作区、不可停靠、无标题栏、无背景的窗口，内部
`DockSpace(..., ImGuiDockNodeFlags_PassthruCentralNode)` 把中央节点留成空洞，于是
引擎渲染出的帧直接透出来充当场景视图，其余区域被面板覆盖。

默认 Unity 布局：左 `Hierarchy`（20%）、右 `Inspector`（20%）、底部 `Project` 与
`Scene View`（24%）。应用时机由两个标记控制：

- `dockLayoutApplied_`：每个 ImGui 上下文只在首帧推导一次（项目打开/创建会重建
  上下文，因此每次切换都会重新推导）。
- `hasDockedPanelLayout()`：检查持久化的 `imgui.ini` 是否**真的**把面板恢复成了
  docked 状态。只判断 ini 文件是否存在是不行的——项目切换迁移上下文时会写出一个空
  ini，那样会错误地抑制默认布局。

`Window > Reset Layout` 置 `forceApplyDefaultLayout_` 并恢复四个面板的可见性。面板
可见性是会话级偏好：窗口在持久化布局里保留自己的 dock 槽位，所以反复开关是安全的。

各面板能力：

- `ProjectPanel`：递归列出 `assets://`，目录优先排序，隐藏 `.meta`，按后缀识别资产
  类型并显示单字母图标。场景可双击打开，其他资产置灰只显示路径 tooltip。
  `FileSystem::listFiles` 只返回文件，子目录是从子路径重建的，因此空目录也能作为
  折叠节点出现。
- `HierarchyPanel`：树形展示 `Scene Root` 及子节点，非激活节点标注 `(inactive)`；
  左键选择、双击就地重命名（Enter 提交、Esc 取消、失焦提交）、`Create Node`、
  `Delete`（根节点禁用）。
- `InspectorPanel`：名称与 Active 开关，然后按组件存在与否依次绘制 Transform、
  Mesh、Material、Camera、Light，最后是 `Add Component` 弹窗（已存在的组件类型不再
  列出）和每个组件的 `Remove Component`。资产引用用 combo 列出 `assets://` 下匹配
  后缀的文件，选中即通过对应 Manager `load` 并写回组件。Primitive Mesh 的参数通过
  局部副本编辑，只有控件真的变化时才 `editPrimitiveRecipe()` +
  `applyPrimitiveChanges()`，否则每帧都会重建网格。所有写操作后调用
  `document_.markDirty()`。
- `SceneViewPanel`：统计节点/相机/网格/灯光数量，显示视口尺寸、帧时间与主相机参数。
  它不承载离屏渲染目标——引擎窗口就是场景视图。
- `ProjectPickerPanel`：最近项目列表（整行按钮 + Open/Remove/Delete）、`New
  Project...` 表单（名称 + 原生文件夹选择）、`Browse Folder...`、`Cancel`。删除走
  `deleteFromDisk(index, true)`，即 Windows 回收站。注册表变化通过 changed 回调触发
  `ENGINE.saveEditorConfig()`。

## 构建

```text
MiniImGui            imgui 四个核心 cpp + imgui_impl_win32；链 gdi32/dwmapi/imm32
MiniEditor           编辑器可执行文件；链 MiniEngineEditor + MiniImGui + ole32/shell32
MiniCopyBuiltin      把 builtin/ 两层扁平合并到可执行文件旁的 assets/（不含 .meta），供游戏运行时使用
```

`MiniEngineEditor` 是同一份引擎源码的第二个静态库变体，`PUBLIC MINI_EDITOR=1`、
`PRIVATE MINI_SOURCE_BUILTIN_DIR`。之所以要单独一个库而不是只在编辑器可执行文件上
定义宏：静态库被游戏、工具和测试共享，只在一侧定义宏会让受保护的函数体没有编译，
`Engine` 的内存布局在不同编译单元之间不一致。

```powershell
cmake --build --preset clang-debug --target MiniEditor
./build/clang-debug/MiniEditor.exe
```

VS Code 中对应 `Debug MiniEditor (CodeLLDB)` 与 `Run MiniEditor Release` 两个启动
配置，任务 `CMake: Build MiniEditor Debug` / `CMake: Build MiniEditor Release`。
`MiniEditor` 在三个配置下都会被构建，但只有 Debug 与 Release 可用：编辑器始终在线
编译 shader、依赖动态导入与 FileWatcher，而 Publish 下 `AssetManager` 进入 Packaged
模式，只读烘焙后的 AssetDatabase 与 Artifact，项目 `assets/` 里的源 JSON 无法导入。

`ole32` 用于 `IFileOpenDialog` 文件夹选择，`shell32` 用于 `SHFileOperationW` 回收站
删除；`-Wno-missing-field-initializers` 与引擎一致，因为 RHI 描述符大量使用省略默认
字段的指定初始化器。

## 测试

- `mini_add_editor_test` 链接 `MiniEngineEditor`（因而能看到 `MINI_EDITOR`）：
  `ProjectSceneTest`、`ProjectTemplateTest`、`ProjectRegistryTest`、
  `ProjectLifecycleTest`、`BuildConfigEditorTest`。
- `ProjectConfigTest` 链接普通 `MiniEngine`；`ProjectConfigEditorTest` 复用相同源码，
  链接 `MiniEngineEditor`。两者验证项目配置与项目 API 可用、编辑器配置 API 正确隔离。
- `ImGuiRendererTest` 直接编译 `tools/editor/ImGuiRenderer.cpp`（UI shader 已内联其中），
  依赖 `MiniImGui`，链接 `MiniEngine`（UI 后端不需要编辑器变体）。
- 面板与 `EditorApplication` 的交互逻辑没有单元测试，需要实跑编辑器验证。

## 当前限制

1. **`AssetManager` 的变更监听是单槽，不是多播**：编辑器和 Engine 无法同时收到
   通知。编辑器抢占该槽位后，`Engine::installSceneChangeListener` 装的自动
   `reloadScene` 在编辑器里实际不生效（`sceneReloadPending_` 永远不会被置位），
   场景重载改由 `SceneDocument::handleExternalChange` 负责。这是刻意的策略归属
   （见“项目生命周期”的第三处顺序约束），但意味着任何新增的监听方都必须先
   解决单槽限制。
2. **`registry.last_scenes` 未接线**：`Engine::openProject` 用
   `selectProjectScene({})`，编辑器也从不写 `setLastScene`，因此“记住上次打开的
   场景”只有实现和测试覆盖，UI 侧尚未使用。
3. **`editor.json` 的 `window.x/y/maximized` 未接线**：`effectiveWindowConfig` 只合并
   width/height/vsync，`Window` 构造也不接受位置与最大化状态；运行时同样不会把用户
   调整后的窗口尺寸写回偏好。
4. `ProjectPanel` 是只读浏览器：不能新建、重命名或删除资产，非场景资产也不能打开。
5. `SceneViewPanel` 不是离屏视图：没有独立相机视口、没有 gizmo，只报告统计信息。
6. `HierarchyPanel` 不支持拖拽改变父子关系与多选；编辑器没有 Undo/Redo，`dirty`
   标记是唯一的状态跟踪。
7. 回收站删除与文件夹选择都是 Win32 专有实现，编辑器目前只在 Windows 上可用；
   Publish 配置下也不可用（资产系统为 Packaged 模式）。
8. `ProjectPickerPanel::drawProjectList` 的删除分支里有一个从未使用的
   `std::string error`（`deleteFromDisk` 不输出错误文本），失败原因只能从日志看。
