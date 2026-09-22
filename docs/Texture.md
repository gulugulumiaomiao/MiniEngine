# Texture 系统

Texture 系统把资产数据、运行时 GPU 资源、视图和采样状态拆成独立层级。当前 Vulkan 创建和上传路径只接受单层 `Texture2D`；`Texture2DArray`、`Texture3D`、`TextureCube`、`TextureCubeArray` 已进入 API 数据模型，但会在统一校验点明确拒绝。

## 1. 分层模型

| 层级 | 类型 | 职责 |
| --- | --- | --- |
| 资产层 | `TextureAsset` | 序列化 `TextureDesc`、完整 mip 数据和默认 sampler 建议 |
| Render 层 | `Texture` | GPU-backed 运行时资源；保存稳定资产身份、RHI handle、`IRHITexture*` 和默认 `TextureView` |
| Render 层 | `TextureView` | 可复制、非拥有包装；保存 texture/view handle 和 view desc |
| Render 层 | `Sampler` | 可复制、非拥有包装；保存 device-cached sampler handle 和 desc |
| RHI 层 | `IRHITexture` / `VulkanTexture` | 管理 VkImage、VMA allocation、默认 view 和唯一的 view desc 缓存 |
| RHI 层 | `IRHITextureView` / `VulkanTextureView` | 管理 VkImageView，引用所属 RHI Texture |
| RHI 层 | `IRHISampler` / `VulkanSampler` | 管理 VkSampler；由 Device 按 SamplerDesc 去重 |

核心约束：`Texture` 不能直接绑定 shader，规范绑定单位始终是 `TextureView + Sampler`。

## 2. 数据与加载流程

```text
源图片 + .meta
  -> TextureAssetImporter
  -> AssetArtifact / AssetDatabase
  -> AssetManager::loadAsset<TextureAsset>()
  -> TextureManager 创建 RHI Texture 并上传所有 mip
  -> RHI Texture 自动创建全范围默认 view
  -> engine::Texture { TextureHandle, IRHITexture*, default TextureView }
  -> Material/Global binding { TextureView, Sampler }
  -> BindGroupEntry { TextureViewHandle, SamplerHandle }
  -> Vulkan descriptor { VkImageView, VkSampler }
```

`TextureManager` 必须在 Device 创建后初始化。`load()` 按 AssetId 去重；未命中时反序列化 `TextureAsset`，立即创建并上传 GPU 资源。white/black/normal/error 内建纹理走同一条创建路径。

`Texture` 构造函数不接收 Device，也不保存 Device。它保存 `IRHITexture*`，因此 `Texture::getView(desc)` 直接委托 `IRHITexture::createView(desc)`。GPU 销毁由 `TextureManager` 统一通过 Device 执行；Engine 保证 TextureManager 早于 Device 关闭。

## 3. TextureView 与缓存

`TextureViewDesc` 的缓存键完整包含：

```text
(type, format, baseMip, mipCount, baseLayer, layerCount, swizzle)
```

`PixelFormat::Undefined` 在进入缓存前会规范化为源纹理格式。`VulkanTexture` 自己维护 `TextureViewDesc -> TextureViewHandle`，这是唯一的描述缓存。`VulkanDevice` 只用全局 `HandlePool<VulkanTextureView>` 分配和解析 handle，不维护第二份 desc 缓存。

流程：

```text
Texture::getView(desc)
  -> IRHITexture::createView(desc)
  -> VulkanTexture 请求 VulkanDevice 分配/解析 view
  -> 命中 VulkanTexture 缓存：返回现有 handle
  -> 未命中：vkCreateImageView，写入全局 handle pool 和纹理本地缓存
```

`IDevice::createTexture()` 会自动创建覆盖完整 mip/layer 范围的默认 view。RenderTarget、RenderGraph transient texture、ImGui 字体纹理和 shadow placeholder 都直接使用 `defaultTextureView()`；只有指定 mip、layer、format 或 swizzle 时才显式创建 view。

销毁 Texture 时，Device 先释放它缓存的全部 view，再销毁 VkImage/VMA allocation。`TextureView` 非拥有，生命周期不能超过父 Texture。

## 4. Sampler

Sampler 与 Texture/TextureView 完全独立。`Sampler::resolve(device, desc)` 调用 Device；`VulkanDevice` 使用 `KeyedHandleRegistry<VulkanSampler,...>` 按钳制后的 `SamplerDesc` 去重。

`TextureDesc.sampler` 仍作为资产默认建议存在，但 Texture 不拥有 sampler。不同材质可以让同一个 TextureView 配合不同 Sampler。Render 层 wrapper 析构不销毁共享 sampler；sampler cache 随 Device 统一释放。

## 5. Material 与 descriptor 绑定

Material 支持两类数据：

- 资产序列化仍保存纹理 VirtualPath，避免把运行时 GPU handle 写入资产；
- 运行时 override 保存 `TextureBinding { TextureView view; Sampler sampler; }`。

主接口接收 `TextureView + Sampler`；接受 `Texture + Sampler` 的便捷重载只转发到 `texture.defaultView()`。`MaterialGpuManager` 每次 resolve 都重新解析 binding 签名，因此纹理热重载替换 default view 后会重建 bind group。

`MaterialGpuFactory` 只把高层 binding 转换为：

```cpp
rhi::TextureBinding{view.rhiHandle(), sampler.rhiHandle()}
```

Vulkan descriptor 最终只接收 VkImageView 和 VkSampler；禁止直接把 TextureHandle 作为 sampled texture 绑定。

## 6. 热重载与 clone

`TextureManager::replace()` 先完整创建并上传新 RHI texture/default view，成功后等待设备空闲，在原 CPU TextureHandle 位置替换运行时对象，再销毁旧 RHI texture。Material binding 签名检测到 view handle 变化后重建 descriptor。

`clone()` 使用保存的 CPU mip 快照创建独立 RHI texture；clone 不继承 AssetId，不进入资产主索引。

## 7. 类型范围与后续扩展

CPU/RHI 描述已经包含 `depth`、`arrayLayers` 和五种 TextureType。当前实现只允许：

```text
Texture2D, depth=1, arrayLayers=1
```

后续实现数组、3D 和 Cube 时，需要同步扩展 importer、上传 region、barrier layer range、VkImage type/flags 和 VkImageView type；上层 Material 的 `TextureView + Sampler` 绑定模型无需改变。

## 8. 验证

Google Test 覆盖 GPU-backed builtin texture、默认 view、view 转发与缓存、独立 sampler、Material TextureBinding、clone、GUID 身份和未实现维度拒绝。完整验证必须同时执行 Debug/Release 构建与 CTest，并分别实跑引擎检查资源缺失、shader、材质和 Vulkan validation 日志。
