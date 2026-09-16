# 写回体系（asset/exporter）

Exporter 层是 Importer 层的写回侧对称物（对齐 Unity 的"编辑后保存回源文件"）：
CPU Asset → 源格式文件。`AssetExporter` 基类、`AssetExporterRegistry` 注册表、
`AssetExportPipeline` 单例与 [Importer.md](Importer.md) 的同名结构一一对应；
内置三个写回器：**Material**（Inspector 改属性保存回 `.material.json`）、
**Scene**（包装现有 `SceneExport`）、**Generic**（字节透传）。

```text
src/asset/importer/                          src/asset/exporter/
AssetImporter.h                    ←对称→    AssetExporter.h
AssetImporterRegistry.h/.cpp       ←对称→    AssetExporterRegistry.h/.cpp
AssetImportPipeline.h/.cpp         ←对称→    AssetExportPipeline.h/.cpp
MaterialAssetImporter.h/.cpp       ←对称→    MaterialAssetExporter.h/.cpp
DefaultAssetImporter.h/.cpp        ←对称→    GenericAssetExporter.h/.cpp
（无对应）                                   SceneAssetExporter.h/.cpp
                                             AssetPackage.h/.cpp（见 Package.md）
```

## AssetExportResult

`asset/exporter/AssetExporter.h`

**作用**：写回结果，`AssetImportResult` 的对称物：`success` / `type` /
`targetPath` / `error`，配套 `failed(type, error)` 与 `succeeded(type,
targetPath)` 工厂。

## AssetExporter

`asset/exporter/AssetExporter.h`

**作用**：所有写回器的抽象接口，三个生命周期回调：

```text
assetType()                    # 产出资产类型（路由键）
version()                      # exporter 代码版本（均为 1，项目约定）
supports(targetPath)           # 路由后的最终确认，默认要求 assets:// 有效路径
write(asset, targetPath)       # 执行写回：CPU Asset → 源格式文件（原子写入）
```

**设计意图——刻意的非对称**（与 AssetImporter 相比少的两个东西）：

- **无 `AssetExportSettings`**：导入设置参与 Artifact hash 失效判定（改设置
  触发重导入）；写回即时执行、无缓存无增量，没有可哈希的东西。
- **无 `gatherDependencies`**：导入是依赖闭包上的拓扑排序（依赖先于本资产
  就绪才能校验）；写回是单资产操作，没有依赖调度。

`write` 的输入是 CPU Asset（`MaterialAsset` / `SceneAsset` / `GenericAsset`）。
**运行时对象提取**（Material → MaterialAsset、Scene → SceneAsset）是各 exporter
模块的自由函数（`exportMaterialToAsset`，对称 scene 模块已有的
`exportSceneToAsset`），不在基类接口上——提取需要类型特有的上下文（Shader
声明、句柄查找），让它留在类型模块比塞进虚接口更诚实。

## AssetExporterRegistry

`asset/exporter/AssetExporterRegistry.h` / `AssetExporterRegistry.cpp`

**作用**：写回器的注册与查找，单表 `AssetType → AssetExporter`。

**运转流程**：`registerExporter` 校验非空且类型非 Unknown，同一类型重复注册
拒绝；`find(type)` 返回注册的写回器或 `nullptr`。

**设计意图**：与导入注册表相比**没有 Scripted 扩展名表**——ScriptedImporter
解决"新扩展名源文件接管导入"，而写回类型集合封闭于引擎内可编辑类型
（Material/Scene/Generic），无对应需求。`find(Shader)` 返回空即明确报错：
Shader/Mesh/Texture 无引擎内编辑工作流（手写 ShaderLab/GLSL、外部工具产
Mesh/Texture），写回无意义。

## AssetExportPipeline

`asset/exporter/AssetExportPipeline.h` / `AssetExportPipeline.cpp`

**作用**：写回管线的单例入口（`ASSET_EXPORT_PIPELINE`），按 `asset.type()`
路由到注册的 exporter；`initialize()` 注册 Material/Scene/Generic 三个内置
写回器。

**运转流程**：

- `exportAsset(asset, targetPath, error)`：路由 → 无 exporter 报
  "No exporter registered for type X" → `supports` 不符报 "Unsupported
  export target" → `write` 执行并回传错误。
- `saveMaterial(material, targetPath, error)`：Material 便捷入口——运行时
  对象先经 `exportMaterialToAsset` 提取，再走 `exportAsset` 路由。
- 挂载点：`AssetManager::initialize` 的 Development 分支，紧随
  `ASSET_IMPORT_PIPELINE.scanAll()` 之后；`AssetManager::shutdown` 中与
  import pipeline 相邻清理。Packaged 分支不初始化（写回对只读包无意义，
  对称导入侧行为）。

**设计意图**：写回后**不主动重导入**——交给 FileWatcher / 调用方（编辑器
模式下 FileWatcher 自动触发级联重导入，显式触发留给调用方）。管线只做路由
与前置校验，写回逻辑全部在 exporter 内。

## 内置写回器总览

| Exporter | 类型 | version | write 实现 | 运行时提取 |
|---|---|---|---|---|
| `MaterialAssetExporter` | Material | 1 | `format::writeMaterialAssetJson` → `writeTextAtomic` | `exportMaterialToAsset` |
| `SceneAssetExporter` | Scene | 1 | 复用 `writeSceneAssetJson` → `writeTextAtomic` | scene 模块 `exportSceneToAsset` |
| `GenericAssetExporter` | Generic | 1 | `GenericAsset.data` → `writeBinaryAtomic` | 无需（GenericAsset 本身是 Asset） |

### MaterialAssetExporter

**运转流程**：`supports` 要求 `.material.json` 扩展名 + assets://；
`write` 将输入 `dynamic_cast<const MaterialAsset*>` 后调
`format::writeMaterialAssetJson(asset, ASSET_DATABASE)` 编码（见
[SourceFormats.md](SourceFormats.md)）并原子写入。

`exportMaterialToAsset` 提取规则：

- `shader` = `SHADER_MANAGER.find(material.shaderHandle())` 取
  `Shader::assetPath()`；句柄失效或路径无效即失败。
- `properties` 按 `Shader::properties()` 声明遍历，按类型读值：
  Float/Range→`getFloat`、Boolean→`getBool`、Vec2/3→`getVec2/3`、
  Vec4/Color→`getVec4`、Texture2D→纹理槽查找（**空纹理槽省略该属性**，
  保持源文件省略语义）。
- `keywords` 全量；`renderQueue` 只取 override（`Material::renderQueueOverride()`
  返回 `std::optional<int>`，nullopt 省略字段——写回生效值会把 shader 默认
  显式化，破坏源文件省略语义）。

### SceneAssetExporter

**运转流程**：`supports` 要求 `.scene.json`；`write` 将输入
`dynamic_cast<const SceneAsset*>` 后调 `format::writeSceneAssetJson(asset,
ASSET_DATABASE)` 编码（见 [SourceFormats.md](SourceFormats.md)）并原子写入。
运行时提取沿用 scene 模块的 `exportSceneToAsset`。
编辑器 `SceneDocument::save` 通过 `exportSceneToAsset` 提取后再走
`ASSET_EXPORT_PIPELINE.exportAsset`，与程序化写回共用同一套路由。

### GenericAssetExporter

**运转流程**：`supports` 拒绝 `.meta`（Meta 永远不是资产本体）；`write` 把
`GenericAsset.data` 字节 `writeBinaryAtomic` 透传（对称 DefaultAssetImporter
的读透传）。

## 测试覆盖

`tests/AssetExporterTest.cpp`（gtest）：注册表路由、Material 运行时往返
（改属性 → 保存 → 逐字段等值）、GUID 引用写出、可选字段省略、renderQueue
override 保留、失败路径（非法目标 / 不可写回类型）、Scene 与 Generic 写回。
