# 资产导入、实例化与热重载管线

> Shader importer 现在只负责 JSON 与 Artifact 转换，不再解析 GLSL include。Shader 源码、include 和 SPIR-V 的依赖统一由 [ShaderCompilePipeline.md](ShaderCompilePipeline.md) 描述的 `FileDependencyGraph` 维护。

本文记录统一更新计划第 26–66 步的实现。当前只支持 Shader 和 Material 两种正式资产；GLSL 文件是 Shader 的依赖，不单独生成 Meta 或运行时 Asset。

## 总体数据流

```text
asset:// 源文件
  -> AssetMeta（稳定 AssetId + AssetType）
  -> AssetImportPipeline
       -> ShaderAssetImporter / MaterialAssetImporter
       -> AssetDatabase（记录、Hash、依赖、反向依赖、状态）
       -> library://artifacts/<AssetId>/asset.bin
  -> AssetManager（weak_ptr<Asset> 缓存）
  -> ShaderManager / MaterialManager（InstanceManager + HandlePool）
  -> Shader Process（首次绘制时预处理、编译 SPIR-V、Reflection）
  -> ShaderProgramCache -> RHI Shader -> PipelineCache
```

## Importer

`ShaderAssetImporter` 解析 ShaderLab JSON，遍历 SubShader/Pass，收集 vertex、fragment 及递归 `#include` 依赖。导入阶段不运行 glslc；SPIR-V 仍保持按需生成。

`MaterialAssetImporter` 解析名称、Shader 虚拟路径、Properties、Keywords 和可选 RenderQueue。导入 Material 前，管线保证它引用的 Shader 已导入，然后从 Shader Artifact 读取声明并验证属性。成功结果把 Shader 的 `asset://` 路径写入依赖表。

Artifact 使用通用 `MART` 二进制信封保存身份和类型，内部 Payload 由资源自行定义。Shader 使用 `SHDR` Payload，Material 使用 `MATL` Payload；两者分别实现序列化和反序列化并维护自己的格式版本。AssetManager 读取信封后按 `AssetType` 调用对应反序列化逻辑，不再解析源 JSON。

解析、验证和输出失败统一记录 `Log::error`，返回失败结果，不调用 fatal。失败记录写入 AssetDatabase，同时保留上一份成功 Artifact 和旧依赖，以便运行中的对象继续工作。

## AssetImportPipeline

通过 `ASSET_IMPORT_PIPELINE` 使用全局单例，提供：

- `scanAll()`：扫描全部受支持资产，先 Shader 后 Material，并清理已删除记录。
- `importAsset()`：按 Hash、Importer 版本和 Artifact 状态执行增量导入。
- `reimportAsset()`：强制重新导入。
- `removeAsset()`：删除 Artifact 和数据库记录。
- `importDependencies()`：导入记录中的资产依赖。
- `processFileEvents()`：在主线程消费 FileWatcher 事件，并沿反向依赖级联重导入。

管线维护正在导入的路径集合。路径再次进入集合时判定为循环依赖，记录错误并终止当前导入链。

## FileWatcher

通过 `FILE_WATCHER` 使用全局单例。后台线程只扫描 `asset://`，产生 `Added`、`Modified`、`Removed`、`Renamed` 四种事件并放入队列，不执行导入。主线程每帧调用 `AssetImportPipeline::processFileEvents()` 消费事件。

事件使用 100–300 ms 的 debounce 窗口，默认 200 ms；同一路径的重复事件会合并，临时文件、隐藏文件、编辑器交换文件会忽略。重命名在轮询实现中通过相同大小和时间戳的删除/新增项配对。

## 运行时 Handle

`Handle<Tag>` 是通用的 index + generation 句柄模板，当前别名包括 `MeshHandle`、`MaterialHandle` 和 `ShaderHandle`。销毁 Slot 时 generation 增加，旧 Handle 无法解析；复用 Slot 不会造成 use-after-free。

`ShaderAsset::instantiate()` 只创建 CPU 运行时 Shader。ShaderManager 和 MaterialManager 继承 `InstanceManager<Resource, HandleType>`，公共的 `insert/load/destroy/find/clear` 语义由抽象层统一。同一路径多次 `load()` 返回同一个 Handle。

`MaterialAsset::instantiate(ShaderHandle)` 创建 Material。Material 只持有 ShaderHandle，可以实时切换 Shader；切换或 Shader revision 更新时，材质重建 UniformBlockLayout，保留同名且类型兼容的属性。

## 热重载与失败回退

```text
FileWatcher 事件
  -> 重导入直接资产
  -> AssetDatabase 反向依赖级联
  -> 成功：AssetManager 失效缓存
  -> ShaderManager 在原 Handle 上 replace，revision + 1
  -> MaterialManager 刷新引用该 Handle 的材质
```

JSON/导入失败时不 replace，旧 Shader 和 Material 保持不变。GLSL 在真正参与绘制时才编译；若新源码编译失败，PipelineCache 以 Shader 路径、Pass、Variant、VertexLayout 和 RenderTarget 格式查找上一条有效 Pipeline，保证失败不会立刻破坏当前画面。成功编译后才把回退入口更新为新 Pipeline。

## 开发与发布流程

Debug 开发：

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug
ctest --test-dir build/clang-debug --output-on-failure
```

Debug 运行时允许在 Artifact 缺失时导入，并持续监听资产变化。

Release 构建：

```powershell
cmake --preset clang-release
cmake --build --preset clang-release
```

构建过程先运行 `MiniAssetCooker <asset-root> <library-root>`。Release 引擎只读取生成的 AssetDatabase 与 Artifact，不启动 FileWatcher，不调用 Importer，也不会从源 JSON 直接实例化资产。

## 生命周期

Renderer 停止并等待 GPU 空闲后，按以下顺序关闭：

1. `MATERIAL_MANAGER.clear()`
2. `SHADER_MANAGER.clear()`
3. 清理 AssetManager 缓存
4. 停止 AssetImportPipeline
5. 停止 FileWatcher
6. 关闭 AssetDatabase
7. 卸载 FileSystem 的 `asset://`、`library://`

这组操作由 `ASSET_MANAGER.shutdown()` 统一编排，避免调用者遗漏单例。

## 测试覆盖

`AssetPipelineTest` 使用独立临时目录覆盖 Meta 创建、数据库记录与反向依赖、Artifact 加载、弱缓存、ShaderHandle 共享、generation 失效、Material Shader 切换、FileWatcher 事件、成功热重载及失败时保留旧 Shader。其他测试继续覆盖 Meta/AssetId 序列化、Importer include 循环、Shader 解析、布局、生成器和 SPIR-V Reflection。
