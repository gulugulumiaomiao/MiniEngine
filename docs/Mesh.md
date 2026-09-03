# 网格数据模型

网格系统采用三层结构：CPU 侧 `MeshAsset`、跨层 `MeshHandle` 和 Vulkan 后端内部的 GPU Mesh。Renderer 使用通用的 `MeshDesc + MeshData` 创建接口；Vulkan 后端通过 staging buffer 把顶点和索引分别上传到 device-local VBO/IBO。

## VertexLayout

`VertexLayout` 使用 stride 和属性列表描述一条交错顶点流。每个 `VertexAttribute` 包含：

- `semantic`：Position、Normal、Tangent、Color0、TexCoord0/1、JointIndices、JointWeights
- `format`：Float32、Vec2/3/4Float32、UInt16x4、UInt8x4Normalized
- `offset`：属性在单个顶点中的字节偏移
- `location`：Shader 顶点输入 location

校验会拒绝零 stride、空布局、重复 semantic、重复 location、越过 stride 或相互重叠的属性。

## MeshDesc 与 MeshData

`MeshDesc` 保存布局和元数据：

- debug name
- vertex layout
- UInt16/UInt32 index type
- Static/Dynamic/Stream usage
- TriangleList/LineList topology
- SubMesh 列表
- 局部空间 bounds
- 是否保留 CPU 副本

`MeshData` 是非拥有的字节视图，包含 vertex/index span 和元素数量。`MeshAsset` 是拥有版本，内部保存字节数组并在构造时执行完整校验。

## SubMesh 与 Bounds

`SubMesh` 只保存 index range、vertex offset、material slot 和局部包围体，不复制顶点。`materialSlot` 由 RenderObject 的材质列表解析，Mesh 本身不持有材质。

`MeshBounds` 同时保存 AABB 和 BoundingSphere。`calculateBounds` 从 position span 计算两种包围体；它们都位于 Mesh 局部空间，世界空间包围体由实例 transform 生成。

## 当前 GPU 执行

Graphics Pipeline 根据 `VertexLayout` 生成 Vulkan vertex binding/attribute description。绘制时绑定 VBO 和 IBO，遍历 `SubMesh` 并执行 `vkCmdDrawIndexed`；UInt16 与 UInt32 索引都会转换成对应的 `VkIndexType`。

当前 Forward Pipeline 只保留一个活动 `VertexLayout`，创建布局不同的第二个 Mesh 会得到明确错误。后续 PipelineCache 将以 VertexLayout、Shader Pass、Render State 和 RenderTarget format 为 key，解除这一限制。
