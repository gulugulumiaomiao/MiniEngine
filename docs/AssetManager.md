# AssetManager

`AssetManager` 统一管理引擎 CPU 资产缓存。缓存键由“资产 C++ 类型 + 规范化绝对路径”组成，因此不同资产类型可以使用相同文件名，同一资产的 `a/../b` 与 `b` 路径会命中同一个对象。

```text
AssetManager
  ├─ ShaderAsset cache
  ├─ MaterialAsset cache
  └─ MeshAsset cache
```

## 加载接口

```cpp
AssetManager& assets = ASSET_MANAGER;
assets.setAssetRoot(assetRoot);

auto shader = assets.loadShaderAsset("shaders/lit.shader.json");
auto material = assets.loadMaterialAsset("materials/brick.material.json");

auto mesh = assets.loadMeshAsset("meshes/cube.mesh", importMesh);

```

Shader 和 Material 的资产加载、路径规范化与缓存都由 AssetManager 统一负责。AssetManager 通过 `FileSystem` 读取文件，JSON 解析函数只接收已经读取的文本，仍是 `detail` 内部实现，不作为业务层的加载入口。

AssetManager 是 Meyers Singleton，通过 `ASSET_MANAGER` 宏全局访问。Renderer 在启动时调用 `setAssetRoot()`，该目录会作为只读 `asset://` 挂载；根目录变化会清空旧缓存。对外只保留具体的 `loadShaderAsset()`、`loadMaterialAsset()`、`loadMeshAsset()` 和整体 `clear()`，不暴露通用 `load/find/store/unload` 接口。`MaterialAsset` 会直接持有缓存中的 `ShaderAsset`。

`MaterialManager` 同样通过 `MATERIAL_MANAGER` 宏全局访问，只负责运行时 Material slot、handle 和 generation。Material JSON 与 Shader 的路径解析和缓存全部委托给 AssetManager；GPU Mesh、Pipeline 和 Descriptor 仍归渲染后端管理，不进入 CPU AssetManager。
