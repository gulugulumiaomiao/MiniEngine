# Mesh 系统

Mesh 系统分为四层：源文件 `MeshAsset`、运行时 `Mesh`、实例管理 `MeshManager` 和 Vulkan 侧 `MeshGpuCache`。引擎句柄只标识运行时实例，不再直接标识 Vulkan Buffer。

## 1. 顶点布局与 CPU 数据

`VertexLayout` 由多个 `VertexBinding` 和 `VertexAttribute` 组成。

- `VertexBinding` 声明 binding 编号、单个元素的 stride，以及按顶点或按实例读取。
- `VertexAttribute` 声明 semantic、semantic index、format、shader location、binding 和 offset。
- `VertexSemantic` 使用类型加编号表示含义，例如 `Position0`、`TexCoord0`、`TexCoord3`。
- `VertexLayout::validate()` 检查重复 Binding、Location、Semantic、越界和同 Binding 内的属性重叠。
- `VertexLayout::hash()` 生成 PipelineCache 使用的稳定布局键。

`MeshData` 为每个 Binding 保存一个拥有数据所有权的 `VertexStream`。顶点结构由调用方决定，模板 `setVertexData()` 和 `setIndexData()` 会通过 `std::as_bytes` 将数组或 vector 复制成统一的字节数据。

一个 Mesh 可以使用交错布局：

```text
binding 0: [position normal uv] [position normal uv] ...
```

也可以使用分离布局：

```text
binding 0: position position ...
binding 1: normal   normal   ...
binding 2: uv       uv       ...
```

## 2. MeshAsset 与 Artifact

`MeshAsset` 直接继承 `Asset`，保存 `MeshDesc` 和 `MeshData`，并实现：

```cpp
bool transfer(Transfer& archive) override;
Mesh instantiate() const;
```

二进制 Payload 使用 `MESH` magic 和版本号，逐字段写入布局、Bounds、SubMesh、顶点流和索引数据，不依赖 C++ struct padding。反序列化先读取临时对象，完整校验成功后才替换当前资产。

Artifact 容器和 `AssetManager::loadAsset()` 已支持 `AssetType::Mesh`。

## 3. `.mesh.json` 源资产

`MeshAssetImporter` 处理 `asset://` 下以 `.mesh.json` 结尾的文件。第一版格式直接保存顶点流原始字节，能够表达任意自定义顶点结构。示例：

```json
{
  "name": "Triangle",
  "index_type": "uint16",
  "usage": "static",
  "topology": "triangle_list",
  "keep_cpu_copy": false,
  "bindings": [
    { "binding": 0, "stride": 12, "input_rate": "vertex" }
  ],
  "attributes": [
    {
      "semantic": "position",
      "semantic_index": 0,
      "format": "vec3_float32",
      "location": 0,
      "binding": 0,
      "offset": 0
    }
  ],
  "vertex_streams": [
    {
      "binding": 0,
      "vertex_count": 3,
      "bytes": [
        0, 0, 128, 191, 0, 0, 128, 191, 0, 0, 0, 0,
        0, 0, 128, 63, 0, 0, 128, 191, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 128, 63, 0, 0, 0, 0
      ]
    }
  ],
  "indices": [0, 1, 2],
  "sub_meshes": [
    {
      "first_index": 0,
      "index_count": 3,
      "vertex_offset": 0,
      "material_slot": 0
    }
  ]
}
```

`bytes` 数量必须严格等于 `stride * vertex_count`。`sub_meshes` 可省略，Importer 会生成覆盖全部索引的默认 SubMesh。第一版要求 `POSITION0` 为 `vec3_float32`，Importer 用它自动计算 Bounds。

## 4. 运行时 Mesh 与 MeshManager

`MeshAsset::instantiate()` 创建独立的运行时 `Mesh`，运行时对象不持有 `MeshAsset`。`Mesh` 保存虚拟资产路径、描述、CPU 数据、version 和 dirty 状态。

`MeshManager` 继承统一的 `InstanceManager<Mesh, MeshHandle>`，通过 `HandlePool` 和 free list 管理实例：

```cpp
MeshHandle handle = MESH_MANAGER.load(
    VirtualPath{"asset://meshes/triangle.mesh.json"});
Mesh* mesh = MESH_MANAGER.find(handle);
MESH_MANAGER.destroy(handle);
```

程序生成的 Mesh 使用 `MESH_MANAGER.insert(Mesh{desc, data})`。`Renderer::createMesh()` 和 `Renderer::loadMesh()` 对这两种入口进行了封装。

Dynamic/Stream Mesh 可以通过 `updateVertexData()` 和 `updateIndexData()` 修改局部字节。更新成功后递增 version 并设置 dirty；Static Mesh 拒绝运行时修改。资源重新导入时，MeshManager 会保持原 Handle 并替换实例，同时递增 version。

## 5. MeshGpuCache 与渲染流程

`MeshGpuCache` 位于 Vulkan 层，以完整的 `MeshHandle(index + generation)` 为键保存 GPU Vertex/Index Buffer 和已上传的 Mesh version。

```text
Scene / RenderObject
        │ MeshHandle
        ▼
MeshManager::find
        │ Mesh + VertexLayout + version
        ▼
MeshGpuCache::prepare
        │ MeshDrawInfo
        ▼
DrawList / Vulkan commands
```

- 第一次参与渲染时惰性创建 GPU Buffer。
- version 未变化时直接复用缓存。
- version 变化时安全等待并重建该 Mesh 的 GPU 数据。
- `Renderer::destroyMesh()` 先释放 GPU 缓存，再销毁运行时实例。
- Renderer 遇到失效 MeshHandle 时记录 warn 并跳过对象，不会触发 fatal。

Pipeline 创建现在显式接收当前 Mesh 的 `VertexLayout`。PipelineCache 的键已经包含所有 Binding 和 Attribute，因此不同顶点布局可以同时参与同一帧渲染，原来的单一 `activeVertexLayout` 限制已移除。

## 6. 验证

`MeshTest` 覆盖多顶点流、布局验证、二进制往返、运行时更新、事务式反序列化和 MeshManager Handle 生命周期。`AssetImporterTest` 覆盖 `.mesh.json` 导入、Artifact 读取和 MeshAsset 反序列化。Debug 与 Release 构建及完整 CTest 均用于回归验证。
