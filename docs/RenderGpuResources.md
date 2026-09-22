# Render GPU 资源分层

Render 层不要求所有资源都经过同一种 Manager/Cache/Factory 模板。资源根据身份和生命周期选择最小抽象：Mesh/Shader/Pipeline 继续使用 GPU Manager；Texture 已是 GPU-backed 运行时对象，由 TextureManager 直接管理。

## 1. 当前资源路径

```text
Mesh/Shader/Pipeline
  -> *GpuManager -> *GpuCache -> *GpuFactory -> IDevice

TextureAsset
  -> TextureManager -> IDevice create/upload
  -> GPU-backed Texture + default TextureView
  -> per-RHI-Texture view cache

Material
  -> MaterialGpuManager -> MaterialBindingCache
  -> TextureBinding { TextureView, Sampler }
  -> BindGroup
```

`IGpuResourceFactory<CreateInfo, Resource>` 与 `IGpuCache<Key, Resource>` 仍服务于需要 CPU/GPU 双份实例和显式退役的资源。Texture 不再使用 `TextureGpuManager/TextureGpuFactory/TextureGpuCache/TextureGpuResource`，避免在 GPU-backed Texture 之外维护第二份 GPU 身份和 view cache。

## 2. GPU Manager

- `MeshGpuManager`：版本化上传顶点/索引 Buffer，持有 `MeshGpuCache`。
- `MaterialGpuManager`：材质 BindGroup 与 uniform buffer 跨帧常驻；每次 resolve 重新解析 `TextureView + Sampler` 签名，纹理热重载会触发 BindGroup 重建。
- `ShaderGpuManager`：管理编译结果、ShaderModule 缓存和延迟退役。
- `GraphicsPipelineManager`：管理 Pipeline 描述、缓存和 shader 变化失效。
- `FrameGpuManager`：管理场景/对象/实例表 Buffer、固定 BindGroupLayout 和每帧 BindGroup。
- `GlobalUniformGpuManager`：按帧保存 global BindGroup，并比较高层 TextureBinding 签名以处理 texture view 热替换。
- `TextureManager`：虽然位于 render/texture 而非 render/gpu，但它直接创建、上传、替换和销毁 RHI Texture，是 Texture GPU 生命周期的唯一上层所有者。

## 3. Texture 与 view cache

`TextureManager::load()` 读取 TextureAsset 后立即调用 IDevice 创建和上传 texture。CPU Texture 保存 RHI handle、`IRHITexture*` 和默认 TextureView，但不保存 Device。

每个 `VulkanTexture` 自己维护：

```text
TextureViewDesc -> TextureViewHandle
```

VulkanDevice 的全局 `HandlePool<VulkanTextureView>` 只负责 handle 分配和解析，不按 desc 缓存。Texture 销毁时级联释放全部 view。默认 view 由 `createTexture()` 自动创建，因此 RenderTarget、RenderGraph transient texture、FrameGpuManager 和 ImGui 不单独销毁它。

Sampler 仍是 Device 级纯值缓存：`KeyedHandleRegistry<VulkanSampler,...>` 按 SamplerDesc 去重。高层 Sampler wrapper 非拥有，不在析构时调用 destroySampler。

## 4. Material binding

材质 shader 纹理的规范输入是：

```cpp
TextureBinding { TextureView view; Sampler sampler; }
```

资产材质继续序列化纹理 VirtualPath；运行时 override 可直接保存高层 TextureBinding。MaterialGpuManager 负责将路径解析为 GPU-backed Texture 的默认 view，并按 TextureAsset 中的默认 sampler 建议解析 Sampler。MaterialGpuFactory 只将高层 binding 转换成 RHI view/sampler handle，TextureHandle 不直接进入 sampled descriptor。

## 5. 常驻与退役

`MaterialBindingCache` 仍按 in-flight frame 分区，避免改写 GPU 可能仍在读取的 uniform buffer。Mesh 的旧上传采用 waitIdle 后释放；ShaderModule/Pipeline 使用 frame serial 延迟退役。

Texture 热重载采用“先完整创建，后原位替换”：新 texture/default view 上传成功后等待 Device 空闲，保持 CPU TextureHandle 稳定，替换 RHI handle，再销毁旧 texture 和其全部 view。Material/Global manager 比较 binding 签名，发现 view handle 变化后重建 descriptor。

## 6. 初始化与关闭

Engine 创建 Renderer/Device 后先初始化 TextureManager，再初始化依赖 texture 的 GPU managers。关闭顺序要求：

1. MaterialGpuManager / GlobalUniformGpuManager 等 descriptor 所有者；
2. Shader/Pipeline 等其他 GPU managers；
3. TextureManager（等待空闲并销毁 GPU-backed Texture）；
4. Frame/Mesh managers；
5. Renderer、Swapchain 和 Device。

该顺序保证 descriptor 不引用已销毁的 TextureView，同时所有 Texture 都早于 Device 释放。
