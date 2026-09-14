# 编辑器面板

本文描述各面板的职责与交互流程，以及它们共享的 `SceneDocument` 与 `SelectionSet`。
ImGui 集成本身见 [ImGui.md](ImGui.md)，项目流程见 [ProjectManagement.md](ProjectManagement.md)。

## 共享基础设施

面板之间没有相互依赖：`HierarchyPanel` 持有选择状态，`EditorApplication` 把它作为
参数传给 `InspectorPanel::draw(selection)`；三个场景面板共享同一个 `SceneDocument`
引用。`EditorApplication` 每帧的绘制顺序：

```text
ProjectPanel -> HierarchyPanel -> InspectorPanel -> SceneViewPanel
（选择在 HierarchyPanel 内产生，InspectorPanel 只读消费）
```

### SceneDocument

`SceneDocument` 不拥有任何场景对象。运行时 `Scene` 始终由 `Engine` 持有（可能在
加载或重载时被替换），所以访问一律走 `ENGINE.scene()`；文档只记录编辑状态：来源
路径、显示名、`attached_`、`dirty_`、`conflictPending_`、`suppressNextChange_`，
外加一个 `revision_`（场景每次替换时递增，面板用它丢弃过期的挂起操作）。

| 方法 | 职责 |
|---|---|
| `open(path)` | 校验 scheme 为 `assets` 且后缀为 `.scene.json`，交给 `ENGINE.loadScene`（失败时保留前一个场景），成功后清空 dirty |
| `createEmpty()` | 无来源路径的空文档 |
| `save(error)` | `saveSceneToFile` -> `reimportAsset` -> `invalidate` -> 清 dirty -> `suppressNextChange_ = true` |
| `saveAs(path, error)` | 先切换来源路径再走 `save`，成功后更新显示名 |
| `handleExternalChange(path, removed)` | 只处理当前来源路径。干净文档自动重载；脏文档置 `conflictPending_` |
| `markDirty()` | 任何面板写操作后调用，菜单栏显示 dirty 星号 |

保存的原子写会被 FileWatcher 报成一次外部变更，`suppressNextChange_` 用来吞掉自触发
的那一次。“恰好一次”是有保证的：`writeBinaryAtomic` 先写 `<目标>.tmp-N` 再
`MoveFileExW(REPLACE_EXISTING)`，而 `FileWatcher::ignored()` 显式排除名字含 `.tmp-`
的文件，所以临时文件从不进快照；FileWatcher 本身是 50ms 轮询快照、按路径去重再加
100–300ms 去抖，目标路径只会产出一条 `Modified`。

脏文档遇到外部变更时由 `EditorApplication::drawConflictModal` 弹出
“Reload From Disk / Keep Local” 二选一。

### SelectionSet

`SelectionSet<NodeHandle>` 是通用多选容器（纯逻辑模板，`SelectionSetTest` 独立验证）：

- **有序集合**，`primary()` 返回首个选中项（单选兼容视图的来源）；
- `click(handle, ctrl, shift, visibleOrder)`：无修饰键替换选择；Ctrl 切换单项；
  Shift 沿传入的可见顺序做范围选择（锚点为 primary）；
- `removeIf`：批量清理失效句柄；
- 拖放支持：payload 序列化用固定大小数组（上限 256），避免 ImGui 浅拷贝 payload 时
  指针失效。

## Dock 布局

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

## HierarchyPanel（场景树）

树形展示 `Scene Root` 及子节点。非激活节点整行变灰（按 `activeInHierarchy`），
树节点 ID 只依赖 `revision` 与 `NodeHandle`（不依赖名称或祖先链），所以重命名与
换父节点不会丢失展开状态；场景重载后 `revision` 变化，旧 ID 自然作废。

### 选择模型

- 左键选择；Ctrl+点击切换；Shift+点击沿本帧可见行顺序做范围选择
  （`visibleNodes_` 在绘制时收集）。
- 右键未选中的节点先单选；已处于多选中则保留整组选择，弹批量菜单。
- 点击空白区域清空选择。

### 重命名

双击节点（非 root、无修饰键、且没有点在展开箭头上）进入就地重命名：Enter 提交、
Esc 取消、失焦提交；空名或名字未变不提交。创建的新节点会直接进入重命名。

### 右键菜单

| 位置 | 菜单项 |
|---|---|
| 节点行（多选时） | `Delete Selected` / `Activate Selected` / `Deactivate Selected` |
| 节点行（单选） | `Create Empty Child` / `Rename` / `Delete` |
| 空白区域（行控件外） | `Create Empty`（挂到 Scene Root） |

### 拖放

拖动源在节点行上激活（重命名中的节点除外）：

- 按下时暂存 `dragStartSelection_`：点到已选组内成员时，拖动携带**替换前的整组**
  （预览显示 `name + N more`，并恢复显示被替换的多选）；点到组外成员则只拖单个。
- payload 按**可见行顺序**稳定排序（折叠的选中项排末尾），批量插入时保持树中
  相对顺序；上限 256 个节点，超限时 tooltip 提示并拒绝放置。
- 悬停目标行按纵向位置分区：上 25% 为 `Before`（前插线）、下 75% 区间下 25% 为
  `After`（后插线）、中间 `Into`（整个矩形框，成为目标节点的末尾子节点）；
  悬停空白区域整行视作 `Into` Scene Root。悬停 `Into` 目标 0.6 秒自动展开，
  拖动时窗口上下边缘 24px 内自动滚动。

放置槽在**排除全部拖动节点后的同级列表**上计算；目标属于拖动集合时拒绝
（“The drop target is part of the dragged nodes.”）。每次放置前还会对每个源
调用 `Scene::canMoveNode` 预检（无效句柄、循环层级等）。

### Request 流水线

交互不直接改场景，而是把意图存进 `pending_`（`Request` 结构），
在 `draw()` 末尾统一 `applyRequest()`：

| Action | 语义 |
|---|---|
| `Create` | 创建 `Node` 挂到目标父级（失败则销毁回滚），选中新节点、展开祖先、进入重命名 |
| `Delete` | 批量销毁（跳过 root 与已失效句柄）；`removeIf` 清理失效选择，全部删光时选中原父节点 |
| `SetActive` | 批量设置激活状态 |
| `Move` | 批量移动，见下 |

`applyRequest` 开头校验 `request.revision == document_.revision()`，场景在交互与
应用之间被重载过就整单丢弃。

批量 `Move` 使用**动态锚点**：先在排除全部源后的同级列表里取出插入位之前的前缀
元素集合，然后逐个源插入到“最后一个锚点（前缀元素或已插入的源）之后”——中间态里
尚未处理的源仍占原位，插入位必须在当前真实列表上动态定位，静态的 `index+i` 会导致
源位于插入点之前时顺序错乱。单个源被 `Scene::moveNode` 拒绝即整体中止。单节点拖动
把选择聚焦到被移动节点，批量拖动整组保持选中（句柄不变）。任何成功变更都会
`markDirty()` 并展开目标的祖先。

> 注意：`HierarchyPanel.h` 的 `Request` 注释仍写着 “Move 只使用首个元素”，这是
> 批量拖动改造前的过时描述；实际实现处理整个 `nodes` 集合。

## InspectorPanel（检查器）

`draw(selection)` 按选择数分支。

### 单选视图

名称与 Active 开关，然后按组件存在与否依次绘制 Transform、Mesh、Material、
Camera、Light，最后是 `Add Component` 弹窗（已存在的组件类型不再列出）和每个组件
的 `Remove Component`。要点：

- Transform：Position/Scale 用 `DragFloat3`，Rotation 以欧拉度显示、提交时归一化
  回四元数。
- 资产引用用 combo 列出 `assets://` 下匹配后缀的文件，选中即通过对应 Manager
  `load` 并写回组件。
- Primitive Mesh 的参数通过局部副本编辑，只有控件真的变化时才
  `editPrimitiveRecipe()` + `applyPrimitiveChanges()`，否则每帧都会重建网格。
- 所有写操作后调用 `document_.markDirty()`。

### 多选视图

只显示共有字段，编辑同步写入所有选中节点：

- 头部显示 `N nodes selected`。
- Active 呈三态：混合状态时用 `ImGuiItemFlags_MixedValue`（编辑器代码不含
  `imgui_internal.h`，数值 `1 << 12` 直接写）让复选框渲染方块，本地值取 false，
  首次点击把整组统一为激活，再次点击统一取消。
- Transform 逐分量判断：所有节点值一致时显示公共值并批量写入；混合值显示灰色
  占位（`TextDisabled`）且不可编辑。

## ProjectPanel（项目浏览器）

递归列出 `assets://`，目录优先排序，隐藏 `.meta`，按后缀识别资产类型并显示单字母
图标。场景可双击打开（通过回调交 `EditorApplication` 换 `SceneDocument` 的来源），
其他资产置灰只显示路径 tooltip。`FileSystem::listFiles` 只返回文件，子目录是从子
路径重建的，因此空目录也能作为折叠节点出现。当前是只读浏览器。

## SceneViewPanel（场景视图统计）

统计节点/相机/网格/灯光数量，显示视口尺寸、帧时间与主相机参数。它不承载离屏
渲染目标——引擎窗口本身即场景视图，面板叠加在透出的帧上（见 Dock 布局）。

## ProjectPickerPanel（项目选择器）

编辑器启动或关闭项目后显示的模态框：

- 最近项目列表：整行按钮打开，行尾 `Open` / `Remove`（仅删注册表记录）/
  `Delete...`（`deleteFromDisk`，默认走 Windows 回收站）。
- `New Project...` 表单：项目名 + 原生文件夹选择（`openNativeFolderPicker`，
  Windows `IFileOpenDialog`），提交后调 `ProjectTemplate::createNewProject`。
- `Browse Folder...`：通过原生对话框选任意已有项目目录。

面板本身不改任何状态，全部通过三个回调交还 `EditorApplication`：

| 回调 | 触发 | 编辑器侧动作 |
|---|---|---|
| `OpenHandler(path)` | 打开/浏览选定项目 | 记入 `pendingProjectRoot_`，下一帧执行打开 |
| `CreateHandler(parent, name)` | 创建成功 | 记入 `pendingProjectRoot_`，下一帧执行打开 |
| `ChangedHandler()` | 注册表增删 | `ENGINE.saveEditorConfig()` 落盘 |

延迟到下一帧的原因与 ImGui 上下文重建有关，详见
[ProjectManagement.md](ProjectManagement.md)。

## 相关测试

| 测试 | 覆盖 |
|---|---|
| `HierarchyPanelTest` | 单选/多选/Shift 范围、拖放（排序/跨父级/目标在源内拒绝/上限）、右键菜单、重命名、场景切换安全 |
| `InspectorPanelTest` | 批量 Active 三态切换、Transform 一致值批量写与混合值占位 |
| `SelectionSetTest` | 容器纯逻辑：修饰键、范围选择、removeIf、payload 序列化 |
| `SceneHierarchyEditorTest` | `Scene` 层的父子重排与世界变换保持（面板的后端依赖） |
