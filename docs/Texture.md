# Texture 系统

Texture 系统覆盖源文件导入、CPU 资产与运行时实例、GPU 延迟上传、材质绑定和 Vulkan 采样。第一版只支持 `Texture2D`，所有模块通过 RHI 句柄交互，资产层和 Render 层不依赖 Vulkan 类型。

## 1. 支持范围

- 源文件：PNG、JPG/JPEG、KTX1、KTX2。
- PNG/JPG：使用跨平台的 `stb_image` 从内存解码为 RGBA8，并在导入时生成完整 mip 链。
- KTX1：支持未压缩的 RGBA8 UNORM / SRGB，保留文件中的 mip 链。
- KTX2：支持 `VK_FORMAT_R8G8B8A8_UNORM` 和 `VK_FORMAT_R8G8B8A8_SRGB`，要求无 supercompression，保留 level index 中的 mip 链。
- 运行时格式：`Rgba8Unorm` 和 `Rgba8Srgb`。

当前不支持 BC/ASTC/ETC 压缩、数组纹理、Cubemap、3D Texture、运行时 mip 生成和可配置 Sampler。这些能力可以在不改变材质引用方式的前提下扩展 `TextureDesc`、Importer 和 RHI。

## 2. 数据分层

```text
源图片 + .meta
  -> TextureAssetImporter
  -> AssetArtifact / AssetDatabase
  -> AssetManager::loadAsset<TextureAsset>()
  -> TextureAsset::instantiate()
  -> TextureManager -> TextureHandle
  -> TextureGpuManager -> TextureGpuCache / TextureGpuFactory
  -> RHI Texture / TextureView / Sampler
  -> MaterialBindingCache / MaterialGpuFactory -> set 1 descriptor
  -> Vulkan draw
```

`TextureAsset` 是可序列化的 CPU 资产，保存 `TextureDesc` 和每一级 `TextureMipData`。`Texture` 是运行时实例，额外保存资产路径、`version` 和 `dirty`。`TextureManager` 继承 `InstanceManager`，负责路径去重、Handle generation、替换和销毁；`TextureGpuCache` 只保存键到 RHI 资源的映射，不拥有资产，也不执行 GPU 上传。

## 3. 导入与序列化

图片旁的 `.meta` 必须声明 `"type": "Texture"`。`AssetImportPipeline` 根据扩展名和 meta 选择 `TextureAssetImporter`，Importer 完成以下步骤：

1. 通过 `FileSystem` 读取源文件字节，不直接访问平台图片 API。
2. PNG/JPG 交给 `stb_image`；KTX 根据文件标识进入对应解析器。
3. 将结果规范化为 `TextureDesc + vector<TextureMipData>` 并调用 `validateTexture()`。
4. 用 `BinaryWriter` 序列化 `TextureAsset`，再包装为 `AssetArtifact` 写入 Library。
5. 在 `AssetDatabase` 中登记源 hash、Importer 版本、Artifact 路径和导入状态。

Material Importer 会解析 Shader 的 `Texture2D` 属性，将非空的相对引用规范化为 `asset://` 路径，并把纹理加入统一资产依赖列表。纹理源文件变化后，现有 `FileDependencyGraph` 会使依赖材质进入重新导入流程；没有单独的纹理依赖图。

## 4. 运行时加载与默认纹理

`TextureManager::load(path)` 先复用已有路径 Handle；未命中时通过 `AssetManager` 读取 Artifact、反序列化 `TextureAsset`，再实例化为 `Texture`。此时仍然只有 CPU 像素，没有创建 GPU Image。

默认纹理由 `TextureManager` 延迟创建并长期持有，Renderer 不自行构造像素：

- `defaultWhite()`：1x1 SRGB 白色；空材质纹理引用使用它。
- `defaultBlack()`：1x1 SRGB 黑色。
- `defaultNormal()`：1x1 Linear `(128, 128, 255, 255)` 法线。
- `errorTexture()`：2x2 SRGB 黑色/品红棋盘；路径无效或加载失败时使用它。

这些资源使用 `builtin://textures/...` 虚拟路径，也通过正常的 `TextureHandle`、Cache 和 Uploader 流程上传。`TextureManager::clear()` 会同时清除内建 Handle 状态和所有运行时实例。

## 5. GPU 上传

Renderer 第一次解析到材质纹理时调用 `TextureGpuManager::resolve(handle)`：

1. 以 Handle 的 index 和 generation 组成缓存 key，并比较 `Texture::version()`。
2. 未命中时由 `TextureGpuFactory` 创建 `Sampled | TransferDestination` 的 RHI Texture。
3. Uploader 把全部 mip 作为 `TextureUploadRegion` 交给 `IDevice::uploadTexture()`。
4. Factory 创建覆盖完整 mip 链的 TextureView，TextureGpuManager 将它与共享 Sampler 组成 `TextureBinding`。
5. 缓存成功后调用 `Texture::markClean()`；版本命中时直接返回现有绑定。

Vulkan 实现创建 `VkImage` 和 device-local 内存，通过 staging buffer 执行 buffer-to-image copy，并为每一级 mip 做 layout transition，最终进入 shader-read-only layout。TextureView 映射为 `VkImageView`，Sampler 映射为 `VkSampler`。当前所有纹理共享一个 linear/repeat Sampler。

运行时替换纹理时，`TextureManager::replace()` 保持 Handle 不变并递增 version。下一次 `resolve()` 会等待设备空闲，按 TextureView 后于 descriptor、先于 Texture 的安全顺序重建缓存资源。显式销毁时先调用 `TextureGpuManager::invalidate()`，再销毁 TextureManager Handle。

## 6. Shader 与 Material 绑定

Shader JSON 中以 `Texture2D` 声明属性：

```json
{
  "name": "BaseMap",
  "type": "Texture2D",
  "default": ""
}
```

`ShaderGenerator` 为 Texture2D 生成 `layout(set = 1, binding = N) uniform sampler2D`，binding 从 1 开始；set 1 binding 0 固定为材质 uniform buffer。Renderer 的材质 BindGroupLayout 预留 16 个采样纹理槽，Shader 生成阶段也会拒绝超过 `kMaxMaterialTextures` 的声明。

`MaterialGpuManager` 按 Shader 属性顺序取得材质纹理字符串，并通过 `TextureGpuManager` 执行路径规范化、`TextureManager::load()`、错误纹理回退和 Texture Cache 查询；未命中时调用 `TextureGpuFactory`。最后由 `MaterialGpuFactory` 把 TextureView 与 Sampler 写入 `SampledTexture` descriptor。材质只保存稳定的虚拟路径，不保存 Texture 或 Vulkan 对象。

示例 `BlinnPhong` Shader 使用 `BaseMap` 采样并与 `BaseColor` 相乘；`blinn_gold.material.json` 引用了 `asset://textures/checker.png`，其他未指定贴图的材质会自动绑定 Manager 创建的白纹理。

## 7. 生命周期顺序

启动后资源按需创建；关闭时 Engine 先关闭 `MaterialGpuManager` 并销毁材质 bind group，再关闭 `TextureGpuManager`，从 TextureGpuCache 提取并释放 TextureView、Texture 和共享 Sampler，最后清理 `TextureManager`。这个顺序保证 descriptor 不会引用已销毁的图片资源。

测试覆盖 PNG/JPG 导入、Texture Artifact 往返、默认纹理内容、GPU cache 命中、version 驱动重建、显式失效和 RHI 资源销毁。运行：

```powershell
ctest --test-dir build/clang-debug -R "TextureTest|AssetImporterTest" --output-on-failure
```
