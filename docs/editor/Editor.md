# 编辑器（MiniEditor）

`tools/editor/` 是一个 Unity 风格的场景编辑器，构建为独立可执行程序 `MiniEditor`。
它复用引擎的全部运行时（资产、场景、渲染管线），只额外提供 Dear ImGui 面板和
项目管理能力。

编辑器文档按主题拆分为四份，本页是总览：

| 文档 | 内容 |
|---|---|
| [ProjectManagement.md](ProjectManagement.md) | 项目的定义、创建/打开/关闭/切换流程、配置文件三件套、内建内容两层策略 |
| [ImGui.md](ImGui.md) | ImGuiLayer/ImGuiRenderer 的分层、帧序、overlay 契约与渲染实现 |
| [Panels.md](Panels.md) | 各面板的职责与交互流程、SelectionSet 多选模型、SceneDocument、Dock 布局 |
| 本页 | 职责边界、目录结构、启动流程、构建、测试、当前限制 |

## 职责边界

- 编辑器是 `Application` 的一个实现，不修改引擎主循环：所有 UI 工作发生在
  `onStart()`、`onUpdate()` 和 `onStop()` 中。
- UI 绘制只经过 RHI。`ImGuiRenderer` 取代官方 `imgui_impl_vulkan` 后端，因此
  `tools/editor/` 里没有任何直接的 Vulkan 调用；只有 Win32 平台后端
  `imgui_impl_win32` 被 vendored 使用。
- `src/render` 完全不感知 ImGui。唯一接触点是 `Renderer` 定义的 `IFrameOverlay`
  接口，`ImGuiLayer` 是它在编辑器侧的唯一实现（详见 [ImGui.md](ImGui.md)）。
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
├── HierarchyPanel.*        场景树：选择、多选、拖放、右键菜单、就地重命名
├── InspectorPanel.*        选中节点的组件编辑：Transform/Mesh/Material/Camera/Light，支持批量编辑
├── SceneViewPanel.*        视图状态与场景统计（引擎窗口本身即场景视图）
├── SelectionSet.h          多选容器：Ctrl 切换、Shift 范围、主选择锚点
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
完整流程见 [ProjectManagement.md](ProjectManagement.md)。

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
- 面板交互逻辑（选择、拖放、右键菜单、批量编辑）由 `HierarchyPanelTest`、
  `InspectorPanelTest`、`SelectionSetTest` 通过模拟 ImGui 输入事件驱动真实面板类验证。
- `EditorApplication` 的整体编排（菜单、快捷键、弹窗时序）没有单元测试，
  需要实跑编辑器验证。

## 当前限制

1. **`AssetManager` 的变更监听是单槽，不是多播**：编辑器和 Engine 无法同时收到
   通知。编辑器抢占该槽位后，`Engine::installSceneChangeListener` 装的自动
   `reloadScene` 在编辑器里实际不生效（`sceneReloadPending_` 永远不会被置位），
   场景重载改由 `SceneDocument::handleExternalChange` 负责。这是刻意的策略归属
   （见 [ProjectManagement.md](ProjectManagement.md) 的第三处顺序约束），但意味着
   任何新增的监听方都必须先解决单槽限制。
2. **`registry.last_scenes` 未接线**：`Engine::openProject` 用
   `selectProjectScene({})`，编辑器也从不写 `setLastScene`，因此“记住上次打开的
   场景”只有实现和测试覆盖，UI 侧尚未使用。
3. **`editor.json` 的 `window.x/y/maximized` 未接线**：`effectiveWindowConfig` 只合并
   width/height/vsync，`Window` 构造也不接受位置与最大化状态；运行时同样不会把用户
   调整后的窗口尺寸写回偏好。
4. `ProjectPanel` 是只读浏览器：不能新建、重命名或删除资产，非场景资产也不能打开。
5. `SceneViewPanel` 不是离屏视图：没有独立相机视口、没有 gizmo，只报告统计信息。
6. 编辑器没有 Undo/Redo，`dirty` 标记是唯一的状态跟踪；`HierarchyPanel` 的移动、
   删除等结构调整同样不可撤销。
7. 回收站删除与文件夹选择都是 Win32 专有实现，编辑器目前只在 Windows 上可用；
   Publish 配置下也不可用（资产系统为 Packaged 模式）。
8. `ProjectPickerPanel::drawProjectList` 的删除分支里有一个从未使用的
   `std::string error`（`deleteFromDisk` 不输出错误文本），失败原因只能从日志看。
