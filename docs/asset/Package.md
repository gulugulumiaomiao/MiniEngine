# 资产包（.mepackage）

`src/asset/exporter/AssetPackage.h` / `AssetPackage.cpp` 实现源文件级的资产
分发（对齐 Unity 的 Export / Import Package 语义）：选中资产 → 收集依赖闭包
→ 源文件 + `.meta` 侧车打入单个 zip 文件；导入时校验、按冲突策略落盘、触发
导入管线重建。**包机制不经过 [Exporter.md](Exporter.md) 的写回体系**：文件级
镜像逐字节复制，零格式知识——写回与包是正交组合而非包含关系（包导出读磁盘
状态，未保存的运行时修改不被捕获，与 Unity 一致）。

zip 读写由 `src/core/archive/ZipArchive.h` 承担（vendored miniz 3.0.2，
pimpl 隔离第三方头）。

## 包格式

```text
manifest.json                        ← 唯一入口声明
assets/<relative-path>               ← 源文件镜像（assets:// 相对路径，正斜杠）
assets/<relative-path>.meta          ← meta 侧车（仅资产条目有）
```

manifest（`$schemaVersion` 一律 1，与全项目版本约定一致）：

```json
{
  "$schemaVersion": 1,
  "entries": [
    { "path": "materials/blinn_blue.material.json", "guid": "…", "type": "Material" },
    { "path": "shaders/blinn_phong.Forward.vert" }
  ]
}
```

- **资产条目**：含 `guid` / `type`，必有对应的 `.meta` 条目。
- **伴随文件条目**（vert/frag/glsl 等）：仅 `path`，无 meta——它们不是资产
  （无数据库 record、无 meta，包内按纯字节条目处理）。
- 条目按 path 排序写入，相同闭包产出相同包字节。
- 防御性校验（导入侧拒绝以下所有情况）：
  - zip 条目与 manifest 一一对应（多余/缺失均拒绝）；
  - 条目名必须是规范化相对路径：禁 `..`、禁 `.` 分段、禁绝对路径、禁反斜杠
    ——防 zip 路径逃逸（zip slip）；
  - manifest 的 guid 必须等于 `AssetId::fromPath(path)` 派生值——不一致即
    拒绝（包被篡改或来自异构系统）。

**设计意图**：`AssetId` 由路径确定性派生（见 [Foundation.md](Foundation.md)）
⇒ path→GUID 单射 ⇒ **同 GUID 必同路径**。因此包冲突只剩"目标路径已存在
是否覆盖"一个维度，Unity 的 GUID 撞车重映射（NewGuid 策略）问题不存在。

## 导出：闭包收集

`collectExportClosure(roots)`（内部 BFS，`queued` 集合防重复入队/防环）：

1. 每个根路径：数据库有 record → 资产节点；无 record（vert/frag/glsl 等）
   → 伴随文件节点。根必须 `assets://`。
2. 资产节点 → 入队 `record.dependencies`（数据库依赖闭包：Scene→Mesh/
   Material→Shader/Texture）。
3. Shader 资产节点 → `format::parseShaderAsset` 读 program 的 vert/frag 路径
   （`path.parent().joined(...)` 规则同导入侧）→ 伴随文件节点入队。数据库
   依赖图不覆盖这一层：`ShaderAssetImporter::gatherDependencies` 返回空，
   伴随文件不是资产。
4. 文本伴随文件（`.vert` / `.frag` / `.glsl`）→ 逐行扫描 local
   `#include "..."`，相对当前文件所在目录解析后入队（递归）；`<>`
   search-path 形式记 warning 跳过（引擎无搜索路径来源，无实际使用）。
5. 任一成员读取失败（源字节或 meta 字节）→ 整体失败。

`exportAssetPackage(roots, packagePath, error)` /
`exportAssetPackageToBuffer(roots, bytes, error)`：闭包 → 逐条目写
`ZipWriter`（源 + 相邻 meta）→ `manifest.json` 收尾 → `writeBinaryAtomic`
落盘。

**设计意图**：闭包算法是包机制唯一"理解"资产语义的地方，且只理解到"引用
长什么样"（找文件，不是编码格式）——数据库依赖图 + shader program 路径 +
GLSL include 递归三条边，其余一切按字节镜像。

## 导入：校验、冲突策略、重建

`importAssetPackage(packagePath, policy, report, error)` 按固定顺序执行：

1. **读包解析**：读字节 → `ZipReader::open`（非 zip 拒绝）→ 解析并校验
   manifest（见"包格式"的防御性校验清单）。
2. **冲突预检**（写任何文件之前全部完成）：目标 `assets://<path>` 已存在？
   - `Overwrite`：覆盖源与 meta；
   - `Skip`：跳过该条目（源 + meta 均不触碰），继续其余；
   - `Abort`：任一冲突立即整体失败，**零落盘**（排在冲突条目之后的文件
     也不会被写入——预检先于任何写入）。
3. **落盘**：`createDirectories(parent)` + `writeBinaryAtomic`（源）+
   `writeBinaryAtomic`（meta，仅资产条目）。
4. **重建**：按 manifest 顺序对每个资产条目调 `ASSET_IMPORT_PIPELINE.
   importAsset`（幂等、自身递归处理依赖）。任一失败记入 `report.failed`
   并打日志，**不中断其余条目**。
5. **report**：`imported` / `skipped` / `failed` 三组路径；返回 false 表示
   包本身无效或整体中止（此时不落盘）。

**设计意图**：预检全部前置保证 Abort 的原子性；重建失败不回滚——落盘的
源文件是合法镜像，失败的只是导入（损坏的源在导出侧就会被闭包收集拒绝），
调用方可按 report 决定后续。

## 测试覆盖

`tests/AssetPackageTest.cpp`（gtest）：闭包完整性（数据库依赖 + Shader 伴随
文件 + GLSL include 递归）、包布局与字节镜像、导出失败路径（根不存在 /
非 assets:// / 空根）、导入往返（新环境源字节逐位一致 + 数据库重建 +
loadAsset 可用）、冲突三策略（Skip 不触碰 / Overwrite 覆盖 / Abort 预检
零落盘）、拒绝损坏包（坏 zip、manifest 缺失、schemaVersion 不符、`..`
路径逃逸、guid 与派生值不符、zip 与 manifest 条目不对应）。

`tests/ZipArchiveTest.cpp`（gtest）：条目 roundtrip、空条目、deflate 压缩、
UTF-8 文件名、损坏数据拒绝、顺序与路径分隔符保留。
