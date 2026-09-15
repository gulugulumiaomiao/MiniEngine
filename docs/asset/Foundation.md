# 资产身份与引用（asset/base）

`src/asset/base/` 定义资产系统的身份体系：128 位 AssetId、类型枚举、源文件旁的
Meta 侧车，以及 GUID / 路径双形态的跨资产引用。这一层不依赖导入管线与运行时，
被 format、importer、manager 各层共同使用。

## AssetId

`asset/base/AssetId.h` / `AssetId.cpp`

**作用**：128 位资产全局唯一标识（`uint64_t high/low`），文本形式为 UUID 样式：

```text
3d246ca4-c46f-4d6b-81e7-94e923731c65
```

它是数据库主键、Artifact 目录名（`library://artifacts/<AssetId>/`）和
`guid://` 引用的载荷。

**运转流程**：

- `AssetId::fromPath(path)`：由规范化 VirtualPath 确定性派生——FNV-1a 64 两轮
  哈希（第二轮以第一轮结果为种子）+ `mixHash64` 混合，再置 RFC 4122
  version/variant 位。同一路径永远得到同一 GUID。
- `AssetId::generate()`：单调序列种子 + `mixHash64` 的随机生成；当前主流程不
  使用，保留给未来"随机 GUID 存 `.meta`"的备选方案。
- `parse(text)` / `toString()`：36 字符 UUID 文本与二进制互转；`operator==` 与
  `std::hash` 特化支持作为容器键。

**设计意图**：确定性派生让源文件之间可以用 `guid://` 互相引用而不必先落
`.meta` 侧车（Meta 只是对派生结果的持久化确认）；也让"路由类型变化重建 Meta"
（见 [Pipeline.md](Pipeline.md)）时资产身份保持稳定。version/variant 位仅让
文本形式看起来像标准 UUID，AssetId 本身是引擎私有的不透明标识。

## AssetType 与 Asset

`asset/base/Asset.h` / `Asset.cpp`

**作用**：`AssetType` 枚举（`Shader` / `Material` / `Mesh` / `Scene` /
`Texture` / `Generic` / `Unknown`）是整个系统的路由键：数据库记录、Meta 侧车、
Artifact 信封、AssetManager 反序列化分派都由它驱动。
`assetTypeName()` / `assetTypeFromName()` 负责 JSON 文本映射。`Asset` 是所有
可序列化资产的抽象基类，持有 `assetId_` 与 `assetPath_`，派生类实现 `type()`
与 `transfer()`，通过 `setAssetIdentity()` 在加载时补全身份。

**设计意图**：

- `Generic` 承载 DefaultImporter 的透传资产（音效、配置等没有专用转换器的
  文件），使"任何文件都能按 GUID 稳定引用"。
- `Asset` 继承 `Transferable`：Artifact payload 的序列化统一走 Transfer 体系，
  与 JSON 源格式解析彻底分离（见 [SourceFormats.md](SourceFormats.md)）。

## AssetMeta

`asset/base/AssetMeta.h` / `AssetMeta.cpp`

**作用**：源文件旁 `.meta` 侧车（`assetMetaPath(source)` = `source + ".meta"`）
的内容：

```json
{
  "version": 1,
  "asset_id": "3d246ca4-c46f-4d6b-81e7-94e923731c65",
  "asset_type": "Shader"
}
```

Meta 是 AssetId 的持久化来源；数据库与 Artifact 都可以由"源文件 + Meta"重建。

**运转流程**：

- `inferAssetType(path)`：扩展名 → 类型映射。`.shader.json` → Shader、
  `.material.json` → Material、`.mesh.json` → Mesh、
  `.png/.jpg/.jpeg/.ktx/.ktx2` → Texture、`.scene.json` → Scene，其余 Unknown。
- `createAssetMeta(path)`：推断类型并写侧车；`createAssetMeta(path, type)` 显式
  指定类型——ScriptedImporter 接管的扩展名（如 `.obj`）无法从路径推断，必须走
  显式重载。
- `parseAssetMeta` / `loadAssetMeta` / `serializeAssetMeta` / `saveAssetMeta`：
  读写与校验（非法 id 或未知类型直接失败）。
- `isAssetScheme(scheme)`：硬编码接受 `"assets"`，是导入管线的总门禁——只有
  `assets://` 下的文件才能成为资产。

**设计意图**：Meta 不记录 Importer 名称与导入设置——路由由管线按
"ScriptedImporter 扩展名接管 > 类型推断 > DefaultImporter"动态决定（见
[Pipeline.md](Pipeline.md)），Meta 只锚定身份与类型，避免路由演进需要迁移旧
侧车。资产源目录因此必须以可写方式挂载（首次导入要生成侧车）；Publish 运行时
可挂只读。

## AssetReference 与 GuidResolver

`asset/base/AssetReference.h` / `AssetReference.cpp`、`asset/base/GuidResolver.h`

**作用**：`AssetReference` 是源文件中跨资产引用的载体，内部为
`std::variant<monostate, AssetId, VirtualPath>` 双形态。`GuidResolver` 是
GUID 与 VirtualPath 互查的抽象接口（`findPath(guid)` / `findGuid(path)`），
`AssetDatabase` 是它的规范实现；测试用 `BuiltinGuidResolver` 等实现同一接口。

**运转流程**：

- 文本解析（`AssetReference(std::string_view)`）：`guid://` 前缀强校验——非法
  GUID 字符串得到无效引用，不会悄悄降级成路径（GUID 是权威形态）；其余文本按
  VirtualPath 解析。
- `resolve(resolver)`：GUID 形态经 `GuidResolver::findPath` 定位路径；路径形态
  原样返回；无效引用返回空。
- `toString()`：写回 `guid://<AssetId>` 或路径文本，供导出器生成源文件。
  `guid()` / `path()` / `isGuid()` / `isPath()` 做形态查询。

**设计意图**：Material / Scene 源格式自 Phase 0 起只接受 GUID 引用，引用不再
因文件移动而断链；路径形态仅保留给内部工具与过渡兼容。GUID 解析通过
`GuidResolver` 依赖注入，format 层不绑定 `AssetDatabase` 单例，解析代码可以在
任意环境（编辑器、离线 cooker、测试）复用。

## GenericAsset

`asset/base/GenericAsset.h` / `GenericAsset.cpp`

**作用**：DefaultImporter 的透传资产：源文件字节原样存进 `data` 字段，
`type()` 返回 `Generic`，`transfer()` 只序列化 `"data"` 一个字段。

**设计意图**：让没有专用转换器的文件享受完整资产待遇——GUID、Meta、设置哈希、
依赖快照、Artifact 信封、AssetManager 加载一应俱全，编辑器与打包管线因此可以
按 GUID 稳定引用任意资产。运行时用
`ASSET_MANAGER.loadAsset<GenericAsset>(path)` 取回字节。见
[Importer.md](Importer.md) 的 DefaultAssetImporter 一节。
