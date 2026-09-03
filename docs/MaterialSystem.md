# 引擎层 Material 系统

## Uniform 字节数据

Material 的数值属性现在以 `UniformBlockLayout + uniformData` 作为唯一运行时数据源：

```cpp
class Material {
    std::shared_ptr<Shader> shader_;
    UniformBlockLayout uniformLayout;
    std::vector<std::byte> uniformData;
    std::unordered_map<std::string, std::string> textures;
};
```

加载 Material 时依次执行：

```text
根据 Shader Properties 创建并清零 uniformData
  → 按布局写入所有 Shader 默认值
  → 写入 Material JSON override
  → dirty = true，version = 1
```

数值属性不再另外保存在 `ShaderValue` map 中，避免 map 与 GPU 字节数据不同步。Texture2D 不进入 uniform buffer，继续保存为独立字符串属性，后续解析为 TextureHandle 和 descriptor。

类型安全接口包括：

```cpp
getFloat / setFloat
getVec2  / setVec2
getVec3  / setVec3
getVec4  / setVec4
getBool  / setBool
getTexture / setTexture
```

Float 接受 Float/Range，Vec4 接受 Vec4/Color。其他类型必须严格一致。Bool 在字节缓冲中编码为 32 位 `0/1`；Vec3 写入 12 字节数据，并将剩余 4 字节 padding 清零。

每次成功修改都会设置 `dirty = true` 并递增 `version`。GPU 上传完成后可调用 `markClean()`；以后逐帧 Material Upload Arena 可以通过 version 判断是否需要重新上传。

游戏层通过 Renderer 的 `setMaterialFloat/setMaterialVec*/setMaterialBool/setMaterialTexture` 修改材质，不直接长期持有 `Material&`，避免 Material 资源表扩容后引用失效。

## 目标

Material 是引擎资产，不是 RHI 资源。它描述 Shader 引用、属性值、keyword 和渲染队列；RHI 只管理 Buffer、Texture、Pipeline、BindGroup 等 GPU 对象。

本次迁移后，`src/rhi` 不再创建、销毁、保存或解析 `MaterialHandle`。Material 的完整生命周期由 `renderer/Material.*` 管理。

## 分层

```text
JSON Shader / JSON Material
           │ 加载、默认值合并、类型校验
           ▼
MaterialManager（引擎层）
           │ MaterialHandle → Material → shared_ptr<Shader>
           ▼
Renderer（每帧提取）
    ├─ renderQueue → DrawItem 排序
    ├─ BaseColor + Transform → ObjectDrawData
    └─ Shader Pass → 后续 PipelineCache 请求
           │
           ▼
DrawList（RHI 句柄 + 纯 GPU 输入数据）
           │
           ▼
IRenderBackend / VulkanBackend
```

## 类型职责

### MaterialHandle

仍位于 `RenderResources.h`，由 `index + generation` 组成。它是游戏对象和 Renderer 之间的稳定引用，不是 RHI handle。

### ShaderAsset 与 Shader

`ShaderAsset` 是 AssetManager 缓存的磁盘资产数据。`Shader` 是由 ShaderAsset 创建的运行时类，持有：

- Shader JSON 的规范化路径；
- 复制后的 Properties 和运行时 SubShader/Pass；
- 由 Properties 构建的 `UniformBlockLayout`。

`AssetManager` 按资产类型和规范化路径缓存 ShaderAsset、MaterialAsset 和 MeshAsset。`MaterialManager::createInstance()` 从 MaterialAsset 创建 Material，并从其 ShaderAsset 复制构建独立的运行时 Shader。Shader 本身不持有 ShaderAsset。

### Material

保存：

- Material 名称；
- 运行时 `Shader` 对象的共享引用；
- 合并默认值后的类型安全属性表；
- Shader keywords；
- render queue。

类型安全的 getter/setter 在属性不存在或类型错误时记录 fatal 日志并退出程序。

### MaterialManager

负责：

- 按路径加载 Material JSON；
- 通过 AssetManager 加载 `MaterialAsset`，并复用它持有的 `ShaderAsset`；
- 在 Shader 已加载后校验 Material 属性与 keyword；
- 将 Shader 默认属性与 Material override 合并；
- 分配、复用和校验 MaterialHandle；
- 销毁后递增 generation，使旧句柄失效。

它不创建 `VkPipeline`、descriptor set 或 GPU buffer。

加载顺序固定为 `AssetManager::loadMaterialAsset → AssetManager::loadShaderAsset → validateMaterialAsset`。Material 文件自己决定 Shader，重复路径直接命中统一资产缓存。

### Renderer

`Renderer::loadMaterial(path)/destroyMaterial` 直接操作 MaterialManager，其中 `loadMaterial` 内部调用 `createInstance()`。MaterialManager 同时支持从路径或已加载的 `shared_ptr<MaterialAsset>` 创建 Material。`Renderer::setMaterialShader(handle, path)` 可实时替换引擎层 ShaderAsset。

构建 DrawList 时，Renderer：

1. 解析 MeshHandle 和 MaterialHandle；
2. 从 Material 读取 render queue 和 BaseColor；
3. 将 transform 与 BaseColor 写入 `DrawList::objects`；
4. 为 SubMesh 生成 DrawItem；
5. 按 render queue 稳定排序。

`firstInstance` 仍引用 `DrawList::objects` 中原对象下标，因此 DrawItem 排序不会打乱对象参数。

### IRenderBackend

以下旧的 RHI Material 接口已经删除：

```cpp
createMaterial(...)
destroyMaterial(...)
materialDrawInfo(...)
```

后端帧入口收窄为：

```cpp
void renderFrame(const DrawList& drawList);
```

VulkanBackend 只上传 `DrawList::objects`，不再读取 RenderScene 或 MaterialHandle。

## 当前 Pipeline 过渡接口

Renderer 选出 Material 的 ShaderPass 后，通过后端请求缓存 Pipeline：

```cpp
rhi::GraphicsPipelineHandle pipelineForPass(const ShaderPass& pass);
```

`PipelineCache` 根据 Pass 程序与 RenderState、当前 VertexLayout 和 RenderTarget format 返回或创建 pipeline handle。Material 不保存 RHI pipeline handle，资产层因此不依赖 GPU 生命周期。Keyword/Variant 仍是后续扩展项。

## 数据生命周期

```text
loadMaterial(path)
  → loadMaterialAsset
  → load/reuse Shader（内部调用 loadShaderAsset）
  → validateMaterialAsset
  → MaterialManager 分配 slot/generation
  → RenderScene 保存 MaterialHandle
  → 每帧 Renderer resolve 并提取数据
  → DrawList 仅存本帧快照
  → VulkanBackend 上传快照

destroyMaterial
  → 清空 Material
  → generation + 1
  → 旧 RenderScene 在下一次提取时得到 stale handle 错误
```

因为 DrawList 是每帧临时快照，后端提交后不需要持有 Material，也不会跨帧引用 MaterialManager 内存。

## 实时切换 Shader

`MaterialManager::setShader(handle, path)` 会加载或复用目标 Shader，然后调用 `Material::setShader`：

1. 保存旧 Shader 中的当前属性值；
2. 使用新 Shader 的布局和默认值重建 uniform/texture 数据；
3. 恢复双方同名且类型兼容的属性；
4. Float/Range、Vec4/Color 视为兼容类型对；
5. 删除新 Shader 未声明的 keyword；
6. 使用 Material 的 render queue override，或者回退到新 Shader 的默认队列；
7. 设置 dirty 并把 version 递增一次。

重建使用临时 `Material` 完成；输入会在修改正式材质前完成校验。切换后 Renderer 会从新 Shader 重新选择 Pass，并通过 PipelineCache 获取对应 GPU Pipeline。

## 验证

`tests/MaterialTest.cpp` 验证：

- JSON Material 可以加载；
- Shader 默认属性和 Material override 正确合并；
- 属性保留 Vec2/Vec4 类型；
- render queue 正确；
- Material 持有实际 Shader，切换后保留同名兼容参数并采用新属性默认值；
- 销毁后的旧句柄会被拒绝；
- slot 复用时 generation 会变化。
