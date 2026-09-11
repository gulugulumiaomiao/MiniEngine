# Render GPU 资源分层

Render 层将 GPU 资源流程分为 GPU Resource Manager、Cache 和 Factory 三部分：

```text
Renderer
  -> *GpuManager                 资源生命周期、创建、替换和退役流程
      -> *GpuCache               Manager 私有持有的纯 CPU 缓存
      -> IGpuResourceFactory     GPU 资源创建、数据上传与释放
          -> RHI IDevice
```

## 抽象接口

`IGpuResourceFactory<CreateInfo, Resource>` 统一提供 `create()` 和 `release()`，并由基类持有 `IDevice&`。Mesh、Texture 和 Material 的 `create()` 包含必要的数据上传；ShaderModule、GraphicsPipeline 和 Sampler 的 `create()` 只创建对应 RHI 对象。

`IGpuCache<Key, Resource>` 提供 `find()`、`put()`、`remove()`、`extractIf()` 和 `extractAll()`。Cache 不保存 `IDevice`，不调用 RHI，也不负责同步或销毁。替换和失效操作会将旧资源交还给持有它的 GPU Resource Manager，由 Manager 决定立即释放还是按 frame serial 延迟退役。

## GPU Manager

- `MeshGpuManager`：查询 CPU Mesh、处理版本缓存、创建和释放顶点/索引 Buffer，并私有持有 `MeshGpuCache`。
- `TextureGpuManager`：查询或加载 CPU Texture、管理纹理缓存和共享默认 Sampler，并私有持有 `TextureGpuCache`。
- `MaterialGpuManager`：材质 GPU 资源跨帧常驻，按版本号三级路径（命中/脏更新/全量重建）刷新，依赖 TextureGpuManager 解析纹理，并私有持有 `MaterialBindingCache`。
- `ShaderGpuManager`：持有 ShaderCompilePipeline，管理 SPIR-V 编译、ShaderModule 缓存和延迟退役，并私有持有 `ShaderModuleCache`。
- `GraphicsPipelineManager`：管理 Pipeline 描述生成、缓存、编译失败回退和延迟退役，并私有持有 `GraphicsPipelineCache`。
- `FrameGpuManager`：管理场景/对象 Buffer、每帧实例表、BindGroupLayout 和每帧 BindGroup，并负责帧数据上传。

这些 Manager 都是单例。Renderer 不拥有它们，也不提供 CPU/GPU 资产的创建、加载、修改或销毁 API。Cache 仅属于对应 Manager，不再通过全局 Registry 暴露。

## Cache 与 Factory

- `MeshGpuFactory` 创建并上传顶点、索引 Buffer；`MeshGpuCache` 使用 MeshHandle generation 和 Mesh version 组成缓存键。
- `TextureGpuFactory` 创建 Texture、上传 mip 并创建 TextureView；`TextureGpuCache` 使用 TextureHandle generation 和 Texture version 组成缓存键。
- `MaterialGpuFactory` 更新 uniform buffer 并创建 BindGroup，另提供 uniform-only 脏更新；`MaterialBindingCache` 跨帧常驻槽位并在容量耗尽时 LRU 驱逐。
- `ShaderModuleGpuFactory` 根据 `CompiledShader` 创建 RHI ShaderModule；`ShaderModuleCache` 按 `CompiledShaderId` 查询。
- `GraphicsPipelineGpuFactory` 根据完整 RHI 描述创建 Pipeline；`GraphicsPipelineCache` 按 Program、布局、RenderState、VertexLayout 和目标格式生成的键查询。
- `SamplerGpuFactory` 创建 TextureGpuManager 持有的共享默认 Sampler。

## 初始化和关闭

Engine 在 Renderer 建立 RHI Device 与 BindGroupLayout 后依次初始化各 GPU Manager。每个 Manager 自行初始化其 Cache；其中 MaterialGpuManager 接收 in-flight frame 数量和常驻容量上限，用它初始化按帧的常驻 binding cache。

关闭时 Engine 先等待 Device 空闲，然后按以下顺序关闭：

1. MaterialGpuManager
2. GraphicsPipelineManager
3. ShaderGpuManager
4. TextureGpuManager
5. MeshGpuManager
6. FrameGpuManager
7. Renderer 持有的 Swapchain 和 Device

每个 Manager 在 `shutdown()` 内从自己的 Cache 提取并释放 GPU 资源，再销毁 Factory。这保证 Material descriptor 先于 Texture/Sampler 释放，GraphicsPipeline 先于 ShaderModule 释放，并保证 Cache 不会晚于 Device 清理资源。

## 更新与退役

Mesh/Texture 版本不匹配时，对应 GPU Manager 先通过 Factory 创建新资源，再从自己的 Cache 提取旧版本；当前采用 `waitIdle()` 后释放。ShaderModule 和 GraphicsPipeline 使用 frame serial 延迟退役。

新增资源类型时，应增加对应 GPU Resource Manager，并让 Manager 私有持有自己的 Cache，通过 `IGpuResourceFactory` 的具体实现完成 RHI 创建、必要的数据上传和释放。
