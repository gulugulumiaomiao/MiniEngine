# AssetManager

`AssetManager` 是 CPU 资产对象的统一加载入口，通过 `ASSET_MANAGER` 访问。它负责：

- 根据 `asset://` 路径查询 `AssetDatabase`。
- 从 `library://` 读取并反序列化 Artifact。
- 在开发模式下按需触发导入。
- 使用 `weak_ptr` 缓存已加载的 Asset，不延长资产生命周期。
- 将成功的资产变更通知运行时。

它不负责创建 Shader、Material、Mesh 或 Texture 的运行时 Handle。对应工作分别属于
各自的 Manager，Manager 的成员函数也必须保留在自己的实现文件中。

```text
asset:// 路径
  -> AssetDatabase::findByPath
  -> 必要时导入（仅 Development）
  -> library://artifacts/.../asset.bin
  -> 反序列化具体 Asset
  -> weak_ptr<Asset> 缓存
```

## 初始化模式

正常运行时调用无参数版本，由当前构建配置选择模式：

```cpp
ASSET_MANAGER.initialize();
```

- Debug 默认使用 `AssetManagerMode::Development`：初始化导入流水线、扫描资产并启动
  `FileWatcher`。
- Release 默认使用 `AssetManagerMode::Packaged`：只加载已经 Cook 的数据库和 Artifact，
  缺失时返回失败，不执行运行时导入。

工具或测试可以显式选择模式，而不需要为了改变行为重新编译 `AssetManager.cpp`：

```cpp
ASSET_MANAGER.initialize(AssetManagerMode::Packaged);
```

这使 AssetManager 能作为 `MiniEngine` 唯一静态库的一部分复用，避免过去把同一批源码重复
编入多个工具和测试目标。

初始化前，启动层必须统一完成文件系统挂载：

```cpp
FILE_SYSTEM.mountDirectory("asset", physicalAssetRoot, true);
FILE_SYSTEM.mountDirectory("library", physicalLibraryRoot, false);
if (!ASSET_MANAGER.initialize()) {
    // 初始化失败
}
```

## 资产与运行时资源

AssetManager 返回可序列化的 CPU 资产描述；领域 Manager 将其转换为运行时资源：

```text
asset://meshes/example.mesh.json
  -> AssetManager::loadAsset<MeshAsset>
  -> MeshManager::load
  -> MeshAsset::instantiate
  -> MeshManager 内部 HandlePool
  -> MeshHandle
```

Shader、Material 和 Texture 使用相同模式。相同路径在对应 Manager 中只映射到一个有效
Handle；AssetManager 的 `weak_ptr` 缓存与运行时 Handle 缓存互不替代。

## 热重载

导入成功后，AssetManager 先使对应 Asset 弱缓存失效，再刷新已实例化的 Shader、Mesh 或
Texture，并发送通用变更通知。Shader 更新后会通知 MaterialManager 刷新相关布局。
导入失败不会覆盖旧的运行时资源。

引擎关闭时，先销毁引用资产的场景和运行时资源，再关闭 AssetManager、导入流水线、文件
监视器、数据库以及文件系统挂载。
