# 源码目录与依赖约定

项目源码按“架构层级 + 子系统职责”分类。目录表达代码属于哪个系统，
不按照 `Manager`、`Utility`、`Handle` 等类的外观分类。

```text
src/
├── core/
│   ├── base/              通用 Handle、对象池、单例和构建配置
│   ├── filesystem/        虚拟路径、挂载点和文件系统
│   ├── logging/           日志基础设施
│   ├── math/              通用数学类型与运算
│   └── serialization/     Transfer、JSON 和二进制序列化
├── rhi/
│   ├── api/               后端无关的 GPU 命令与句柄
│   └── vulkan/            Vulkan 底层资源和命令实现
├── render/
│   ├── cache/             Mesh、Material、Shader 与 Pipeline 的 GPU 缓存
│   ├── material/          Material 资产与运行时对象
│   ├── mesh/              Mesh 资产、运行时对象和顶点格式
│   ├── render_graph/      RenderGraph
│   ├── renderer/          Renderer、DrawList 和 RenderScene
│   └── shader/            Shader、编译、生成、反射和实例管理
├── asset/
│   ├── base/              Asset、AssetId 和 AssetMeta
│   ├── database/          AssetDatabase
│   ├── derived_data/      Artifact/派生数据
│   ├── importer/          Importer、导入流水线和资产文件监控
│   └── manager/           运行时 AssetManager
├── scene/
│   ├── components/        运行时组件及对应组件资产描述
│   ├── node/              Node、SceneNodeAsset 和场景句柄
│   └── scene/             Scene、SceneAsset 和场景实例化
└── runtime/
    ├── application/       Application 与游戏入口实现
    ├── engine/            引擎生命周期和系统组装
    ├── window/            运行时窗口
    └── main.cpp           程序入口
```

## 依赖方向

```text
Core
  ↑
RHI
  ↑
Render
  ↑
Asset / Scene
  ↑
Runtime
```

- `Core` 不理解资产、场景、材质或 Vulkan 渲染流程。
- `RHI/api` 只暴露 GPU 资源、命令和句柄，不暴露 Material、Scene 或
  ShaderLab 概念。
- `Render` 可以使用 RHI，并负责 Shader、Material、Mesh 和渲染流程。
- `Asset` 管理磁盘资产、导入、数据库、派生数据和运行时资产缓存。
- `Scene` 管理节点、组件、场景资产及运行时场景。
- `Runtime` 负责把窗口、资产、场景和渲染系统组装成可执行程序。

Render 层已经不再包含 Vulkan/VMA 类型。`Renderer` 负责 DrawList 编排，`render/cache`
负责 Mesh、Material、Shader、Pipeline 的 GPU 缓存；BindGroup、Swapchain、帧同步、
命令提交和所有 Vulkan 原生对象均位于 `rhi/vulkan`。RHI Context 由程序入口选择具体工厂
创建并注入 Renderer，Render 不需要包含具体图形 API 的头文件。任何 `rhi` 文件都不能
反向依赖 Material、Mesh 或 Shader 等 Render 概念。

Buffer、Image、Sampler 和 ShaderModule 在 `rhi/api` 定义后端无关接口，在
`rhi/vulkan` 分别由 `VulkanBuffer`、`VulkanImage`、`VulkanSampler` 和
`VulkanShaderModule` 实现。Renderer 仍只通过 `IDevice` 和带 generation 的句柄使用资源。

新增代码时应优先选择其所属子系统，不建立 `Managers/`、`Utils/`、
`Misc/` 等跨职责聚合目录。
