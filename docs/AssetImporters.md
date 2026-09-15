# Asset Importer

Importer 负责把源资产验证并写成 Artifact，不负责更新 AssetDatabase，也不创建运行时对象。数据库更新、依赖顺序和失败状态由 `AssetImportPipeline` 统一处理。

## 接口

```text
AssetImportContext
  ├─ AssetMeta
  ├─ 源文件 VirtualPath
  ├─ Meta VirtualPath
  ├─ Artifact VirtualPath
  └─ searchPaths（解析相对引用的搜索上下文）

AssetImportResult
  ├─ success / AssetType
  ├─ Artifact VirtualPath
  ├─ VirtualPath 依赖列表
  └─ 错误信息

AssetImporter
  ├─ assetType() / version()
  ├─ supports(sourcePath)          # 默认返回 true
  ├─ createDefaultSettings(path)   # 类型化的导入设置
  ├─ gatherDependencies(ctx, settings) # 声明依赖，不读依赖 Artifact
  └─ import(ctx, settings)         # 源文件 → CPU Asset → Artifact
```

`AssetImportContext` 不携带数据库引用。Importer 必须查询已导入资产时，通过全局 `ASSET_DATABASE` 访问。

导入设置（`AssetImportSettings`）是可序列化、可哈希、可克隆的类型化对象，每个
Importer 提供自己的默认设置（如 `TextureImportSettings.generateMipmaps`）；设置哈希参与
Artifact 失效判定。依赖收集（`gatherDependencies`）与执行导入拆分：importer 只声明依赖
源资产列表，由管线保证依赖先导入，避免读取尚未生成的 Artifact。

## 注册规则与路由

内置 Importer 以 `AssetType` 为唯一键注册在 `AssetImporterRegistry`。Meta 不记录
Importer 名称；管线按照路由结果选取 Importer。当前内置：

- `ShaderAssetImporter`
- `MaterialAssetImporter`
- `MeshAssetImporter`
- `TextureAssetImporter`
- `SceneAssetImporter`

重复注册同一种类型会记录 error 并拒绝覆盖。

源文件的路由优先级：

1. **ScriptedImporter**：按源文件扩展名（小写归一，支持 `.shader.json` 等多级后缀，
   更长（更具体）的注册优先）接管路由；
2. **内置类型推断**：`inferAssetType` 按扩展名映射到内置 Importer；
3. **DefaultImporter**：前两者都没有命中时，显式 `importAsset` 调用透传导入为
   `Generic` 资产。`scanAll` 与文件监听不做兑底，中间文件（`.vert`/`.frag`/`.meta`）
   不会被被动扫描进资产库。

路由结果决定 Meta 里的 `asset_type`（`createAssetMeta(path, type)` 显式重载）；
路由类型与已有 Meta 不一致（如 ScriptedImporter 刚接管了该扩展名）时会重新生成
Meta——GUID 由路径确定性派生，资产身份保持稳定。

## ScriptedImporter 与 DefaultImporter

`ScriptedImporter`（`src/asset/importer/ScriptedImporter.h`）是用户/编辑器自定义
导入器的注册点，对齐 Unity 的同名概念：派生类声明 `sourceExtension()`（含前导点，
如 `.obj`）、`outputExtension()` 与目标 `assetType()`，经
`ASSET_IMPORT_PIPELINE.registerScriptedImporter(...)` 注册；同一扩展名仅允许一个。
`supports()` 默认按扩展名后缀匹配，可覆盖以支持按文件头区分内容。

`DefaultAssetImporter` 是管线内置的兑底透传：没有专用 Importer 的源文件按
`Generic` 类型导入，源字节原样进 Artifact（`GenericAsset`），GUID、settingsHash、
dependencyHashes 照常记录，因此编辑器与打包管线可以按 GUID 稳定引用任意资产。
`.meta` 伴生文件永远被拒绝。运行时通过 `ASSET_MANAGER.loadAsset<GenericAsset>`
取回字节。

## 源格式模块（asset/format）

`.shader.json` / `.material.json` / `.scene.json` 的源格式解析与验证位于独立模块
`src/asset/format/`（`ShaderAssetFormat` / `MaterialAssetFormat` /
`SceneAssetFormat`，命名空间 `engine::format`），共享 JSON 基元
`AssetFormatJson.h`。Importer 只负责编排：读文件、调用 format 解析、写 Artifact；
运行时模块（`Shader.cpp` / `Material.cpp` / `SceneAsset.cpp`）只保留二进制 transfer
与运行时行为，不再包含源文件解析。未来的 Exporter 同样复用这些 format 单元。

## Shader 导入

Shader Importer 只解析并验证 ShaderLab JSON、生成 Shader Artifact，不扫描 include，
也不运行 glslc。阶段源码和递归 include 由 `ShaderPreprocessor` 在实际编译时读取并记录到
全局 `FileDependencyGraph`。

依赖收集器分别维护 `visiting` 和 `visited`：前者检测当前递归栈中的循环，后者用于去重。格式错误、文件缺失、循环 include 或写 Artifact 失败都会记录 `Log::error` 并返回失败，不会调用 fatal。

Shader 导入输出类型专用的二进制 Artifact，不运行 glslc。SPIR-V、Reflection、ShaderProgram 和 Pipeline 在 Shader 真正参与绘制时按需生成。Material 同样使用独立的二进制序列化格式；运行时不再从 Artifact 二次解析 JSON。

## 依赖驱动调度

`AssetImportPipeline` 对任何资产通用执行：先调用 Importer 的 `gatherDependencies`
拿到声明依赖，递归导入每个依赖（后序遍历，叶子资产最先落盘），再执行本资产的
`import`。入口的 `importing_` 集合同时拦截依赖声明中的循环依赖：重复进入的路径记
录 "Cyclic asset dependency" 并使整条导入链失败。任一依赖导入失败时，本资产直接标
记 Failed，错误信息为 `Dependency import failed: <path>`，不会执行 import 阶段。

当前内置类型的源格式校验（Scene 组件扩展名、Material 的 Shader 类型检查）天然
阻止资产级循环；循环防护同样覆盖 ScriptedImporter 声明的依赖。

## Material 导入

调度器先导入 Material 声明的 Shader 与实际引用的 Texture。Material Importer 随后从
Shader Artifact 读取属性声明，校验 Material 的 Properties 和 Keywords，并输出
Material Artifact。Material 依赖列表包含 Shader 与 Texture 的 `assets://` 路径，
因此它们变化时会通过 AssetDatabase 的反向依赖触发 Material 重导入。

验证调度行为的测试是 `AssetDependencySchedulerTest`（gtest）。

完整导入与热重载流程见 [AssetPipeline.md](AssetPipeline.md)。
