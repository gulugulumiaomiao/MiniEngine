# 导入管线：AssetImportPipeline 与 FileWatcher

`AssetImportPipeline`（`src/asset/importer/AssetImportPipeline.h` / `.cpp`，单例宏
`ASSET_IMPORT_PIPELINE`）是资产系统的调度中枢：路由源文件到具体 importer、按
依赖 DAG 决定导入顺序、执行增量判定、维护数据库与失败状态、驱动热重载级联。
`FileWatcher` 提供后台文件变化事件，二者配合实现"改源文件即热重载"。

## API 总览

| 方法 | 作用 |
|---|---|
| `initialize()` / `shutdown()` | 注册内置 importer、初始化数据库 / 保存并清理 |
| `registerScriptedImporter(importer)` | 注册扩展名接管的脚本导入器（initialize 之后） |
| `scanAll()` | 扫描全部受支持资产并清理已删除记录 |
| `importAsset(path)` | 增量导入单个资产（含依赖调度） |
| `reimportAsset(path)` | 强制重新导入 |
| `removeAsset(path)` | 删除 Artifact 与数据库记录 |
| `importDependencies(path)` | 导入记录中声明的依赖 |
| `processFileEvents()` | 消费 FileWatcher 事件并级联重导入（主线程每帧） |
| `setListener(listener)` | 订阅导入通知（AssetManager 用它驱动热重载） |

## importAssetInternal：单次导入的完整流程

`importAsset` / `reimportAsset` / 依赖递归最终都进入 `importAssetInternal(path,
force)`：

```text
1. 入口校验：initialized、路径有效、assets:// scheme
2. importing_ 集合查重 -> 循环依赖拦截（ImportGuard 保证异常安全出栈）
3. 三级路由确定 importer 与目标 assetType
4. Meta 协商：不存在或类型与路由不符 -> createAssetMeta(path, type) 重建
5. 计算源文件哈希与 Meta 哈希（任一失败即返回）
6. createDefaultSettings -> settings->hash()
7. 增量判定：全部命中则直接返回成功（见下节）
8. gatherDependencies(ctx, settings) 声明依赖
9. 逐个递归 importAssetInternal(dependency)
   - 任一失败：本资产直接标记 Failed，错误 "Dependency import failed: <path>"，
     不执行 import 阶段
10. importer->import(ctx, settings) 执行转换落盘
11. 校验 Artifact 确已生成（hashFile），否则标记失败
12. 写 AssetRecord（成功时记录 settingsHash、dependencies 与依赖哈希快照）
13. ASSET_DATABASE.save() + notify(导入通知)
```

**设计意图**：递归的 `importAssetInternal` 构成依赖 DAG 的后序遍历——叶子资产
最先落盘，根资产最后；`importing_` 集合覆盖 gather + import 整个生命周期，依赖
声明中的循环（A→B→A）在第二次进入同一节点时被拦截并使整条导入链失败。内置
类型的源格式校验天然阻止资产级循环，该防护主要面向 ScriptedImporter 声明的
依赖。

## 三级路由与 Meta 协商

```text
1. ScriptedImporter：registry_.findScripted(path) 扩展名接管
2. 内置推断：inferAssetType(path) -> registry_.find(type)
3. DefaultImporter：透传为 Generic（仅显式 importAsset 到达）
```

路由结果决定 Meta 中的 `asset_type`（走 `createAssetMeta` 显式类型重载）。已存在
的 Meta 若解析失败、或类型与当前路由不符（例如 ScriptedImporter 刚接管了该
扩展名），则整条重建——**GUID 由路径确定性派生**（见
[Foundation.md](Foundation.md)），重建侧车不改变资产身份。

**设计意图**：先路由再建 Meta（而不是用旧 Meta 的类型反查 importer），让路由
规则成为唯一事实来源，接管扩展名、恢复损坏侧车都是同一套协商逻辑。

## 增量判定

已有记录满足以下**全部**条件时跳过重新导入：

- `status == Imported`，且 `id` 与 Meta 一致；
- `importerVersion` 与当前 importer 版本一致；
- `sourceHash` / `metaHash` 与当前文件哈希一致；
- `settingsHash` 与当前默认设置的 hash 一致；
- `dependencyHashes` 快照与依赖源文件当前哈希一一匹配（长度不符的旧记录视为
  失效，重导入一次后自愈）；
- Artifact 文件仍存在。

**设计意图**：五个维度覆盖了所有"需要重导入"的起因——源变化、Meta 变化、
importer 代码升级、导入设置变化、依赖源变化（改 Shader 必重导 Material）。
`reimportAsset` 的 `force` 参数绕过判定强制执行。

## scanAll

扫描 `assets://` 下全部受支持源文件（内置类型 + ScriptedImporter 接管的扩展名；
未知扩展名不做兜底），按类型分组排序后逐个增量导入：

```text
组 0：Shader / Texture / Mesh / ScriptedImporter 扩展名
组 1：Material（引用 shader/texture）
组 2：Scene（引用 material/mesh）
```

不在磁盘上的历史记录由 `removeAsset` 清理（删除 Artifact 与记录），最后保存
数据库。

**设计意图**：分组排序是依赖调度的一级近似（项目启动时的批量导入）；组内仍由
依赖递归保证正确顺序。分组本身只影响吞吐（减少重复导入），不影响正确性。

## FileWatcher

`asset/importer/FileWatcher.h` / `FileWatcher.cpp`，单例宏 `FILE_WATCHER`

**作用**：后台轮询 `assets://` 根目录，产出 `Added / Modified / Removed /
Renamed` 四种 `FileChangeEvent` 入队，不执行任何导入。

**运转流程**：`start(root, debounce, background)` 启动工作线程；默认 200 ms
debounce 窗口（100–300 ms 可调）合并同路径重复事件；快照对比（大小 + 修改时间）
产生事件，`Renamed` 通过相同大小与时间戳的删除/新增项配对识别；临时文件、隐藏
文件与编辑器交换文件被忽略。主线程调用 `pollEvents()` 取走队列。

**设计意图**：监视与导入解耦——后台线程只做无副用的观察，所有导入都发生在主
线程 `processFileEvents()` 内，天然串行、无并发导入问题。

## processFileEvents 与级联重导入

主线程每帧消费事件：

```text
事件（.meta 变化折算回源文件；Renamed 拆成 Removed + Added）
  -> isKnownSourceAsset？
       Removed: 先级联依赖者，再 removeAsset
       Added/Modified: importAssetInternal（Modified 强制重导入）
  -> 导入成功后 reimportDependents(path)：
       沿 ASSET_DATABASE.dependentsOf 反向递归，逐个强制重导入引用者
       （cascaded 集合防重复访问）
```

**设计意图**：改 Shader 源文件时，Shader 自身重导入后，其所有 Material 依赖者
（及更上层的 Scene）沿反向依赖自动级联重建——依赖哈希快照失效与级联重导入共同
构成"改一处、链路自动更新"的闭环。`.meta` 侧车事件折算回源文件处理，避免
导入器把自己的写入当成外部变化。

## 失败语义

解析、验证与写盘失败统一记录 `Log::error` 并返回失败，不调用 fatal：失败记录
写入数据库（`status = Failed` + `lastError`），**保留上一份成功 Artifact 与旧依赖
表**，运行中的对象继续工作；依赖失败同样只标记状态，不删除产物。Shader 编译或
Pipeline 创建失败时，Forward Pass 用内置洋红 Error Material 呈现错误。

## 运行模式与构建集成

- **Debug / Release**（开发）：AssetManager 初始化管线 + `scanAll` + 启动
  FileWatcher；Artifact 缺失时按需导入，持续监听变化。
- **Publish**（发布）：构建期由 `MiniAssetCooker <asset-root> <library-root>`
  离线执行导入；运行时只读取 Cook 好的数据库与 Artifact，不启动 FileWatcher、
  不调用 Importer、不从源 JSON 实例化资产。Shader 的打包 SPV 由独立资产构建
  步骤调用 `MiniShaderCompiler` 生成。

测试约定（独立临时目录、FileWatcher 关闭等）见
[test/AssetEnvironment.md](../test/AssetEnvironment.md)。
