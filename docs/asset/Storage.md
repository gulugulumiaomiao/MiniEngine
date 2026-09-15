# 资产存储：AssetDatabase 与 AssetArtifact

导入产物分两层：`AssetDatabase`（`src/asset/database/`）记录每个资产的导入状态
与哈希快照；`AssetArtifact`（`src/asset/derived_data/`）是 Artifact 文件的二进制
信封。两者都存放在 `library://` 下，整体属于可删除重建的派生数据。

## AssetRecord 与 AssetImportStatus

`asset/database/AssetDatabase.h`

**作用**：一条资产的完整导入记录，是增量判定（见
[Pipeline.md](Pipeline.md)）与依赖级联重导入的数据源。

| 字段 | 作用 |
|---|---|
| `id` / `type` | 资产身份与类型 |
| `sourcePath` / `metaPath` / `artifactPath` | 源文件、Meta 侧车、Artifact 三者的虚拟路径 |
| `importerVersion` | 产出该 Artifact 的 Importer 代码版本 |
| `sourceHash` / `metaHash` / `artifactHash` | 三份文件的当前哈希 |
| `settingsHash` | ImportSettings 的 hash 快照 |
| `dependencies` / `dependencyHashes` | 依赖源路径列表 + 一一对应的依赖源哈希快照 |
| `status` | `NotImported` / `Imported` / `Failed` / `Missing` |
| `lastError` | 最近一次失败原因 |

**设计意图**：`settingsHash` 与 `dependencyHashes` 是可靠重导入触发的关键快照——
导入设置变化（如 `TextureImportSettings.generateMipmaps` 切换）或任一依赖源文件
变化（改 Shader 必重导 Material）都会强制重导入。快照缺失或长度与依赖列表不符
（旧格式记录）同样视为失效：触发一次重导入后记录自愈为完整形态。

## AssetDatabase

`asset/database/AssetDatabase.h` / `AssetDatabase.cpp`，单例宏 `ASSET_DATABASE`

**作用**：Meyers 单例，实现 `GuidResolver` 接口。内存中持有
`AssetId → AssetRecord` 主表与 `path → AssetId` 路径索引，持久化为
`library://AssetDatabase.json`。

**运转流程**：

- 生命周期：`initialize()` 要求先挂载可写的 `library://`；`load()` 从 JSON
  读取记录并重建索引；`save()` 以原子替换写回；`shutdown()` / `clear()` 清空。
- 查询：`findById(id)` / `findByPath(path)`；`assetIdFromPath(path)` /
  `pathFromAssetId(id)` 即 `GuidResolver::findGuid` / `findPath` 的实现，供
  `AssetReference::resolve` 使用。
- 依赖图：`dependenciesOf(path)` 返回记录声明的依赖；`dependentsOf(path)` 从
  全部记录反推引用者，支撑文件变化后的级联重导入。
- Artifact 布局：`artifactPath(id)` =
  `library://artifacts/<AssetId>/asset.bin`；`artifactDirectory` /
  `prepareArtifactDirectory` 负责目录创建。
- 更新：`addOrUpdate(record)` 插入或替换并维护索引；`remove(path)` 删除记录。

**设计意图**：

- 数据库是可重建缓存而非事实来源——`.meta` 侧车才是 AssetId 的持久化锚点。
  JSON `version` 保持 1，不写兼容代码：缺 `settings_hash` / `dependency_hashes`
  字段的旧记录在 `load()` 时整条丢弃并打 warn 日志，由下一次导入重新生成。
- 依赖只保存规范化虚拟路径，与源文件内部的 `guid://` 引用同构（导入时统一解析
  成路径落库），反向索引因此可以直接建立。
- 读写全部经 `std::mutex` 保护；查询返回记录副本，调用方不持锁使用。

## AssetArtifact

`asset/derived_data/AssetArtifact.h` / `AssetArtifact.cpp`

**作用**：Artifact 文件的通用二进制信封。`AssetArtifact` 结构体携带
`version`（资产自身版本）、`assetId`、`assetType`、`sourcePath`、`payload`；
`serializeAssetArtifact` / `parseAssetArtifact` / `saveAssetArtifact` /
`loadAssetArtifact` 四个自由函数负责编解码与落盘。

**运转流程**：信封固定小端布局：

```text
MART magic | container version(2) | asset type | asset version
AssetId high/low | source virtual path | payload bytes
```

`parseAssetArtifact` 校验 magic、容器版本、类型白名单（六种资产类型）、AssetId
有效性与字节边界（`reader.finished()`），任一不符返回空。写入方统一走
`writeAssetArtifact` helper（见 [Importer.md](Importer.md)），读取方是
AssetManager。

**设计意图**：信封与 payload 分层——信封只负责身份、类型与边界校验，payload
格式由各资产类型自定义并维护独立版本（Shader / Material / Scene 的 payload 自带
`SHDR` / `MATL` / `SCNE` magic），容器格式演进（version 1 → 2）与单资产格式演进
互不影响。AssetManager 读信封后按 `assetType` 分派反序列化，运行时不再碰源
JSON。
