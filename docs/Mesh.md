# Mesh 系统

Mesh 系统分为源资产 `MeshAsset`、运行时 `Mesh`、实例管理器 `MeshManager` 和 Vulkan 侧 `MeshGpuCache`。除了导入原始顶点流，现在还支持使用可序列化配方在运行时或导入阶段构造基础几何体。

## 1. 顶点布局与 CPU 数据

`VertexLayout` 由多个 `VertexBinding` 和 `VertexAttribute` 组成。`MeshData` 为每个 binding 保存一个拥有数据所有权的 `VertexStream`，并以独立字节数组保存索引。`VertexLayout::validate()` 检查重复项、属性越界和重叠；`validateMesh()` 进一步检查数据大小、索引宽度、SubMesh 范围及顶点引用。

## 2. 可序列化构建配方

`MeshBuildRecipe` 是可通过 `Transfer` 写入二进制或 JSON 的纯数据。它包含名称、顶点布局预设、索引策略、usage、CPU 副本选项以及多个 `MeshPrimitivePart`。

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

支持的图元参数：

- `PlaneGeometry`：XZ 平面，二维尺寸和 X/Z 细分，法线朝 +Y。
- `BoxGeometry`：中心位于原点的盒体，X/Y/Z 尺寸和独立细分。
- `UvSphereGeometry`：Y 轴朝上的 UV 球，半径、经线和纬线细分。
- `CylinderGeometry`：Y 轴朝上的圆柱或圆台，上下半径、高度、径向/高度细分和端盖开关。

每个 part 还包含 translation、rotation、scale 和 material slot。组合时变换被烘焙到顶点，一个 part 对应一个 SubMesh。非均匀缩放使用逆转置法线矩阵；镜像变换会反转三角形绕序和 tangent handedness。

单轴细分上限是 512，总顶点上限是 4M；尺寸和变换必须有限且非退化。失败返回 `std::nullopt`，不会产生半成品 Mesh。

## 3. 运行时构建

```cpp
auto result = MeshBuilder::build(recipe);       // MeshDesc + MeshData
auto asset = MeshBuilder::buildAsset(recipe);   // 同时保留 recipe
MeshHandle handle = renderer.createProceduralMesh(recipe);
```

默认 `PositionNormalTangentUv` 是 stride 48 的交错流：POSITION Vec3、NORMAL Vec3、TANGENT Vec4、TEXCOORD0 Vec2。也可选 `Position`（stride 12）或 `PositionNormalUv`（stride 32）。

`Auto` 索引策略在最多 65536 个可寻址顶点时使用 UInt16，否则使用 UInt32；强制 UInt16 且越界会失败。

## 4. 程序化 `.mesh.json`

Importer 保留原有 raw vertex stream 格式，同时识别 `source.type = "procedural"`：

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

四元数顺序为 `[x,y,z,w]`。`box`/`cube`、`sphere`/`uv_sphere` 是等价名称。省略变换时使用单位变换。完整示例见 `assets/meshes/procedural_showcase.mesh.json`。

Importer 会构建并验证数据，把配方和烘焙结果一起写入 Artifact。运行时直接加载烘焙数据；工具可读取配方进行编辑或重建。

## 5. 版本兼容与运行时管理

Mesh payload 当前为 v3：`magic + version + optional build_recipe + description + mesh_data`。读取器兼容没有配方字段的 v2。反序列化使用临时对象，仅在完整读取和 Mesh 校验成功后提交。

`MeshAsset::instantiate()` 把配方、描述和 CPU 数据复制到运行时 `Mesh`。`Renderer::createMesh()` 继续支持原始数据，`Renderer::createProceduralMesh()` 是配方入口。Dynamic/Stream Mesh 的局部更新会递增 version 并置 dirty，GPU 缓存据此惰性重建。

## 6. 测试覆盖

`MeshTest` 覆盖四种图元、数量和包围盒、组合变换/材质槽、镜像缩放、索引升级、无效参数、配方往返和 v2 兼容。`AssetImporterTest` 同时覆盖 raw 与 procedural `.mesh.json`。
