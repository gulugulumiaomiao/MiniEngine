# Texture 系统

## 常用渲染纹理工厂

```cpp
auto color = Texture::createColor(1280, 720); // RGBA8 Unorm，颜色附件 + 采样
auto srgb = Texture::createColor(1280, 720, TextureFormat::Rgba8Srgb);
auto depth = Texture::createDepth(1280, 720); // Depth32Float，深度附件
auto shadow = Texture::createShadowMap(1024, 1024); // Depth32Float，深度附件 + 采样
```

这些静态函数返回拥有纹理及默认视图的 `Ref<Texture>`，默认采样器由设备管理。
它们创建单 mip 的二维纹理，采用边缘钳制采样；创建后内容尚未初始化，需先清除或渲染再读取。
宽高为零、颜色格式不合法、没有活动设备或创建失败时返回空引用。
渲染纹理不支持 `upload()` 上传像素，也不关联资产；已有资产纹理的上传和克隆流程保持原有用法。
`usage()` 可以查询创建时的能力声明。RenderGraph 的 transient 纹理仍通过图和资源池创建。

`Texture::create(const engine::TextureDesc&)` 提供通用创建入口，
支持当前 RHI 的二维纹理及全部非压缩格式、自定义 mip 和采样数。
`pixelFormat()`、`samples()`、`tiling()` 查询实际创建属性；多采样纹理只支持单 mip。
颜色、深度和 shadow map 工厂共用这条创建路径，资产像素支持当前所有非压缩格式的单采样纹理。
Texture 记录创建时的设备 UID；设备不匹配时不会向当前设备上传或销毁旧句柄。

RenderGraph::createTexture 和 RgTexturePool::acquire 同样使用 engine::TextureDesc。
图的动态尺寸 resolver 作为 createTexture 的独立参数，不放入通用描述：

```cpp
TextureDesc desc{TextureType::Texture2D, TextureFormat::Rgba8Unorm, 0, 0};
auto color = graph.createTexture(desc, [](const RenderContext& context) {
    return rhi::Rect{0, 0, context.sceneWidth(), context.sceneHeight()};
});
```

engine::TextureDesc 包含 `sampler`、`samples` 和 `debugName`，不暴露 usage、tiling。
`samples` 为 `std::uint8_t`，默认 1；`toRhiSampleCount` 仅接受 1、2、4、8，
显式映射到 RHI 枚举，其他值返回空 optional。Texture 创建及图编译均在转换时检查。
引擎纹理固定使用 Optimal tiling；RHI 描述仍保留底层 usage、tiling 和 SampleCount。
`Texture::create(desc)` 使用 Sampled | TransferDestination；`createColor(desc)` 使用
ColorAttachment | Sampled；`createDepth(desc)` 使用 DepthStencilAttachment；
`createShadowMap(desc)` 使用 DepthStencilAttachment | Sampled。
RenderGraph 汇总所有 pass 的资源用途及 finalState，生成实际 usage；资源池按描述与实际 usage 匹配。
资产文件格式不变，不保存运行时采样数，资产校验要求 samples 为 1。

Texture 系统把资产数据、运行时 GPU 资源与采样状态拆成三层：`TextureAsset`（层1 序列化源）、`Texture` / `Sampler`（层2 运行时）、`rhi::RID`（层3；实例为 `VulkanDevice` 分类型池持有的 `VulkanTexture`/`VulkanTextureView`/`VulkanSampler`，`IRHITexture` 等抽象接口已删除）。当前 Vulkan 创建与上传路径只接受单层 `Texture2D`；`Texture2DArray`、`Texture3D`、`TextureCube`、`TextureCubeArray` 已进入数据模型，但在 `computeTextureLayout`/`validateTextureAsset` 统一校验点被明确拒绝。

## 1. 分层模型

| 层级 | 类型 | 职责 |
| --- | --- | --- |
| 层1 资产 | `TextureAsset` | 序列化 `TextureDesc` + 完整扁平 mip 像素；`instantiate()` 唯一实例、`clone()` 脱离实例 |
| 层2 运行时 | `Texture` | 必要属性 + 三个 RHI RID（texture / 默认 view / 默认 sampler）；不持 desc 与像素 |
| 层2 运行时 | `Sampler` | `RefCounted`，持一个设备去重的 sampler RID + 必要属性访问器；非拥有 |
| 层3 RHI | `IRHITexture`（仅虚析构）/ `VulkanTexture` | 管理 VkImage + VMA allocation，只留 VkImageCreateInfo 类原生 info（VkFormat/PixelFormat/extent/mipLevels/arrayLayers/usage），**不持 desc、不持 view** |
| 层3 RHI | `IRHITextureView` / `VulkanTextureView` | 管理 VkImageView，回指所属 RHI Texture；**不持 TextureViewDesc** |
| 层3 RHI | `IRHISampler`（仅虚析构）/ `VulkanSampler` | 管理 VkSampler，只留原生 create-info（不持 SamplerDesc）；由 `IDevice` 按 `SamplerDesc` 去重 |

句柄池按类型分开、直接存 `VulkanTexture`/`VulkanTextureView`/`VulkanSampler` 实例，全部内嵌具体 `VulkanDevice`；`rhi::IDevice`（全局单例，`IDevice::active()`）只声明 RID 生命周期虚函数，`IRHITexture`/`IRHITextureView`/`IRHISampler` 抽象接口已删除。**不存在** `TextureManager` / `TextureResourceManager` / gpu 层 `TextureStorage`，层2 也没有 `TextureView` 类（view 概念只在层3）。核心约束不变：`Texture` 不直接绑定 shader，规范绑定单位是 `rhi::TextureBinding { view, sampler }`。

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
  -> UniformSet::setResource(Ref<Texture>) -> upload 组装 DescriptorWrite -> Vulkan descriptor { VkImageView, VkSampler }
```

`Texture` 构造经 `IDevice::active()` 取设备（没有管理器可注入）：`createTexture` 建纹理、`createTextureView`（翻译后的层2 `TextureViewDesc`）建**层2 拥有**的默认视图、`createSampler` 取去重采样器。white/black/normal/error 内建纹理由 `Texture::defaultWhite()` 等静态方法经同一构造路径自建（非 asset-backed）。纹理引用字符串（路径/内建名）由自由函数 `resolveTextureReference` 统一解析。

## 3. 去重与生命周期

- **去重**：无管理器时，"每个 `TextureAsset` 只 `instantiate()` 出一个运行时实例"天然承担去重——`resolveTextureReference` → 缓存 asset → `instantiate()` 命中同一实例。`clone()` 产出互不影响、不随 asset 变化的脱离实例（不继承 asset 链接）。
- **两步初始化**：构造只分配 RID（不上传）；`initialize`/`upload(pixels)` 完成首次上传。已有图像的更新需显式同步；当前热重载的重复上传路径尚需单独调整。
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

`Material::uniformSet` 按 Program 反射纹理名称重新解析签名：`resolveTexture(name)`（通过资产层注入的延迟回调缓存 Ref<Texture>）+ `resolveSampler(name)`，组装 `sampler ? texture->binding(sampler) : texture->binding()`。缓存按帧及 ProgramId 分开；仍被帧引用的旧 UniformSet 通过创建新集避免原地改写。只向 RHI 提交 view/sampler handle，禁止把 texture handle 直接作为 sampled texture 绑定。

## 6. 热重载

`AssetManager` 导入监听器对纹理走 `reloadInPlace(path)`：就地重传缓存中的**同一** `TextureAsset`（不 invalidate、不新建对象），`TextureAsset::transfer` 读取分支随即 `syncInstance()` 把新像素重上传给唯一实例，实例身份（及其 view/sampler handle）保持稳定。这取代了旧的 `TextureManager::replace()` "先建后换" 流程。

## 7. 类型范围与后续扩展

CPU/RHI 描述含 `depth`、`arrayLayers` 与五种 `TextureType`，但当前实现只允许 `Texture2D, depth=1, arrayLayers=1`。后续实现数组/3D/Cube 时需同步扩展 importer、上传 region、barrier layer range、VkImage type/flags 与 VkImageView type；上层 `Texture + Sampler → rhi::TextureBinding` 绑定模型无需改变。

## 8. 验证

运行时创建已增加以下非压缩格式：`R8Unorm`、`Rg8Unorm`、`R16Float`、`Rg16Float`、
`Rgba16Float`、`R32Float`、`Rg32Float`、`Rgba32Float`、`Depth16Unorm`。
格式属性统一查询 `rhi::formatInfo()`。`TextureDesc` 不存储 colorSpace，
`Texture::colorSpace()` 从格式推导；深度工厂也可选择格式：

```cpp
auto hdr = Texture::createColor(1920, 1080, TextureFormat::Rgba16Float);
auto depth = Texture::createDepth(1920, 1080, TextureFormat::Depth16Unorm);
auto shadow = Texture::createShadowMap(2048, 2048, TextureFormat::Depth16Unorm);
```

上述接口只分配纹理，不初始化像素。默认 view 使用纹理自身格式，深度格式使用 depth aspect。
纹理资产使用版本 5，不再序列化 `color_space`，直接拒绝旧版本，无兼容读取或迁移代码。
纹理导入器版本升为 3，旧缓存从源图片/KTX 重新导入后生成新版本资产。
`computeTextureLayout` 和运行时 `Texture::upload` 支持当前全部 15 种非压缩格式；
资产的 `validateTextureAsset` 接受当前全部 15 种非压缩格式，包括单/双通道、BGRA、HDR、深度及 R32Uint 格式。
原始像素必须符合目标格式，接口不会转换像素编码。现有图片/KTX 导入器仍只解码其原有 RGBA8 格式。

CPU 像素按 mip 从大到小紧密排列，每级提供 `offsets`、`sizes`，`totalSize` 为总字节数。
尺寸逐级除以 2 并至少为 1；按格式块字节数计算，拒绝非法 mip 数和字节数溢出。
Vulkan staging 单独将每级偏移对齐到 `lcm(4, bytesPerBlock)`，填充不属于调用方像素数据。
RHI 上传 region 可以乱序，但必须恰好覆盖所有 mip，尺寸和字节数必须一致，不能重复。
上传仅用于尚未初始化的单层二维、单采样纹理，要求 `Sampled | TransferDestination`，
同步完成后处于 `ShaderRead`。需要更新已有内容时，调用方负责转换状态并使用 `updateImage`；
该接口验证 mip、layer、区域边界和精确数据长度，copy 接口从格式推导 color/depth aspect。
设备能力与静态格式属性分开查询：

```cpp
auto* device = rhi::IDevice::active();
auto caps = device->texture_format_capabilities(rhi::PixelFormat::Rgba16Float,
                                                rhi::TextureTiling::Optimal);
// caps.sampled / linearFilter / colorAttachment / depthStencilAttachment
// caps.colorAttachmentBlend / transferSource / transferDestination
rhi::TextureDesc desc{.format = rhi::PixelFormat::Rgba16Float,
    .width = 1280, .height = 720,
    .usage = rhi::TextureUsage::ColorAttachment | rhi::TextureUsage::Sampled};
if (device->texture_is_supported(desc)) {
    auto image = device->texture_create(desc); // 仍需检查 RID；查询不预留显存。
}
```

`texture_format_capabilities` 对应指定 tiling 的格式能力；单项支持不代表任意 usage 组合都支持。
`texture_is_supported` 校验当前实现的二维单层范围、usage 组合、尺寸、mip 和 sample 数，
再对照物理设备的 image format properties；未知枚举或不支持的描述返回 false。
`texture_create` 在分配前执行该检查，描述不支持或原生分配失败时返回无效 RID。
只有 transfer usage 的底层图像可以创建，但不能创建 view。引擎通用创建入口始终包含 Sampled 用途。
默认 sampler 请求线性过滤或各向异性而格式不支持时，`Texture::create` 返回空，调用方可显式改为 Point。
附件和 input attachment 不强制线性过滤；多采样图像也不以普通 sampler 的过滤能力判断。
覆盖 sampler、直接 RHI 绑定以及动态混合模式仍由调用方依据能力选择，接口不会自动降低过滤或混合模式。
图形管线与传统 renderpass 创建也检查附件格式和 sample 数；HDR 测试按 blend 能力选择测试范围。

## 整数 ID 附件

`TextureFormat::R32Uint` / `rhi::PixelFormat::R32Uint` 是单通道 32 位无符号整数，
每像素 4 字节，色彩空间推导为 Linear。CPU mip 布局、上传和资产原始像素存储均支持该格式。
源图片导入器仍保持原有解码范围，不会自动将普通图片转成整数 ID。

```cpp
auto ids = Texture::createColor(1280, 720, TextureFormat::R32Uint); // 工厂选择 Point。
rhi::ColorAttachment attachment{ids->defaultView(), rhi::LoadOp::Clear,
    rhi::StoreOp::Store, rhi::ColorClearValue::uint32(0xffffffffU)};
// RenderGraph 的 RgColorAttachment.clearColor 和 clearColorResolver 使用同一个类型。
```

`ColorClearValue` 保存浮点颜色或四个 uint32 分量，不把整数转成 float。
浮点颜色可继续使用 `math::Vec4` 或四个浮点分量；无符号整数显式使用 `uint32(...)`。
实际执行 Clear 时检查类型：整数附件要求整数清屏，浮点/normalized 附件要求浮点清屏。
Load/DontCare 不读取清屏值。动态渲染和传统 renderpass 均走同样的转换。

Shader 源资产的 `fragmentOutputs` 支持 `"type": "Uint"`，生成 GLSL `uint` 输出，
`FragmentMain` 中可写入 `outputValue.objectId = 0xf1234567u;`。
本次只开放无符号标量片元输出，不开放整数 vertexInput/varyings。
后端按选中的 fragment entry point 反射实际 SPIR-V，创建管线时检查输出的数值类型，
拒绝 float 输出绑定 UINT 附件以及 uint 输出绑定浮点/normalized 附件。
当前后端输出反射支持直接 location 的 32 位标量/向量；数组、矩阵、接口块、component/index 特殊布局明确拒绝。

整数附件不能启用混合。当前 `setBlendState` 对全部颜色附件设置同一种模式，
因此含整数附件的 MRT 也必须使用 Off。draw/drawIndexed 在发出 Vulkan draw 前，
依据实际 view 的格式及 tiling 检查混合能力，并在换 pass/subpass 后重新判断。
整数纹理的显式 TextureDesc 应选择 Point；请求线性过滤会失败。
需要在着色器中采样时使用与 UINT 匹配的 `usampler2D`；现有材质的普通 `Texture2D` 声明仍生成浮点 sampler。

Google Test 覆盖：内建默认纹理（互异、上传、可采样）、`instantiate` 单实例与 `clone` 脱离、`Sampler` 覆盖绑定、Material 按 Ref 持有、GUID 身份（同路径同实例）、最后 Ref 归零释放 GPU、`reloadInPlace` 活链接与未实现维度拒绝。完整验证必须同时执行 Debug/Release 构建与 CTest，并实跑引擎（含工程切换/设备重建的 `SceneViewIntegrationTest`、`ProjectConfigEditorTest`）检查资源缺失、shader、材质与 Vulkan validation 日志。
