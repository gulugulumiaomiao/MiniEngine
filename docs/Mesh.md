# Mesh 系统

Mesh 系统分为四层：源资产与导入产物、CPU 侧 `MeshAsset`、运行时 `Mesh`、Render 侧 `MeshGpuCache` 与 RHI Device。核心原则是资产加载与 GPU 上传解耦：加载只把 Artifact 反序列化为 CPU 数据，Mesh 第一次进入渲染准备时才延迟创建顶点/索引 Buffer。

## 1. 数据模型

`MeshDesc` 描述如何解释和绘制数据：

- `VertexLayout`：由 `VertexBinding` 和 `VertexAttribute` 组成，指定 binding、stride、输入频率、语义、格式、location 和 offset。
- `IndexType`：`UInt16` 或 `UInt32`。
- `MeshUsage`：`Static`、`Dynamic` 或 `Stream`。
- `MeshTopology`：当前为三角形列表或线列表。
- `SubMesh`：保存 `firstIndex`、`indexCount`、`vertexOffset`、`materialSlot` 和局部包围体。
- `MeshBounds`：整个 Mesh 的 AABB 与包围球。

`MeshData` 持有 CPU 侧实际字节：每个 binding 对应一个拥有数据所有权的 `VertexStream`，索引集中存放在 `indices`。`validateMesh()` 会统一检查布局合法性、流大小和顶点数、索引字节数、SubMesh 范围、索引引用及包围体。

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

构建器限制最多 512 个 part、总顶点最多 4M；尺寸、变换或细分非法以及乘法溢出时返回 `std::nullopt`，不会产生半成品 Mesh。

## 3. 运行时构建入口

```cpp
auto result = MeshBuilder::build(recipe);       // MeshDesc + MeshData
auto asset = MeshBuilder::buildAsset(recipe);   // 同时保留 recipe
MeshHandle handle = renderer.createProceduralMesh(recipe);
```

默认 `PositionNormalTangentUv` 是 stride 48 的交错流：POSITION Vec3、NORMAL Vec3、TANGENT Vec4、TEXCOORD0 Vec2。也可选择 `Position`（stride 12）或 `PositionNormalUv`（stride 32）。

`Auto` 索引策略在所有顶点可由 UInt16 寻址时使用 UInt16，否则使用 UInt32；强制 UInt16 但发生越界会构建失败。

`Renderer::createMesh()` 可直接提交已有 `MeshDesc + MeshData`，`Renderer::createProceduralMesh()` 则经 `MeshBuilder` 构建后插入 `MeshManager`。这两种运行时入口不会经过资产数据库，但后续的 GPU 准备和绘制流程完全相同。

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
        "parameters": {
          "radius": 0.5,
          "longitude_segments": 32,
          "latitude_segments": 16
        },
        "translation": [1.0, 0.0, 0.0],
        "material_slot": 1
      }
    ]
  }
}
```

四元数顺序是 `[x,y,z,w]`；`box`/`cube`、`sphere`/`uv_sphere` 是等价别名；省略变换时使用单位变换。完整示例见 `assets/meshes/procedural_showcase.mesh.json`。

导入流程如下：

1. `AssetImportPipeline` 根据 `.meta` 确定 AssetId 和 `AssetType::Mesh`，比较源文件 hash、meta hash、Importer 版本和已有 Artifact，决定是否需要重导入。
2. `MeshAssetImporter` 读取 JSON。raw 数据直接解析；procedural 数据先解析为 `MeshBuildRecipe`，再交给 `MeshBuilder` 烘焙。
3. Importer 调用 `validateMesh()`，然后由 `BinaryWriter` 执行 `MeshAsset::transfer()`，序列化配方、`MeshDesc` 和 `MeshData`。
4. Mesh payload 被包装进 `AssetArtifact` 并写入 Library，`AssetDatabase` 记录源路径、Artifact 路径、hash、Importer 版本和导入状态。

这里有两个独立版本号：`MeshAssetImporter::version()` 当前为 2，用于决定源资产是否需要重导入；Mesh 二进制 payload 当前为 v3，读取端兼容不含构建配方的 v2。

## 5. 从资产加载到 GPU 绘制的完整流程

```text
.mesh.json
  -> MeshAssetImporter -> AssetArtifact / AssetDatabase
  -> AssetManager::loadAsset<MeshAsset>()
  -> BinaryReader + MeshAsset::transfer()
  -> MeshAsset::instantiate() -> MeshManager -> MeshHandle
  -> Scene::buildRenderScene() -> RenderObject
  -> Renderer::renderFrame() -> prepareMesh() -> MeshGpuCache
  -> staging buffer -> device vertex/index buffers
  -> DrawItem -> Vulkan command buffer -> vkCmdDrawIndexed
```

### 5.1 加载 Artifact 并实例化运行时 Mesh

场景实例化遇到 `MeshComponentAsset` 时，通过 `SceneInstantiationContext::loadMesh` 最终调用 `MeshManager::load(meshPath)`：

1. `MeshManager` 先按资产路径查找已有 handle，避免重复创建运行时对象。
2. 缓存未命中时调用 `AssetManager::loadAsset<MeshAsset>()`。
3. `AssetManager` 先查弱引用缓存，再通过 `ensureImported()` 确认数据库记录和 Artifact 有效。Debug 构建可以按需导入源资产；Release 构建只接受已经 Cook 好的 Artifact。
4. 根据 `AssetDatabase` 记录读取 Artifact，校验 AssetId 和 AssetType，再以数据库中的请求路径设置资产身份。
5. `BinaryReader` 调用 `MeshAsset::transfer()`，完整读取 payload，并在提交数据前再次执行 `validateMesh()`。
6. `MeshAsset::instantiate()` 把资产路径、`MeshDesc`、`MeshData` 和可选构建配方复制到运行时 `Mesh`；`MeshManager` 保存实例并返回带 generation 的 `MeshHandle`。

到此为止只有 CPU 数据，还没有分配 Vulkan 顶点/索引 Buffer。

### 5.2 场景提取与 DrawItem 生成

每帧 `Scene::buildRenderScene()` 遍历有效节点：Camera 和 Light 被提取为场景数据；同时具有有效 `MeshComponent` 与 `MaterialComponent` 的可见节点被提交为 `RenderObject`，其中保存 `MeshHandle`、材质槽、世界矩阵、layer mask 和阴影标记。

`Renderer::renderFrame()` 对 RenderObject 执行以下工作：

1. 按相机 culling mask 过滤对象，并用 `MeshHandle` 从 `MeshManager` 取得运行时 `Mesh`。
2. 调用后端 `prepareMesh(handle, mesh)`。这是 CPU Mesh 到 GPU Mesh 的唯一准备边界，也是首次使用时的延迟上传点。
3. 把对象世界矩阵写入 `DrawList::objects`，其数组下标作为 `firstInstance`，供 shader 从对象缓冲读取变换。
4. 遍历 `MeshDrawInfo::subMeshes`，用 `materialSlot` 选择对象材质。
5. 对材质的 `MiniForward` SubShader 查找 ShadowCaster、DepthOnly、Forward pass；结合材质关键字生成 variant，并以 shader pass、variant 和 Mesh 的 `VertexLayout` 获取或创建图形管线。
6. 每个有效的 SubMesh/pass 生成一个 `DrawItem`，携带 vertex buffers、index buffer、index format 和 `DrawIndexedArguments`。
7. DrawItem 按渲染阶段和 render queue 稳定排序，再交给后端提交。

### 5.3 `MeshGpuCache` 创建显存 Buffer

`MeshGpuCache` 使用 `MeshHandle` 的 index 与 generation 组合成 key，并把运行时 `Mesh::version()` 作为缓存版本：

- 如果 handle 和 version 都命中，直接复用 `MeshDrawInfo`，本帧不再上传。
- 首次使用或 version 不一致时，`MeshGpuCache` 为每个 `VertexStream` 创建 `Vertex | TransferDestination`、`DeviceLocal` 的 RHI Buffer，并通过 `IDevice::uploadBuffer()` 提交 CPU 字节；缓存本身不再接触 Vk/VMA 类型。
- 索引数据采用相同流程，目标 usage 为 `Index | TransferDestination`，同时把 `IndexType` 映射成 RHI 的 UInt16/UInt32 `IndexFormat`。
- 每个 CPU `SubMesh` 被转换为轻量的 GPU 绘制范围：`firstIndex`、`indexCount`、`vertexOffset`、`materialSlot`。
- VulkanDevice 在 `uploadBuffer()` 内部创建 host-access staging buffer，通过一次性 command buffer 记录 `vkCmdCopyBuffer` 并提交到 graphics queue；当前实现每次复制后调用 `vkQueueWaitIdle`，优先保证生命周期正确，再销毁 staging buffer。
- 准备成功后缓存 `MeshDrawInfo` 并调用 `mesh.markClean()`。

当前 `keepCpuCopy` 会被序列化并保存在描述/配方中，但 `MeshGpuCache` 上传后尚未据此释放 `MeshData`。也就是说，当前实现始终保留 CPU 字节；不能把该字段理解为已经生效的内存回收开关。

### 5.4 RHI 后端实际绘制

`RhiRenderBackend::renderFrame()` 通过 `ISwapchain::beginFrame()` 获取当前 back buffer 和
命令编码器，上传场景和对象数据，然后由 RenderGraph 记录 Forward 图形 pass。Fence、
swapchain image acquire 和命令提交由 `VulkanSwapchain` 封装。对排序后的每个 `DrawItem`：

1. 管线变化时绑定 graphics pipeline，并绑定 set 0 的场景/对象 descriptor。
2. 材质变化时通过 `MaterialGpuCache` 准备并绑定 set 1。
3. 按各流的 binding 调用 `vkCmdBindVertexBuffers`。
4. 按 UInt16/UInt32 格式调用 `vkCmdBindIndexBuffer`。
5. 用 SubMesh 生成的参数调用 `vkCmdDrawIndexed`。

记录完成后 command buffer 提交到 graphics queue，由 semaphore 串联 image acquire、绘制完成与 present；frame fence 保护帧内资源复用。

## 6. 更新、热重载与销毁

### 6.1 运行时数据更新

`Dynamic` 和 `Stream` Mesh 可以通过 `updateVertexData()` / `updateIndexData()` 局部修改 CPU 数据；`Static` Mesh 拒绝更新。成功修改会执行 `markChanged()`，增加 version 并设置 dirty。下一帧 `MeshGpuCache::prepare()` 发现版本不一致后，会等待设备空闲、销毁旧 Buffer，再从完整 CPU 数据重建 GPU Buffer。

缓存失效的权威依据是 `version`；`dirty` 表示 CPU/GPU 状态，上传完成后会被清除，但缓存命中判断并不直接读取 dirty。

### 6.2 资产热重载

Debug 模式下 `FileWatcher` 的事件由 `AssetImportPipeline` 处理。Mesh 重导入成功后，`AssetManager` 先使对应资产缓存失效，再调用 `MeshManager::replace(path)`。replace 保留原 `MeshHandle`，用新实例替换内容，同时把 version 增加一并标记 dirty。因此场景组件不必换 handle，下一次渲染准备会自动重建 GPU 缓存。

### 6.3 生命周期

`Renderer::destroyMesh(handle)` 会先调用后端 `releaseMesh()`；`MeshGpuCache::invalidate()` 等待设备空闲并销毁该 handle 的所有顶点/索引 Buffer，然后 `MeshManager` 才销毁 CPU 实例。后端整体析构时 `MeshGpuCache::clear()` 释放剩余 Buffer。

当前 GPU 缓存更新与销毁采用 `waitIdle()` 的保守同步，逻辑简单但会造成 stall。未来可改为基于 frame serial/fence 的延迟回收，而不改变 `prepare/release` 接口。

## 7. 排查入口

遇到“资产存在但没有绘制”时，建议按以下顺序定位：

1. `AssetDatabase` 中记录是否为 Imported，Artifact 文件是否存在且类型为 Mesh。
2. `AssetManager::loadAsset<MeshAsset>()` 是否成功完成 Artifact 校验、反序列化和 `validateMesh()`。
3. `SceneAsset::instantiate()` 是否取得有效 `MeshHandle`，节点是否同时有启用的 Mesh/Material component。
4. 对象是否被 active、visible、layer/culling mask 或 cast-shadow 条件过滤。
5. `prepareMesh()` 是否返回非空 SubMesh 范围；Mesh version 是否触发了预期的 GPU 重建。
6. 材质槽是否有效，shader 是否包含 `MiniForward` SubShader 及目标 pass，VertexLayout 是否与 shader 输入匹配。
7. DrawItem 是否进入目标 phase，最终是否记录 `vkCmdBindVertexBuffers`、`vkCmdBindIndexBuffer` 和 `vkCmdDrawIndexed`。

## 8. 代码导航与测试

主要实现位置：

- `src/asset/importer/MeshAssetImporter.cpp`：源 JSON 解析、构建与 Artifact 写入。
- `src/asset/manager/AssetManager.cpp`：导入保证、Artifact 加载、反序列化与资产缓存。
- `src/render/mesh/Mesh.cpp`：传输格式、校验、实例化、版本更新与 `MeshManager`。
- `src/render/mesh/MeshBuilder.cpp`：基础几何体构建和组合。
- `src/scene/scene/SceneAsset.cpp`、`Scene.cpp`：Mesh handle 加载和渲染场景提取。
- `src/render/renderer/Renderer.cpp`：材质/pass/管线选择与 DrawList 生成。
- `src/render/backend/MeshGpuCache.cpp`：API 无关的 GPU Buffer 延迟创建、版本缓存与释放。
- `src/rhi/api/Device.h`、`ResourceDesc.h`：设备接口和 API 无关的资源描述。
- `src/rhi/vulkan/VulkanDevice.cpp`：Buffer/Shader/Pipeline 资源表和 Vulkan staging 上传。
- `src/render/backend/RhiRenderBackend.cpp`：后端无关的上传与 DrawList/RenderGraph 编排。
- `src/rhi/vulkan/VulkanSwapchain.cpp`、`VulkanCommandEncoder.cpp`：命令记录、提交与 Vulkan draw 调用。

`MeshTest` 覆盖多种图元、布局和包围体、组合变换/材质槽、索引升级、非法参数、配方 round-trip、v2 数据以及运行时版本更新；`AssetImporterTest` 覆盖 raw 与 procedural `.mesh.json`；`AssetPipelineTest` 覆盖导入和 Mesh 热替换。
