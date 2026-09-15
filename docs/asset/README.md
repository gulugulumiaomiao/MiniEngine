# 资产系统

MiniEngine 的资产系统负责"源文件 → 可寻址资产 → 运行时对象"的全链路：源资产以
`assets://` 虚拟路径存放，导入管线把它们转换成 `library://` 下的二进制 Artifact，
AssetManager 在运行时按需加载，领域 Manager 再实例化为运行时句柄与 GPU 资源。

本文是文档导航与总览；每个类的作用、运转流程和设计意图见子文档。

## 设计原则

- **GUID 引用**：资产间引用写 `guid://<AssetId>`（与 Unity 一致），路径只是内部
  兼容形态；GUID 由路径确定性派生，见 [Foundation.md](Foundation.md)。
- **派生数据可重建**：`library/`（数据库 + Artifact）整体是缓存，删除后可由源
  文件与 `.meta` 完整重建。所有版本号保持 1，不做旧格式兼容：旧记录在加载时
  直接丢弃，由下一次导入重新生成。
- **源格式与运行时分离**：`.shader.json` 等源格式解析集中在 `asset/format`，
  运行时类只保留二进制 transfer，见 [SourceFormats.md](SourceFormats.md)。
- **依赖驱动调度**：importer 只声明依赖（`gatherDependencies`），导入顺序由管线
  统一调度，所有资产类型共用同一套代码，见 [Pipeline.md](Pipeline.md)。
- **增量导入**：源 / Meta / ImportSettings / 依赖源的哈希快照任一变化才重导入，
  见 [Storage.md](Storage.md)。
- **导入即纯转换**：Importer 把源文件验证并写成 Artifact，不更新数据库、不创建
  运行时对象；数据库更新、依赖顺序和失败状态由 `AssetImportPipeline` 统一处理。

## 模块地图

```text
src/asset/
├── base/            身份与引用：AssetId、AssetType、Asset、AssetMeta、
│                    AssetReference、GuidResolver、GenericAsset
├── database/        AssetDatabase / AssetRecord（导入记录、GUID 索引、依赖图）
├── derived_data/    AssetArtifact（MART 二进制信封）
├── format/          源格式解析与验证（Shader/Material/Scene AssetFormat）
├── importer/        AssetImporter 接口、内置与 Scripted/Default 导入器、
│                    AssetImporterRegistry、AssetImportPipeline、FileWatcher
└── manager/         AssetManager（运行时加载与弱缓存）
```

## 端到端数据流

```text
assets:// 源文件（+ .meta 侧车）
  -> AssetImportPipeline（路由 -> 依赖调度 -> import）
  -> library://artifacts/<AssetId>/asset.bin（MART 信封）
  -> AssetDatabase（记录、哈希快照、依赖/反向依赖）
  -> AssetManager（weak_ptr<Asset> 缓存）
  -> ShaderManager / MeshManager / ...（KeyedHandleRegistry -> Handle）
  -> GPU 资源（Shader 编译、Texture 上传等按需执行）
```

## 文档导航

| 文档 | 内容 |
|---|---|
| [Foundation.md](Foundation.md) | 资产身份与引用：AssetId / AssetType / Asset / AssetMeta / AssetReference / GuidResolver / GenericAsset |
| [Storage.md](Storage.md) | 资产存储：AssetRecord / AssetDatabase / AssetArtifact |
| [SourceFormats.md](SourceFormats.md) | 源格式模块 asset/format |
| [Importer.md](Importer.md) | AssetImporter 接口体系、注册表、内置与扩展导入器 |
| [Pipeline.md](Pipeline.md) | AssetImportPipeline / FileWatcher：路由、调度、增量、热重载 |
| [Manager.md](Manager.md) | AssetManager：运行时加载、缓存与生命周期 |

相关文档：[FileSystem.md](../FileSystem.md)（虚拟路径与挂载）、
[ShaderMaterialPipeline.md](../ShaderMaterialPipeline.md)（Shader 编译链路）、
[SceneAsset.md](../SceneAsset.md)、[MaterialSystem.md](../MaterialSystem.md)、
[ShaderLabJson.md](../ShaderLabJson.md)。

## 测试覆盖

| 测试 | 覆盖 |
|---|---|
| `AssetFoundationTest` | Meta / AssetId / 数据库记录序列化往返 |
| `AssetReferenceTest` | AssetReference 双形态解析与 GUID 校验（gtest） |
| `AssetImporterTest` | 五个内置 importer：解析、设置哈希、依赖收集 |
| `AssetDependencySchedulerTest` | 依赖驱动调度与失败级联（gtest） |
| `AssetReimportDecisionTest` | settingsHash / dependencyHashes 快照、旧记录丢弃（gtest） |
| `AssetScriptedImporterTest` | ScriptedImporter 注册与路由、DefaultImporter 透传（gtest） |
| `AssetPipelineTest` | 管线集成：Meta、反向依赖、热重载、失败回退 |
| `AssetReleaseTest` | Publish 打包后的只读加载 |

测试环境约定见 [test/AssetEnvironment.md](../test/AssetEnvironment.md)。
