# Render GPU 资源分层

Render 层不要求所有资源都经过同一种 Manager/Cache/Factory 模板。资源根据身份和生命周期选择最小抽象：Mesh/Shader/Pipeline/Material 继续使用层2 Storage 家族；**Texture 不再有独立 Manager 或 gpu 层 Storage**——层2 `Texture` 自身持有 RHI 句柄，经全局活动设备直接创建/上传/销毁。

## 1. 当前资源路径

```text
Mesh/Shader/Pipeline/Material
  -> *Storage -> *StorageFactory / *StorageCache -> IDevice

TextureAsset (层1，序列化源)
  -> instantiate() 唯一实例 / clone() 脱离实例 / Texture::defaultWhite() 等静态默认
  -> 层2 Texture：经 IDevice::active() 分配 texture/view/sampler 三个 RID
  -> initialize/upload(pixels) 上传

Material
  -> MaterialStorage -> rhi::TextureBinding { view, sampler }
  -> BindGroup
```

句柄池按对象类型分开、直接存 `Vulkan*` 实例（不存指针、不经抽象接口），全部内嵌在具体 `VulkanDevice`；`rhi::IDevice` 只声明 RID 生命周期虚函数（`<type>_create/_destroy/_upload`），不含任何后端原生类型，也不再内嵌句柄池。`IRHITexture`/`IRHITextureView`/`IRHISampler` 抽象接口已删除。IDevice 是全局单例：后端构造时把自己注册为 `IDevice::active()`、析构时让位，未构造时 `active()` 返回 `nullptr`。层2 `Texture`/`Sampler` 与静态默认纹理都经 `IDevice::active()` 取设备（没有纹理管理器可注入）。

## 2. 层2 Storage（Mesh/Shader/Pipeline/Material）

- `MeshStorage`：版本化上传顶点/索引 Buffer。
- `MaterialStorage`：材质 BindGroup 与 uniform buffer 跨帧常驻；每次 resolve 经 `collectTextureBindings` 重新解析 `rhi::TextureBinding` 签名，签名变化触发 BindGroup 重建。
- `ShaderStorage`：管理编译结果、ShaderModule 缓存和延迟退役。
- `GraphicsPipelineStorage`：管理 Pipeline 描述、缓存和 shader 变化失效。
- `FrameGpuManager`：管理场景/对象/实例表 Buffer、固定 BindGroupLayout 和每帧 BindGroup。
- `GlobalUniformGpuManager`：按帧保存 global BindGroup，比较 `rhi::TextureBinding` 签名以处理纹理热替换。

## 3. Texture 生命周期与去重

层2 `Texture` 不持有 `TextureDesc` 与像素数据，只保留必要属性 + 三个 RID（texture / 默认 view / 默认 sampler）。构造（接 `TextureDesc`）经 `IDevice::active()` 分配三个 RID：`texture_create` 建纹理、`texture_view_create`（翻译后的层2 `TextureViewDesc`）建层2 拥有的默认视图、`sampler_create` 取去重采样器；`initialize`/`upload(pixels)` 按 `computeTextureLayout` 逐 mip 切片上传。

- **去重**：没有纹理管理器时，"每个 `TextureAsset` 只 `instantiate()` 出一个运行时实例"天然承担去重——同一 asset 反复解析（`resolveTextureReference` → `AssetManager.loadAsset<TextureAsset>` → `instantiate`）命中缓存 asset 的同一实例。`clone()` 产出互不影响的脱离实例。
- **Sampler**：设备级按 `SamplerDesc` 去重（`IDevice::sampler_create`）；层2 `Sampler` 与 `Texture` 的默认 sampler 都**非拥有**，析构不 `sampler_destroy`，随设备释放。
- **View**：层2 `Texture` 经 `IDevice::texture_view_create` 创建并拥有默认 view；view 去重与所有权在 `VulkanDevice`（设备级 `(textureRID, rhi::TextureViewDesc)→RID`），`texture_destroy` 级联释放该纹理全部 view；`texture_default_view` 是设备级惰性入口（RenderTarget/Swapchain/ImGui 用）。层3 `VulkanTexture/View/Sampler` 只留 Vk create-info 类原生 info、不持 desc，且不再有任何抽象基类（`IRHITexture` 等已删除），对上只以 RID 可见。

## 4. Material binding

材质 shader 纹理的规范输入是 `rhi::TextureBinding { RID view; RID sampler; }`。`Material` 提供两种写法：

```cpp
void setTexture(name, Ref<Texture> texture);                    // 默认 view + 默认 sampler
void setTexture(name, Ref<Texture> texture, Ref<Sampler> sampler); // 默认 view + 指定 sampler
```

`MaterialStorage::collectTextureBindings` 对每个 Texture2D 属性取 `material.resolveTexture(name)` 与 `resolveSampler(name)`，组装 `sampler ? texture->binding(sampler) : texture->binding()`（两者都用纹理默认 view）。资产材质序列化纹理 VirtualPath；`Material::resolveTexture` 惰性经 `resolveTextureReference` 解析路径并缓存 `Ref<Texture>`。只有 view/sampler handle 进入 sampled descriptor，texture handle 从不直接写入。

## 5. 热重载与跨设备

- **热重载（活链接）**：`AssetManager` 导入监听器对纹理走 `reloadInPlace(path)`——就地重传缓存中的同一 `TextureAsset`（不 invalidate、不新建对象），`TextureAsset::transfer` 读取分支随即 `syncInstance()` 把新像素重上传给唯一实例，保持实例身份稳定。
- **跨设备/工程切换**：层2 `Texture` 记录创建它的设备 `uid`（`IDevice` 的进程内单调标识，永不复用）。`~Texture` 仅当 `active()->uid()` 等于自身 `deviceUid_` 时才销毁 GPU 资源，避免旧设备资源被打到新设备上（裸指针比较会因地址复用误判）。静态默认纹理（white/black/normal/error）按 `uid` 检测设备切换并自动重建，取代了旧的 `retainPixels` 补丁。
- **双向观察者**：`TextureAsset` 以裸指针 `instance_` 观察其唯一实例，实例以裸指针 `asset_` 回指 asset（asset 由 `AssetManager` 强缓存常驻）；`~Texture` 与 `~TextureAsset` 互相清空对方指针，任一方先销毁都安全。

## 6. 初始化与关闭

Engine 创建 Renderer/Device 后，后端设备构造即成为 `IDevice::active()`，随后初始化各层2 Storage（不再单独初始化纹理管理器）。关闭顺序：

1. `MaterialStorage` / `GlobalUniformGpuManager` 等 descriptor 所有者；
2. Shader/Pipeline 等其他 Storage；
3. Mesh/Frame managers；
4. Renderer、Swapchain 和 Device（Device 析构让出 active 并清空三池，级联释放纹理/视图/采样器）。

层2 `Texture` 的 GPU 资源在所属材质/场景释放时经 `~Texture` 归还设备；因设备销毁会清空三池，切换设备前无需显式清理纹理。
