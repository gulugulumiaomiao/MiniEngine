# Mini Vulkan Engine

项目的开发讨论与上下文记录见
[当前 ChatGPT 对话](https://chatgpt.com/s/cx_6a99ab8d9ef88191be32e9d4a4c3baf2)。

新电脑的依赖安装、克隆后构建和 VS Code F5 调试流程见
[`docs/GettingStarted.md`](docs/GettingStarted.md)。

RHI 命令抽象、DrawList、传输编码器和最小 RenderGraph 的实现记录见
[`docs/RhiCommandSystem.md`](docs/RhiCommandSystem.md)。

引擎层 Material 管理、数据提取和 RHI 边界见
[`docs/MaterialSystem.md`](docs/MaterialSystem.md)。

Shader Properties 的 std140 uniform 内存布局见
[`docs/ShaderUniformLayout.md`](docs/ShaderUniformLayout.md)。

ShaderLab Properties 到 GLSL 的离线生成约定见
[`docs/ShaderCodeGeneration.md`](docs/ShaderCodeGeneration.md)。

Shader → SubShader → Pass 的运行时选择流程见
[`docs/ShaderRuntime.md`](docs/ShaderRuntime.md)。

Shader、MaterialAsset 和 MeshAsset 的统一路径缓存见
[`docs/AssetManager.md`](docs/AssetManager.md)。

资产身份、Meta、AssetDatabase 与 Artifact 基础设施见
[`docs/AssetFoundation.md`](docs/AssetFoundation.md)。

Importer 接口、注册方式和 Shader 依赖收集见
[`docs/AssetImporters.md`](docs/AssetImporters.md)。

虚拟路径、目录挂载和统一文件读写见
[`docs/FileSystem.md`](docs/FileSystem.md)。

SPIR-V descriptor、uniform offset 和阶段接口反射校验见
[`docs/SpirvReflection.md`](docs/SpirvReflection.md)。

Shader、材质、编译缓存、RHI Shader、Pipeline、GPU 数据与热重载的完整分层见
[`docs/ShaderMaterialPipeline.md`](docs/ShaderMaterialPipeline.md)。

日志级别、颜色、输出格式和 fatal 退出语义见
[`docs/Logging.md`](docs/Logging.md)。

一个刻意保持简单、但可以自然长成游戏引擎的 Vulkan 1.3 渲染器骨架。当前版本会在深蓝色背景上绘制两个共享 Mesh/Material、但拥有不同变换的三色三角形，已经覆盖 shader、graphics pipeline、Vulkan 生命周期、帧同步和窗口缩放等最小完整渲染链路。

## 当前能力

- C++20、CMake、原生 Win32 窗口、Vulkan 1.3
- Debug 构建自动开启 `VK_LAYER_KHRONOS_validation`
- 自动选择图形队列和呈现队列
- 双帧并行（2 frames in flight）
- Vulkan 1.3 Dynamic Rendering，无需提前创建 RenderPass
- `glslc` 自动把 GLSL 编译为 SPIR-V
- RAII `ShaderModule`、`GraphicsPipeline`、`Buffer`、`Image` 与 `Sampler`
- Vulkan Memory Allocator（VMA 3.3.0）统一管理 GPU 内存
- 使用真实 vertex buffer 的三色三角形
- Vulkan 无关的 `RenderScene`、`MeshHandle`、`MaterialHandle` 场景提交
- 带 generation 的资源句柄，可检测已销毁或复用后的 stale handle
- 静态 Mesh 经 host-visible staging buffer 上传到 device-local vertex buffer
- 每个 `FrameContext` 独立持有对象 transform storage buffer 与 descriptor set
- 顶点着色器通过 `gl_InstanceIndex` 读取每个 `RenderObject` 的矩阵
- RAII `DescriptorSetLayout` 与逐帧 `DescriptorAllocator`
- descriptor pool 耗尽或碎片化时自动扩容，帧 fence 完成后统一 reset 复用
- JSON ShaderLab v1：Properties、Tags、多 Pass、Render State 与 Feature 声明
- JSON Material：Shader 引用、类型安全的属性覆盖、Keyword 与 RenderQueue
- `Material` 持有运行时 `Shader`，可按路径实时切换并保留兼容属性；`Shader` 与 `ShaderAsset` 明确分层
- 当前 Forward Pipeline 的源码和 Vulkan 状态由 JSON Shader 驱动，SPIR-V 在 Pass 首次参与绘制时按需生成
- 虚拟路径与目录挂载文件系统；AssetManager 的 JSON 资产读取统一经过 `asset://`
- GLM 1.0.3 数学层：`Vec2/3/4`、`Mat33/44`、`Quat` 和游戏引擎常用运算
- ShaderLab 属性新增 `Vec2`、`Vec3`、`Vec4`，并保留 `Vector → Vec4` 兼容解析
- 离线 `MiniShaderCompiler` 根据 Properties 和统一布局生成 GLSL uniform/texture 声明
- ShaderLab Pass 自动生成 vertex/fragment 接口、插值限定符与入口包装函数
- ShaderLab 在 Pass/Variant 首次参与绘制时编译为 Vulkan 1.3 SPIR-V，并按 Debug/Release 选择调试信息或优化；离线目标仅用于显式验证
- 后端无关的 Mesh 数据模型：VertexLayout、IndexType、SubMesh、Bounds、MeshDesc/MeshData
- 通用 Mesh 上传接口、device-local VBO/IBO 与 `vkCmdDrawIndexed`
- 窗口最小化/缩放时安全重建 Swapchain
- `IRenderBackend` 隔离上层渲染器与 Vulkan 实现
- 线程安全的五级彩色日志；引擎错误不再主动抛出 C++ 异常，致命错误统一记录后退出

## 依赖与构建

已验证的 Windows 工具链：

1. CMake 4.4.3
2. Ninja 1.13.2
3. WinLibs UCRT Clang 19.1.1
4. Vulkan SDK 1.4.357.0
5. 支持 Vulkan 1.3 的显卡驱动

VMA 3.3.0、nlohmann/json 3.11.3 与 GLM 1.0.3 已固定版本并随源码放在 `third_party`，工程配置和构建过程不需要在线下载依赖。

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug
./build/clang-debug/MiniVulkanEngine.exe
```

Release 构建不要求安装验证层：

```powershell
cmake --preset clang-release
cmake --build --preset clang-release
./build/clang-release/MiniVulkanEngine.exe
```

## Debug 与 Release

CMake 保证每个翻译单元只会定义一个工程宏：

- Debug：`MINI_DEBUG=1`
- Release、RelWithDebInfo、MinSizeRel：`MINI_RELEASE=1`

`src/core/BuildConfig.h` 会在两个宏同时存在或同时缺失时产生编译错误，避免不同模块使用不一致的类布局。

Debug 版本包含 Vulkan validation layer、`VK_EXT_debug_utils`、debug callback 和 debug messenger，窗口标题为 `Mini Vulkan Engine [Debug]`。Release 版本不会编译这些调试代码，启用 CMake Release 优化，窗口标题为 `Mini Vulkan Engine [Release]`。

在 VS Code 的“运行和调试”下拉框中选择：

- `Debug MiniVulkanEngine (CodeLLDB)`：构建并调试 Debug。
- `Run MiniVulkanEngine Release`：构建并运行 Release；也可按 `Ctrl+F5` 无调试运行。

## VS Code 调试

项目已配置并安装以下扩展：

- clangd：代码补全、跳转、诊断和 clang-tidy
- CMake Tools：识别 `clang-debug` / `clang-release` preset
- CodeLLDB：调试 MinGW/UCRT Clang 生成的程序

使用方法：

1. 用 VS Code 打开本工程根目录。
2. 在 `src/main.cpp` 或其他 `.cpp` 文件行号左侧单击设置断点。
3. 按 `F5`，选择 `Debug MiniVulkanEngine (CodeLLDB)`。
4. VS Code 会依次执行 CMake configure、build，再启动渲染器。

`Ctrl+Shift+B` 可单独执行默认 Debug 构建任务。调试配置位于 `.vscode/launch.json`，任务位于 `.vscode/tasks.json`；工具路径均显式配置，不依赖 VS Code 启动时是否已经刷新系统 PATH。

MinGW/WinLibs 的 C++、GCC 和线程运行库会静态链接进可执行文件；Vulkan 与 Win32 仍使用系统 DLL。因此从 CodeLLDB 或资源管理器启动不依赖父进程的 PATH，也不会出现缺少 `libstdc++-6.dll` 等运行库的问题。

## 结构

```text
.
├── assets/
│   ├── shaders/                   JSON ShaderLab 资产
│   └── materials/                 JSON Material 资产
├── docs/ShaderLabJson.md          JSON ShaderLab v1 格式说明
├── docs/ShaderCodeGeneration.md   Properties 到 GLSL 的生成与绑定约定
├── docs/Math.md                   数学类型、坐标约定与常用运算
├── docs/Mesh.md                   网格布局、数据、SubMesh 与 Bounds
└── src/
    ├── main.cpp                   程序入口和启动/停止日志
    ├── core/
    │   ├── BuildConfig.h          Debug/Release 宏契约与构建信息
    │   ├── Log.*                  五级彩色日志与 fatal 退出策略
    │   ├── io/                    虚拟路径、挂载点与同步文件读写
    │   └── Application.*          生命周期、主循环；未来放 Engine/World
    ├── math/Math.h                引擎数学类型与常用运算
    ├── platform/Window.*          Win32 封装；不泄露 Vulkan 对象
    ├── renderer/                  高层渲染入口和后端稳定边界
    │   ├── Mesh.*                 CPU Mesh 数据模型与严格校验
    │   ├── RenderResources.h      Mesh/Material 句柄与资源描述
    │   ├── RenderScene.h          每帧可提交的渲染对象列表
    │   ├── Shader.*               ShaderAsset、ShaderLayout 与运行时 Shader
    │   └── Vertex.h               当前最小 Mesh 顶点格式
    └── rhi/vulkan/
        ├── GpuAllocator.*         VMA allocator RAII
        ├── Buffer.*               VkBuffer + VMA allocation RAII 与上传
        ├── DescriptorAllocator.*  Descriptor layout RAII 与逐帧自动扩容池
        ├── Image.*                VkImage + allocation + image view RAII
        ├── Sampler.*              VkSampler RAII
        ├── ShaderModule.*         SPIR-V shader module RAII
        ├── GraphicsPipeline.*     Dynamic Rendering pipeline RAII
        └── VulkanBackend.*        Vulkan 实例、设备、Swapchain、命令和同步
```

依赖方向只有一条：

```text
Application → RenderScene → Renderer → IRenderBackend ← VulkanBackend
      ↓                                         ↓
    Window ────────────────────────────────── Surface
```

上层不持有 `VkDevice`、`VkImage` 等原生句柄。这一点比一开始抽象几十个 Vulkan 类型更重要：它保留更换后端或做无窗口测试的可能，又没有引入空洞的“大引擎接口”。

## 一帧如何运行

1. 等待当前 `FrameContext` 的 fence。
2. 从 Swapchain 获取一张图像。
3. 等 fence 后，reset 当前帧的 descriptor allocator，重新分配场景 descriptor，并把对象矩阵写入独立 storage buffer。
4. 遍历 `RenderScene`，校验 Mesh/Material 句柄的 index 与 generation，解析后端资源。
5. 绑定当前帧 descriptor set；逐对象绑定 device-local vertex/index buffer，遍历 SubMesh，以 `firstInstance` 选择对象矩阵并执行 `vkCmdDrawIndexed`。
6. 提交 graphics queue，使用 semaphore 串联 acquire/render/present。
7. 呈现；遇到 resize、out-of-date 或 suboptimal 时重建 Swapchain。

每帧的 command buffer、acquire semaphore 和 fence 归 `FrameContext` 所有；presentation semaphore 按 Swapchain 图像分配，直到对应图像再次 acquire 才能确认呈现操作不再使用它。以后增加 uniform ring buffer、descriptor allocator、timestamp query 时，应优先放进 `FrameContext`，避免 CPU 覆盖 GPU 仍在使用的数据。

## 推荐演进顺序

不要一次性把“完整游戏引擎”搬进来。按下面的顺序，每一步都保持可运行：

1. **画三角形（已完成）**：`ShaderModule`、`GraphicsPipeline`、GLSL 自动编译，继续使用 Dynamic Rendering。
2. **资源 RAII（已完成）**：`GpuAllocator` 统一持有 VMA；`Buffer`、`Image`、`Sampler` 自动释放资源，三角形已使用真实 VBO。
3. **场景提交（已完成）**：定义与 Vulkan 无关的 `RenderScene`、`MeshHandle`、`MaterialHandle`；`Renderer` 转交场景，Vulkan 后端解析带 generation 的资源表。
4. **GPU 数据策略（已完成）**：per-frame storage buffer 隔离动态对象数据；Mesh 通过 staging buffer 复制到 device-local VBO；descriptor 将矩阵提供给 shader。
5. **Descriptor 管理（已完成）**：每个 `FrameContext` 拥有可 reset、自动扩容的 descriptor allocator；layout 和 pool 都由 RAII 管理。资源量大后再评估 bindless。
6. **RenderGraph**：只有在出现阴影、后处理、多 pass 资源依赖后再引入，不要提前设计。
7. **引擎层**：`World/ECS → RenderExtraction → RenderScene → Renderer`，让游戏线程不直接操作 GPU 对象。
8. **异步与工具链**：后台资源加载、shader 热重载、离线资产导入、编辑器。

建议未来的核心数据流是：

```text
Game World (可变、面向玩法)
        │ 每帧提取快照
        ▼
RenderScene (只含渲染需要的数据)
        │ 构建 pass
        ▼
RenderGraph (资源依赖和执行顺序)
        │ 编码命令
        ▼
IRenderBackend / VulkanBackend
```

## 应保持的边界

- 游戏逻辑不能 include Vulkan 头文件。
- `Renderer` 接受 handle 和纯数据，不接受 `VkBuffer` 等原生对象。
- GPU 对象创建/销毁集中在后端；销毁要考虑 frames-in-flight，可增加 deferred deletion queue。
- resize 只使与 Swapchain 尺寸相关的资源失效，资产资源不应重建。
- 后端接口按真实需求扩展，避免预先复制整套 Vulkan API。

## 下一步最小接口草案

画三角形后，可逐渐让后端拥有这些显式资源接口：

```cpp
struct BufferHandle  { std::uint32_t id; };
struct TextureHandle { std::uint32_t id; };

BufferHandle  createBuffer(const BufferDesc&, std::span<const std::byte> initialData);
TextureHandle createTexture(const TextureDesc&, std::span<const std::byte> initialData);
void          destroy(BufferHandle);   // 实际进入延迟销毁队列
void          destroy(TextureHandle);
```

句柄应带 generation，防止 use-after-free；`Desc` 应是后端无关的数据结构。暂时不要暴露通用 `void* nativeHandle()`，否则边界很快会被绕开。

## 当前刻意没做的事

- 没有纹理 descriptor：当前 Mesh 的 VBO/IBO staging 上传会同步等待 graphics queue，资源量增大后应升级为批量异步 upload queue。
- PipelineCache 按 ShaderPass、RenderState、当前 VertexLayout 和 RenderTarget format 缓存 Vulkan Pipeline。
- 当前 Mesh 仍支持一个活动 `VertexLayout`；缓存键已经包含布局，多布局资源管理将在后续解除这一限制。
- `Material` 持有可切换的运行时 `Shader`；Renderer 按 `Shader → SubShader → ShaderPass` 选择 Pipeline，并通过 `MaterialGpuCache` 上传 uniform、绑定材质 descriptor。
- 对象 storage buffer 当前最多容纳 1024 个 transform；后续可改为可增长的 per-frame linear/ring allocator。
- Renderer 已按 `ShadowCaster → DepthOnly → Forward` 收集存在的 Pass；DepthOnly 与 ShadowCaster 仍需各自的深度/阴影 RenderTarget 才能形成完整渲染路径。
- `Texture2D` 属性当前完成类型解析，尚未接入图片解码、上传和材质纹理 descriptor。
- 没有 ECS：它属于游戏世界，不属于渲染底层。
- 没有多线程录制：场景和 pass 规模出现后再决定任务粒度。
- 没有 RenderGraph：只有一个清屏 pass 时它只会增加样板代码。

这使当前代码仍可阅读、可调试，同时每个缺失能力都有明确的落点。
