# 资产基础设施

这一阶段建立资产系统的前 19 步基础能力，不包含 Importer、FileWatcher、
AssetImportPipeline，也暂时不改变 AssetManager 从源 JSON 加载 Shader 和 Material
的现有行为。

## 分层

```text
FileSystem
  └─ VirtualPath 和挂载目录的原始文件操作

AssetMeta
  └─ 源资产旁的稳定 AssetId 与 AssetType

AssetDatabase
  ├─ AssetId 到 AssetRecord
  ├─ VirtualPath 到 AssetId
  └─ 依赖路径到反向依赖资产

AssetArtifact
  └─ library:// 下的第一版 JSON 导入产物封装
```

资产之间只引用 `VirtualPath`。`AssetId` 仅作为数据库主键、Artifact 目录名和
以后热重载使用的内部稳定身份。

## Asset 和 AssetId

`ShaderAsset` 与 `MaterialAsset` 已继承 `Asset`。公共基类持有 `AssetId` 和
`assetPath`，派生类型通过 `type()` 返回 `Shader` 或 `Material`。

当前源 JSON 加载尚未接入 Meta，因此过渡阶段解析出的 Asset 允许 AssetId 无效；
Importer 接入后会使用 Meta 为它设置完整身份。

`AssetId` 是 128 位值，文本格式使用 UUID 形式：

```text
3d246ca4-c46f-4d6b-81e7-94e923731c65
```

它不是路径哈希。移动源文件并同时移动 Meta 时，AssetId 不变。

## Meta

只有 ShaderLab 和 Material JSON 是第一版资产，GLSL 阶段源码只是 Shader 的文件
依赖，不单独生成 Meta。

```text
vertex_color.shader.json
vertex_color.shader.json.meta
```

Meta 格式：

```json
{
  "version": 1,
  "asset_id": "3d246ca4-c46f-4d6b-81e7-94e923731c65",
  "asset_type": "Shader"
}
```

Meta 不记录 Importer。后续导入管线将根据 `asset_type` 选择对应 Importer。没有
Meta 时，仅用 `.shader.json` 和 `.material.json` 推断初始类型并创建 Meta。

## AssetDatabase

`AssetDatabase` 是 Meyers Singleton，通过 `ASSET_DATABASE` 使用。调用
`initialize()` 前必须先挂载可写的 `library://`：

```cpp
FILE_SYSTEM.mountDirectory("library", libraryRoot, false);
ASSET_DATABASE.initialize();
```

数据库保存在：

```text
library://AssetDatabase.json
```

`AssetRecord` 保存源路径、Meta 路径、Artifact 路径、版本、哈希、依赖、导入状态
和最后错误。依赖与资产文件内部引用一样，只保存规范化的虚拟路径。

数据库写入使用原子替换。它属于可重建缓存；Meta 才是 AssetId 的持久化来源。

## Artifact

目录约定为：

```text
library://artifacts/<AssetId>/asset.json
```

第一版 Artifact 是 JSON 信封：

```json
{
  "version": 1,
  "asset_id": "3d246ca4-c46f-4d6b-81e7-94e923731c65",
  "asset_type": "Shader",
  "source_path": "asset://shaders/vertex_color.shader.json",
  "payload": {}
}
```

`payload` 留给后续 `ShaderAssetImporter` 和 `MaterialAssetImporter`。格式稳定后可以
把 payload 改为二进制，而不改变 AssetDatabase 的索引关系和目录布局。

`library/` 已加入 `.gitignore`，可以由源资产和 Meta 完整重建。

## FileSystem 扩展

新增接口包括：

- `stat`
- `createDirectories`
- `removeFile`
- 同一挂载内的 `move`
- `writeTextAtomic` / `writeBinaryAtomic`
- 物理路径到最长匹配挂载点的 `toVirtualPath`

写操作仍服从挂载点的只读属性。Asset 源目录在编辑器导入阶段需要生成 Meta 时应
以可写方式挂载；打包后的 Release 运行时可以将其挂为只读。
