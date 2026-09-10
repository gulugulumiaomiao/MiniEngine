# 源码目录与构建边界

项目按“架构层 + 子系统职责”组织源码。目录表达代码属于哪个系统，不按
`Manager`、`Utility`、`Handle` 等类的外观建立跨领域聚合目录。

```text
src/
├── core/
│   ├── base/              Handle、对象池、单例和构建配置
│   ├── filesystem/        虚拟路径、挂载点、依赖图和文件系统
│   ├── logging/           日志基础设施
│   ├── math/              通用数学类型
│   └── serialization/     Transfer、JSON 和二进制序列化
├── asset/
│   ├── base/              Asset、AssetId 和 AssetMeta
│   ├── database/          AssetDatabase
│   ├── derived_data/      Artifact 和派生数据
│   ├── importer/          Importer、导入流水线和文件监视
│   └── manager/           运行时 AssetManager
├── rhi/
│   ├── api/               后端无关的 GPU 接口、描述和句柄
│   └── vulkan/            Vulkan 资源与命令实现
├── render/
│   ├── gpu/               GPU 资源 Manager、Factory 和 Cache
│   │   ├── common/
│   │   ├── frame/
│   │   ├── material/
│   │   ├── mesh/
│   │   ├── pipeline/
│   │   ├── shader/
│   │   └── texture/
│   ├── material/          Material 资产、运行时对象和 Manager
│   ├── mesh/              Mesh 资产、Primitive、Builder 和 Manager
│   ├── render_graph/      RenderGraph
│   ├── renderer/          Renderer 与 DrawList
│   ├── scene/             RenderScene 和 Lighting 数据
│   ├── shader/            Shader、预处理、编译、生成和 Manager
│   └── texture/           Texture 资产、运行时对象和 Manager
├── scene/
│   ├── components/        运行时组件
│   ├── node/              Node 和场景句柄
│   └── scene/             Scene、SceneAsset 和场景实例化
├── runtime/
│   ├── application/       Application 与游戏实现
│   ├── engine/            引擎生命周期和系统组装
│   ├── window/            运行时窗口
│   └── main.cpp           程序入口
└── CMakeLists.txt         MiniEngine 库与运行时程序
```

根目录、源码、工具和测试各自维护自己的 CMake 文件：

```text
CMakeLists.txt             全局编译选项、依赖发现和子目录编排
src/CMakeLists.txt         MiniEngine 静态库、MiniVulkanEngine
tools/CMakeLists.txt       MiniShaderCompiler、MiniAssetCooker
tests/CMakeLists.txt       测试目标和测试专用配置
```

## 构建边界

所有引擎实现统一编入一个 `MiniEngine` 静态库。原有的 `MiniCore`、`MiniAsset`、
`MiniMesh`、`MiniAssetImporters`、`MiniScene` 和 `MiniSpirvCrossCore` 不再分别生成。
目录和头文件依赖仍然表达逻辑层级，但不再用大量静态库制造人工链接边界。

这样处理有三个直接结果：

- 每个类的成员函数都留在自己的实现文件，例如 `MeshManager` 的全部方法都在
  `render/mesh/MeshManager.cpp`。
- 工具、运行时和测试只链接 `MiniEngine`，不再重复列出 AssetManager、Shader、
  Material 等实现源文件。
- 新增 `src/**/*.cpp` 时由 `src/CMakeLists.txt` 自动纳入，程序入口
  `runtime/main.cpp` 是唯一明确排除并单独生成可执行文件的源码。

## 逻辑依赖方向

构建目标合并不代表架构层级消失。代码仍遵循以下方向：

```text
Runtime ──> Scene ──> Render resources
   │                     │
   ├────> Asset <────────┤
   │                     v
   └────> Renderer ──> RHI API <── Vulkan

Core 被所有层使用，不反向依赖其他层。
```

- `core` 不理解资产、场景、材质或 Vulkan 渲染流程。
- `rhi/api` 不暴露 Material、Mesh、Scene 或 ShaderLab 概念。
- `rhi/vulkan` 只实现 RHI，不反向依赖 Render 层。
- CPU 侧 Shader、Material、Mesh、Texture 及其 Manager 位于各自领域目录。
- `render/gpu` 负责 CPU Handle 到 GPU 资源的解析、创建和缓存。
- `asset` 负责磁盘资产、导入、数据库、派生数据和资产对象缓存。
- `runtime` 负责挂载文件系统并组装资产、场景、GPU Manager 和 Renderer。

新增代码时应优先放入其所属子系统，不建立 `Managers/`、`Utils/`、`Misc/`
等跨职责目录，也不要为了绕过静态库链接顺序把某个类的成员函数放进其他类的
实现文件。
