# Render GPU 资源分层

Render 层不要求所有资源都经过同一种 Manager/Cache/Factory 模板。资源根据身份和生命周期选择最小抽象：Mesh/Pipeline/Material 继续使用层2 Storage 家族（Shader 程序不再是独立 Storage——由 `GraphicsPipelineStorage` 经 `ShaderCompilePipeline` 产出并缓存为 `Ref<ShaderProgram>`）；**Texture 不再有独立 Manager 或 gpu 层 Storage**——层2 `Texture` 自身持有 RHI 句柄，经全局活动设备直接创建/上传/销毁。

## 1. 当前资源路径

```text
Mesh/Pipeline/Material
  -> *Storage -> *StorageFactory / *StorageCache -> IDevice

Shader（ShaderProgram：vs/fs 模块 + 反射 set 布局 + pipeline layout）
  -> GraphicsPipelineStorage.getOrCreateProgram -> ShaderCompilePipeline -> IDevice

TextureAsset (层1，序列化源)
  -> instantiate() 唯一实例 / clone() 脱离实例 / Texture::defaultWhite() 等静态默认
  -> 层2 Texture：经 IDevice::active() 分配 texture/view/sampler 三个 RID
  -> initialize/upload(pixels) 上传

Material
  -> MaterialStorage -> set-1 UniformSet（材质 UBO + 纹理 setResourceAt(Ref<Texture>)）
  -> rhi::descriptor_set（VkDescriptorSet）
```

句柄池按对象类型分开、直接存 `Vulkan*` 实例（不存指针、不经抽象接口），全部内嵌在具体 `VulkanDevice`；`rhi::IDevice` 只声明 RID 生命周期虚函数（`<type>_create/_destroy/_upload`），不含任何后端原生类型，也不再内嵌句柄池。`IRHITexture`/`IRHITextureView`/`IRHISampler` 抽象接口已删除。IDevice 是全局单例：后端构造时把自己注册为 `IDevice::active()`、析构时让位，未构造时 `active()` 返回 `nullptr`。层2 `Texture`/`Sampler` 与静态默认纹理都经 `IDevice::active()` 取设备（没有纹理管理器可注入）。

## 2. 层2 Storage（Shader/Pipeline/Material）

> Mesh 不再有自己的层2 Storage：它已对齐 Texture，成为自持 RHI 句柄的层2 运行时对象（见第 3 节）。

- `MaterialStorage`：材质 set-1 `UniformSet` 跨帧常驻（LRU）；`resolve(material, program)` 用 program 反射出的 set-1 `UniformSetLayout`/`UboLayout` 建集，材质 UBO 走 `Ubo::setRawBytes(uniformBytes)`、纹理走 `setResourceAt(binding, Ref<Texture>)`；`collectTextureBindings` 解析 `rhi::TextureBinding` 签名，program 的 set-1 布局或签名变化触发重建。set-1 被 SPIR-V 剥离（如 depth-only shadow caster）时返回空集，合法不绑。
- `GraphicsPipelineStorage`（位于 `src/render/`）：持有引擎的 `ShaderCompilePipeline`（吸收原 `ShaderStorage` 角色），`getOrCreateProgram` 产出并缓存 `Ref<ShaderProgram>`（vs/fs 模块 + 反射 set 布局 + pipeline layout），并按 (program, 顶点布局, RT 格式) 缓存 VkPipeline、处理 shader 变化失效。图形管线创建落在这里而非 `ShaderProgram`，因为它需要绘制期状态（mesh 顶点布局、RT 格式）。
- `FrameGpuManager`：管理场景/对象/实例表 Buffer 与 set-0 `UniformSet`（硬编码约定经 `UniformSetLayout::acquire` 去重命中反射布局）；场景 UBO 走 `Ubo::setRawBytes(SceneDrawData)`，objects/instanceTable 用 `StorageBufferResource`，阴影贴图用 `Texture::adoptBinding`。
- `GlobalUniformGpuManager`：按帧保存 set-2 `UniformSet`（layout={0:UB?,1..M:CIS}，M=实际全局纹理数），处理纹理热替换。

## 3. Texture / Mesh 生命周期与去重

层2 `Texture` 不持有 `TextureDesc` 与像素数据，只保留必要属性 + 三个 RID（texture / 默认 view / 默认 sampler）。构造（接 `TextureDesc`）经 `IDevice::active()` 分配三个 RID：`texture_create` 建纹理、`texture_view_create`（翻译后的层2 `TextureViewDesc`）建层2 拥有的默认视图、`sampler_create` 取去重采样器；`initialize`/`upload(pixels)` 按 `computeTextureLayout` 逐 mip 切片上传。

层2 `Mesh` 同理不持有 `MeshDesc`/`MeshData`，只保留绘制必要属性 + vertex/index buffer 的 RHI RID。构造经 `IDevice::active()` 对每个 buffer 走拆分的三步创建：`buffer_allocate_rid`（仅句柄）→ `buffer_allocate_memory`（VkBuffer+VMA）→ `upload` 经 `buffer_upload`；`~Mesh` 两步销毁：`buffer_free_memory` → `buffer_release_rid`。`MeshUsage` 决定策略：Static→DeviceLocal 持久（staging 拷贝），Dynamic→host-visible 持久（就地 memcpy），Stream→host-visible 瞬态（每次 upload 经 `buffer_acquire_transient` orphan 旧的，设备按帧 fence 回收）。去重同样靠“每 `MeshAsset` 唯一 `instantiate` 实例”，热重载经 `reloadInPlace` → `syncInstance` 重上传。

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
