# Texture 系统

Texture 系统把资产数据、运行时 GPU 资源与采样状态拆成三层：`TextureAsset`（层1 序列化源）、`Texture` / `Sampler`（层2 运行时）、`rhi::IRHITexture` / `IRHITextureView` / `IRHISampler`（层3）。当前 Vulkan 创建与上传路径只接受单层 `Texture2D`；`Texture2DArray`、`Texture3D`、`TextureCube`、`TextureCubeArray` 已进入数据模型，但在 `computeTextureLayout`/`validateTexture` 统一校验点被明确拒绝。

## 1. 分层模型

| 层级 | 类型 | 职责 |
| --- | --- | --- |
| 层1 资产 | `TextureAsset` | 序列化 `TextureDesc` + 完整扁平 mip 像素；`instantiate()` 唯一实例、`clone()` 脱离实例 |
| 层2 运行时 | `Texture` | 必要属性 + 三个 RHI RID（texture / 默认 view / 默认 sampler）；不持 desc 与像素 |
| 层2 运行时 | `Sampler` | `RefCounted`，持一个设备去重的 sampler RID + 必要属性访问器；非拥有 |
| 层3 RHI | `IRHITexture`（仅虚析构）/ `VulkanTexture` | 管理 VkImage + VMA allocation，只留 VkImageCreateInfo 类原生 info（VkFormat/PixelFormat/extent/mipLevels/arrayLayers/usage），**不持 desc、不持 view** |
| 层3 RHI | `IRHITextureView` / `VulkanTextureView` | 管理 VkImageView，回指所属 RHI Texture；**不持 TextureViewDesc** |
| 层3 RHI | `IRHISampler`（仅虚析构）/ `VulkanSampler` | 管理 VkSampler，只留原生 create-info（不持 SamplerDesc）；由 `IDevice` 按 `SamplerDesc` 去重 |

三个句柄池 `HandlePool<IRHITexture|IRHITextureView|IRHISampler, RID>` 全部内嵌 `rhi::IDevice`（全局单例，`IDevice::active()`）。**不存在** `TextureManager` / `TextureResourceManager` / gpu 层 `TextureStorage`，层2 也没有 `TextureView` 类（view 概念只在层3）。核心约束不变：`Texture` 不直接绑定 shader，规范绑定单位是 `rhi::TextureBinding { view, sampler }`。

## 2. 数据与加载流程

```text
源图片 + .meta
  -> TextureAssetImporter
  -> AssetArtifact / AssetDatabase
  -> AssetManager::loadAsset<TextureAsset>()（强缓存常驻）
  -> TextureAsset::instantiate()  唯一实例 / clone() 脱离实例
  -> 层2 Texture 构造：IDevice::active() 分配 texture/view/sampler 三个 RID
  -> initialize/upload(pixels)：按 computeTextureLayout 逐 mip 切片上传
  -> Material/Global binding { view, sampler }
  -> BindGroupEntry -> Vulkan descriptor { VkImageView, VkSampler }
```

`Texture` 构造经 `IDevice::active()` 取设备（没有管理器可注入）：`createTexture` 建纹理、`createTextureView`（翻译后的层2 `TextureViewDesc`）建**层2 拥有**的默认视图、`createSampler` 取去重采样器。white/black/normal/error 内建纹理由 `Texture::defaultWhite()` 等静态方法经同一构造路径自建（非 asset-backed）。纹理引用字符串（路径/内建名）由自由函数 `resolveTextureReference` 统一解析。

## 3. 去重与生命周期

- **去重**：无管理器时，"每个 `TextureAsset` 只 `instantiate()` 出一个运行时实例"天然承担去重——`resolveTextureReference` → 缓存 asset → `instantiate()` 命中同一实例。`clone()` 产出互不影响、不随 asset 变化的脱离实例（不继承 asset 链接）。
- **两步初始化**：构造只分配 RID（不上传）；`initialize`/`upload(pixels)` 上传，可在热重载时重复调用。
- **所有权**：层2 `Texture` 经 `createTextureView` 创建并**拥有**默认 view（`~Texture` 先 `destroyTextureView` 再 `destroyTexture`）；view 去重与级联释放在 `VulkanDevice`（设备级 `(textureRID, rhi::TextureViewDesc)→RID`，`destroyTexture` 释放该纹理全部 view）；默认 sampler 由设备按 desc 去重持有，`~Texture` 不销毁它。
- **跨设备**：`IDevice` 有进程内单调、永不复用的 `uid()`。`~Texture` 用 `IDevice::active()` + 空检查销毁 GPU 资源（asset-backed 纹理在其设备仍 active 时析构，由工程 teardown 顺序保证）；静态内建纹理按 `uid` 检测设备切换，重建前对旧实例调 `detachFromDeadDevice()` 置空三 RID，使旧实例析构跳过销毁（旧设备已亡、资源随之消失）。这取代了裸指针比较（地址复用会误判）与旧的 `retainPixels` 补丁。
- **双向观察者**：`TextureAsset` 以裸指针 `instance_` 观察唯一实例，实例以裸指针 `asset_` 回指 asset（asset 由 `AssetManager` 强缓存常驻）；`~Texture` 与 `~TextureAsset` 互相清空对方指针，任一方先销毁都安全（无 UAF）。

## 4. Sampler

层2 `Sampler::resolve(const rhi::SamplerDesc&)` 经 `IDevice::active()->createSampler` 创建设备去重的 sampler 并包装；它**非拥有**，析构不 `destroySampler`，sampler 随设备释放。`TextureDesc.sampler`（`filterMode`/`addressModeU`/`addressModeV`/`maxAnisotropy`）作为资产默认建议，在 `Texture` 构造时映射为 `rhi::SamplerDesc` 生成默认 sampler。不同材质可让同一纹理默认 view 配合不同 `Sampler`。

## 5. Material 与 descriptor 绑定

Material 支持两类数据：资产序列化仍保存纹理 VirtualPath（不把运行时 handle 写入资产）；运行时以 `Ref<Texture>` + 可选 `Ref<Sampler>` 覆盖：

```cpp
setTexture(name, Ref<Texture> texture);                      // 默认 view + 默认 sampler
setTexture(name, Ref<Texture> texture, Ref<Sampler> sampler); // 默认 view + 指定 sampler
```

`MaterialStorage::collectTextureBindings` 每次 resolve 重新解析签名：`resolveTexture(name)`（缺省经 `resolveTextureReference` 由 VirtualPath 解析并缓存 `Ref<Texture>`）+ `resolveSampler(name)`，组装 `sampler ? texture->binding(sampler) : texture->binding()`。签名变化触发 BindGroup 重建。只向 RHI 提交 view/sampler handle，禁止把 texture handle 直接作为 sampled texture 绑定。

## 6. 热重载

`AssetManager` 导入监听器对纹理走 `reloadInPlace(path)`：就地重传缓存中的**同一** `TextureAsset`（不 invalidate、不新建对象），`TextureAsset::transfer` 读取分支随即 `syncInstance()` 把新像素重上传给唯一实例，实例身份（及其 view/sampler handle）保持稳定。这取代了旧的 `TextureManager::replace()` "先建后换" 流程。

## 7. 类型范围与后续扩展

CPU/RHI 描述含 `depth`、`arrayLayers` 与五种 `TextureType`，但当前实现只允许 `Texture2D, depth=1, arrayLayers=1`。后续实现数组/3D/Cube 时需同步扩展 importer、上传 region、barrier layer range、VkImage type/flags 与 VkImageView type；上层 `Texture + Sampler → rhi::TextureBinding` 绑定模型无需改变。

## 8. 验证

Google Test 覆盖：内建默认纹理（互异、上传、可采样）、`instantiate` 单实例与 `clone` 脱离、`Sampler` 覆盖绑定、Material 按 Ref 持有、GUID 身份（同路径同实例）、最后 Ref 归零释放 GPU、`reloadInPlace` 活链接与未实现维度拒绝。完整验证必须同时执行 Debug/Release 构建与 CTest，并实跑引擎（含工程切换/设备重建的 `SceneViewIntegrationTest`、`ProjectConfigEditorTest`）检查资源缺失、shader、材质与 Vulkan validation 日志。
