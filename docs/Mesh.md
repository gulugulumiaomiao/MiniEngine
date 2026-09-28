# Mesh 系统

Mesh 系统分为三层，与 Texture 对齐：① 源资产与导入产物；② CPU 侧序列化源 `MeshAsset`（`render/mesh`）；③ 运行时 `Mesh`（`render/mesh`，GPU 资源持有者）。运行时 `Mesh` 直接持有顶点/索引 Buffer 的 RHI `RID`，经 `rhi::IDevice` 三步创建（分配 RID → 分配 GPU 内存 → 上传数据）、两步销毁（销毁 GPU 内存 → 回收 RID）。

不存在 `MeshManager`，也没有独立的 Render 侧 Cache/Uploader 层：每个 `MeshAsset` 只 `instantiate()` 出唯一运行时 `Mesh`（"每 asset 唯一实例"天然承担去重），`clone()` 产出脱离 asset 的独立实例。`Mesh` 与 `Texture` 一样属于层2，可包含 `rhi/api`（见 `tests/RenderRhiBoundaryTest.cmake` 的层2 carve-out）。

## 1. 数据模型

`MeshDesc` 描述如何解释和绘制数据（序列化源，供 `MeshAsset` 持有；运行时 `Mesh` 不保留它，只分解出绘制必要属性）：

- `VertexLayout`：由 `VertexStreamLayout` 组成，指定 binding、location、输入频率、语义、格式。
- `IndexType`：`UInt16` 或 `UInt32`。
- `MeshUsage`：`Static`、`Dynamic` 或 `Stream`，决定 GPU 内存与上传策略（见 5.2）。
- `MeshTopology`：当前为三角形列表或线列表。
- `SubMesh`：保存 `firstIndex`、`indexCount`、`vertexOffset`、`materialSlot` 和局部包围体。
- `MeshBounds`：整个 Mesh 的 AABB 与包围球。

`MeshData` 持有 CPU 侧实际字节：每个 binding 对应一个拥有数据所有权的 `VertexStream`，索引集中存放在 `indices`。`validateMesh()` 会统一检查布局合法性、流大小和顶点数、索引字节数、SubMesh 范围、索引引用及包围体。

运行时 `Mesh` 只保留绘制必要属性 + GPU 句柄：`vertexBuffers()`（每 binding 一个 `{binding, rhi::RID}`）、`indexBuffer()`、`indexFormat()`、`topology()`、`subMeshes()`、`vertexLayout()`（管线构建顶点输入态所需）、`bounds()`、`usage()`。它不持有 `MeshDesc`/`MeshData`/`buildRecipe`/version/assetPath/assetId/device。

## 2. 可序列化的构建配方

`MeshBuildRecipe` 是可通过 `Transfer` 写入二进制或 JSON 的纯数据，包含名称、顶点布局预设、索引策略、usage、CPU 副本选项以及多个 `MeshPrimitivePart`。

```cpp
MeshBuildRecipe recipe;
recipe.name = "Runtime shapes";

MeshPrimitivePart ground;
ground.primitive = PlaneGeometry{{8.0F, 8.0F}, 4, 4};
ground.translation = {0.0F, -1.0F, 0.0F};
ground.materialSlot = 0;
recipe.parts.push_back(ground);

MeshPrimitivePart sphere;
sphere.primitive = UvSphereGeometry{0.5F, 32, 16};
sphere.materialSlot = 1;
recipe.parts.push_back(sphere);
```

支持的图元包括：

- `PlaneGeometry`：XZ 平面，二维尺寸和 X/Z 细分，法线朝 +Y。
- `BoxGeometry`：中心位于原点的盒体，X/Y/Z 尺寸和各轴细分。
- `UvSphereGeometry`：Y 轴朝上的 UV 球，配置半径、经线和纬线细分。
- `CylinderGeometry`：Y 轴朝上的圆柱或圆台，配置上下半径、高度、径向/高度细分和端盖。

每个 part 可配置 translation、rotation、scale 和 material slot。构建时变换会烘焙进顶点，一个 part 对应一个 SubMesh。法线使用逆转置矩阵；镜像变换会翻转三角形绕序和 tangent handedness。

构建器限制最多 4096 个 part、总顶点最多 4M；尺寸、变换或细分非法以及乘法溢出时返回 `std::nullopt`，不会产生半成品 Mesh。

## 3. 运行时构建入口

```cpp
auto result = MeshBuilder::build(recipe);       // MeshBuildResult{MeshDesc, MeshData}
auto asset  = MeshBuilder::buildAsset(recipe);  // Ref<MeshAsset>（同时保留 recipe）
Ref<Mesh> mesh = asset->instantiate();          // 唯一运行时实例（分配 GPU 缓冲并上传）
```

运行时图元不再经过任何 Manager：`MeshBuilder::buildAsset(recipe)` 产出临时 `MeshAsset`，再 `instantiate()` 得到 `Mesh`。临时 asset 随后析构并清除观察者回指，得到的 `Mesh` 脱离 asset（等价 clone）。

### 3.1 运行时 Primitive MeshComponent

`MeshComponent` 有两种互斥的来源模式：`Asset` 保存由 `.mesh.json` 解析的 `Ref<Mesh>` 且不拥有其资产身份；`Primitive` 保存 `MeshBuildRecipe`、拥有运行时 Mesh，并在脱离 Scene 时释放它。

```cpp
MeshComponent* mesh = node.addComponent<MeshComponent>();
mesh->setPrimitive(UvSphereGeometry{0.75F, 32, 16});

if (MeshBuildRecipe* recipe = mesh->editPrimitiveRecipe()) {
    auto& sphere = std::get<UvSphereGeometry>(recipe->parts[0].primitive.value);
    sphere.radius = 1.25F;
}
```

`editPrimitiveRecipe()` 会置 dirty；下一次 `onUpdate`/`onAttach`/`onEnable` 触发 `applyPrimitiveChanges()`。重建即替换：用新 recipe 经 `MeshBuilder::buildAsset + instantiate` 生成一个全新的 `Mesh` 并赋给组件，旧 `Mesh` 的最后一个 `Ref` 归零时 `~Mesh` 自动释放其 GPU 缓冲。构建失败时保留旧 Mesh。

Scene JSON 保留原有的 `"mesh": "...mesh.json"` 资产写法，并新增运行时图元写法：

```json
{
  "type": "Mesh",
  "primitive": {
    "type": "sphere",
    "parameters": { "radius": 0.75, "longitude_segments": 32, "latitude_segments": 16 }
  }
}
```

`primitive.type` 支持 `plane`、`box`/`cube`、`sphere`/`uv_sphere` 和 `cylinder`；缺省参数使用对应 Geometry 默认值。资产路径和 primitive 配置不能同时出现。配方会随 `MeshComponentAsset` 序列化进 Scene Artifact，但不会加入资产依赖图。

默认 `PositionNormalTangentUv` 是 stride 48 的交错流：POSITION Vec3、NORMAL Vec3、TANGENT Vec4、TEXCOORD0 Vec2。也可选择 `Position`（stride 12）或 `PositionNormalUv`（stride 32）。

`Auto` 索引策略在所有顶点可由 UInt16 寻址时使用 UInt16，否则使用 UInt32；强制 UInt16 但发生越界会构建失败。

## 4. `.mesh.json` 与导入产物

`MeshAssetImporter` 同时支持原始 vertex stream 格式和 `source.type = "procedural"` 的构建配方：

```json
{
  "name": "Two Shapes",
  "usage": "static",
  "keep_cpu_copy": false,
  "source": {
    "type": "procedural",
    "vertex_layout": "position_normal_tangent_uv",
    "index_policy": "auto",
    "parts": [
      {
        "type": "box",
        "parameters": { "size": [1.0, 1.0, 1.0] },
        "translation": [-1.0, 0.0, 0.0],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0],
        "material_slot": 0
      },
      {
        "type": "sphere",
        "parameters": { "radius": 0.5, "longitude_segments": 32, "latitude_segments": 16 },
        "translation": [1.0, 0.0, 0.0],
        "material_slot": 1
      }
    ]
  }
}
```

四元数顺序是 `[x,y,z,w]`；`box`/`cube`、`sphere`/`uv_sphere` 是等价别名；省略变换时使用单位变换。完整示例见 `builtin/samples/meshes/procedural_showcase.mesh.json`。

导入流程如下：

1. `AssetImportPipeline` 根据 `.meta` 确定 AssetId 和 `AssetType::Mesh`，比较源文件 hash、meta hash、Importer 版本和已有 Artifact，决定是否需要重导入。
2. `MeshAssetImporter` 读取 JSON。raw 数据直接解析；procedural 数据先解析为 `MeshBuildRecipe`，再交给 `MeshBuilder` 烘焙。
3. Importer 调用 `validateMesh()`，然后由 `BinaryWriter` 执行 `MeshAsset::transfer()`，序列化配方、`MeshDesc` 和 `MeshData`。
4. Mesh payload 被包装进 `AssetArtifact` 并写入 Library，`AssetDatabase` 记录源路径、Artifact 路径、hash、Importer 版本和导入状态。

`MeshAssetImporter::version()` 用于决定源资产是否需要重导入；Mesh 二进制 payload 当前为 v4。

## 5. 从资产加载到 GPU 绘制的完整流程

```text
.mesh.json
  -> MeshAssetImporter -> AssetArtifact / AssetDatabase
  -> AssetManager::loadAsset<MeshAsset>()  (强缓存，按路径去重)
  -> BinaryReader + MeshAsset::transfer()
  -> MeshAsset::instantiate() -> 唯一运行时 Mesh
       构造: buffer_allocate_rid (步1) + buffer_allocate_memory (步2)
       upload: buffer_upload (步3)
  -> Scene::buildRenderScene() -> RenderObject (持 Ref<Mesh>)
  -> DrawListBuilder: 直接读 Mesh 的 vertexBuffers()/indexBuffer()/subMeshes()
  -> DrawItem -> Vulkan command buffer -> vkCmdDrawIndexed
```

### 5.1 加载 Artifact 并实例化运行时 Mesh

场景实例化遇到 `MeshComponentAsset` 时，通过 `SceneInstantiationContext::loadMesh` 调用 `resolveMeshReference(path)`：

1. `resolveMeshReference` 把引用规范为 `assets://` 路径，调用 `AssetManager::loadAsset<MeshAsset>()`（先查 `Ref<Asset>` 强缓存，再 `ensureImported()` 确认数据库记录和 Artifact 有效，然后 `MeshAsset::transfer()` 反序列化并 `validateMesh()`）。
2. `MeshAsset::instantiate()` 返回唯一运行时实例：首次调用构造 `Mesh`（分配 GPU 缓冲）并 `upload`，登记 `instance_` 裸观察者；之后复用同一实例。

`Mesh` 构造即分配 GPU 缓冲（三步创建的步1+步2），`instantiate()` 紧接着 `upload`（步3）。因此实例化后 Mesh 已 GPU 常驻，无需延迟到渲染准备阶段。

### 5.2 三步创建 / 两步销毁与 MeshUsage 策略

`Mesh` 构造对每个 vertex stream（`BufferUsage::Vertex | TransferDestination`）与 index（`BufferUsage::Index | TransferDestination`）：

- 步1 `IDevice::buffer_allocate_rid(desc)`：仅分配池句柄，不分配 GPU 内存。
- 步2 `IDevice::buffer_allocate_memory(rid)`：创建 `VkBuffer` + VMA 分配。
- 步3 `Mesh::upload(data)` → `IDevice::buffer_upload(rid, bytes)`。

`MeshUsage` 决定 GPU 策略（三条不同路径）：

- `Static` → `MemoryUsage::DeviceLocal` 持久缓冲；上传经设备内部 staging 拷贝。构造期走完三步创建，`~Mesh` 两步销毁。
- `Dynamic` → `MemoryUsage::Upload`（host-visible）持久缓冲；上传直接 memcpy、就地更新。生命周期同 Static。
- `Stream` → host-visible **瞬态**缓冲（orphan/rename）：构造**不**分配持久缓冲，每次 `upload` 经 `IDevice::buffer_acquire_transient` 获取全新缓冲并 orphan 旧的（旧缓冲由设备按帧 fence 自动回收，`~Mesh` 不释放）。这实现“零停顿、从不覆盖在飞数据”的流式语义，适合每帧重生成的几何（程序化/粒子/即时绘制）。**前提**：Stream mesh 必须每帧重新 `upload`；引擎尚无自动每帧生产者，属前瞻基础设施。

`~Mesh` 两步销毁（仅 Static/Dynamic）：对每个 buffer 先 `buffer_free_memory`（销毁 GPU 内存）再 `buffer_release_rid`（回收句柄）；Stream 的瞬态缓冲由设备 fence 池拥有，`~Mesh` 仅弃置引用。设备已亡（`IDevice::active()` 为空）时仅弃置句柄。

### 5.3 场景提取与 DrawItem 生成

每帧 `Scene::buildRenderScene()` 遍历有效节点：Camera 和 Light 被提取为场景数据；同时具有有效 `MeshComponent` 与 `MaterialComponent` 的可见节点被提交为 `RenderObject`，其中保存 `Ref<Mesh>`、材质槽、世界矩阵、layer mask、包围半径和阴影标记。

`DrawListBuilder::build()` 对每个 RenderObject：

1. 按相机 culling mask 与视锥（用 `mesh->bounds()` 的世界包围球）过滤对象。
2. 直接读取 `Mesh` 的 GPU 句柄与属性（不再有任何 resolve/cache 中间层）。
3. 把对象世界矩阵写入 `DrawList::objects`，其数组下标作为 `firstInstance`。
4. 遍历 `mesh->subMeshes()`，用 `materialSlot` 选择对象材质。
5. 对材质的 `MiniForward` SubShader 查找 ShadowCaster、DepthOnly、Forward pass；结合材质关键字生成 variant，并以 shader pass、variant 和 `mesh->vertexLayout()` 获取或创建图形管线（管线缓存键含 `vertexLayout().hash()`，按需计算）。
6. 每个有效的 SubMesh/pass 生成一个 `DrawItem`，携带 `mesh->vertexBuffers()`、`indexBuffer()`、`indexFormat()` 和 `DrawIndexedArguments`。
7. DrawItem 按渲染阶段和 render queue 稳定排序（Mesh 排序键用 index buffer RID 的 index），再交给后端提交。

## 6. 更新、热重载与销毁

### 6.1 资产热重载

Debug 模式下 `FileWatcher` 的事件由 `AssetImportPipeline` 处理。Mesh 重导入成功后，`AssetManager` 对 Mesh 走 `reloadInPlace(path)`（与 Texture 相同）：重新反序列化进已缓存的同一 `MeshAsset`，其 `transfer()` 读取分支调用 `syncInstance()`，把新 `MeshData` 重新 `upload` 到唯一运行时实例。因此实例身份不变、场景组件不必换 `Ref<Mesh>`，GPU 缓冲被就地重写。

### 6.2 生命周期

`Mesh` 由 `Ref<Mesh>` 引用计数管理（侵入式 `RefCounted`）。`MeshAsset` 只持 `instance_` 裸观察者指针（不延长实例生命周期），实例只持 `asset_` 裸回指；双方析构时互清回指，避免悬垂。最后一个 `Ref<Mesh>` 归零即 `~Mesh`，两步释放其 GPU 缓冲。`AssetManager` 强缓存常驻 `MeshAsset`（作为 upload/clone 的数据源），工程 teardown 时清缓存触发 `~MeshAsset`。

## 7. 排查入口

遇到"资产存在但没有绘制"时，建议按以下顺序定位：

1. `AssetDatabase` 中记录是否为 Imported，Artifact 文件是否存在且类型为 Mesh。
2. `AssetManager::loadAsset<MeshAsset>()` 是否成功完成 Artifact 校验、反序列化和 `validateMesh()`。
3. `resolveMeshReference()` / `MeshAsset::instantiate()` 是否取得有效 `Mesh`（`isValid()`：构造时是否有 active device、buffer RID 是否分配成功）。
4. 对象是否被 active、visible、layer/culling mask 或 cast-shadow 条件过滤。
5. `mesh->subMeshes()` 是否非空、`vertexBuffers()`/`indexBuffer()` 是否有效。
6. 材质槽是否有效，shader 是否包含 `MiniForward` SubShader 及目标 pass，`vertexLayout()` 是否与 shader 输入匹配。
7. DrawItem 是否进入目标 phase，最终是否记录 `vkCmdBindVertexBuffers`、`vkCmdBindIndexBuffer` 和 `vkCmdDrawIndexed`。

## 8. 代码导航与测试

主要实现位置：

- `src/asset/importer/MeshAssetImporter.cpp`：源 JSON 解析、构建与 Artifact 写入。
- `src/asset/manager/AssetManager.cpp`：导入保证、Artifact 加载、反序列化、资产缓存与 Mesh 热重载（`reloadInPlace`）。
- `src/render/mesh/Mesh.h/.cpp`：数据模型与传输格式、校验、层2 `Mesh`（GPU 句柄持有 + 三步创建/两步销毁）、层1 `MeshAsset`（instantiate/clone/syncInstance）与 `resolveMeshReference`。
- `src/render/mesh/MeshBuilder.cpp`：基础几何体构建和组合（`build`/`buildAsset`）。
- `src/scene/components/MeshComponent.cpp`：Asset/Primitive 两种来源，运行时图元经 `buildAsset + instantiate` 创建与重建。
- `src/scene/scene/SceneAsset.cpp`、`Scene.cpp`：Mesh 引用加载（`loadMesh` 回调）和渲染场景提取（`bounds()` 算包围半径）。
- `src/render/renderer/DrawListBuilder.cpp`：直接读 `Mesh` 访问器生成 DrawItem。
- `src/render/gpu/pipeline/GraphicsPipelineStorage.cpp`：以 `mesh.vertexLayout()` 构建管线顶点输入态。
- `src/rhi/api/Device.h`：设备接口（`IDevice` + `ISwapchain`）；后端无关的资源描述统一在 `src/rhi/api/ResourceDesc.h`（含拆分的 buffer 生命周期 `buffer_allocate_rid/allocate_memory/free_memory/release_rid` 与组合的 `buffer_create/destroy/upload`）。
- `src/rhi/vulkan/VulkanBuffer.cpp`：两阶段缓冲（构造记录参数、`allocateMemory`/`freeMemory`），保留并暴露 `BufferUsage`。
- `src/rhi/vulkan/VulkanDevice.cpp`：Buffer 资源表、拆分的 buffer 生命周期实现与 Vulkan staging 上传。
- `src/rhi/vulkan/VulkanSwapchain.cpp`、`VulkanCommandBuffer.cpp`：命令记录、提交与 Vulkan draw 调用。

测试：`MeshTest` 覆盖序列化 round-trip、`instantiate` 三步创建（buffer 分配/上传计数）、唯一实例复用、就地重传（syncInstance 重上传）、`clone` 脱离实例、两步销毁释放 buffer，以及多种图元/布局/包围体/索引升级/非法参数；`MeshGuidIdentityTest` 覆盖同路径解析复用唯一实例、clone 非 asset-backed、末 Ref 释放 GPU buffer、`reloadInPlace` 保持实例并重上传；`AssetImporterTest` 覆盖 raw 与 procedural `.mesh.json`；`AssetPipelineTest` 覆盖导入与 Mesh 热重载；`SceneTest`/`SceneAssetTest`/`SceneExportTest` 覆盖场景实例化与导出（均以 `MockDevice` 提供 headless active 设备）。
