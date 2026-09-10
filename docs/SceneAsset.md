# SceneAsset 运行时流程

本文记录 SceneAsset 计划第 11–16 步：实例化、导入、加载、重载、测试和使用约定。

## 1. Scene 实例化

`SceneNodeAsset`、`TransformComponentAsset`、`MeshComponentAsset`、`MaterialComponentAsset`、`CameraComponentAsset` 和 `LightComponentAsset` 都继承 `Transferable`，并分别覆盖 `transfer(Transfer&)`。因此 Scene 的所有复合数据节点都遵循同一套可传输接口。

资产描述与对应运行时对象放在同一组文件中：`SceneNodeAsset` 位于
`Node.h/.cpp`，其余组件资产分别位于 `TransformComponent`、
`MeshComponent`、`MaterialComponent`、`CameraComponent` 和
`LightComponent` 的 `.h/.cpp` 文件中。`SceneAsset.h/.cpp` 只负责场景级
数据、JSON 解析、验证和实例化流程。

`SceneAsset` 保存可序列化的数据，`Scene` 保存实际运行时对象。两者之间通过：

```cpp
std::unique_ptr<Scene> SceneAsset::instantiate(
    const SceneInstantiationContext& context) const;
```

`SceneInstantiationContext` 提供 Mesh 和 Material 的路径加载函数。这样 Scene 模块只认识虚拟路径和资源 Handle，不直接依赖全局 Manager。

实例化分两遍执行：

1. 创建全部 Node，复制 Active、Transform 和其他 Component 数据，并把 Mesh/Material 虚拟路径解析成 Handle。
2. 根据稳定的 `SceneNodeAssetId` 建立父子关系。

任何资源加载、组件创建或父子关系建立失败都会返回 `nullptr`，调用方原有 Scene 不受影响。

## 2. Scene Importer

`SceneAssetImporter` 负责：

1. 读取 `.scene.json`。
2. 调用 `parseSceneAsset` 完成格式解析与验证。
3. 收集 Mesh 和 Material 虚拟路径，排序并去重后写入 AssetDatabase 依赖。
4. 使用 `BinaryWriter` 调用 `SceneAsset::transfer`。
5. 将二进制载荷写入 Scene Artifact。

Importer 不创建运行时 Scene，也不持有运行时资源。

## 3. AssetManager

`AssetManager::loadAsset<SceneAsset>(path)` 的路径为：

```text
asset://scenes/example.scene.json
  -> AssetDatabase
  -> Scene Artifact
  -> BinaryReader
  -> SceneAsset::transfer
  -> weak_ptr 缓存
```

Scene 和 Shader、Material、Mesh 共用同一个 Asset 缓存。

## 4. Engine 加载

Engine 提供：

```cpp
bool loadScene(const VirtualPath& scenePath);
bool reloadScene();
const VirtualPath& activeScenePath() const;
```

`loadScene` 先完整加载并实例化新 Scene，成功后才替换当前 Scene。因此加载失败时会继续运行旧 Scene。运行时 Mesh 和 Material 分别由 `MESH_MANAGER` 与 `MATERIAL_MANAGER` 加载。

## 5. 热重载

Debug 模式下，FileWatcher 发现 Scene 源文件变化后触发重新导入。AssetManager 会先清除旧的 SceneAsset 弱缓存，再通知 Engine。Engine 在下一帧开始时重新加载当前 Scene。

热重载遵循最后一次有效结果策略：

- 导入失败：不发送成功变更通知，继续使用当前 Scene。
- 实例化失败：保留当前 Scene。
- 活动 Scene 源文件被删除：记录警告并保留当前 Scene。
- 非活动 Scene 变化：只更新 Asset/Artifact，不切换当前 Scene。

Scene 重载会重新建立 Node 和 Component，因此外部代码不应长期保存跨重载的 `NodeHandle`。需要跨重载定位对象时，应在后续版本增加稳定 SceneNode ID 查询接口。

## 6. 测试范围

- JSON Scene 解析与严格验证。
- Binary Transfer 往返和损坏数据拒绝。
- 五类 Component 的运行时实例化。
- 父子层级重建。
- Scene Importer 注册、Artifact 和依赖收集。
- AssetManager 的 SceneAsset 加载与缓存失效通知。
- Mesh/Material 路径到运行时 Handle 的解析。
