# AssetManager

`AssetManager` 是 CPU 资产的统一加载入口，通过 `ASSET_MANAGER` 全局访问。第一版只管理 `ShaderAsset` 和 `MaterialAsset`，缓存统一为：

```cpp
std::unordered_map<std::string, std::weak_ptr<Asset>> cache;
```

缓存键是规范化后的 `asset://` 虚拟路径。缓存保存 `weak_ptr`，因此 AssetManager 不负责延长资产生命期；调用方需要持有返回的 `shared_ptr`。

## 加载路径

```text
asset:// 路径
  -> AssetDatabase 查询 AssetRecord
  -> library:// 读取 Artifact
  -> 反序列化 ShaderAsset / MaterialAsset
  -> weak_ptr<Asset> 缓存
```

对外只保留类型明确的入口：

```cpp
auto shader = ASSET_MANAGER.loadAsset<ShaderAsset>(
    VirtualPath{"asset://shaders/lit.shader.json"});
auto material = ASSET_MANAGER.loadAsset<MaterialAsset>(
    VirtualPath{"asset://materials/brick.material.json"});
```

AssetManager 不直接读取源 JSON 来构造资产，也不加载运行时 `Shader` 或 `Material`。运行时对象分别交给 `SHADER_MANAGER` 和 `MATERIAL_MANAGER`。

AssetManager 的路径接口只接受 `VirtualPath`。它不接收、保存、返回或解析 `std::filesystem::path`，也不负责物理目录挂载。启动层应先挂载 `asset://` 和 `library://`，再初始化 AssetManager：

```cpp
FILE_SYSTEM.mountDirectory("asset", physicalAssetRoot, false);
FILE_SYSTEM.mountDirectory("library", physicalLibraryRoot, false);
if (!ASSET_MANAGER.initialize()) {
    // 初始化失败
}
```

## Debug 与 Release

- Debug：`initialize()` 初始化导入管线，扫描资产并启动 FileWatcher。Artifact 缺失或过期时允许导入。
- Release：只初始化 AssetDatabase 并读取已经 Cook 的 Artifact；缺失时记录 error 并返回 `nullptr`，绝不在运行时导入。
- `MiniAssetCooker` 是构建期工具，负责为 Release 输出 `library/AssetDatabase.json` 和 `library/artifacts/`。

## 运行时实例

```text
Material 路径
  -> MaterialManager::load
  -> AssetManager::loadAsset<MaterialAsset>
  -> ShaderManager::load(materialAsset.shader)
  -> MaterialAsset::instantiate(ShaderHandle)
  -> KeyedHandleRegistry::insert(Material)
```

同一路径在各自 KeyedHandleRegistry 中只对应一个活动 Handle。ShaderManager 和 MaterialManager 共同继承 KeyedHandleRegistry，由它组合 HandlePool、free list 和可自定义的 Key → Handle 索引。`Material` 只保存 `ShaderHandle`，不持有 `ShaderAsset` 或 `shared_ptr<Shader>`。

## 热重载

成功重新导入后，AssetManager 清除对应弱缓存。已经实例化的 Shader 会由 ShaderManager 在原 Handle 上替换并增加 revision，MaterialManager 随后重建相关材质布局、迁移同名兼容属性。导入失败不会触碰旧运行时 Shader；Shader 编译失败时 GraphicsPipelineManager 会尝试返回上一条有效 GraphicsPipeline。

关闭顺序为：MaterialManager → ShaderManager → AssetManager 缓存 → AssetImportPipeline → FileWatcher → AssetDatabase → FileSystem 挂载。
