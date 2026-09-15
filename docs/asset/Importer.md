# 导入器体系（asset/importer）

Importer 层负责"源文件 → CPU Asset → Artifact"的纯转换：读源文件、调用
format 解析（见 [SourceFormats.md](SourceFormats.md)）、把 Asset 序列化成
Artifact 落盘。Importer 不更新 AssetDatabase、不创建运行时对象、不编排依赖
顺序——这些由 [Pipeline.md](Pipeline.md) 的调度器统一处理。

## AssetImportSettings 与 AssetImportSettingsBase

`asset/importer/AssetImporter.h`

**作用**：类型化的导入设置基类（Unity ImportSettings 的对位物），继承
`Transferable`。纯虚 `hash()` 与 `clone()`；CRTP 模板
`AssetImportSettingsBase<Derived>` 为可拷贝构造的派生类自动实现 `clone()`。

**运转流程**：每个 importer 在 `createDefaultSettings(path)` 里返回自己的默认
设置（设置内建在 importer 中，手写设置文件是可选的）。管线取当前设置的
`hash()` 写入 `AssetRecord::settingsHash`，与快照不一致即强制重导入（见
[Storage.md](Storage.md)）。

**设计意图**：设置可序列化（未来可持久化到 `.meta` 或独立文件）、可哈希（参与
Artifact 失效判定）、可克隆（管线与 UI 各持一份互不干扰）。当前唯一有真实语义
的设置是 `TextureImportSettings.generateMipmaps`；Shader/Mesh/Generic 的设置类
为空实现，是后续扩展点（如 include 扫描开关、scaleFactor）。

## AssetImportContext 与 AssetImportResult

`asset/importer/AssetImporter.h`

**作用**：`AssetImportContext` 是单次导入的上下文：`meta`（GUID 与类型）、
`sourcePath` / `metaPath` / `artifactPath`、`searchPaths`（解析相对引用的搜索
上下文）。`AssetImportResult` 是导入结果：`success` / `type` / `artifactPath` /
`dependencies` / `error`，配套 `failed(type, error)` 与
`succeeded(type, artifactPath, dependencies)` 工厂。

**设计意图**：Context 不携带数据库引用——importer 必须查询已导入资产时通过
全局 `ASSET_DATABASE`（依赖先导入由管线保证，此时查询必然命中）。Result 显式
携带依赖列表，成功后由管线写入记录并生成依赖哈希快照。

## AssetImporter

`asset/importer/AssetImporter.h` / `AssetImporter.cpp`

**作用**：所有导入器的抽象接口，五个生命周期回调：

```text
assetType()                       # 产出资产类型（路由键）
version()                         # importer 代码版本（格式演进触发重导入）
supports(sourcePath)              # 路由后的最终确认，默认 true
createDefaultSettings(path)       # 类型化的默认导入设置
gatherDependencies(ctx, settings) # 声明依赖（此时依赖尚未导入）
import(ctx, settings)             # 执行转换并落盘 Artifact
```

**设计意图**：把 importer 从"单个函数"扩展为"可配置、可扩展、可声明依赖"的
管线单元。`gatherDependencies` 与 `import` 拆分是关键：importer 在收集阶段只解析
源文件里声明的引用、**不得读取依赖的 Artifact**（依赖可能尚未生成），由管线保证
依赖先于本资产导入——这使得依赖调度完全通用，不需要按类型写特殊分支。

## writeAssetArtifact

`asset/importer/AssetImportHelpers.h` / `AssetImportHelpers.cpp`

**作用**：所有 importer 共用的落盘 helper：`BinaryWriter` 序列化 Asset →
封装 `AssetArtifact`（MART 信封）→ 原子写入 `context.artifactPath` → 依赖列表
排序去重 → 返回 `AssetImportResult`。

**设计意图**：压缩每个 importer 重复的"序列化 + 信封 + 原子写 + 依赖规整"样板
（Phase 1 成果）。失败路径统一返回 `AssetImportResult::failed` 并以资产类型名打
日志。

## AssetImporterRegistry

`asset/importer/AssetImporterRegistry.h` / `AssetImporterRegistry.cpp`

**作用**：导入器的注册与查找，内部两张表：

- `importers_`：`AssetType → AssetImporter`，内置路由（一个类型一个 importer）。
- `scriptedImporters_`：`扩展名 → ScriptedImporter`，扩展名接管路由。

**运转流程**：

- `registerImporter(importer)`：类型重复注册记 error 并拒绝。
- `registerScriptedImporter(importer)`：校验扩展名（小写归一、前导点、至少
  2 字符）；同一扩展名仅允许一个，重复注册拒绝。
- `find(type)`：内置查找。`findScripted(path)`：扩展名匹配查找——大小写不
  敏感的后缀匹配（`lowercaseExtension` + `sourceExtensionMatches`），支持
  `.shader.json` 等多级后缀；多个注册命中时更长（更具体）的扩展名优先。

**设计意图**：扩展名注册表让编辑器/用户脚本可以"接管"任意扩展名的导入（如
`.obj` → Mesh），而不必修改内置类型表。多级后缀 + 最长匹配保证 `.json` 与
`.shader.json` 可以同时被不同脚本接管且语义正确。

## ScriptedImporter

`asset/importer/ScriptedImporter.h` / `ScriptedImporter.cpp`

**作用**：用户 / 编辑器自定义导入器的抽象基类（对齐 Unity 同名概念）。派生类
声明 `sourceExtension()`（含前导点，如 `".obj"`）、`outputExtension()`（产出
资产的目标扩展名，透传导入器返回源扩展名本身，供编辑器 UI 与未来 exporter
使用）与目标 `assetType()`；其余五个生命周期方法按普通 importer 实现。

**运转流程**：经 `ASSET_IMPORT_PIPELINE.registerScriptedImporter(...)` 注册（需
在管线 `initialize()` 之后，随 `shutdown()` 一并清除）。`supports()` 默认按
扩展名后缀匹配，可覆盖以支持按文件头区分内容。

**设计意图**：与内置 importer 按 `AssetType` 路由不同，ScriptedImporter 按
扩展名路由——一个扩展名可以接管为任意目标类型（`.obj` → Mesh，或声明
`Generic` 做转换透传）。路由优先级高于内置推断，见 [Pipeline.md](Pipeline.md)。

## DefaultAssetImporter 与 GenericImportSettings

`asset/importer/DefaultAssetImporter.h` / `DefaultAssetImporter.cpp`

**作用**：管线内置的兜底透传导入器（对齐 Unity DefaultImporter），不进注册表。
没有专用 Importer 的源文件按 `Generic` 类型导入：`readBinary` 读源字节 →
`GenericAsset.data` → `writeAssetArtifact`。`supports()` 只拒绝 `.meta`——Meta
是导入管线的伴生文件，永远不是资产本体。

**运转流程**：只在显式 `importAsset` / 依赖声明路由到它时执行；`scanAll` 与
文件监听不做兜底，因此中间文件（`.vert` / `.frag` / `.meta`）不会被被动扫描
进资产库。运行时用 `ASSET_MANAGER.loadAsset<GenericAsset>` 取回字节。

**设计意图**：让音效、配置、文本等任意文件享受完整资产待遇——GUID、Meta、
settingsHash（`GenericImportSettings`，当前为空设置）、dependencyHashes、
Artifact 信封照常记录，编辑器与打包管线可以按 GUID 稳定引用一切。

## BuiltinAssetImporters

`asset/importer/BuiltinAssetImporters.h` / `BuiltinAssetImporters.cpp`

**作用**：管线初始化时注册五个内置导入器的聚合入口（`Shader / Material /
Mesh / Texture / Scene`），任一注册失败则初始化失败。

## 内置导入器总览

| Importer | 类型 | version | 设置 | 声明依赖 |
|---|---|---|---|---|
| `ShaderAssetImporter` | Shader | 3 | 空 | 无 |
| `MaterialAssetImporter` | Material | 4 | 空 | Shader + 引用的 Texture |
| `MeshAssetImporter` | Mesh | 2 | 空 | 无 |
| `TextureAssetImporter` | Texture | 1 | `generateMipmaps` | 无 |
| `SceneAssetImporter` | Scene | 2 | 空 | Mesh + Material |
| `DefaultAssetImporter` | Generic | 1 | 空 | 无 |

### ShaderAssetImporter

**运转流程**：读源文本 → `format::parseShaderAsset` 解析 ShaderLab JSON →
`writeAssetArtifact`。不扫描 include、不运行 glslc（编译链路见
[ShaderCompilePipeline.md](../ShaderCompilePipeline.md)）。
`gatherDependencies` 返回空：GLSL include 由 `ShaderPreprocessor` 在运行时追踪
到 `FileDependencyGraph`，不属于资产级依赖。

### MaterialAssetImporter

**运转流程**：`gatherDependencies` 走共享的 `collectMaterialDependencies`：解析
material 源得到 shader 引用 → 解析该 shader 源 → 遍历其中 `Texture2D` 属性，
取 material 的 override 值、缺省取 shader 的 defaultValue → 校验引用是 Texture
类型 → 返回去重的 `{shader 路径, texture 路径...}`。`import` 阶段读已导入的
Shader Artifact 拿属性声明，`format::validateMaterialAsset` 校验 Properties /
Keywords 后落盘 Material Artifact。

**设计意图**：依赖收集在源文件层面完成（读的是声明，不是 Artifact），符合
`gatherDependencies` 契约；Material 的属性校验必须等 Shader Artifact 就绪，因此
放在 `import` 阶段。

### TextureAssetImporter

**运转流程**：`TextureImportSettings.generateMipmaps`（默认 true）控制 PNG/JPG
解码后是否生成完整 Mip 链（`decodeImage(source, generateMipmaps)`，false 时仅
保留 base mip）；KTX/KTX2 容器自带 Mip 数据，直接透传不受此项影响。`import`
中 `dynamic_cast` 校验 settings 类型。

**设计意图**：首个真实生效的导入设置，验证 settingsHash 失效链路——切换
`generateMipmaps` 即触发该 Texture 重导入。

### MeshAssetImporter

**运转流程**：解析 `.mesh.json` 的 raw 顶点/索引流或程序化几何配方（Plane /
Box / UvSphere / Cylinder 等），构建 `MeshAsset`（含子网格、包围体）后落盘。
依赖为空（配方参数自包含）。

### SceneAssetImporter

**运转流程**：`gatherDependencies` 与 `import` 共享 `collectSceneDependencies`：
解析场景（`format::parseSceneAsset`）→ 遍历节点组件，收集 MeshComponent 引用的
mesh 与 MaterialComponent 引用的 materials（去重）。`import` 阶段结构校验后
落盘 Scene Artifact。

**设计意图**：场景是依赖链的顶端（Scene → Material → Shader/Texture、
Scene → Mesh），依赖收集复用解析结果避免读两遍源文件。
