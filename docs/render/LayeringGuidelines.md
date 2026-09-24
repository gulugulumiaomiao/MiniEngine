# 资源系统三层架构规范（Texture / Mesh / Material / Shader）

本规范记录引擎四大资源系统当前采用的三层分层模型、命名、目录与跨层引用规则，作为后续扩展资源类型的依据。

## 1. 为什么是三层

从通用到专用，三层管理者职责逐层收窄，数据结构越来越贴近硬件。三层各自隔离一个独立的变化轴：

1. 用户需求变化（新属性、新脚本 API）只影响层1
2. 渲染策略变化（新管线、新优化）只影响层2
3. GPU 后端变化（新图形 API）只影响层3

少一层就会耦合（脚本状态与渲染策略混在一起，或渲染策略直接调驱动 API）；多一层通常是过度抽象。故统一为三层。

```
层 1 资源管理器        用户看到什么        标识: 路径 / AssetId
层 2 后端存储管理器    渲染器看到什么      标识: 存储层 RID
层 3 设备资源管理器    GPU 看到什么        标识: 设备层 RID
```

## 2. 三层管理器职责（逐层收窄）

每个资源类型都拆成三个职责收窄的管理器。

### 层1 资源管理器 `XxxResourceManager`
位置：`src/render/<x>/` 运行时对象 + `src/asset/{format,importer,exporter}/` 源格式与 I/O。线程：主线程为主。

- 管：路径去重（一路径一实例）、加载调度（同步/异步/优先级/请求合并）、引用计数（决定生死）、序列化、类型多态（可选，未来丰富类型体系）、流送调度（mip 需求 / 内存预算 / 重载排队）。
- 标识 = 路径 / AssetId；生命周期 = 引用计数：层1 对象一律继承 `RefCounted`、相互引用一律用 `Ref<T>`，引用归零即析构并级联触发层2/层3 释放（见 7. 约定 2）；`XxxResourceManager` 退化为路径/AssetId 去重缓存（弱引用），不再是单一所有者。
- 不做：不知 GPU 格式、不分配显存、不管 descriptor。**层1 头文件禁止持有任何 `rhi::` 类型或 GPU 句柄。**

### 层2 后端存储管理器 `XxxStorage`
位置：`src/render/gpu/<x>/`。线程：渲染线程为主。

- 管：槽位分配（两段式 allocate + initialize）、句柄查找（RID -> 后端记录）、格式转换（资源格式 -> 设备格式 + swizzle）、视图管理（sRGB/linear 双视图、slice 缓存）、代理/替换（proxy：逻辑句柄不变、底层可换）、图集合并（atlas）、默认资源（白/黑/法线占位）、依赖追踪（变更通知使用者）。
- 标识 = 存储层 RID（Storage 自有 RID_Owner）；生命周期 = 显式 free。持有层3 的设备层 RID。
- 不做：不管路径 / 引用计数 / 序列化，不直接调驱动 API。

### 层3 设备资源管理器 `IDevice`
位置：`src/rhi/`。单一、跨资源类型（对应 Godot 的 `RenderingDevice`）。线程：渲染线程。

- 管：GPU 对象创建（驱动 API）、共享视图、数据上传/下载（staging 分块）、传输调度、有效性验证、同步追踪（记录被哪些 pass 读写）。
- 标识 = 设备层 RID（Device 自有 RID_Owner，与层2 的 RID **不可互换**）；生命周期 = 显式 free。
- 不做：不知语义（albedo/法线）、不管 proxy/atlas/streaming、不管资源路径。GPU 对象结构体只含硬件字段。

## 3. 命名与目录约定

- 层1 资源管理器：`Xxx` / `XxxAsset` / `XxxResourceManager`，`src/render/<x>/` + `src/asset/{format,importer,exporter}/`。
- 层2 后端存储管理器：`XxxStorage` / `XxxStorageEntry` / `XxxStorageFactory` / `XxxStorageCache`，`src/render/gpu/<x>/`（是否整体更名为 `src/render/storage/` 见 7. 约定 5）；跨资源调度仍在 `src/render/renderer/`。
- 层3 设备资源管理器：统一 `IDevice`（`rhi/api/Device.h`）+ 每类型 GPU 对象 `IRHIXxx`/`VulkanXxx`；不再每资源单独设管理器。

四系统命名对照：

| 资源 | 层1 资源管理器 | 层2 后端存储管理器 | 层3 设备资源（统一 IDevice） |
|---|---|---|---|
| Texture | `TextureAsset`（无运行时管理器） | 无（层2 `Texture` 自持 RHI 句柄） | `IDevice` 三池 texture/view/sampler + `IRHITexture` |
| Mesh | `MeshResourceManager` | `MeshStorage` | `IDevice` buffer owner |
| Material | `MaterialResourceManager` | `MaterialStorage` | `IDevice` bindGroup / uniform owner |
| Shader | `ShaderResourceManager` | `ShaderStorage` | `IDevice` shaderModule / pipeline owner |

## 4. 跨层引用规则（单向向下）

```
层1 之间：`Ref<T>` 表达拥有关系，VirtualPath / AssetId 用于序列化与去重
层1 -> 层2：Storage 以资源对象的内部 resourceId + version 建立后端记录；调用者不持有该 RID
层2 -> 层3：Storage 持设备 RID，经 IDevice 创建/解析/销毁；rhi 类型不上浮到层1
```

硬约束：
- `src/render/<x>/`（层1）内的头文件不得 `#include "rhi/..."`。**例外**：`render/texture/Texture.h` 是层2 头（持 `rhi::RID`/`rhi::TextureBinding`、直连 `IDevice`），允许依赖 `rhi/api`，故 `RenderRhiBoundaryTest.cmake` 的层1 检查不含它。
- `src/rhi/`（层3）不得 `#include "render/..."` 或 `asset/...`。现有 `tests/RenderRhiBoundaryTest.cmake` 守护该边界，迁移时应扩展其覆盖到层1 头文件。

两套 RID 隔离：层2、层3 各自拥有独立的 RID_Owner/Registry 实例，句柄不可互换（层2 记录持有层3 RID）。RID 数值类型仍复用统一的 `engine::RID`（不重新引入按类型的强类型别名，避免推翻既有 RID 归一决策）；隔离靠“不同 owner 实例 + 命名约定”实现。

生命周期与级联销毁：层1对象（Scene / Node / Component / Material / Shader / Mesh / Texture）继承 `RefCounted`。Scene 以 `Ref<Node>` 持有节点，Node 以 `Ref<Component>` 持有组件，组件与资源之间以 `Ref<T>` 持有；为避免循环，Node→Scene 回指和父子拓扑 RID 是明确的非拥有关系。资源最后一个 Ref 归零后，析构函数向弱缓存反注册，ResourceManager 的 destroy observer 通知对应 Storage，Storage 再经 IDevice 释放设备对象。

## 5. 层2 标准骨架

复用现有 `src/render/gpu/common/` 的 `IGpuCache` / `IGpuResourceFactory` / `GpuResourceKey`。每个资源在层2 统一实现（Mesh/Material/Shader 已是此范式，作为模板）：

- `XxxStorageEntry`：设备层 RID + 该资源的渲染策略状态（视图 / proxy / atlas / 流送）。
- `XxxStorageFactory : IGpuResourceFactory`：allocate / initialize / update / release。
- `XxxStorageCache : IGpuCache`（键 = 层1 RID + version）：命中 / 失效 / 驱逐。
- `XxxStorage`（Singleton，持 `IDevice*`）：`resolve(RID)` 三级路径（命中 / 脏更新 / 重建），并提供两段式 allocate + initialize。

## 6. 四系统落地状态

### 6.1 Texture（三层：TextureAsset / Texture / rhi::Texture）
- 层1 `TextureAsset`：序列化源（desc + 扁平像素），由 `AssetManager` 强缓存常驻；`instantiate()` 出**唯一**运行时实例（重复调用复用，天然去重），`clone()` 出多个脱离实例。
- 层2 `Texture`：只保留必要属性 + 三个 RID（texture / 默认 view / 默认 sampler），不持 desc 与像素；构造经 `IDevice::active()` 分配 RID，`initialize`/`upload` 上传；无独立 Manager，也无 gpu 层 `TextureStorage`。
- 层2 `Sampler`：`RefCounted`，持一个设备去重的 sampler RID，非拥有。层2 无 `TextureView` 类，view 概念只在层3。
- 三个句柄池内嵌 `IDevice`（全局单例 `active()`）；`~Texture` 按设备 `uid` 判断是否仍需销毁 GPU 资源（防跨设备/地址复用误伤）。热重载经 `AssetManager::reloadInPlace` 就地重传 asset → `syncInstance` 推唯一实例。

### 6.2 Mesh
- `Mesh` 以 Ref 管理，Component 与 RenderScene 持 `Ref<Mesh>`。
- `MeshStorageEntry` 包含 DrawInfo，并预留 surface/LOD/skinned/blendshape/keepCpuCopy 策略字段。
- Manager 弱索引的 resourceId 只在 Storage 内作为版本缓存键；内容重建不触发生命周期销毁通知。

### 6.3 Material
- `Material` 持 `Ref<Shader>`、按属性名缓存的 `Ref<Texture>` 与可选 `Ref<Sampler>`（`setTexture(tex)` 用默认 sampler，`setTexture(tex, sampler)` 用指定 sampler），不持 Shader RID。
- `MaterialStorage` 集中完成 pass/variant 选择、GraphicsPipeline 查询、TextureBinding 解析、uniform 上传与跨帧常驻。
- Material 最后一个 Ref 归零时，MaterialStorage 从所有帧缓存移除对应记录并释放 bind group/uniform buffer。

### 6.4 Shader
- `ShaderAsset` 已拆到 `src/asset/types/ShaderAsset.h`；运行时 Shader 只保留用户/渲染语义。
- `ShaderStorage` 管编译产物、ShaderModule 缓存与销毁；`IShaderModule` 通过 `ShaderModuleInfo` 暴露 stage/bytecodeSize/debugName，不泄漏 Vulkan 句柄。
- `GraphicsPipelineStorage` 是 Program→Pipeline 的显式映射与缓存边界。

## 7. 已确认的架构约定
1. 层2 与层3 缓存边界：渲染策略与 GPU 资源缓存留在层2（`render/gpu`）；层3 `IDevice` 只做无状态的 GPU 对象 create/destroy/去重/resolve。此约定优先于旧的“所有 `render/gpu` 缓存下沉 IDevice”表述。
2. 层1 生命周期（一律 Ref）：Scene / Node / Component / Material / Shader / Mesh / Texture 等层1 对象一律继承 `RefCounted`、相互引用一律用 `Ref<T>`（不再用裸 RID/指针持有）。引用计数归零即析构，析构级联触发层2 `XxxStorage` 释放后端记录、层3 `IDevice` 释放 GPU 对象（层1 -> 层2 -> 层3 单向级联）。`XxxResourceManager` 从单一所有者 KeyedHandleRegistry 调整为路径/AssetId 去重缓存（保存弱引用，命中返回既有 `Ref`，析构时反注册），对应 Godot `ResourceCache` 模型。此约定取代早前‘以 RID+Manager 为主、Ref 仅用于共享所有权’的表述。去重缓存的弱引用需先补 `Ref` 的 `WeakRef`（上一轮列为未来扩展），或以裸指针 + 析构反注册过渡。
3. Texture view/sampler：句柄池与去重在层3 `IDevice`；层2 `Texture` 持默认 view/sampler RID、`Sampler` 为设备去重值的非拥有包装。
4. Mesh 的 LOD/blendshape/skeleton：只在层2 预留结构位，不纳入实现范围。
5. 目录更名：先只改类名（`XxxGpu*` -> `XxxStorage*`），`src/render/gpu/` -> `src/render/storage/` 的目录更名作为可选收尾，降低 diff 噪音。
6. 命名与 RID：采用层1 `XxxResourceManager` / 层2 `XxxStorage` 家族 / 层3 统一 `IDevice`；两套独立 RID_Owner、句柄不可互换、RID 数值类型仍复用 `engine::RID`。

## 8. 实施记录
- [x] 阶段 0：规范固化；`RenderRhiBoundaryTest` 已增加四个层1资源头的 RHI 直接依赖检查。
- [x] 阶段 1：层1统一为 `XxxResourceManager`；层2统一为 `XxxStorage` / `XxxStorageEntry` / `XxxStorageFactory` / `XxxStorageCache`。
- [x] 阶段 1b：Scene/Node/Component/Material/Shader/Mesh/Texture 继承 `RefCounted`；拥有关系使用 `Ref<T>`；四个 ResourceManager 使用裸指针弱索引与析构反注册。
- [x] 阶段 2：（已被纹理三层重构取代）删除 `TextureStorage` 四件套与 `TextureResourceManager`；层2 `Texture` 改为自持三个 RHI 句柄、经 `IDevice::active()` 创建，`TextureAsset` 提供 `instantiate`/`clone`。
- [x] 阶段 3：`MaterialAsset` / `ShaderAsset` 定义迁至 `src/asset/types/`，使用方显式包含资产类型头。
- [x] 阶段 4：pass/variant 选择、pipeline 查询、纹理 View/Sampler 解析和 GPU 常驻统一进入 `MaterialStorage`；Material 只持 `Ref<Shader>` / `Ref<Texture>` 和用户参数。
- [x] 阶段 5：`ShaderModuleInfo` 补全 RHI ShaderModule 描述契约；Program→Pipeline 映射由 `GraphicsPipelineStorage` 显式管理。
- [x] 阶段 6：`MeshStorageEntry` 增加 surface/LOD/skinned/blendshape/keepCpuCopy 扩展位；当前单 LOD 数据在上传时建立。
- [x] 阶段 7：命名与边界收口；Debug/Release 全量构建和 177 项 CTest 通过；两配置均完成 Vulkan demo 场景实跑。

`src/render/gpu/` 目录保留，不执行可选的 `src/render/storage/` 物理目录重命名，以避免纯路径变更制造无价值 diff。旧 `XxxManager` 类型名与宏仅作为迁移兼容别名保留，新代码必须使用 `XxxResourceManager`。

## 9. 验证要求
- 每阶段独立成型、独立 `ctest`，遵循 Debug + Release 双配置验证。构建成败看 cmake 退出码与 `error:`/`FAILED`，忽略 WinLibs `duplicate section` 链接警告。
- 高风险项（阶段 2 Texture、阶段 4 Material）需引擎实跑 + 跨配置逐像素对照：同配置两次运行应逐位相同，跨配置差异应为个位灰阶。
