# Mini Vulkan Engine

一个使用 C++20、原生 Win32 和 Vulkan 1.3 编写的最小游戏引擎骨架。项目从
可运行的 Vulkan 渲染器出发，逐步建立了场景、资产、Shader、Material、Mesh
和 RHI 分层，同时刻意保持实现规模可读、可调试、可继续扩展。

## 当前效果

示例程序创建 Win32 窗口，加载 `blinn_phong_showcase.scene.json` 场景：一块地面、
一个盒子、一个球体和一个圆柱体，分别使用不同 Blinn-Phong 材质；同时包含一个
投射阴影的方向光和一个点光源。它覆盖了从场景组件、光照与阴影到 DrawList、
Shader Pass、Vulkan Pipeline 和最终 Present 的完整链路。

## 引擎架构

源码按照“架构层级 + 子系统职责”组织：

```text
src/
├── core/
│   ├── base/              Handle、HandlePool、KeyedHandleRegistry、Singleton
│   ├── filesystem/        VirtualPath、挂载点和统一文件访问
│   ├── logging/           五级彩色日志
│   ├── math/              Vec、Mat、Quat 和常用数学运算
│   └── serialization/     Transfer、JSON/Binary Reader/Writer
├── rhi/
│   ├── api/               后端无关的 GPU 句柄与命令接口
│   └── vulkan/            Vulkan Buffer、Image、Sampler、Descriptor 等封装
├── render/
│   ├── cache/             Mesh、Material、Shader 与 Pipeline 的 GPU 缓存
│   ├── shader/            Shader、SubShader、Pass、生成、编译和反射
│   ├── material/          MaterialAsset 与运行时 Material
│   ├── mesh/              MeshAsset、Mesh、VertexLayout 和 Bounds
│   ├── render_graph/      RenderGraph 基础设施
│   └── renderer/          RenderScene、DrawList 和 Renderer
├── asset/
│   ├── base/              Asset、AssetId 和 AssetMeta
│   ├── database/          AssetDatabase
│   ├── derived_data/      二进制 Artifact
│   ├── importer/          Importer、导入流水线和 FileWatcher
│   └── manager/           AssetManager 资产缓存
├── scene/
│   ├── components/        Transform、Mesh、Material、Camera、Light
│   ├── node/              Node、SceneNodeAsset 和场景句柄
│   └── scene/             Scene、SceneAsset、验证和实例化
└── runtime/
    ├── application/       Application 与 GameApplication
    ├── engine/            Engine 生命周期和系统组装
    ├── window/            Win32 Window
    └── main.cpp           程序入口
```

主要依赖方向为：

```text
Core → RHI → Render → Asset / Scene → Runtime
```

- Core 不理解场景、材质或 Vulkan 渲染流程。
- RHI API 只表达 GPU 资源、句柄和命令。
- Render 负责 Shader、Material、Mesh、DrawList 和渲染后端。
- Asset 负责磁盘资产、导入、Artifact、缓存和热重载通知。
- Scene 负责 Root Node、层级、组件以及渲染快照提取。
- Runtime 负责初始化各层、运行主循环并按顺序关闭系统。

更完整的目录规则见 [源码目录与依赖约定](docs/SourceLayout.md)。

## 已实现功能

### Core 与资源生命周期

- 通用 `slot + generation` Handle，可识别销毁或槽位复用后的失效句柄。
- `HandlePool` 使用 free list 进行 O(1) 槽位复用。
- `KeyedHandleRegistry` 组合 HandlePool 和可自定义的 Key → Handle 索引，统一运行时对象的 insert、find、destroy 和 clear；具体 Manager 自行实现 load。
- RAII 管理 Vulkan Buffer、Image、Sampler、ShaderModule 和 Pipeline。
- VMA 3.3.0 统一管理 GPU 内存。
- `info/debug/warn/error/fatal` 五级线程安全彩色日志；fatal 记录后退出。

### 文件与资产系统

- `VirtualPath` 和目录挂载；引擎资源统一通过 `asset://`、派生数据通过
  `library://` 访问。
- `AssetDatabase` 记录资产身份、类型、依赖、导入状态和 Artifact 路径。
- Shader、Material、Mesh、Scene 四类 Importer。
- 每类 Asset 通过 `Transferable::transfer(Transfer&)` 自行实现 JSON/二进制
  数据传输。
- Artifact 使用二进制格式保存导入后的运行时数据。
- Debug 与 Release 下 FileWatcher 驱动重新导入、缓存失效以及 Shader/Mesh/Scene 热重载。
- Publish 下只读取已烘焙 AssetDatabase 和 Artifact，不依赖源 JSON 的动态导入。

### Scene

- Scene 固定拥有 Root Node，并从 Root 更新层级和 Transform。
- Node 支持父子关系、循环检测、激活状态传播和 Transform dirty 标记。
- Transform、Mesh、Material、Camera、Light 作为 Component 挂载到 Node。
- SceneAsset 可从 JSON 导入、验证、二进制序列化并实例化运行时 Scene。
- 每帧将可变 Scene 提取成只包含渲染所需信息的 `RenderScene`。

### Shader、Material 与 Mesh

- JSON ShaderLab v1 支持 Properties、SubShader、多个 Pass、Render State、
  Keyword/Feature 和顶点/片元接口声明。
- Pass 的 `vertex`、`fragment` 字段引用 `.vert`/`.frag` 源码。
- Shader 预处理器生成 Properties uniform/texture 声明和阶段接口包装代码。
- GLSL 按需编译为 Vulkan 1.3 SPIR-V，并使用 SPIRV-Cross 进行 descriptor、
  uniform offset、顶点输入和阶段输出反射验证。
- 运行时结构为 `Shader → SubShader → ShaderPass`，最终由 ShaderPass 参与绘制。
- Material 持有 `ShaderHandle`，支持实时切换 Shader、属性覆盖、Keyword 和
  RenderQueue。
- 自定义 VertexLayout、交错或分离 VertexStream、16/32 位索引、SubMesh、
  AABB 和 BoundingSphere。
- 静态 Mesh 通过 staging buffer 上传到 device-local VBO/IBO。

### 渲染与 Vulkan 后端

- Vulkan 1.3 Dynamic Rendering。
- 自动选择 graphics/present queue 和 Swapchain 格式。
- 双帧并行；每个 FrameContext 独立维护命令、同步和 descriptor 资源。
- 每帧相机/方向光 UBO 与对象 Transform SSBO。
- VulkanDescriptorAllocator 可自动扩容，并在对应帧 fence 完成后 reset 复用。
- PipelineCache 按 Shader Pass、Variant、VertexLayout、RenderState 和
  RenderTarget format 缓存 Vulkan Pipeline。
- Renderer 按 `ShadowCaster → DepthOnly → Forward` 收集和排序存在的 Pass。
- 窗口 resize、最小化、out-of-date 和 suboptimal 时安全重建 Swapchain。
- Renderer 只通过 RHI 接口提交绘制，不依赖 Vulkan 类型。

## 运行流程

### 1. 启动

```text
main
  → 创建 GameApplication
  → Engine::run(Application)
  → 挂载 asset:// 与 library://
  → 初始化 AssetManager / AssetDatabase / ImportPipeline
  → 创建 Window
  → 创建 Renderer 和 VulkanBackend
  → Application::onStart()
```

Debug 与 Release 构建会扫描源资产并启动 FileWatcher；Publish 构建直接读取 Cooker
生成的数据库和 Artifact。示例 `GameApplication::onStart()` 直接加载
`asset://scenes/blinn_phong_showcase.scene.json`，场景中已包含地面、几何体、
Camera、Directional Light 和 Point Light 组件。

### 2. 资产加载

```text
asset:// 虚拟路径
  → AssetManager 查询内存缓存
  → AssetDatabase 查询资产记录
  → Debug 缺失时调用对应 Importer
  → 读取 library:// 二进制 Artifact
  → 创建具体 Asset 并调用 transfer(BinaryReader)
  → 缓存 Asset
  → MaterialManager / ShaderManager / MeshManager 实例化运行时对象
```

ShaderAsset 在加载时只保留资产数据。某个 ShaderPass 首次真正参与渲染时，才会
执行预处理、代码生成、SPIR-V 编译、反射验证以及 Pipeline 创建；后续绘制直接
命中各级缓存。

### 3. 每帧

```text
Window::pollEvents
  → 处理待执行的 Scene 热重载
  → Application::onUpdate(deltaTime)
  → Scene 从 Root 更新 Node、Component 和 Transform
  → Scene::buildRenderScene
  → Renderer 构建并排序 DrawList
  → VulkanBackend 准备 Mesh/Material/Pipeline GPU 资源
  → 录制并提交 CommandBuffer
  → Present
```

后端在复用 FrameContext 前等待对应 fence，然后 reset 该帧 descriptor allocator、
上传场景和对象数据。DrawItem 绑定 Pipeline、Material descriptor、VBO/IBO 后调用
`vkCmdDrawIndexed`。Swapchain 状态变化只重建与窗口相关的资源，不重建资产资源。

### 4. 热重载

```text
FileWatcher
  → AssetImportPipeline::processFileEvents
  → 重新导入并生成 Artifact
  → AssetManager 缓存失效
  → 替换已加载 Shader/Mesh，刷新关联 Material
  → 活动 Scene 标记为待重载
```

重载失败时记录 error，并尽量保留之前仍可运行的实例。

### 5. 关闭

```text
退出循环
  → Renderer::waitIdle
  → Application::onStop
  → 清理 Scene / RenderScene
  → 销毁 Renderer 和 Vulkan 资源
  → 清理 Mesh/Material/Shader 实例池
  → 关闭 AssetManager 和 FileWatcher
  → 卸载虚拟文件系统
  → 销毁 Window
```

这个顺序保证引用 GPU 资源的高层对象先停止使用，再释放底层 Vulkan/VMA 对象。

## 构建与运行

已验证的 Windows 工具链：

- CMake 4.4.3
- Ninja 1.13.2
- WinLibs UCRT Clang 19.1.1
- Vulkan SDK 1.4.357.0
- 支持 Vulkan 1.3 的显卡驱动

VMA、nlohmann/json、GLM 和 SPIRV-Cross 已固定版本放在 `third_party`，配置时
不需要在线下载依赖。

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug
./build/clang-debug/MiniVulkanEngine.exe
```

Release（优化构建，仍启用热重载与运行时 shader 编译）：

```powershell
cmake --preset clang-release
cmake --build --preset clang-release
./build/clang-release/MiniVulkanEngine.exe
```

Publish（发布构建，需先打包 SPIR-V）：

```powershell
cmake --preset clang-publish
cmake --build --preset clang-publish
cmake --build --preset clang-publish --target MiniShaderPackagedShaders
./build/clang-publish/MiniVulkanEngine.exe
```

运行全部测试：

```powershell
ctest --test-dir build/clang-debug --output-on-failure
```

- Debug 定义 `MINI_DEBUG=1`：启用 Vulkan Validation、debug messenger、运行时 shader 编译与热重载。
- Release 定义 `MINI_RELEASE=1`：仍启用运行时 shader 编译与热重载，但关闭验证层并启用优化。
- Publish 定义 `MINI_PUBLISH=1`：关闭验证层与热重载，使用预打包资产与 SPIR-V。

## VS Code 调试

推荐安装仓库建议的 clangd、CMake Tools 和 CodeLLDB 扩展。用 VS Code 打开
工程根目录后：

1. 在任意 `.cpp` 文件设置断点。
2. 按 `F5`。
3. 选择 `Debug MiniVulkanEngine (CodeLLDB)`。

VS Code 会自动执行 Debug configure 和 build。选择
`Run MiniVulkanEngine Release` 可启动 Release，选择
`Run MiniVulkanEngine Publish` 可启动 Publish；`Ctrl+Shift+B` 只执行默认
Debug 构建任务。

更完整的新电脑安装和调试说明见 [Getting Started](docs/GettingStarted.md)。

## 进一步阅读

- [资产流水线](docs/AssetPipeline.md)
- [文件系统](docs/FileSystem.md)
- [资产基础设施](docs/AssetFoundation.md)
- [Importer](docs/AssetImporters.md)
- [Scene 系统](docs/SceneSystem.md)
- [SceneAsset](docs/SceneAsset.md)
- [Shader/Material Pipeline](docs/ShaderMaterialPipeline.md)
- [ShaderLab JSON](docs/ShaderLabJson.md)
- [SPIR-V Reflection](docs/SpirvReflection.md)
- [Mesh](docs/Mesh.md)
- [RHI Command System](docs/RhiCommandSystem.md)
- [Transfer 序列化](docs/Transfer.md)
- [日志系统](docs/Logging.md)
