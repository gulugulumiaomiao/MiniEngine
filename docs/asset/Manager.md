# AssetManager：运行时加载

`AssetManager`（`src/asset/manager/AssetManager.h` / `AssetManager.cpp`，单例宏
`ASSET_MANAGER`）是 CPU 资产对象的统一加载入口。它负责：

- 根据 `assets://` 路径查询 `AssetDatabase`；
- 从 `library://` 读取并反序列化 Artifact（MART 信封 → 具体 Asset）；
- 开发模式下按需触发导入；
- 用 `weak_ptr` 缓存已加载的 Asset（不延长生命周期）；
- 将成功的资产变更通知运行时（热重载）。

它**不**负责创建 Shader、Material、Mesh、Texture 的运行时 Handle——那是各领域
Manager 的工作，成员函数也必须留在各自的实现文件中。

## 初始化模式

```cpp
ASSET_MANAGER.initialize();      // 由构建配置选择模式
ASSET_MANAGER.initialize(AssetManagerMode::Packaged);  // 显式指定（工具/测试）
```

- `Development`（Debug/Release 默认）：初始化 `AssetImportPipeline`、执行
  `scanAll()`、挂载 `AssetExportPipeline`（见 [Exporter.md](Exporter.md)）、
  启动 `FileWatcher`，并订阅导入通知驱动热重载。
- `Packaged`（Publish 或 `MINI_PUBLISH` 构建）：只加载已 Cook 的
  `AssetDatabase` 与 Artifact，缺失即返回失败，不执行任何运行时导入，也不
  初始化写回管线（只读包上写回无意义）。

初始化前，启动层必须统一完成文件系统挂载（`assets://` 在导入阶段需可写以生成
Meta；Publish 运行时可挂只读；`library://` 可写）：

```cpp
FILE_SYSTEM.mountDirectory("assets", physicalAssetRoot, true);
FILE_SYSTEM.mountDirectory("library", physicalLibraryRoot, false);
if (!ASSET_MANAGER.initialize()) { /* 初始化失败 */ }
```

**设计意图**：模式显式化让 AssetManager 作为唯一静态库的一部分被引擎、编辑器、
cooker 与测试复用，行为差异不依赖重新编译。

## loadAsset：加载流程

```text
assets:// 路径
  -> 校验路径与 assets scheme
  -> findCached：weak_ptr 缓存命中直接返回
  -> ensureImported：
       记录已 Imported 且 Artifact 存在 -> 通过；
       否则（仅 Development）触发 ASSET_IMPORT_PIPELINE.importAsset
  -> loadAssetArtifact：读 MART 信封，校验 assetId / assetType 与记录一致
  -> 按 assetType 构造具体 Asset（Shader/Material/Mesh/Texture/Scene/Generic）
  -> setAssetIdentity + BinaryReader transfer（要求读满）
  -> cache(path, asset) 后返回
```

模板重载 `loadAsset<AssetTypeT>(path)` 在此之上做 `dynamic_pointer_cast`，类型
不符返回空。

**设计意图**：信封与记录双重校验（id 与 type 都必须一致）防止 library 与数据库
错位；`reader.finished()` 保证 Artifact 没有尾部垃圾。缓存用 `weak_ptr`——
AssetManager 只加速重复加载，不决定资产生命周期，与领域 Manager 的 Handle 缓存
互不替代。

## 资产与运行时资源

AssetManager 返回可序列化的 CPU 资产描述；领域 Manager 将其转换为运行时资源：

```text
assets://meshes/example.mesh.json
  -> AssetManager::loadAsset<MeshAsset>
  -> MeshManager::load
  -> MeshAsset::instantiate
  -> MeshManager 内部 KeyedHandleRegistry / HandlePool
  -> MeshHandle
```

Shader、Material、Texture 使用相同模式。`Handle<Tag>` 是 index + generation
句柄：销毁 Slot 时 generation 递增，旧 Handle 无法解析，复用 Slot 不会造成
use-after-free。同一路径在对应 Manager 中只映射到一个有效 Handle
（`KeyedHandleRegistry` 的 Key → Handle 索引）。`ShaderAsset::instantiate()` 只
创建 CPU 运行时对象；SPIR-V 编译、Reflection 与 Pipeline 在 Shader 真正参与
绘制时按需生成。详见 [HandlePool.md](../HandlePool.md) 与
[KeyedHandleRegistry.md](../KeyedHandleRegistry.md)。

## 热重载

AssetManager 订阅 `AssetImportPipeline` 的导入通知，成功导入后：

```text
通知（成功且未 Removed）
  -> invalidate(path)：失效 weak_ptr 缓存
  -> Shader：SHADER_MANAGER.replace（原 Handle 上替换，revision + 1）
            -> MATERIAL_MANAGER.refreshShader：引用该 Shader 的材质重建布局
  -> Mesh / Texture：对应 Manager 在原 Handle 上 replace
  -> changeListener_：通用变更通知（编辑器等订阅方）
```

导入失败不会替换旧资源——运行中的对象继续使用上一份成功产物（失败语义见
[Pipeline.md](Pipeline.md)）。

**设计意图**：热重载链沿"资产 → 领域资源"单向流动，AssetManager 不理解各领域
的刷新细节，只做缓存失效与通知分发；Handle 不变保证场景中的引用在重载前后持续
有效。

## 生命周期与关闭顺序

引擎关闭时，先让 Renderer 等待 GPU 空闲并销毁 GPU 资源 Manager（Material、
GraphicsPipeline、Shader、Texture、Mesh），再由 `ASSET_MANAGER.shutdown()` 统一
编排 CPU 侧清理：

```text
1. 清空 changeListener_ 与 AssetManager 缓存
2. AssetImportPipeline::shutdown()（保存数据库、清理注册表）
3. AssetExportPipeline::shutdown()（清理写回注册表）
4. FILE_WATCHER.stop()
5. ASSET_DATABASE.shutdown()
```

文件系统挂载（`assets://` / `library://`）随后由启动层卸载。集中编排避免调用方
遗漏单例，也让关闭顺序与初始化顺序严格互逆。
